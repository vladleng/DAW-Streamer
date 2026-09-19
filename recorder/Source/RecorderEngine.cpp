#include "RecorderEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr std::uint32_t kRequiredSampleRate = 48000;
constexpr int kRequiredBitsPerSample = 24;
}

RecorderEngine::RecorderEngine(juce::File outputRootToUse)
    : juce::Thread("DAW Streamer Recorder multistream")
{
    configuredOutputRoot = outputRootToUse.getFullPathName().isNotEmpty()
        ? std::move(outputRootToUse)
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
              .getChildFile("DAW Streamer Recordings");

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        auto& stream = streams[i];
        stream.role = static_cast<dawstreamer::StreamRole>(i);
        stream.transport = std::make_unique<dawstreamer::SharedAudioTransport>(stream.role);
        stream.midiTransport = std::make_unique<dawstreamer::SharedMidiTransport>(stream.role);
    }

    lastSharedCommandWord = recorderControl.snapshot().commandWord;
    publishSnapshot();
    startThread();
}

RecorderEngine::~RecorderEngine()
{
    signalThreadShouldExit();
    waitForThreadToExit(5000);
    closeWriters(false);
    recorderControl.publishOffline();
}

void RecorderEngine::startRecording() noexcept
{
    pendingCommand.store(static_cast<int>(Command::start), std::memory_order_release);
}

void RecorderEngine::stopRecording() noexcept
{
    pendingCommand.store(static_cast<int>(Command::stop), std::memory_order_release);
}

RecorderEngine::Snapshot RecorderEngine::getSnapshot() const
{
    const juce::ScopedLock lock(snapshotLock);
    return publishedSnapshot;
}

void RecorderEngine::setOutputRoot(juce::File directory)
{
    if (directory.getFullPathName().isEmpty())
        return;

    const juce::ScopedLock lock(configLock);
    configuredOutputRoot = std::move(directory);
}

juce::File RecorderEngine::getOutputRoot() const
{
    const juce::ScopedLock lock(configLock);
    return configuredOutputRoot;
}

void RecorderEngine::setSessionName(juce::String name)
{
    const juce::ScopedLock lock(configLock);
    configuredSessionName = std::move(name).trim();
}

juce::String RecorderEngine::getSessionName() const
{
    const juce::ScopedLock lock(configLock);
    return configuredSessionName;
}

void RecorderEngine::run()
{
    while (!threadShouldExit())
    {
        const auto command = static_cast<Command>(
            pendingCommand.exchange(static_cast<int>(Command::none), std::memory_order_acq_rel));

        if (command != Command::none)
            handleCommand(command);

        handleSharedControlCommand();

        bool consumedAny = false;

        // Audio is consumed before MIDI. The sender also publishes audio before
        // MIDI for each callback, so the role's take anchor is established first.
        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            auto& stream = streams[i];
            if (stream.transport == nullptr)
                continue;

            dawstreamer::AudioBlock block;
            if (!stream.transport->pop(block))
                continue;

            consumedAny = true;
            updatePeak(i, block);

            if (!sessionActiveInternal)
                continue;

            handleAudioBlock(i, block);
            if (!sessionActiveInternal)
                break;
        }

        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            auto& stream = streams[i];
            if (stream.midiTransport == nullptr)
                continue;

            if (stream.midiTransport->pendingEvents() > 0)
                consumedAny = true;

            drainMidiEvents(i, 512);
        }

        if (sessionActiveInternal)
            waitingForStreamsInternal = !allStreamsStarted();

        publishSnapshot();

        if (!consumedAny)
            wait(2);
    }

    if (sessionActiveInternal)
        finishTake();

    publishSnapshot();
}

void RecorderEngine::handleCommand(Command command)
{
    if (command == Command::start)
    {
        if (!sessionActiveInternal)
            beginTake();
        return;
    }

    if (command == Command::stop && sessionActiveInternal)
        finishTake();
}

void RecorderEngine::handleSharedControlCommand()
{
    std::uint64_t currentWord = lastSharedCommandWord;
    dawstreamer::RecorderCommand command = dawstreamer::RecorderCommand::none;

    if (!recorderControl.readCommandAfter(lastSharedCommandWord, currentWord, command))
        return;

    lastSharedCommandWord = currentWord;

    if (command == dawstreamer::RecorderCommand::record)
    {
        if (!sessionActiveInternal)
            beginTake();
        return;
    }

    if (command == dawstreamer::RecorderCommand::stop && sessionActiveInternal)
        finishTake();
}

dawstreamer::RecorderState RecorderEngine::currentSharedState() const noexcept
{
    if (!lastError.isEmpty())
        return dawstreamer::RecorderState::error;
    if (sessionActiveInternal && waitingForStreamsInternal)
        return dawstreamer::RecorderState::waitingForStreams;
    if (sessionActiveInternal)
        return dawstreamer::RecorderState::recording;
    return dawstreamer::RecorderState::idle;
}

void RecorderEngine::beginTake()
{
    closeWriters(false);

    globalTakeFrontier = 0;
    takeOriginEstablished = false;
    takeHostOriginValid = false;
    takeHostOriginSamples = 0;
    midiSourceSeenInternal = false;
    midiSourceRoleInternal = dawstreamer::StreamRole::Keys;
    midiReceivedEventsInternal = 0;
    midiIgnoredOtherRoleEventsInternal = 0;
    midiFirstTakeFrameInternal = 0;
    midiLastTakeFrameInternal = 0;
    midiLastMessageSizeInternal = 0;
    midiLastMessageBytesInternal.fill(0);
    midiFileWrittenInternal = false;
    midiFilePathInternal.clear();
    midiExportErrorInternal.clear();
    capturedMidiEvents.clear();
    capturedMidiEvents.reserve(16384);
    lastError.clear();

    for (auto& stream : streams)
    {
        if (stream.transport != nullptr)
        {
            stream.transport->discardPending();
            stream.droppedBaseline = stream.transport->droppedBlocks();
            stream.oversizedBaseline = stream.transport->oversizedBlocks();
            stream.counterBaselineValid = true;
        }
        else
        {
            stream.counterBaselineValid = false;
        }

        if (stream.midiTransport != nullptr)
        {
            stream.midiTransport->discardPending();
            stream.midiDroppedBaseline = stream.midiTransport->droppedEvents();
            stream.midiOversizedBaseline = stream.midiTransport->oversizedEvents();
            stream.midiProducerEventsBaseline = stream.midiTransport->producerEvents();
            stream.midiCounterBaselineValid = true;
        }
        else
        {
            stream.midiCounterBaselineValid = false;
        }

        stream.writer.reset();
        stream.producerAnchorFrame = 0;
        stream.takeBaseOffset = 0;
        stream.framesWritten = 0;
        stream.gapFrames = 0;
        stream.gapEvents = 0;
        stream.fileChannels = 0;
        stream.peakLinear = 0.0f;
    }

    juce::File root;
    juce::String sessionName;
    {
        const juce::ScopedLock lock(configLock);
        root = configuredOutputRoot;
        sessionName = sanitiseSessionName(configuredSessionName);
    }

    if (root.getFullPathName().isEmpty())
    {
        root = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("DAW Streamer Recordings");
    }

    const auto rootResult = root.createDirectory();
    if (rootResult.failed())
    {
        failTake("Cannot create recording folder: " + rootResult.getErrorMessage());
        return;
    }

    auto sessionDirectory = root;
    if (sessionName.isNotEmpty())
    {
        sessionDirectory = root.getChildFile(sessionName);
        const auto sessionResult = sessionDirectory.createDirectory();
        if (sessionResult.failed())
        {
            failTake("Cannot create session folder: " + sessionResult.getErrorMessage());
            return;
        }
    }

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    const auto baseTakeName = "Take_" + timestamp;
    takeDirectory = sessionDirectory.getChildFile(baseTakeName);

    for (int suffix = 2; takeDirectory.exists(); ++suffix)
        takeDirectory = sessionDirectory.getChildFile(baseTakeName + "_" + juce::String(suffix));

    const auto takeResult = takeDirectory.createDirectory();
    if (takeResult.failed())
    {
        failTake("Cannot create take folder: " + takeResult.getErrorMessage());
        return;
    }

    sessionActiveInternal = true;
    waitingForStreamsInternal = true;
}

void RecorderEngine::finishTake()
{
    if (!sessionActiveInternal)
    {
        closeWriters(false);
        waitingForStreamsInternal = false;
        return;
    }

    drainPendingAudioForStop();
    drainPendingMidiForStop();

    sessionActiveInternal = false;
    waitingForStreamsInternal = false;
    closeWriters(true);
    exportMidiTake();
}

void RecorderEngine::drainPendingAudioForStop()
{
    constexpr std::uint32_t kMaximumDrainPerStream = dawstreamer::kRingCapacity * 2;

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        auto& stream = streams[i];
        if (stream.transport == nullptr)
            continue;

        dawstreamer::AudioBlock block;
        std::uint32_t drained = 0;

        while (drained < kMaximumDrainPerStream && stream.transport->pop(block))
        {
            updatePeak(i, block);
            handleAudioBlock(i, block);
            ++drained;

            if (!sessionActiveInternal)
                return;
        }
    }
}

void RecorderEngine::drainPendingMidiForStop()
{
    constexpr std::uint32_t kMaximumDrainPerStream = dawstreamer::kMidiRingCapacity * 2;
    for (std::size_t i = 0; i < streams.size(); ++i)
        drainMidiEvents(i, kMaximumDrainPerStream);
}

void RecorderEngine::drainMidiEvents(std::size_t streamIndex, std::uint32_t maximumEvents)
{
    auto& stream = streams[streamIndex];
    if (stream.midiTransport == nullptr)
        return;

    dawstreamer::MidiEvent event;
    std::uint32_t drained = 0;
    while (drained < maximumEvents && stream.midiTransport->pop(event))
    {
        if (sessionActiveInternal)
            handleMidiEvent(streamIndex, event);
        ++drained;
    }
}

void RecorderEngine::handleMidiEvent(std::size_t streamIndex, const dawstreamer::MidiEvent& event)
{
    if (!sessionActiveInternal || event.size == 0 || event.size > dawstreamer::kMaxMidiMessageBytes)
        return;

    ++midiReceivedEventsInternal;

    auto& stream = streams[streamIndex];
    if (stream.writer == nullptr || event.producerFrame < stream.producerAnchorFrame)
        return;

    const auto role = stream.role;
    if (!midiSourceSeenInternal)
    {
        midiSourceSeenInternal = true;
        midiSourceRoleInternal = role;
    }
    else if (role != midiSourceRoleInternal)
    {
        ++midiIgnoredOtherRoleEventsInternal;
        return;
    }

    dawstreamer::RecordedMidiEvent captured;
    captured.takeFrame = stream.takeBaseOffset + (event.producerFrame - stream.producerAnchorFrame);
    captured.size = event.size;
    std::copy_n(event.data.begin(), event.size, captured.data.begin());

    if (capturedMidiEvents.empty())
        midiFirstTakeFrameInternal = captured.takeFrame;

    midiLastTakeFrameInternal = std::max(midiLastTakeFrameInternal, captured.takeFrame);
    midiLastMessageSizeInternal = captured.size;
    midiLastMessageBytesInternal.fill(0);
    const auto copyBytes = std::min<std::uint32_t>(3, captured.size);
    std::copy_n(captured.data.begin(), copyBytes, midiLastMessageBytesInternal.begin());

    capturedMidiEvents.push_back(captured);
}

void RecorderEngine::exportMidiTake()
{
    midiFileWrittenInternal = false;
    midiFilePathInternal.clear();
    midiExportErrorInternal.clear();

    if (capturedMidiEvents.empty())
        return;

    const auto result = dawstreamer::writeMidiTakeFile(takeDirectory,
                                                       capturedMidiEvents,
                                                       globalTakeFrontier);
    midiFileWrittenInternal = result.written;
    midiFilePathInternal = result.file.getFullPathName();
    midiExportErrorInternal = result.error;
}

bool RecorderEngine::startStreamFromFirstBlock(std::size_t streamIndex,
                                                const dawstreamer::AudioBlock& firstBlock)
{
    auto& stream = streams[streamIndex];
    if (stream.writer != nullptr)
        return true;

    if (firstBlock.sampleRate != kRequiredSampleRate)
    {
        failTake("DAW Streamer requires all sources at 48 kHz. Resampling is disabled.");
        return false;
    }

    if (firstBlock.numChannels == 0 || firstBlock.numChannels > dawstreamer::kMaxChannels)
    {
        failTake("Unsupported channel count on stream "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return false;
    }

    stream.producerAnchorFrame = firstBlock.producerFrameStart;

    if (!takeOriginEstablished)
    {
        takeOriginEstablished = true;
        takeHostOriginValid = firstBlock.hostTimeValid != 0;
        takeHostOriginSamples = firstBlock.hostTimeInSamples;
        stream.takeBaseOffset = 0;
    }
    else if (takeHostOriginValid && firstBlock.hostTimeValid != 0)
    {
        if (firstBlock.hostTimeInSamples >= takeHostOriginSamples)
        {
            stream.takeBaseOffset = static_cast<std::uint64_t>(
                firstBlock.hostTimeInSamples - takeHostOriginSamples);
        }
        else
        {
            stream.takeBaseOffset = 0;
        }
    }
    else
    {
        stream.takeBaseOffset = globalTakeFrontier;
    }

    if (!openWriter(streamIndex, firstBlock))
        return false;

    waitingForStreamsInternal = !allStreamsStarted();
    return true;
}

bool RecorderEngine::allStreamsStarted() const noexcept
{
    return std::all_of(streams.begin(), streams.end(), [](const auto& stream)
    {
        return stream.writer != nullptr;
    });
}

bool RecorderEngine::openWriter(std::size_t streamIndex, const dawstreamer::AudioBlock& firstBlock)
{
    auto& stream = streams[streamIndex];
    auto file = takeDirectory.getChildFile(juce::String(dawstreamer::streamRoleName(stream.role)) + ".wav");
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> fileStream { file.createOutputStream() };
    if (fileStream == nullptr)
    {
        failTake("Cannot create " + file.getFullPathName());
        return false;
    }

    juce::WavAudioFormat wavFormat;
    using Options = juce::AudioFormatWriterOptions;
    auto newWriter = wavFormat.createWriterFor(
        fileStream,
        Options {}
            .withSampleRate(static_cast<double>(firstBlock.sampleRate))
            .withNumChannels(static_cast<int>(firstBlock.numChannels))
            .withBitsPerSample(kRequiredBitsPerSample));

    if (newWriter == nullptr)
    {
        failTake("JUCE could not create a 24-bit WAV writer for "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return false;
    }

    stream.writer = std::move(newWriter);
    stream.fileChannels = firstBlock.numChannels;
    return true;
}

void RecorderEngine::handleAudioBlock(std::size_t streamIndex, const dawstreamer::AudioBlock& block)
{
    auto& stream = streams[streamIndex];

    if (stream.writer == nullptr && !startStreamFromFirstBlock(streamIndex, block))
        return;

    if (stream.writer == nullptr)
        return;

    if (block.sampleRate != kRequiredSampleRate || block.numChannels != stream.fileChannels)
    {
        failTake("Source format changed during the take: "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return;
    }

    if (block.producerFrameStart < stream.producerAnchorFrame)
        return;

    const auto producerRelative = block.producerFrameStart - stream.producerAnchorFrame;
    auto desiredStart = stream.takeBaseOffset + producerRelative;

    const auto syncFloor = globalTakeFrontier > block.numFrames
        ? globalTakeFrontier - block.numFrames
        : std::uint64_t { 0 };
    desiredStart = std::max(desiredStart, syncFloor);

    if (desiredStart > stream.framesWritten)
    {
        const auto missingFrames = desiredStart - stream.framesWritten;
        if (!writeSilence(stream, missingFrames))
        {
            failTake("Failed while inserting timeline silence for "
                     + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
            return;
        }

        stream.gapFrames += missingFrames;
        ++stream.gapEvents;
    }

    std::uint64_t overlap = 0;
    if (stream.framesWritten > desiredStart)
        overlap = stream.framesWritten - desiredStart;

    if (overlap >= block.numFrames)
        return;

    const auto framesToWrite = static_cast<std::uint32_t>(block.numFrames - overlap);
    const auto sampleOffset = static_cast<std::uint32_t>(overlap);

    const float* channels[dawstreamer::kMaxChannels] {};
    for (std::uint32_t channel = 0; channel < block.numChannels; ++channel)
        channels[channel] = block.samples[channel].data() + sampleOffset;

    if (!stream.writer->writeFromFloatArrays(channels,
                                              static_cast<int>(block.numChannels),
                                              static_cast<int>(framesToWrite)))
    {
        failTake("Failed while writing WAV audio for "
                 + juce::String(dawstreamer::streamRoleName(stream.role)) + ".");
        return;
    }

    stream.framesWritten += framesToWrite;
    globalTakeFrontier = std::max(globalTakeFrontier, stream.framesWritten);
}

void RecorderEngine::updatePeak(std::size_t streamIndex,
                                const dawstreamer::AudioBlock& block) noexcept
{
    auto peak = 0.0f;
    const auto channels = std::min<std::uint32_t>(block.numChannels, dawstreamer::kMaxChannels);
    const auto frames = std::min<std::uint32_t>(block.numFrames, dawstreamer::kMaxFramesPerBlock);

    for (std::uint32_t channel = 0; channel < channels; ++channel)
    {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            peak = std::max(peak, std::abs(block.samples[channel][frame]));
    }

    streams[streamIndex].peakLinear = peak;
}

bool RecorderEngine::writeSilence(StreamState& stream, std::uint64_t frames)
{
    if (stream.writer == nullptr || stream.fileChannels == 0)
        return false;

    const float* zeroChannels[dawstreamer::kMaxChannels] {
        silenceBuffer.data(), silenceBuffer.data()
    };

    while (frames > 0)
    {
        const auto chunk = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(frames, dawstreamer::kMaxFramesPerBlock));

        if (!stream.writer->writeFromFloatArrays(zeroChannels,
                                                  static_cast<int>(stream.fileChannels),
                                                  static_cast<int>(chunk)))
            return false;

        stream.framesWritten += chunk;
        frames -= chunk;
    }

    return true;
}

void RecorderEngine::closeWriters(bool padToCommonEnd)
{
    if (padToCommonEnd)
    {
        std::uint64_t finalFrames = globalTakeFrontier;
        for (const auto& stream : streams)
            finalFrames = std::max(finalFrames, stream.framesWritten);

        for (auto& stream : streams)
        {
            if (stream.writer != nullptr && stream.framesWritten < finalFrames)
            {
                const auto padding = finalFrames - stream.framesWritten;
                if (writeSilence(stream, padding))
                {
                    stream.gapFrames += padding;
                    if (padding > 0)
                        ++stream.gapEvents;
                }
            }
        }

        globalTakeFrontier = finalFrames;
    }

    for (auto& stream : streams)
        stream.writer.reset();
}

void RecorderEngine::failTake(const juce::String& message)
{
    lastError = message;
    sessionActiveInternal = false;
    waitingForStreamsInternal = false;
    closeWriters(false);
}

void RecorderEngine::publishSnapshot()
{
    Snapshot result;
    result.sessionActive = sessionActiveInternal;
    result.waitingForStreams = waitingForStreamsInternal;
    result.takeFrames = globalTakeFrontier;
    result.takeDirectory = takeDirectory.getFullPathName();
    result.lastError = lastError;

    {
        const juce::ScopedLock lock(configLock);
        result.outputRoot = configuredOutputRoot.getFullPathName();
        result.sessionName = configuredSessionName;
    }

    result.midi.sourceSeen = midiSourceSeenInternal;
    result.midi.sourceRole = midiSourceRoleInternal;
    result.midi.receivedEvents = midiReceivedEventsInternal;
    result.midi.capturedEvents = static_cast<std::uint64_t>(capturedMidiEvents.size());
    result.midi.ignoredOtherRoleEvents = midiIgnoredOtherRoleEventsInternal;
    result.midi.firstTakeFrame = midiFirstTakeFrameInternal;
    result.midi.lastTakeFrame = midiLastTakeFrameInternal;
    result.midi.lastMessageSize = midiLastMessageSizeInternal;
    result.midi.lastMessageBytes = midiLastMessageBytesInternal;
    result.midi.fileWritten = midiFileWrittenInternal;
    result.midi.filePath = midiFilePathInternal;
    result.midi.exportError = midiExportErrorInternal;

    for (std::size_t i = 0; i < streams.size(); ++i)
    {
        const auto& state = streams[i];
        auto& out = result.streams[i];
        out.role = state.role;
        out.writerOpen = state.writer != nullptr;
        out.framesWritten = state.framesWritten;
        out.gapFrames = state.gapFrames;
        out.gapEvents = state.gapEvents;
        out.peakLinear = state.peakLinear;

        if (state.transport != nullptr)
        {
            out.transportOpen = state.transport->isOpen();
            out.producerPresent = state.transport->producerOwner() != 0;
            out.producerCallbacks = state.transport->producerCallbacks();
            out.pendingBlocks = state.transport->pendingBlocks();

            const auto droppedNow = state.transport->droppedBlocks();
            const auto oversizedNow = state.transport->oversizedBlocks();
            out.droppedBlocks = state.counterBaselineValid && droppedNow >= state.droppedBaseline
                ? droppedNow - state.droppedBaseline
                : droppedNow;
            out.oversizedBlocks = state.counterBaselineValid && oversizedNow >= state.oversizedBaseline
                ? oversizedNow - state.oversizedBaseline
                : oversizedNow;

            out.duplicateClaims = state.transport->duplicateClaims();
            out.sourceSampleRate = state.transport->lastSampleRate();
            out.sourceChannels = state.transport->lastNumChannels();
            out.sourceBlockFrames = state.transport->lastNumFrames();
        }

        if (state.midiTransport != nullptr)
        {
            result.midi.pendingEvents += state.midiTransport->pendingEvents();

            const auto droppedNow = state.midiTransport->droppedEvents();
            const auto oversizedNow = state.midiTransport->oversizedEvents();
            result.midi.droppedEvents += state.midiCounterBaselineValid && droppedNow >= state.midiDroppedBaseline
                ? droppedNow - state.midiDroppedBaseline
                : droppedNow;
            result.midi.oversizedEvents += state.midiCounterBaselineValid && oversizedNow >= state.midiOversizedBaseline
                ? oversizedNow - state.midiOversizedBaseline
                : oversizedNow;
        }
    }

    recorderControl.publishState(currentSharedState(), globalTakeFrontier);

    const juce::ScopedLock lock(snapshotLock);
    publishedSnapshot = std::move(result);
}

juce::String RecorderEngine::sanitiseSessionName(juce::String name)
{
    name = name.trim();
    name = name.replaceCharacters("<>:\"/\\|?*", "_________");

    if (name.length() > 80)
        name = name.substring(0, 80);

    return name.trim();
}
