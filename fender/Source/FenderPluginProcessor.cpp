#include "FenderPluginProcessor.h"
#include "FenderPluginEditor.h"

namespace
{
constexpr std::uint32_t kStateMagic = 0x44534631u; // DSF1
constexpr std::uint32_t kStateVersion = 2u;

bool recorderStateIsRecording(dawstreamer::RecorderState state) noexcept
{
    return state == dawstreamer::RecorderState::waitingForStreams
        || state == dawstreamer::RecorderState::recording;
}

DAWStreamerFenderProcessor::MidiMessageType classifyMidiMessage(const juce::MidiMessage& message) noexcept
{
    using Type = DAWStreamerFenderProcessor::MidiMessageType;
    if (message.isNoteOn()) return Type::noteOn;
    if (message.isNoteOff()) return Type::noteOff;
    if (message.isController()) return Type::controller;
    if (message.isPitchWheel()) return Type::pitchWheel;
    if (message.isChannelPressure()) return Type::channelPressure;
    if (message.isAftertouch()) return Type::polyAftertouch;
    if (message.isProgramChange()) return Type::programChange;
    return Type::other;
}
}

DAWStreamerFenderProcessor::DAWStreamerFenderProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    ownerToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    configuredMasterOutputRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                     .getChildFile("DAW Streamer Recordings");

    recordingParameter = new juce::AudioParameterBool(
        juce::ParameterID { "recording", 1 },
        "Recording",
        false);
    addParameter(recordingParameter);
    recordingParameter->addListener(this);

    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        audioTransports[i] = std::make_unique<dawstreamer::SharedAudioTransport>(role);
        midiTransports[i] = std::make_unique<dawstreamer::SharedMidiTransport>(role);
        streamMetadata[i] = std::make_unique<dawstreamer::SharedStreamMetadata>(role);
    }

    claimSelectedSenderRole();

    const auto control = recorderControl.snapshot();
    previousRecorderHeartbeat = control.heartbeat;
    lastRecorderHeartbeatChangeMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(10);
}

DAWStreamerFenderProcessor::~DAWStreamerFenderProcessor()
{
    stopTimer();

    if (recordingParameter != nullptr)
        recordingParameter->removeListener(this);

    embeddedRecorder.reset();
    releaseSenderClaims();
}

void DAWStreamerFenderProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentExpectedBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
}

void DAWStreamerFenderProcessor::releaseResources()
{
}

bool DAWStreamerFenderProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return input == juce::AudioChannelSet::mono()
        || input == juce::AudioChannelSet::stereo();
}

void DAWStreamerFenderProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    processBlockCount.fetch_add(1, std::memory_order_relaxed);
    currentNumSamples.store(buffer.getNumSamples(), std::memory_order_relaxed);
    currentInputChannels.store(getTotalNumInputChannels(), std::memory_order_relaxed);
    currentOutputChannels.store(getTotalNumOutputChannels(), std::memory_order_relaxed);

    const auto midiCount = midiMessages.getNumEvents();
    midiEventsLastBlock.store(midiCount, std::memory_order_relaxed);
    if (midiCount > 0)
        midiInputSeen.store(true, std::memory_order_relaxed);

    bool blockHasHostTime = false;
    std::int64_t blockHostTime = 0;

    if (auto* playHead = getPlayHead())
    {
        playHeadAvailable.store(true, std::memory_order_relaxed);

        if (const auto position = playHead->getPosition())
        {
            positionAvailable.store(true, std::memory_order_relaxed);
            hostIsPlaying.store(position->getIsPlaying(), std::memory_order_relaxed);

            if (const auto value = position->getTimeInSamples())
            {
                blockHasHostTime = true;
                blockHostTime = *value;
                hasTimeInSamples.store(true, std::memory_order_relaxed);
                hostTimeInSamples.store(*value, std::memory_order_relaxed);
            }
            else
            {
                hasTimeInSamples.store(false, std::memory_order_relaxed);
            }

            if (const auto value = position->getPpqPosition())
            {
                hasPpqPosition.store(true, std::memory_order_relaxed);
                hostPpqPosition.store(*value, std::memory_order_relaxed);
            }
            else
            {
                hasPpqPosition.store(false, std::memory_order_relaxed);
            }

            if (const auto value = position->getBpm())
            {
                hasBpm.store(true, std::memory_order_relaxed);
                hostBpm.store(*value, std::memory_order_relaxed);
            }
            else
            {
                hasBpm.store(false, std::memory_order_relaxed);
            }

            if (const auto value = position->getTimeSignature())
            {
                hasTimeSignature.store(true, std::memory_order_relaxed);
                hostTimeSignatureNumerator.store(value->numerator, std::memory_order_relaxed);
                hostTimeSignatureDenominator.store(value->denominator, std::memory_order_relaxed);
            }
            else
            {
                hasTimeSignature.store(false, std::memory_order_relaxed);
            }
        }
        else
        {
            positionAvailable.store(false, std::memory_order_relaxed);
            hostIsPlaying.store(false, std::memory_order_relaxed);
            hasTimeInSamples.store(false, std::memory_order_relaxed);
            hasPpqPosition.store(false, std::memory_order_relaxed);
            hasBpm.store(false, std::memory_order_relaxed);
            hasTimeSignature.store(false, std::memory_order_relaxed);
        }
    }
    else
    {
        playHeadAvailable.store(false, std::memory_order_relaxed);
        positionAvailable.store(false, std::memory_order_relaxed);
        hostIsPlaying.store(false, std::memory_order_relaxed);
        hasTimeInSamples.store(false, std::memory_order_relaxed);
        hasPpqPosition.store(false, std::memory_order_relaxed);
        hasBpm.store(false, std::memory_order_relaxed);
        hasTimeSignature.store(false, std::memory_order_relaxed);
    }

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto numFrames = static_cast<std::uint32_t>(buffer.getNumSamples());
    const auto sampleRate = static_cast<std::uint32_t>(
        currentSampleRate.load(std::memory_order_relaxed) + 0.5);

    if (getPluginMode() == PluginMode::sender)
    {
        const auto role = getStreamRole();
        const auto blockProducerFrameStart = producerFrameCounter;

        if (auto* transport = transportForRole(role))
        {
            transport->push(buffer.getArrayOfReadPointers(),
                            static_cast<std::uint32_t>(totalInputChannels),
                            numFrames,
                            sampleRate,
                            blockProducerFrameStart,
                            blockHasHostTime,
                            blockHostTime,
                            ownerToken);
        }

        auto* midiTransport = midiTransportForRole(role);
        for (const auto metadata : midiMessages)
        {
            const auto message = metadata.getMessage();
            const auto type = classifyMidiMessage(message);
            const auto* rawData = message.getRawData();
            const auto rawSize = message.getRawDataSize();

            midiEventCount.fetch_add(1, std::memory_order_relaxed);
            lastMidiSampleOffset.store(metadata.samplePosition, std::memory_order_relaxed);
            lastMidiMessageType.store(static_cast<int>(type), std::memory_order_relaxed);
            lastMidiChannel.store(message.getChannel(), std::memory_order_relaxed);
            lastMidiData1.store(rawSize > 1 ? static_cast<int>(rawData[1]) : 0,
                                std::memory_order_relaxed);
            lastMidiData2.store(rawSize > 2 ? static_cast<int>(rawData[2]) : 0,
                                std::memory_order_relaxed);

            if (midiTransport != nullptr
                && metadata.samplePosition >= 0
                && static_cast<std::uint32_t>(metadata.samplePosition) < numFrames)
            {
                midiTransport->push(rawData,
                                    static_cast<std::uint32_t>(rawSize),
                                    blockProducerFrameStart,
                                    static_cast<std::uint32_t>(metadata.samplePosition),
                                    ownerToken);
            }
        }

        producerFrameCounter += numFrames;
    }

    const auto totalOutputChannels = getTotalNumOutputChannels();
    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

bool DAWStreamerFenderProcessor::setPluginMode(PluginMode mode)
{
    const auto current = getPluginMode();
    if (current == mode)
        return true;

    if (current == PluginMode::masterRecorder && embeddedRecorder != nullptr)
    {
        const auto snapshot = embeddedRecorder->getSnapshot();
        if (snapshot.sessionActive)
            return false;
    }

    if (mode == PluginMode::masterRecorder)
    {
        releaseSenderClaims();
        pluginMode.store(static_cast<int>(PluginMode::masterRecorder), std::memory_order_release);

        juce::File root;
        juce::String session;
        {
            const juce::ScopedLock lock(masterConfigLock);
            root = configuredMasterOutputRoot;
            session = configuredMasterSessionName;
        }

        embeddedRecorder = std::make_unique<RecorderEngine>(root);
        embeddedRecorder->setSessionName(session);
        previousEmbeddedSessionActive = false;
        return true;
    }

    embeddedRecorder.reset();
    previousEmbeddedSessionActive = false;
    pluginMode.store(static_cast<int>(PluginMode::sender), std::memory_order_release);
    claimSelectedSenderRole();
    return true;
}

DAWStreamerFenderProcessor::PluginMode DAWStreamerFenderProcessor::getPluginMode() const noexcept
{
    const auto value = pluginMode.load(std::memory_order_acquire);
    return value == static_cast<int>(PluginMode::masterRecorder)
        ? PluginMode::masterRecorder
        : PluginMode::sender;
}

void DAWStreamerFenderProcessor::setStreamRole(dawstreamer::StreamRole role) noexcept
{
    const auto newIndex = static_cast<int>(role);
    if (newIndex < 0 || newIndex >= static_cast<int>(dawstreamer::kStreamRoleCount))
        return;

    const auto oldIndex = currentRole.exchange(newIndex, std::memory_order_acq_rel);
    if (getPluginMode() != PluginMode::sender)
        return;

    if (oldIndex >= 0 && oldIndex < static_cast<int>(dawstreamer::kStreamRoleCount)
        && oldIndex != newIndex)
    {
        const auto oldRoleIndex = static_cast<std::size_t>(oldIndex);
        if (auto* oldMetadata = streamMetadata[oldRoleIndex].get())
            oldMetadata->clearLabel(ownerToken);
        if (auto* oldTransport = audioTransports[oldRoleIndex].get())
            oldTransport->releaseProducer(ownerToken);
        if (auto* oldMidiTransport = midiTransports[oldRoleIndex].get())
            oldMidiTransport->releaseProducer(ownerToken);
    }

    claimSelectedSenderRole();
}

dawstreamer::StreamRole DAWStreamerFenderProcessor::getStreamRole() const noexcept
{
    const auto index = currentRole.load(std::memory_order_acquire);
    if (index < 0 || index >= static_cast<int>(dawstreamer::kStreamRoleCount))
        return dawstreamer::StreamRole::Vocal;
    return static_cast<dawstreamer::StreamRole>(index);
}

void DAWStreamerFenderProcessor::setSenderName(juce::String name)
{
    name = name.trim();
    {
        const juce::ScopedLock lock(senderNameLock);
        configuredSenderName = std::move(name);
    }
    publishSenderMetadata();
}

juce::String DAWStreamerFenderProcessor::getSenderName() const
{
    const juce::ScopedLock lock(senderNameLock);
    return configuredSenderName;
}

void DAWStreamerFenderProcessor::claimSelectedSenderRole() noexcept
{
    if (getPluginMode() != PluginMode::sender)
        return;

    const auto index = static_cast<std::size_t>(getStreamRole());
    if (index >= audioTransports.size())
        return;

    if (auto* transport = audioTransports[index].get())
        transport->claimProducer(ownerToken);
    if (auto* midiTransport = midiTransports[index].get())
        midiTransport->claimProducer(ownerToken);

    publishSenderMetadata();
}

void DAWStreamerFenderProcessor::releaseSenderClaims() noexcept
{
    for (std::size_t i = 0; i < audioTransports.size(); ++i)
    {
        if (streamMetadata[i] != nullptr)
            streamMetadata[i]->clearLabel(ownerToken);
        if (audioTransports[i] != nullptr)
            audioTransports[i]->releaseProducer(ownerToken);
        if (midiTransports[i] != nullptr)
            midiTransports[i]->releaseProducer(ownerToken);
    }
}

void DAWStreamerFenderProcessor::publishSenderMetadata() noexcept
{
    if (getPluginMode() != PluginMode::sender)
        return;

    const auto role = getStreamRole();
    const auto index = static_cast<std::size_t>(role);
    if (index >= audioTransports.size() || index >= streamMetadata.size())
        return;

    auto* transport = audioTransports[index].get();
    auto* metadata = streamMetadata[index].get();
    if (transport == nullptr || metadata == nullptr || !transport->producerClaimedBy(ownerToken))
        return;

    juce::String name;
    {
        const juce::ScopedLock lock(senderNameLock);
        name = configuredSenderName;
    }
    const auto utf8 = name.toUTF8();
    metadata->publishLabel(ownerToken,
                           std::string_view(utf8.getAddress(), static_cast<std::size_t>(utf8.sizeInBytes() - 1)));
}

void DAWStreamerFenderProcessor::requestRecord() noexcept
{
    recorderControl.sendCommand(dawstreamer::RecorderCommand::record);
}

void DAWStreamerFenderProcessor::requestStop() noexcept
{
    recorderControl.sendCommand(dawstreamer::RecorderCommand::stop);
}

void DAWStreamerFenderProcessor::setMasterOutputRoot(juce::File directory)
{
    if (directory.getFullPathName().isEmpty())
        return;

    {
        const juce::ScopedLock lock(masterConfigLock);
        configuredMasterOutputRoot = directory;
    }

    if (embeddedRecorder != nullptr)
        embeddedRecorder->setOutputRoot(std::move(directory));
}

juce::File DAWStreamerFenderProcessor::getMasterOutputRoot() const
{
    const juce::ScopedLock lock(masterConfigLock);
    return configuredMasterOutputRoot;
}

void DAWStreamerFenderProcessor::setMasterSessionName(juce::String name)
{
    name = name.trim();
    {
        const juce::ScopedLock lock(masterConfigLock);
        configuredMasterSessionName = name;
    }

    if (embeddedRecorder != nullptr)
        embeddedRecorder->setSessionName(std::move(name));
}

juce::String DAWStreamerFenderProcessor::getMasterSessionName() const
{
    const juce::ScopedLock lock(masterConfigLock);
    return configuredMasterSessionName;
}

bool DAWStreamerFenderProcessor::hasEmbeddedRecorder() const noexcept
{
    return embeddedRecorder != nullptr;
}

RecorderEngine::Snapshot DAWStreamerFenderProcessor::getEmbeddedRecorderSnapshot() const
{
    if (embeddedRecorder != nullptr)
        return embeddedRecorder->getSnapshot();

    RecorderEngine::Snapshot result;
    result.lastError = "Embedded Recorder is not active.";
    return result;
}

juce::String DAWStreamerFenderProcessor::getPublishedNameForRole(dawstreamer::StreamRole role) const
{
    const auto index = static_cast<std::size_t>(role);
    if (index >= streamMetadata.size())
        return dawstreamer::streamRoleName(role);

    const auto* transport = audioTransports[index].get();
    const auto* metadata = streamMetadata[index].get();
    if (transport == nullptr || metadata == nullptr)
        return dawstreamer::streamRoleName(role);

    const auto snapshot = metadata->snapshot();
    const auto activeOwner = transport->producerOwner();
    if (snapshot.open && activeOwner != 0 && snapshot.ownerToken == activeOwner && !snapshot.label.empty())
        return juce::String::fromUTF8(snapshot.label.c_str());

    return dawstreamer::streamRoleName(role);
}

void DAWStreamerFenderProcessor::captureTakeStreamNames()
{
    juce::StringArray used;
    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        auto base = sanitiseStreamFileBaseName(getPublishedNameForRole(role), role);
        auto unique = base;
        for (int suffix = 2; used.contains(unique, true); ++suffix)
            unique = base + "-" + juce::String(suffix);
        used.add(unique);
        takeStreamFileBaseNames[i] = unique;
    }
}

void DAWStreamerFenderProcessor::renameFinishedTakeFiles(const RecorderEngine::Snapshot& snapshot)
{
    if (snapshot.takeDirectory.isEmpty())
        return;

    const juce::File directory(snapshot.takeDirectory);
    if (!directory.isDirectory())
        return;

    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        const auto sourceBase = juce::String(dawstreamer::streamRoleName(role));
        const auto destinationBase = takeStreamFileBaseNames[i].isNotEmpty()
            ? takeStreamFileBaseNames[i]
            : sourceBase;

        if (destinationBase == sourceBase)
            continue;

        const auto source = directory.getChildFile(sourceBase + ".wav");
        if (!source.existsAsFile())
            continue;

        auto destination = directory.getChildFile(destinationBase + ".wav");
        if (destination.existsAsFile())
        {
            auto suffix = 2;
            do
            {
                destination = directory.getChildFile(destinationBase + "-" + juce::String(suffix++) + ".wav");
            }
            while (destination.existsAsFile());
        }

        source.moveFileTo(destination);
    }
}

juce::String DAWStreamerFenderProcessor::sanitiseStreamFileBaseName(
    juce::String name,
    dawstreamer::StreamRole fallbackRole)
{
    name = name.trim();
    name = name.replaceCharacters("<>:\"/\\|?*", "_________");
    name = name.replace("\r", "_").replace("\n", "_").replace("\t", "_");
    while (name.endsWithChar('.'))
        name = name.dropLastCharacters(1);
    name = name.trim();

    if (name.isEmpty())
        name = dawstreamer::streamRoleName(fallbackRole);
    if (name.length() > 80)
        name = name.substring(0, 80).trim();

    const auto upper = name.toUpperCase();
    const auto numberedReserved = upper.length() == 4
                               && (upper.startsWith("COM") || upper.startsWith("LPT"))
                               && upper[3] >= '1' && upper[3] <= '9';
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" || numberedReserved)
        name = "_" + name;

    return name;
}

void DAWStreamerFenderProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    if (recordingParameter == nullptr
        || parameterIndex != recordingParameter->getParameterIndex()
        || suppressRecordingParameterCommand.load(std::memory_order_acquire))
        return;

    if (!recorderOnlineForControl.load(std::memory_order_acquire))
        return;

    const auto desiredRecording = newValue >= 0.5f;
    pendingDesiredRecording.store(desiredRecording, std::memory_order_release);
    pendingRecordingCommand.store(true, std::memory_order_release);

    if (desiredRecording)
        requestRecord();
    else
        requestStop();
}

void DAWStreamerFenderProcessor::parameterGestureChanged(int, bool)
{
}

void DAWStreamerFenderProcessor::timerCallback()
{
    if (embeddedRecorder != nullptr)
    {
        const auto embedded = embeddedRecorder->getSnapshot();
        if (!previousEmbeddedSessionActive && embedded.sessionActive)
            captureTakeStreamNames();
        else if (previousEmbeddedSessionActive && !embedded.sessionActive)
            renameFinishedTakeFiles(embedded);
        previousEmbeddedSessionActive = embedded.sessionActive;
    }

    const auto control = recorderControl.snapshot();
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (control.heartbeat != previousRecorderHeartbeat)
    {
        previousRecorderHeartbeat = control.heartbeat;
        lastRecorderHeartbeatChangeMs = now;
    }

    const auto recorderOnline = control.open
                             && control.heartbeat != 0
                             && control.state != dawstreamer::RecorderState::offline
                             && (now - lastRecorderHeartbeatChangeMs) <= 1000.0;

    recorderOnlineForControl.store(recorderOnline, std::memory_order_release);

    if (!recorderOnline)
    {
        pendingRecordingCommand.store(false, std::memory_order_release);
        syncRecordingParameterFromRecorder(false);
        return;
    }

    const auto actualRecording = recorderStateIsRecording(control.state);

    if (control.state == dawstreamer::RecorderState::error)
    {
        pendingRecordingCommand.store(false, std::memory_order_release);
        syncRecordingParameterFromRecorder(false);
        return;
    }

    if (pendingRecordingCommand.load(std::memory_order_acquire))
    {
        const auto desiredRecording = pendingDesiredRecording.load(std::memory_order_acquire);
        const auto acknowledged = desiredRecording
            ? actualRecording
            : control.state == dawstreamer::RecorderState::idle;

        if (!acknowledged)
            return;

        pendingRecordingCommand.store(false, std::memory_order_release);
    }

    syncRecordingParameterFromRecorder(actualRecording);
}

void DAWStreamerFenderProcessor::syncRecordingParameterFromRecorder(bool recording)
{
    if (recordingParameter == nullptr)
        return;

    if (recordingParameter->get() == recording)
        return;

    suppressRecordingParameterCommand.store(true, std::memory_order_release);
    recordingParameter->setValueNotifyingHost(recording ? 1.0f : 0.0f);
    suppressRecordingParameterCommand.store(false, std::memory_order_release);
}

dawstreamer::SharedAudioTransport* DAWStreamerFenderProcessor::transportForRole(
    dawstreamer::StreamRole role) const noexcept
{
    const auto index = static_cast<std::size_t>(role);
    return index < audioTransports.size() ? audioTransports[index].get() : nullptr;
}

dawstreamer::SharedMidiTransport* DAWStreamerFenderProcessor::midiTransportForRole(
    dawstreamer::StreamRole role) const noexcept
{
    const auto index = static_cast<std::size_t>(role);
    return index < midiTransports.size() ? midiTransports[index].get() : nullptr;
}

dawstreamer::SharedStreamMetadata* DAWStreamerFenderProcessor::metadataForRole(
    dawstreamer::StreamRole role) const noexcept
{
    const auto index = static_cast<std::size_t>(role);
    return index < streamMetadata.size() ? streamMetadata[index].get() : nullptr;
}

DAWStreamerFenderProcessor::DiagnosticsSnapshot DAWStreamerFenderProcessor::getDiagnosticsSnapshot() const noexcept
{
    DiagnosticsSnapshot result;
    result.mode = getPluginMode();
    result.processBlockCount = processBlockCount.load(std::memory_order_relaxed);
    result.playHeadAvailable = playHeadAvailable.load(std::memory_order_relaxed);
    result.positionAvailable = positionAvailable.load(std::memory_order_relaxed);
    result.isPlaying = hostIsPlaying.load(std::memory_order_relaxed);
    result.hasTimeInSamples = hasTimeInSamples.load(std::memory_order_relaxed);
    result.timeInSamples = hostTimeInSamples.load(std::memory_order_relaxed);
    result.hasPpqPosition = hasPpqPosition.load(std::memory_order_relaxed);
    result.ppqPosition = hostPpqPosition.load(std::memory_order_relaxed);
    result.hasBpm = hasBpm.load(std::memory_order_relaxed);
    result.bpm = hostBpm.load(std::memory_order_relaxed);
    result.hasTimeSignature = hasTimeSignature.load(std::memory_order_relaxed);
    result.timeSignatureNumerator = hostTimeSignatureNumerator.load(std::memory_order_relaxed);
    result.timeSignatureDenominator = hostTimeSignatureDenominator.load(std::memory_order_relaxed);
    result.sampleRate = currentSampleRate.load(std::memory_order_relaxed);
    result.expectedBlockSize = currentExpectedBlockSize.load(std::memory_order_relaxed);
    result.lastNumSamples = currentNumSamples.load(std::memory_order_relaxed);
    result.inputChannels = currentInputChannels.load(std::memory_order_relaxed);
    result.outputChannels = currentOutputChannels.load(std::memory_order_relaxed);

    result.midiInputSeen = midiInputSeen.load(std::memory_order_relaxed);
    result.midiEventCount = midiEventCount.load(std::memory_order_relaxed);
    result.midiEventsLastBlock = midiEventsLastBlock.load(std::memory_order_relaxed);
    result.lastMidiSampleOffset = lastMidiSampleOffset.load(std::memory_order_relaxed);
    result.lastMidiMessageType = static_cast<MidiMessageType>(
        lastMidiMessageType.load(std::memory_order_relaxed));
    result.lastMidiChannel = lastMidiChannel.load(std::memory_order_relaxed);
    result.lastMidiData1 = lastMidiData1.load(std::memory_order_relaxed);
    result.lastMidiData2 = lastMidiData2.load(std::memory_order_relaxed);

    result.streamRole = getStreamRole();
    if (result.mode == PluginMode::sender)
    {
        if (auto* transport = transportForRole(result.streamRole))
        {
            result.transportOpen = transport->isOpen();
            result.roleClaimed = transport->producerClaimedBy(ownerToken);
            result.transportPendingBlocks = transport->pendingBlocks();
            result.transportDroppedBlocks = transport->droppedBlocks();
            result.transportOversizedBlocks = transport->oversizedBlocks();
            result.duplicateRoleClaims = transport->duplicateClaims();
        }

        if (auto* midiTransport = midiTransportForRole(result.streamRole))
        {
            result.midiTransportOpen = midiTransport->isOpen();
            result.midiRoleClaimed = midiTransport->producerClaimedBy(ownerToken);
            result.midiPendingEvents = midiTransport->pendingEvents();
            result.midiDroppedEvents = midiTransport->droppedEvents();
            result.midiOversizedEvents = midiTransport->oversizedEvents();
        }
    }

    const auto control = recorderControl.snapshot();
    result.recorderControlOpen = control.open;
    result.recorderState = control.state;
    result.recorderHeartbeat = control.heartbeat;
    result.recorderTakeFrames = control.takeFrames;
    result.recordingParameterOn = recordingParameter != nullptr && recordingParameter->get();
    result.recordingControlOnline = recorderOnlineForControl.load(std::memory_order_acquire);
    result.recordingControlPending = pendingRecordingCommand.load(std::memory_order_acquire);
    result.embeddedRecorderPresent = embeddedRecorder != nullptr;
    if (embeddedRecorder != nullptr)
        result.embeddedRecorderOwner = embeddedRecorder->getSnapshot().backendOwner;

    return result;
}

juce::AudioProcessorEditor* DAWStreamerFenderProcessor::createEditor()
{
    return new DAWStreamerFenderEditor(*this);
}

bool DAWStreamerFenderProcessor::hasEditor() const
{
    return true;
}

const juce::String DAWStreamerFenderProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DAWStreamerFenderProcessor::acceptsMidi() const
{
    return true;
}

bool DAWStreamerFenderProcessor::producesMidi() const
{
    return false;
}

bool DAWStreamerFenderProcessor::isMidiEffect() const
{
    return false;
}

double DAWStreamerFenderProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DAWStreamerFenderProcessor::getNumPrograms()
{
    return 1;
}

int DAWStreamerFenderProcessor::getCurrentProgram()
{
    return 0;
}

void DAWStreamerFenderProcessor::setCurrentProgram(int)
{
}

const juce::String DAWStreamerFenderProcessor::getProgramName(int)
{
    return {};
}

void DAWStreamerFenderProcessor::changeProgramName(int, const juce::String&)
{
}

void DAWStreamerFenderProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData.reset();
    juce::MemoryOutputStream output(destData, false);
    output.writeInt(static_cast<int>(kStateMagic));
    output.writeInt(static_cast<int>(kStateVersion));
    output.writeInt(static_cast<int>(getPluginMode()));
    output.writeInt(static_cast<int>(getStreamRole()));
    output.writeString(getMasterOutputRoot().getFullPathName());
    output.writeString(getMasterSessionName());
    output.writeString(getSenderName());
}

void DAWStreamerFenderProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < static_cast<int>(sizeof(std::int32_t) * 4))
        return;

    juce::MemoryInputStream input(data, static_cast<std::size_t>(sizeInBytes), false);
    const auto magic = static_cast<std::uint32_t>(input.readInt());
    const auto version = static_cast<std::uint32_t>(input.readInt());
    if (magic != kStateMagic || version < 1u || version > kStateVersion)
        return;

    const auto modeValue = input.readInt();
    const auto roleValue = input.readInt();
    const auto outputRoot = input.readString();
    const auto sessionName = input.readString();
    const auto senderName = version >= 2u ? input.readString() : juce::String();

    if (outputRoot.isNotEmpty())
        setMasterOutputRoot(juce::File(outputRoot));
    setMasterSessionName(sessionName);

    if (roleValue >= 0 && roleValue < static_cast<int>(dawstreamer::kStreamRoleCount))
        setStreamRole(static_cast<dawstreamer::StreamRole>(roleValue));
    setSenderName(senderName);

    if (modeValue == static_cast<int>(PluginMode::masterRecorder))
        setPluginMode(PluginMode::masterRecorder);
    else
        setPluginMode(PluginMode::sender);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DAWStreamerFenderProcessor();
}
