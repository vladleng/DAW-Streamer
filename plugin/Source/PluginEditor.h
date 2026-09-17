#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class DAWStreamerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit DAWStreamerAudioProcessorEditor(DAWStreamerAudioProcessor& processor);
    ~DAWStreamerAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    static juce::String yesNo(bool value);
    static juce::String available(bool hasValue, const juce::String& value);

    DAWStreamerAudioProcessor& processor;
    DAWStreamerAudioProcessor::DiagnosticsSnapshot snapshot;
    std::uint64_t previousProcessBlockCount = 0;
    std::uint64_t previousRecorderHeartbeat = 0;
    double lastRecorderHeartbeatChangeMs = 0.0;
    bool callbacksActive = false;
    bool recorderOnline = false;

    juce::ComboBox roleBox;
    juce::TextButton recordStopButton { "Record" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWStreamerAudioProcessorEditor)
};
