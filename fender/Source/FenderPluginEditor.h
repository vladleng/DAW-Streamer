#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "FenderPluginProcessor.h"

class DAWStreamerFenderEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit DAWStreamerFenderEditor(DAWStreamerFenderProcessor& processor);
    ~DAWStreamerFenderEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseOutputFolder();
    void updateControlVisibility();
    void updateLayoutSize();
    void updateMasterDetails(const RecorderEngine::Snapshot& snapshot, double nowMs);
    void updateSenderDetails();

    static juce::String peakText(float peakLinear);
    static juce::String midiBytesText(const RecorderEngine::MidiSnapshot& midi);
    static juce::String yesNo(bool value);

    DAWStreamerFenderProcessor& processor;
    DAWStreamerFenderProcessor::DiagnosticsSnapshot diagnostics;
    RecorderEngine::Snapshot recorderSnapshot;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::uint64_t previousProcessBlockCount = 0;
    bool callbacksActive = false;
    std::array<std::uint64_t, dawstreamer::kStreamRoleCount> previousCallbacks {};
    std::array<double, dawstreamer::kStreamRoleCount> lastCallbackChangeMs {};

    juce::ComboBox modeBox;
    juce::ComboBox roleBox;
    juce::TextEditor sessionEditor;
    juce::TextButton browseButton { "Choose folder..." };
    juce::TextButton recordStopButton { "Record" };
    juce::TextButton detailsButton { "Details" };
    juce::TextButton setupButton { "Setup" };

    juce::Label status;
    juce::Label recordingTime;
    juce::Label audioHealth;
    juce::Label midiHealth;
    juce::Label masterHealth;
    juce::Label alert;
    juce::Label details;
    juce::Label outputPath;
    juce::Label takePath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DAWStreamerFenderEditor)
};
