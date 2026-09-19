#include "FenderPluginEditor.h"

#include <algorithm>
#include <cmath>

#include "RecordingTime.h"

namespace
{
constexpr int kEditorWidth = 560;
constexpr int kCompactHeight = 304;
constexpr int kSenderSetupHeight = 54;
constexpr int kMasterSetupHeight = 92;
constexpr int kDetailsHeight = 300;

juce::String effectiveSenderName(const DAWStreamerFenderProcessor& processor)
{
    const auto custom = processor.getSenderName();
    return custom.isNotEmpty()
        ? custom
        : juce::String(dawstreamer::streamRoleName(processor.getStreamRole()));
}

void ensureSenderHasFreeSlot(DAWStreamerFenderProcessor& processor)
{
    if (processor.getPluginMode() != DAWStreamerFenderProcessor::PluginMode::sender)
        return;

    if (processor.getDiagnosticsSnapshot().roleClaimed)
        return;

    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        dawstreamer::SharedAudioTransport probe(role);
        if (!probe.isOpen() || probe.producerOwner() != 0)
            continue;

        processor.setStreamRole(role);
        if (processor.getDiagnosticsSnapshot().roleClaimed)
            return;
    }
}
}

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
        else if (desired == DAWStreamerFenderProcessor::PluginMode::sender)
        {
            ensureSenderHasFreeSlot(processor);
            updateSenderEditorFromState();
        }

        updateControlVisibility();
        updateLayoutSize();
    };
    addAndMakeVisible(modeBox);

    ensureSenderHasFreeSlot(processor);

    senderNameEditor.setText(effectiveSenderName(processor), false);
    senderNameEditor.setSelectAllWhenFocused(true);
    senderNameEditor.onTextChange = [this]
    {
        processor.setSenderName(senderNameEditor.getText());
    };
    addAndMakeVisible(senderNameEditor);

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

    detailsButton.setClickingTogglesState(true);
    detailsButton.onClick = [this]
    {
        updateControlVisibility();
        updateLayoutSize();
    };
    addAndMakeVisible(detailsButton);

    setupButton.setClickingTogglesState(true);
    setupButton.onClick = [this]
    {
        updateControlVisibility();
        updateLayoutSize();
    };
    addAndMakeVisible(setupButton);

    status.setJustificationType(juce::Justification::centred);
    status.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(status);

    recordingTime.setJustificationType(juce::Justification::centred);
    recordingTime.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    addAndMakeVisible(recordingTime);

    for (auto* label : { &audioHealth, &midiHealth, &masterHealth })
    {
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(14.0f, juce::Font::bold));
        addAndMakeVisible(*label);
    }

    alert.setJustificationType(juce::Justification::centred);
    alert.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(alert);

    details.setJustificationType(juce::Justification::topLeft);
    details.setFont(juce::FontOptions(13.0f));
    addAndMakeVisible(details);

    outputPath.setJustificationType(juce::Justification::centredLeft);
    outputPath.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(outputPath);

    takePath.setJustificationType(juce::Justification::centredLeft);
    takePath.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(takePath);

    const auto now = juce::Time::getMillisecondCounterHiRes();
    lastCallbackChangeMs.fill(now);
    previousProcessBlockCount = processor.getDiagnosticsSnapshot().processBlockCount;

    updateControlVisibility();
    updateLayoutSize();
    startTimerHz(5);
    timerCallback();
}

void DAWStreamerFenderEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff17191c));
    graphics.setColour(juce::Colour(0xff7f858c));
    graphics.setFont(10.0f);
    graphics.drawText("Moon River Studio", 24, 2, getWidth() - 48, 16,
                      juce::Justification::centredLeft);

    graphics.setColour(juce::Colours::white);
    graphics.setFont(20.0f);
    graphics.drawText("DAW Streamer 0.1", 24, 16, getWidth() - 48, 30,
                      juce::Justification::centredLeft);

    graphics.setFont(13.0f);
    graphics.setColour(juce::Colour(0xffaeb4bc));

    const auto master = processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder;
    graphics.drawText(master ? "Session" : "Instance", 24, 56, 80, 28,
                      juce::Justification::centredLeft);

    int nextSectionY = kCompactHeight;
    if (setupButton.getToggleState())
    {
        graphics.drawHorizontalLine(nextSectionY, 24.0f, static_cast<float>(getWidth() - 24));
        graphics.drawText("Setup", 24, nextSectionY + 8, 70, 24, juce::Justification::centredLeft);
        graphics.drawText("Mode", 104, nextSectionY + 8, 55, 24, juce::Justification::centredLeft);

        if (master)
            graphics.drawText("Folder", 24, nextSectionY + 48, 70, 24, juce::Justification::centredLeft);

        nextSectionY += master ? kMasterSetupHeight : kSenderSetupHeight;
    }

    if (detailsButton.getToggleState())
    {
        graphics.drawHorizontalLine(nextSectionY, 24.0f, static_cast<float>(getWidth() - 24));
        graphics.drawText("Diagnostics", 24, nextSectionY + 8, 100, 24, juce::Justification::centredLeft);
    }
}

void DAWStreamerFenderEditor::resized()
{
    const auto master = processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder;

    senderNameEditor.setBounds(104, 56, 432, 30);
    sessionEditor.setBounds(104, 56, 432, 30);

    status.setBounds(24, 98, getWidth() - 48, 28);
    recordingTime.setBounds(24, 128, getWidth() - 48, 34);

    audioHealth.setBounds(24, 169, master ? 240 : 160, 24);
    midiHealth.setBounds(master ? 296 : 200, 169, master ? 240 : 160, 24);
    masterHealth.setBounds(376, 169, 160, 24);

    recordStopButton.setBounds((getWidth() - 150) / 2, 202, 150, 36);
    alert.setBounds(24, 242, getWidth() - 48, 22);
    detailsButton.setBounds(24, 270, 118, 26);
    setupButton.setBounds(150, 270, 98, 26);

    int nextSectionY = kCompactHeight;
    if (setupButton.getToggleState())
    {
        modeBox.setBounds(160, nextSectionY + 7, 210, 28);
        if (master)
        {
            browseButton.setBounds(104, nextSectionY + 47, 130, 28);
            outputPath.setBounds(244, nextSectionY + 47, getWidth() - 268, 28);
            nextSectionY += kMasterSetupHeight;
        }
        else
        {
            nextSectionY += kSenderSetupHeight;
        }
    }

    if (detailsButton.getToggleState())
    {
        details.setBounds(24, nextSectionY + 38, getWidth() - 48, 220);
        takePath.setBounds(24, nextSectionY + 262, getWidth() - 48, 24);
    }
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
    const auto setupOpen = setupButton.getToggleState();
    const auto detailsOpen = detailsButton.getToggleState();

    senderNameEditor.setVisible(!master);
    sessionEditor.setVisible(master);

    modeBox.setVisible(setupOpen);
    browseButton.setVisible(setupOpen && master);
    outputPath.setVisible(setupOpen && master);

    details.setVisible(detailsOpen);
    takePath.setVisible(detailsOpen && master);

    masterHealth.setVisible(!master);

    detailsButton.setButtonText(detailsOpen ? "Details -" : "Details +");
    setupButton.setButtonText(setupOpen ? "Setup -" : "Setup +");
    repaint();
}

void DAWStreamerFenderEditor::updateLayoutSize()
{
    const auto master = processor.getPluginMode() == DAWStreamerFenderProcessor::PluginMode::masterRecorder;
    auto height = kCompactHeight;

    if (setupButton.getToggleState())
        height += master ? kMasterSetupHeight : kSenderSetupHeight;
    if (detailsButton.getToggleState())
        height += kDetailsHeight;

    setSize(kEditorWidth, height);
}

void DAWStreamerFenderEditor::updateSenderEditorFromState()
{
    if (senderNameEditor.hasKeyboardFocus(true))
        return;

    const auto desired = effectiveSenderName(processor);
    if (senderNameEditor.getText() != desired)
        senderNameEditor.setText(desired, false);
}

void DAWStreamerFenderEditor::timerCallback()
{
    diagnostics = processor.getDiagnosticsSnapshot();
    callbacksActive = diagnostics.processBlockCount != previousProcessBlockCount;
    previousProcessBlockCount = diagnostics.processBlockCount;

    const auto expectedMode = diagnostics.mode == DAWStreamerFenderProcessor::PluginMode::masterRecorder ? 2 : 1;
    if (modeBox.getSelectedId() != expectedMode)
        modeBox.setSelectedId(expectedMode, juce::dontSendNotification);

    updateSenderEditorFromState();

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
        senderNameEditor.setEnabled(!recorderIsRecording);
    }

    updateControlVisibility();
}

void DAWStreamerFenderEditor::updateMasterDetails(const RecorderEngine::Snapshot& snapshot, double nowMs)
{
    int claimedStreams = 0;
    int writingStreams = 0;
    std::uint64_t audioDrops = 0;
    std::uint64_t audioOversized = 0;
    std::uint64_t duplicateClaims = 0;

    for (std::size_t i = 0; i < snapshot.streams.size(); ++i)
    {
        const auto& stream = snapshot.streams[i];
        if (stream.producerPresent)
            ++claimedStreams;
        if (stream.writerOpen)
            ++writingStreams;

        audioDrops += stream.droppedBlocks;
        audioOversized += stream.oversizedBlocks;
        duplicateClaims += stream.duplicateClaims;

        if (stream.producerCallbacks != previousCallbacks[i])
        {
            previousCallbacks[i] = stream.producerCallbacks;
            lastCallbackChangeMs[i] = nowMs;
        }
    }

    juce::String state;
    if (!snapshot.backendOwner)
        state = "MASTER CONFLICT";
    else if (!snapshot.lastError.isEmpty())
        state = "ERROR";
    else if (snapshot.sessionActive && snapshot.waitingForStreams)
        state = "RECORDING  " + juce::String(writingStreams) + "/4";
    else if (snapshot.sessionActive)
        state = "RECORDING";
    else if (claimedStreams == static_cast<int>(dawstreamer::kStreamRoleCount))
        state = "READY";
    else
        state = "IDLE";

    status.setText(state, juce::dontSendNotification);
    recordingTime.setText(dawstreamer::formatRecordingTime(snapshot.takeFrames), juce::dontSendNotification);

    audioHealth.setText("Audio  " + juce::String(claimedStreams) + "/4"
                            + (claimedStreams == 4 ? "  OK" : ""),
                        juce::dontSendNotification);

    midiHealth.setText(snapshot.midi.sourceSeen
                           ? juce::String("MIDI  OK  ") + processor.getPublishedNameForRole(snapshot.midi.sourceRole)
                           : juce::String("MIDI  waiting"),
                       juce::dontSendNotification);

    juce::String warning;
    if (!snapshot.backendOwner)
        warning = "Another recorder backend is active";
    else if (!snapshot.lastError.isEmpty())
        warning = snapshot.lastError;
    else if (audioDrops > 0 || snapshot.midi.droppedEvents > 0)
        warning = "Drop detected: audio " + juce::String(audioDrops)
                + ", MIDI " + juce::String(snapshot.midi.droppedEvents);
    else if (audioOversized > 0 || snapshot.midi.oversizedEvents > 0)
        warning = "Oversized data detected";
    else if (duplicateClaims > 0)
        warning = "Duplicate instance slot detected";
    else if (snapshot.midi.ignoredOtherRoleEvents > 0)
        warning = "MIDI arrived from more than one instance slot";
    else if (snapshot.sessionActive && claimedStreams < 4)
        warning = "Waiting for " + juce::String(4 - claimedStreams) + " audio stream(s)";

    alert.setText(warning, juce::dontSendNotification);

    juce::String text;
    for (std::size_t i = 0; i < snapshot.streams.size(); ++i)
    {
        const auto& stream = snapshot.streams[i];
        juce::String connection = "UNCLAIMED";
        if (stream.producerPresent)
            connection = (nowMs - lastCallbackChangeMs[i] < 1000.0) ? "ACTIVE" : "CLAIMED";

        const auto publishedName = processor.getPublishedNameForRole(stream.role);
        text << publishedName;
        if (publishedName != dawstreamer::streamRoleName(stream.role))
            text << " [" << dawstreamer::streamRoleName(stream.role) << "]";
        text << ": " << connection;
        if (stream.sourceSampleRate != 0)
            text << "  " << stream.sourceSampleRate << " Hz / " << stream.sourceChannels
                 << "ch / block " << stream.sourceBlockFrames;
        text << "  peak " << peakText(stream.peakLinear)
             << "  queue " << stream.pendingBlocks
             << "  drop " << stream.droppedBlocks
             << "  oversized " << stream.oversizedBlocks
             << "  gaps " << stream.gapEvents << " (" << stream.gapFrames << ")"
             << "  written " << stream.framesWritten << "\n";
    }

    text << "\nMIDI: ";
    if (snapshot.midi.sourceSeen)
        text << "source " << processor.getPublishedNameForRole(snapshot.midi.sourceRole) << "  ";
    else
        text << "source WAITING  ";

    text << "received " << snapshot.midi.receivedEvents
         << "  captured " << snapshot.midi.capturedEvents
         << "  queue " << snapshot.midi.pendingEvents
         << "  drop " << snapshot.midi.droppedEvents
         << "  oversized " << snapshot.midi.oversizedEvents
         << "  other-role " << snapshot.midi.ignoredOtherRoleEvents
         << "  last " << midiBytesText(snapshot.midi);

    if (snapshot.midi.fileWritten)
        text << "\nMIDI file: " << snapshot.midi.filePath;
    else if (!snapshot.midi.exportError.isEmpty())
        text << "\nMIDI export error: " << snapshot.midi.exportError;

    details.setText(text, juce::dontSendNotification);
    outputPath.setText(snapshot.outputRoot, juce::dontSendNotification);
    takePath.setText(snapshot.takeDirectory.isEmpty()
                         ? juce::String("Current / last take: -")
                         : juce::String("Current / last take: ") + snapshot.takeDirectory,
                     juce::dontSendNotification);
}

void DAWStreamerFenderEditor::updateSenderDetails()
{
    const auto recorderIsRecording = diagnostics.recorderState == dawstreamer::RecorderState::waitingForStreams
                                  || diagnostics.recorderState == dawstreamer::RecorderState::recording;

    juce::String state;
    if (!diagnostics.roleClaimed)
        state = "INSTANCE CONFLICT";
    else if (!diagnostics.recordingControlOnline)
        state = "MASTER OFFLINE";
    else if (recorderIsRecording)
        state = "RECORDING";
    else
        state = "READY";

    status.setText(state, juce::dontSendNotification);
    recordingTime.setText(dawstreamer::formatRecordingTime(diagnostics.recorderTakeFrames),
                          juce::dontSendNotification);

    audioHealth.setText(diagnostics.roleClaimed ? "Audio  OK" : "Audio  conflict",
                        juce::dontSendNotification);
    midiHealth.setText(diagnostics.midiInputSeen ? "MIDI  ACTIVE" : "MIDI  ready",
                       juce::dontSendNotification);
    masterHealth.setText(diagnostics.recordingControlOnline ? "Master  ONLINE" : "Master  OFFLINE",
                         juce::dontSendNotification);

    juce::String warning;
    if (!diagnostics.roleClaimed)
        warning = "All four instance slots are already in use";
    else if (!diagnostics.recordingControlOnline)
        warning = "Master Recorder is not online";
    else if (!callbacksActive)
        warning = "No host audio callbacks";
    else if (diagnostics.transportDroppedBlocks > 0 || diagnostics.midiDroppedEvents > 0)
        warning = "Drop detected";
    else if (diagnostics.transportOversizedBlocks > 0 || diagnostics.midiOversizedEvents > 0)
        warning = "Oversized data detected";

    alert.setText(warning, juce::dontSendNotification);

    const auto displayName = effectiveSenderName(processor);

    juce::String text;
    text << "Instance: " << displayName
         << "  [slot " << dawstreamer::streamRoleName(diagnostics.streamRole) << "]\n";
    text << "processBlock: " << (callbacksActive ? "RUNNING" : "NO CALLBACKS") << "\n";
    text << "Audio: " << (diagnostics.roleClaimed ? "CLAIMED" : "NOT CLAIMED")
         << "  queue " << diagnostics.transportPendingBlocks
         << "  drop " << diagnostics.transportDroppedBlocks
         << "  oversized " << diagnostics.transportOversizedBlocks
         << "  duplicate claims " << diagnostics.duplicateRoleClaims << "\n";
    text << "MIDI: " << (diagnostics.midiRoleClaimed ? "CLAIMED" : "NOT CLAIMED")
         << "  events " << diagnostics.midiEventCount
         << "  queue " << diagnostics.midiPendingEvents
         << "  drop " << diagnostics.midiDroppedEvents
         << "  oversized " << diagnostics.midiOversizedEvents << "\n";
    text << "Recorder: " << dawstreamer::recorderStateName(diagnostics.recorderState)
         << "  control " << (diagnostics.recordingControlOnline ? "ONLINE" : "OFFLINE") << "\n";
    text << "Host play: " << yesNo(diagnostics.isPlaying)
         << "  sample rate " << juce::String(diagnostics.sampleRate, 1)
         << "  block " << diagnostics.lastNumSamples;

    details.setText(text, juce::dontSendNotification);
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
