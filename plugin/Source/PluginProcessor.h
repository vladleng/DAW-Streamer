#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

class DAWStreamerAudioProcessor final : public juce::AudioProcessor
{
public:
    struct DiagnosticsSnapshot
    {
        std::uint64_t processBlockCount = 0;
        bool playHeadAvailable = false;
        bool positionAvailable = false;
        bool isPlaying = false;

        bool hasTimeInSamples = false;
        std::int64_t timeInSamples = 0;

        bool hasPpqPosition = false;
        double ppqPosition = 0.0;

        bool hasBpm = false;
        double bpm = 0.0;

        bool hasTimeSignature = false;
        int timeSignatureNumerator = 0;
        int timeSignatureDenominator = 0;

        double sampleRate = 0.0;
        int expectedBlockSize = 0;
        int lastNumSamples = 0;
        int inputChannels = 0;
        int outputChannels = 0;
    };

    DAWStreamerAudioProcessor();
    ~DAWStreamerAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    DiagnosticsSnapshot getDiagnosticsSnapshot() const noexcept;

private:
    std::atomic<std::uint64_t> processBlockCount { 0 };
    std::atomic<bool> playHeadAvailable { false };
    std::atomic<bool> positionAvailable { false };
    std::atomic<bool> hostIsPlaying { false };

    std::atomic<bool> hasTimeInSamples { false };
    std::atomic<std::int64_t> hostTimeInSamples { 0 };

    std::atomic<bool> hasPpqPosition { false };
    std::atomic<double> hostPpqPosition { 0.0 };

    std::atomic<bool> hasBpm { false };
    std::atomic<double> hostBpm { 0.0 };

    std::atomic<bool> hasTimeSignature { false };
    std::atomic<int> hostTimeSignatureNumerator { 0 };
    std::atomic<int> hostTimeSignatureDenominator { 0 };

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentExpectedBlockSize { 0 };
    std::atomic<int> currentNumSamples { 0 };
    std::atomic<int> currentInputChannels { 0 };
    std::atomic<int> currentOutputChannels { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWStreamerAudioProcessor)
};
