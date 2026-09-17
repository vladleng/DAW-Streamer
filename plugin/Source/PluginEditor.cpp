#include "PluginEditor.h"

DAWStreamerAudioProcessorEditor::DAWStreamerAudioProcessorEditor(DAWStreamerAudioProcessor& processorToUse)
    : AudioProcessorEditor(processorToUse),
      processor(processorToUse)
{
    roleBox.addItem("Vocal", 1);
    roleBox.addItem("Guitar", 2);
    roleBox.addItem("Keys", 3);
    roleBox.addItem("Playback", 4);
    roleBox.setSelectedId(static_cast<int>(processor.getStreamRole()) + 1, juce::dontSendNotification);
    roleBox.onChange = [this]
    {
        const auto index = roleBox.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(dawstreamer::kStreamRoleCount))
            processor.setStreamRole(static_cast<dawstreamer::StreamRole>(index));
    };
    addAndMakeVisible(roleBox);

    setSize(560, 600);
    snapshot = processor.getDiagnosticsSnapshot();
    previousProcessBlockCount = snapshot.processBlockCount;
    startTimerHz(10);
}

void DAWStreamerAudioProcessorEditor::resized()
{
    roleBox.setBounds(230, 58, 250, 28);
}

void DAWStreamerAudioProcessorEditor::timerCallback()
{
    snapshot = processor.getDiagnosticsSnapshot();
    callbacksActive = snapshot.processBlockCount != previousProcessBlockCount;
    previousProcessBlockCount = snapshot.processBlockCount;

    const auto expectedId = static_cast<int>(snapshot.streamRole) + 1;
    if (roleBox.getSelectedId() != expectedId)
        roleBox.setSelectedId(expectedId, juce::dontSendNotification);

    repaint();
}

juce::String DAWStreamerAudioProcessorEditor::yesNo(bool value)
{
    return value ? "YES" : "NO";
}

juce::String DAWStreamerAudioProcessorEditor::available(bool hasValue, const juce::String& value)
{
    return hasValue ? value : "N/A";
}

void DAWStreamerAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff17191c));
    graphics.setColour(juce::Colours::white);

    graphics.setFont(22.0f);
    graphics.drawText("DAW Streamer — Stage 4 Sender", 20, 16, getWidth() - 40, 32,
                      juce::Justification::centredLeft);

    graphics.setFont(15.0f);
    graphics.setColour(juce::Colour(0xffaeb4bc));
    graphics.drawText("Stream role", 24, 58, 190, 28, juce::Justification::centredLeft);

    auto y = 100;
    constexpr int lineHeight = 25;

    const auto drawLine = [&graphics, &y](const juce::String& name, const juce::String& value)
    {
        graphics.setColour(juce::Colour(0xffaeb4bc));
        graphics.drawText(name, 24, y, 205, lineHeight, juce::Justification::centredLeft);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(value, 230, y, 305, lineHeight, juce::Justification::centredLeft);
        y += lineHeight;
    };

    drawLine("Role status", snapshot.roleClaimed ? "CLAIMED" : "DUPLICATE / NOT CLAIMED");
    drawLine("processBlock", callbacksActive ? "RUNNING" : "NO CALLBACKS");
    drawLine("Callback count", juce::String(snapshot.processBlockCount));
    drawLine("Shared transport", yesNo(snapshot.transportOpen));
    drawLine("Queued blocks", juce::String(snapshot.transportPendingBlocks));
    drawLine("Dropped blocks", juce::String(snapshot.transportDroppedBlocks));
    drawLine("Oversized blocks", juce::String(snapshot.transportOversizedBlocks));
    drawLine("Duplicate claims", juce::String(snapshot.duplicateRoleClaims));
    drawLine("Host is playing", snapshot.positionAvailable ? yesNo(snapshot.isPlaying) : "N/A");
    drawLine("AudioPlayHead", yesNo(snapshot.playHeadAvailable));
    drawLine("PositionInfo", yesNo(snapshot.positionAvailable));
    drawLine("Time in samples", available(snapshot.hasTimeInSamples, juce::String(snapshot.timeInSamples)));
    drawLine("PPQ position", available(snapshot.hasPpqPosition, juce::String(snapshot.ppqPosition, 3)));
    drawLine("BPM", available(snapshot.hasBpm, juce::String(snapshot.bpm, 3)));

    const auto timeSignature = juce::String(snapshot.timeSignatureNumerator)
                             + "/"
                             + juce::String(snapshot.timeSignatureDenominator);
    drawLine("Time signature", available(snapshot.hasTimeSignature, timeSignature));
    drawLine("Sample rate", juce::String(snapshot.sampleRate, 1) + " Hz");
    drawLine("Expected block", juce::String(snapshot.expectedBlockSize) + " samples");
    drawLine("Current block", juce::String(snapshot.lastNumSamples) + " samples");
    drawLine("I/O channels", juce::String(snapshot.inputChannels) + " / " + juce::String(snapshot.outputChannels));

    graphics.setColour(juce::Colour(0xff8d949d));
    graphics.setFont(13.0f);
    graphics.drawText("Assign a unique role to every sender. Keep all senders enabled while recording.",
                      20, getHeight() - 36, getWidth() - 40, 22, juce::Justification::centredLeft);
}
