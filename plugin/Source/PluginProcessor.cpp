#include "PluginProcessor.h"
#include "PluginEditor.h"

DAWStreamerAudioProcessor::DAWStreamerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
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

    if (auto* playHead = getPlayHead())
    {
        playHeadAvailable.store(true, std::memory_order_relaxed);

        if (const auto position = playHead->getPosition())
        {
            positionAvailable.store(true, std::memory_order_relaxed);
            hostIsPlaying.store(position->getIsPlaying(), std::memory_order_relaxed);

            if (const auto value = position->getTimeInSamples())
            {
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

    // Stage 3 sender. This is intentionally only a fixed-size memory copy plus
    // lock-free atomic cursor updates. It never waits for the Recorder.
    const auto totalInputChannels = getTotalNumInputChannels();
    const auto sampleRate = static_cast<std::uint32_t>(currentSampleRate.load(std::memory_order_relaxed) + 0.5);

    audioTransport.push(buffer.getArrayOfReadPointers(),
                        static_cast<std::uint32_t>(totalInputChannels),
                        static_cast<std::uint32_t>(buffer.getNumSamples()),
                        sampleRate);

    // True pass-through: input samples are left untouched. Clear only any
    // hypothetical output-only channels for safety.
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
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

    result.transportOpen = audioTransport.isOpen();
    result.transportPendingBlocks = audioTransport.pendingBlocks();
    result.transportDroppedBlocks = audioTransport.droppedBlocks();
    result.transportOversizedBlocks = audioTransport.oversizedBlocks();
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

void DAWStreamerAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void DAWStreamerAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DAWStreamerAudioProcessor();
}
