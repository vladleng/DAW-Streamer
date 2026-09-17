#include "PluginProcessor.h"

DAWStreamerAudioProcessor::DAWStreamerAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void DAWStreamerAudioProcessor::prepareToPlay(double, int)
{
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

    // The host supplies the input samples in this buffer. Leaving the input
    // channels untouched makes this processor a true pass-through effect.
    // Clear only hypothetical output-only channels for safety.
    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DAWStreamerAudioProcessor::createEditor()
{
    return nullptr;
}

bool DAWStreamerAudioProcessor::hasEditor() const
{
    return false;
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
