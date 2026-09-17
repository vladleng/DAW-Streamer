#include "PluginProcessor.h"
#include "PluginEditor.h"

DAWStreamerAudioProcessor::DAWStreamerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    ownerToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));

    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        audioTransports[i] = std::make_unique<dawstreamer::SharedAudioTransport>(role);
    }

    setStreamRole(dawstreamer::StreamRole::Vocal);
}

DAWStreamerAudioProcessor::~DAWStreamerAudioProcessor()
{
    for (auto& transport : audioTransports)
    {
        if (transport != nullptr)
            transport->releaseProducer(ownerToken);
    }
}

void DAWStreamerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentExpectedBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
}

void DAWStreamerAudioProcessor::releaseResources()
{
}

bool DAWStreamerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return input == juce::AudioChannelSet::mono()
        || input == juce::AudioChannelSet::stereo();
}

void DAWStreamerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    processBlockCount.fetch_add(1, std::memory_order_relaxed);
    currentNumSamples.store(buffer.getNumSamples(), std::memory_order_relaxed);
    currentInputChannels.store(getTotalNumInputChannels(), std::memory_order_relaxed);
    currentOutputChannels.store(getTotalNumOutputChannels(), std::memory_order_relaxed);

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
    const auto role = getStreamRole();

    if (auto* transport = transportForRole(role))
    {
        transport->push(buffer.getArrayOfReadPointers(),
                        static_cast<std::uint32_t>(totalInputChannels),
                        numFrames,
                        sampleRate,
                        producerFrameCounter,
                        blockHasHostTime,
                        blockHostTime,
                        ownerToken);
    }

    // The producer counter advances even when a ring is full. The next successful
    // block therefore exposes the exact missing frame interval to the Recorder.
    producerFrameCounter += numFrames;

    // True pass-through: input samples are left untouched.
    const auto totalOutputChannels = getTotalNumOutputChannels();
    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

void DAWStreamerAudioProcessor::setStreamRole(dawstreamer::StreamRole role) noexcept
{
    const auto newIndex = static_cast<int>(role);
    if (newIndex < 0 || newIndex >= static_cast<int>(dawstreamer::kStreamRoleCount))
        return;

    const auto oldIndex = currentRole.exchange(newIndex, std::memory_order_acq_rel);

    if (oldIndex >= 0 && oldIndex < static_cast<int>(dawstreamer::kStreamRoleCount)
        && oldIndex != newIndex)
    {
        if (auto* oldTransport = audioTransports[static_cast<std::size_t>(oldIndex)].get())
            oldTransport->releaseProducer(ownerToken);
    }

    if (auto* newTransport = audioTransports[static_cast<std::size_t>(newIndex)].get())
        newTransport->claimProducer(ownerToken);
}

dawstreamer::StreamRole DAWStreamerAudioProcessor::getStreamRole() const noexcept
{
    const auto index = currentRole.load(std::memory_order_acquire);
    if (index < 0 || index >= static_cast<int>(dawstreamer::kStreamRoleCount))
        return dawstreamer::StreamRole::Vocal;
    return static_cast<dawstreamer::StreamRole>(index);
}

dawstreamer::SharedAudioTransport* DAWStreamerAudioProcessor::transportForRole(
    dawstreamer::StreamRole role) const noexcept
{
    const auto index = static_cast<std::size_t>(role);
    if (index >= audioTransports.size())
        return nullptr;
    return audioTransports[index].get();
}

DAWStreamerAudioProcessor::DiagnosticsSnapshot DAWStreamerAudioProcessor::getDiagnosticsSnapshot() const noexcept
{
    DiagnosticsSnapshot result;
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

    result.streamRole = getStreamRole();
    if (auto* transport = transportForRole(result.streamRole))
    {
        result.transportOpen = transport->isOpen();
        result.roleClaimed = transport->producerClaimedBy(ownerToken);
        result.transportPendingBlocks = transport->pendingBlocks();
        result.transportDroppedBlocks = transport->droppedBlocks();
        result.transportOversizedBlocks = transport->oversizedBlocks();
        result.duplicateRoleClaims = transport->duplicateClaims();
    }

    return result;
}

juce::AudioProcessorEditor* DAWStreamerAudioProcessor::createEditor()
{
    return new DAWStreamerAudioProcessorEditor(*this);
}

bool DAWStreamerAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String DAWStreamerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DAWStreamerAudioProcessor::acceptsMidi() const
{
    return false;
}

bool DAWStreamerAudioProcessor::producesMidi() const
{
    return false;
}

bool DAWStreamerAudioProcessor::isMidiEffect() const
{
    return false;
}

double DAWStreamerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DAWStreamerAudioProcessor::getNumPrograms()
{
    return 1;
}

int DAWStreamerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DAWStreamerAudioProcessor::setCurrentProgram(int)
{
}

const juce::String DAWStreamerAudioProcessor::getProgramName(int)
{
    return {};
}

void DAWStreamerAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void DAWStreamerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    destData.reset();
    juce::MemoryOutputStream output(destData, false);
    output.writeInt(static_cast<int>(getStreamRole()));
}

void DAWStreamerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < static_cast<int>(sizeof(std::int32_t)))
        return;

    juce::MemoryInputStream input(data, static_cast<std::size_t>(sizeInBytes), false);
    const auto roleIndex = input.readInt();
    if (roleIndex >= 0 && roleIndex < static_cast<int>(dawstreamer::kStreamRoleCount))
        setStreamRole(static_cast<dawstreamer::StreamRole>(roleIndex));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DAWStreamerAudioProcessor();
}
