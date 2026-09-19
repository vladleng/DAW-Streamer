#include "FenderPluginEditor.h"

#include <algorithm>
#include <cmath>

#include "RecordingTime.h"

DAWStreamerFenderEditor::DAWStreamerFenderEditor(DAWStreamerFenderProcessor& processorToUse)
    : AudioProcessorEditor(processorToUse),
      processor(processorToUse)
{
    modeBox.addItem("Sender", 1);
    modeBox.addItem("Master Recorder", 2);
    modeBox.setSelectedId(processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder ? 2 : 1,
                          juce::dontSendNotification);
    modeBox.onChange = [this]
    {
        const auto desired = modeBox.getSelectedId() == 2
            ? DAWStreamerFenderProcessor::PluginMode::masterRecorder
            : DAWStreamerFenderProcessor::PluginMode::sender;

        if (!processor.setPluginMode(desired))
        {
            modeBox.setSelectedId(processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder ? 2 : 1,
                                  juce::dontSendNotification);
        }

        updateControlVisibility();
    };
    addAndMakeVisible(modeBox);

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

    sessionEditor.setText(processor.getMasterSessionName(), false);
    sessionEditor.setSelectAllWhenFocused(true);
    sessionEditor.onTextChange = [this]
    {
        processor.setMasterSessionName(sessionEditor.getText());
    };
    addAndMakeVisible(sessionEditor);

    browseButton.onClick = [this] { chooseOutputFolder(); };
    addAndMakeVisible(browseButton);

    recordStopButton.onClick = [this]
    {
        const auto control = processor.getDiagnosticsSnapshot();
        const auto isRecording = control.recorderState == dawstreamer::RecorderState::waitingForStreams
                              || control.recorderState == dawstreamer::RecorderState::recording;
        if (isRecording)
            processor.requestStop();
        else
            processor.requestRecord();
    };
    addAndMakeVisible(recordStopButton);

    status.setJustificationType(juce::Justification::centred);
    status.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(status);

    recordingTime.setJustificationType(juce::Justification::centred);
    recordingTime.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(recordingTime);

    details.setJustificationType(juce::Justification::topLeft);
    details.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(details);

    outputPath.setJustificationType(juce::Justification::topLeft);
    outputPath.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(outputPath);

    takePath.setJustificationType(juce::Justification::topLeft);
    takePath.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(takePath);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    lastCallbackChangeMs.fill(now);
    previousProcessBlockCount = processor.getDiagnosticsSnapshot().processBlockCount;

    setSize(1040, 720);
    updateControlVisibility();
    startTimerHz(5);
    timerCallback();
}

void DAWStreamerFenderEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff17191c));
    graphics.setColour(juce::Colours::white);
    graphics.setFont(22.0f);
    graphics.drawText("DAW Streamer for Fender Studio v0.1a", 24, 16, getWidth() - 48, 32,
                      juce::Justification::centredLeft);

    graphics.setFont(14.0f);
    graphics.setColour(juce::Colour(0xffaeb4bc));
    graphics.drawText("Mode", 24, 60, 120, 28, juce::Justification::centredLeft);

    if (processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::sender)
    {
        graphics.drawText("Stream role", 24, 100, 120, 28, juce::Justification::centredLeft);
        graphics.drawText("Sender mode copies the selected track signal to the embedded Master Recorder.",
                          24, getHeight() - 38, getWidth() - 48, 22,
                          juce::Justification::centredLeft);
    }
    else
    {
        graphics.drawText("Show / session", 24, 100, 120, 28, juce::Justification::centredLeft);
        graphics.drawText("Recording folder", 24, 140, 120, 28, juce::Justification::centredLeft);
        graphics.drawText("Master mode owns the Recorder but passes Main audio through unchanged; Main mix is not recorded.",
                          24, getHeight() - 38, getWidth() - 48, 22,
                          juce::Justification::centredLeft);
    }
}

void DAWStreamerFenderEditor::resized()
{
    modeBox.setBounds(160, 60, 250, 28);
    roleBox.setBounds(160, 100, 250, 28);
    sessionEditor.setBounds(160, 100, 300, 28);
    browseButton.setBounds(160, 140, 120, 28);

    recordStopButton.setBounds((getWidth() - 180) / 2, 186, 180, 40);
    status.setBounds(24, 238, getWidth() - 48, 34);
    recordingTime.setBounds(24, 272, getWidth() - 48, 34);
    details.setBounds(24, 318, getWidth() - 48, 294);
    outputPath.setBounds(24, 616, getWidth() - 48, 24);
    takePath.setBounds(24, 642, getWidth() - 48, 24);
}

void DAWStreamerFenderEditor::chooseOutputFolder()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose DAW Streamer recording folder",
        processor.getMasterOutputRoot());

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected.getFullPathName().isNotEmpty())
            processor.setMasterOutputRoot(selected);
        fileChooser.reset();
    });
}

void DAWStreamerFenderEditor::updateControlVisibility()
{
    const auto master = processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder;
    roleBox.setVisible(!master);
    sessionEditor.setVisible(master);
    browseButton.setVisible(master);
    repaint();
}

void DAWStreamerFenderEditor::timerCallback()
{
    diagnostics = processor.getDiagnosticsSnapshot();
    callbacksActive = diagnostics.processBlockCount != previousProcessBlockCount;
    previousProcessBlockCount = diagnostics.processBlockCount;

    const auto expectedMode = diagnostics.mode == DAWStreamerFenderProcessor::PluginMode::masterRecorder ? 2 : 1;
    if (modeBox.getSelectedId() != expectedMode)
        modeBox.setSelectedId(expectedMode, juce::dontSendNotification);

    const auto expectedRole = static_cast<int>(diagnostics.streamRole) + 1;
    if (roleBox.getSelectedId() != expectedRole)
        roleBox.setSelectedId(expectedRole, juce::dontSendNotification);

    updateControlVisibility();

    const auto recorderIsRecording = diagnostics.recorderState == dawstreamer::RecorderState::waitingForStreams
                                  || diagnostics.recorderState == dawstreamer::RecorderState::recording;
    recordStopButton.setButtonText(recorderIsRecording ? "Stop" : "Record");
    recordStopButton.setEnabled(diagnostics.recordingControlOnline);

    if (diagnostics.mode == DAWStreamerFenderProcessor::PluginMode::masterRecorder)
    {
        recorderSnapshot = processor.getEmbeddedRecorderSnapshot();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        updateMasterDetails(recorderSnapshot, now);
        const auto canChangeMode = !recorderSnapshot.sessionActive;
        modeBox.setEnabled(canChangeMode);
        sessionEditor.setEnabled(canChangeMode && recorderSnapshot.backendOwner);
        browseButton.setEnabled(canChangeMode && recorderSnapshot.backendOwner);
    }
    else
    {
        updateSenderDetails();
        modeBox.setEnabled(true);
        roleBox.setEnabled(true);
    }
}

void DAWStreamerFenderEditor::updateMasterDetails(const RecorderEngine::Snapshot& snapshot, double nowMs)
{
    int startedStreams = 0;
    for (std::size_t i = 0; i < snapshot.streams.size(); ++i)
    {
        const auto& stream = snapshot.streams[i];
        if (stream.writerOpen)
            ++startedStreams;

        if (stream.producerCallbacks != previousCallbacks[i])
        {
            previousCallbacks[i] = stream.producerCallbacks;
            lastCallbackChangeMs[i] = nowMs;
        }
    }

    juce::String state;
    if (!snapshot.backendOwner)
        state = "MASTER BACKEND CONFLICT";
    else if (!snapshot.lastError.isEmpty())
        state = "ERROR";
    else if (snapshot.sessionActive && snapshot.waitingForStreams)
        state = "RECORDING - " + juce::String(startedStreams) + "/4 STREAMS";
    else if (snapshot.sessionActive)
        state = "RECORDING";
    else
        state = "IDLE";

    status.setText(state, juce::dontSendNotification);
    recordingTime.setText("Recording time: " + juce::String(dawstreamer::formatRecordingTime(snapshot.takeFrames)),
                          juce::dontSendNotification);

    juce::String text;
    text << "Role       State       Format              Peak        Callbacks   Queue  Drop  Gaps(frames)  Written\n";
    text << "------------------------------------------------------------------------------------------------\n";

    for (std::size_t i = 0; i < snapshot.streams.size(); ++i)
    {
        const auto& stream = snapshot.streams[i];
        const auto role = juce::String(dawstreamer::streamRoleName(stream.role)).paddedRight(' ', 10);

        juce::String connection = "UNCLAIMED";
        if (stream.producerPresent)
            connection = (nowMs - lastCallbackChangeMs[i] < 1000.0) ? "ACTIVE" : "CLAIMED";
        connection = connection.paddedRight(' ', 12);

        juce::String format = "N/A";
        if (stream.sourceSampleRate != 0)
        {
            format = juce::String(stream.sourceSampleRate) + " Hz "
                   + juce::String(stream.sourceChannels) + "ch b"
                   + juce::String(stream.sourceBlockFrames);
        }
        format = format.paddedRight(' ', 20);

        text << role << connection << format
             << peakText(stream.peakLinear).paddedRight(' ', 12)
             << juce::String(stream.producerCallbacks).paddedRight(' ', 12)
             << juce::String(stream.pendingBlocks).paddedRight(' ', 7)
             << juce::String(stream.droppedBlocks).paddedRight(' ', 6)
             << (juce::String(stream.gapEvents) + " (" + juce::String(stream.gapFrames) + ")").paddedRight(' ', 14)
             << juce::String(stream.framesWritten) << "\n";
    }

    text << "\nMIDI: ";
    if (snapshot.midi.sourceSeen)
        text << "source=" << dawstreamer::streamRoleName(snapshot.midi.sourceRole) << "  ";
    else
        text << "source=WAITING  ";

    text << "received=" << snapshot.midi.receivedEvents
         << "  captured=" << snapshot.midi.capturedEvents
         << "  queue=" << snapshot.midi.pendingEvents
         << "  drop=" << snapshot.midi.droppedEvents
         << "  oversized=" << snapshot.midi.oversizedEvents
         << "  other-role=" << snapshot.midi.ignoredOtherRoleEvents
         << "  last=" << midiBytesText(snapshot.midi);

    if (snapshot.midi.fileWritten)
        text << "\nMIDI file: " << snapshot.midi.filePath;
    else if (!snapshot.midi.exportError.isEmpty())
        text << "\nMIDI export error: " << snapshot.midi.exportError;

    if (!snapshot.lastError.isEmpty())
        text << "\n\nError: " << snapshot.lastError;

    details.setText(text, juce::dontSendNotification);
    outputPath.setText("Base folder: " + snapshot.outputRoot, juce::dontSendNotification);
    takePath.setText(snapshot.takeDirectory.isEmpty()
                         ? juce::String("Current/last take: -")
                         : juce::String("Current/last take: ") + snapshot.takeDirectory,
                     juce::dontSendNotification);
}

void DAWStreamerFenderEditor::updateSenderDetails()
{
    juce::String state = diagnostics.roleClaimed ? "SENDER ACTIVE" : "SENDER NOT CLAIMED";
    if (!diagnostics.recordingControlOnline)
        state += " / RECORDER OFFLINE";

    status.setText(state, juce::dontSendNotification);
    recordingTime.setText("Recording time: "
                              + juce::String(dawstreamer::formatRecordingTime(diagnostics.recorderTakeFrames)),
                          juce::dontSendNotification);

    juce::String text;
    text << "Role: " << dawstreamer::streamRoleName(diagnostics.streamRole) << "\n";
    text << "processBlock: " << (callbacksActive ? "RUNNING" : "NO CALLBACKS") << "\n";
    text << "Audio transport: " << (diagnostics.roleClaimed ? "CLAIMED" : "NOT CLAIMED")
         << "  queue=" << diagnostics.transportPendingBlocks
         << "  drop=" << diagnostics.transportDroppedBlocks
         << "  oversized=" << diagnostics.transportOversizedBlocks << "\n";
    text << "MIDI transport: " << (diagnostics.midiRoleClaimed ? "CLAIMED" : "NOT CLAIMED")
         << "  events=" << diagnostics.midiEventCount
         << "  queue=" << diagnostics.midiPendingEvents
         << "  drop=" << diagnostics.midiDroppedEvents
         << "  oversized=" << diagnostics.midiOversizedEvents << "\n";
    text << "Recorder: " << dawstreamer::recorderStateName(diagnostics.recorderState)
         << "  control=" << (diagnostics.recordingControlOnline ? "ONLINE" : "OFFLINE") << "\n";
    text << "Host play: " << yesNo(diagnostics.isPlaying)
         << "  sample rate=" << juce::String(diagnostics.sampleRate, 1)
         << "  block=" << diagnostics.lastNumSamples;

    details.setText(text, juce::dontSendNotification);
    outputPath.setText("Master Recorder controls folder and session naming.", juce::dontSendNotification);
    takePath.setText("Sender audio remains transparent in the host signal path.", juce::dontSendNotification);
}

juce::String DAWStreamerFenderEditor::peakText(float peakLinear)
{
    if (peakLinear <= 0.000001f)
        return "-inf dBFS";

    const auto db = 20.0 * std::log10(static_cast<double>(peakLinear));
    return juce::String(db, 1) + " dBFS";
}

juce::String DAWStreamerFenderEditor::midiBytesText(const RecorderEngine::MidiSnapshot& midi)
{
    if (midi.lastMessageSize == 0)
        return "N/A";

    juce::String result;
    const auto count = std::min<std::uint32_t>(midi.lastMessageSize, 3);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (i > 0)
            result << " ";
        result << juce::String::toHexString(static_cast<int>(midi.lastMessageBytes[i])).paddedLeft('0', 2);
    }
    return result.toUpperCase();
}

juce::String DAWStreamerFenderEditor::yesNo(bool value)
{
    return value ? "YES" : "NO";
}
