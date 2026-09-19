#include "PluginEditor.h"
#include "RecordingTime.h"

namespace
{
juce::String midiMessageSummary(const DAWStreamerAudioProcessor::DiagnosticsSnapshot& snapshot)
{
    using Type = DAWStreamerAudioProcessor::MidiMessageType;

    if (!snapshot.midiInputSeen || snapshot.lastMidiMessageType == Type::none)
        return "N/A";

    const auto channel = snapshot.lastMidiChannel > 0
        ? " ch " + juce::String(snapshot.lastMidiChannel)
        : juce::String();

    switch (snapshot.lastMidiMessageType)
    {
        case Type::noteOn:
            return "Note On" + channel + " note " + juce::String(snapshot.lastMidiData1)
                 + " vel " + juce::String(snapshot.lastMidiData2);
        case Type::noteOff:
            return "Note Off" + channel + " note " + juce::String(snapshot.lastMidiData1)
                 + " vel " + juce::String(snapshot.lastMidiData2);
        case Type::controller:
            return "CC" + channel + " #" + juce::String(snapshot.lastMidiData1)
                 + " = " + juce::String(snapshot.lastMidiData2);
        case Type::pitchWheel:
        {
            const auto value = snapshot.lastMidiData1 + (snapshot.lastMidiData2 << 7);
            return "Pitch" + channel + " = " + juce::String(value);
        }
        case Type::channelPressure:
            return "Channel pressure" + channel + " = " + juce::String(snapshot.lastMidiData1);
        case Type::polyAftertouch:
            return "Aftertouch" + channel + " note " + juce::String(snapshot.lastMidiData1)
                 + " = " + juce::String(snapshot.lastMidiData2);
        case Type::programChange:
            return "Program" + channel + " = " + juce::String(snapshot.lastMidiData1);
        case Type::other:
            return "Other MIDI" + channel + " data " + juce::String(snapshot.lastMidiData1)
                 + ", " + juce::String(snapshot.lastMidiData2);
        case Type::none:
            break;
    }

    return "N/A";
}
}

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

    recordStopButton.onClick = [this]
    {
        if (!recorderOnline)
            return;

        if (snapshot.recorderState == dawstreamer::RecorderState::idle)
        {
            processor.requestRecord();
            return;
        }

        if (snapshot.recorderState == dawstreamer::RecorderState::waitingForStreams
            || snapshot.recorderState == dawstreamer::RecorderState::recording)
        {
            processor.requestStop();
        }
    };
    addAndMakeVisible(recordStopButton);

    setSize(560, 870);
    snapshot = processor.getDiagnosticsSnapshot();
    previousProcessBlockCount = snapshot.processBlockCount;
    previousRecorderHeartbeat = snapshot.recorderHeartbeat;
    lastRecorderHeartbeatChangeMs = juce::Time::getMillisecondCounterHiRes();
    recorderOnline = snapshot.recorderControlOpen
                  && snapshot.recorderState != dawstreamer::RecorderState::offline
                  && snapshot.recorderHeartbeat != 0;
    startTimerHz(10);
}

void DAWStreamerAudioProcessorEditor::resized()
{
    roleBox.setBounds(230, 58, 250, 28);
    recordStopButton.setBounds(230, 100, 250, 36);
}

void DAWStreamerAudioProcessorEditor::timerCallback()
{
    snapshot = processor.getDiagnosticsSnapshot();
    callbacksActive = snapshot.processBlockCount != previousProcessBlockCount;
    previousProcessBlockCount = snapshot.processBlockCount;

    const auto expectedId = static_cast<int>(snapshot.streamRole) + 1;
    if (roleBox.getSelectedId() != expectedId)
        roleBox.setSelectedId(expectedId, juce::dontSendNotification);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (snapshot.recorderHeartbeat != previousRecorderHeartbeat)
    {
        previousRecorderHeartbeat = snapshot.recorderHeartbeat;
        lastRecorderHeartbeatChangeMs = now;
        recorderOnline = snapshot.recorderControlOpen
                      && snapshot.recorderState != dawstreamer::RecorderState::offline;
    }
    else if (now - lastRecorderHeartbeatChangeMs > 1000.0)
    {
        recorderOnline = false;
    }

    const auto recorderIsRecording = snapshot.recorderState == dawstreamer::RecorderState::waitingForStreams
                                  || snapshot.recorderState == dawstreamer::RecorderState::recording;
    const auto recorderIsIdle = snapshot.recorderState == dawstreamer::RecorderState::idle;

    recordStopButton.setButtonText(recorderIsRecording ? "Stop recording" : "Record");
    recordStopButton.setEnabled(recorderOnline && (recorderIsIdle || recorderIsRecording));
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
    graphics.drawText("DAW Streamer v0.3c - MIDI file export", 20, 16, getWidth() - 40, 32,
                      juce::Justification::centredLeft);

    graphics.setFont(15.0f);
    graphics.setColour(juce::Colour(0xffaeb4bc));
    graphics.drawText("Stream role", 24, 58, 190, 28, juce::Justification::centredLeft);
    graphics.drawText("Recorder control", 24, 103, 190, 28, juce::Justification::centredLeft);

    auto y = 150;
    constexpr int lineHeight = 25;

    const auto drawLine = [&graphics, &y](const juce::String& name, const juce::String& value)
    {
        graphics.setColour(juce::Colour(0xffaeb4bc));
        graphics.drawText(name, 24, y, 205, lineHeight, juce::Justification::centredLeft);
        graphics.setColour(juce::Colours::white);
        graphics.drawText(value, 230, y, 305, lineHeight, juce::Justification::centredLeft);
        y += lineHeight;
    };

    const auto recorderState = recorderOnline
        ? juce::String(dawstreamer::recorderStateName(snapshot.recorderState))
        : juce::String("OFFLINE");

    drawLine("Recorder", recorderState);
    drawLine("Recording time",
             recorderOnline
                 ? juce::String(dawstreamer::formatRecordingTime(snapshot.recorderTakeFrames))
                 : juce::String("N/A"));

    juce::String hostControlState = "OFFLINE";
    if (snapshot.recordingControlOnline)
    {
        if (snapshot.recordingControlPending)
            hostControlState = snapshot.recordingParameterOn ? "PENDING ON" : "PENDING OFF";
        else
            hostControlState = snapshot.recordingParameterOn ? "ON" : "OFF";
    }
    drawLine("Host Recording", hostControlState);

    drawLine("MIDI input", snapshot.midiInputSeen ? "RECEIVED" : "WAITING");
    drawLine("MIDI events total", juce::String(snapshot.midiEventCount));
    drawLine("Events in last MIDI block", juce::String(snapshot.midiEventsLastBlock));
    drawLine("Last MIDI", midiMessageSummary(snapshot));
    drawLine("MIDI sample offset",
             snapshot.lastMidiSampleOffset >= 0
                 ? juce::String(snapshot.lastMidiSampleOffset) + " samples"
                 : juce::String("N/A"));
    drawLine("MIDI IPC",
             snapshot.midiTransportOpen && snapshot.midiRoleClaimed
                 ? juce::String("CLAIMED")
                 : juce::String("NOT CLAIMED"));
    drawLine("MIDI queue", juce::String(snapshot.midiPendingEvents));
    drawLine("MIDI dropped", juce::String(snapshot.midiDroppedEvents));
    drawLine("MIDI oversized", juce::String(snapshot.midiOversizedEvents));

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
    graphics.drawText("v0.3c captures MIDI on the shared take timeline; Recorder writes MIDI.mid on Stop.",
                      20, getHeight() - 36, getWidth() - 40, 22, juce::Justification::centredLeft);
}
