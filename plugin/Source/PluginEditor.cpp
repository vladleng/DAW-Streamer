#include "PluginEditor.h"

DAWStreamerAudioProcessorEditor::DAWStreamerAudioProcessorEditor(DAWStreamerAudioProcessor& processorToUse)
    : AudioProcessorEditor(processorToUse),
      processor(processorToUse)
{
    setSize(560, 430);
    snapshot = processor.getDiagnosticsSnapshot();
    previousProcessBlockCount = snapshot.processBlockCount;
    startTimerHz(10);
}

void DAWStreamerAudioProcessorEditor::timerCallback()
{
    snapshot = processor.getDiagnosticsSnapshot();
    callbacksActive = snapshot.processBlockCount != previousProcessBlockCount;
    previousProcessBlockCount = snapshot.processBlockCount;
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
    graphics.drawText("DAW Streamer — Stage 2 Diagnostics", 20, 16, getWidth() - 40, 32,
                      juce::Justification::centredLeft);

    graphics.setFont(15.0f);

    auto y = 62;
    constexpr int lineHeight = 27;

    const auto drawLine = [&graphics, &y](const juce::String& name, const juce::String& value)
    {
        graphics.setColour(juce::Colour(0xffaeb4bc));
        graphics.drawText(name, 24, y, 205, lineHeight, juce::Justification::centredLeft);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(value, 230, y, 305, lineHeight, juce::Justification::centredLeft);
        y += lineHeight;
    };

    const auto callbackText = callbacksActive ? "RUNNING" : "NO CALLBACKS";
    drawLine("processBlock", callbackText);
    drawLine("Callback count", juce::String(snapshot.processBlockCount));
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
    graphics.drawText("Keep this window open while testing Play, Stop, bypass, mute and Song switching.",
                      20, getHeight() - 36, getWidth() - 40, 22, juce::Justification::centredLeft);
}
