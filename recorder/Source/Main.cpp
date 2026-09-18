#include <array>
#include <cmath>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "RecorderEngine.h"
#include "RecordingTime.h"

namespace
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "DAW Streamer Recorder";
        options.filenameSuffix = "settings";
        options.folderName = "DAW Streamer";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        settings = std::make_unique<juce::PropertiesFile>(options);

        const auto defaultRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                     .getChildFile("DAW Streamer Recordings");
        const auto storedRoot = settings->getValue("outputRoot", defaultRoot.getFullPathName());
        const auto storedSession = settings->getValue("sessionName", "Show");

        engine.setOutputRoot(juce::File(storedRoot));
        engine.setSessionName(storedSession);

        title.setText("DAW Streamer Recorder", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(title);

        stage.setText("Version 0.2.0 - Stage 8A recording time",
                      juce::dontSendNotification);
        stage.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(stage);

        sessionLabel.setText("Show / session", juce::dontSendNotification);
        addAndMakeVisible(sessionLabel);

        sessionEditor.setText(storedSession, false);
        sessionEditor.setSelectAllWhenFocused(true);
        sessionEditor.onTextChange = [this]
        {
            engine.setSessionName(sessionEditor.getText());
            persistSettings();
        };
        addAndMakeVisible(sessionEditor);

        folderLabel.setText("Recording folder", juce::dontSendNotification);
        addAndMakeVisible(folderLabel);

        browseButton.setButtonText("Choose...");
        browseButton.onClick = [this] { chooseOutputFolder(); };
        addAndMakeVisible(browseButton);

        recordStopButton.onClick = [this]
        {
            const auto snapshot = engine.getSnapshot();
            if (snapshot.sessionActive)
                engine.stopRecording();
            else
                engine.startRecording();
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

        setSize(1040, 650);
        startTimerHz(5);
        timerCallback();
    }

    ~MainComponent() override
    {
        persistSettings();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24);
        title.setBounds(area.removeFromTop(42));
        stage.setBounds(area.removeFromTop(28));
        area.removeFromTop(10);

        auto sessionRow = area.removeFromTop(34);
        sessionLabel.setBounds(sessionRow.removeFromLeft(120));
        sessionRow.removeFromLeft(8);
        sessionEditor.setBounds(sessionRow.removeFromLeft(300));

        area.removeFromTop(8);
        auto folderRow = area.removeFromTop(34);
        folderLabel.setBounds(folderRow.removeFromLeft(120));
        folderRow.removeFromLeft(8);
        browseButton.setBounds(folderRow.removeFromLeft(120));

        area.removeFromTop(12);
        auto controls = area.removeFromTop(44);
        recordStopButton.setBounds(controls.withSizeKeepingCentre(180, 40));

        area.removeFromTop(8);
        status.setBounds(area.removeFromTop(34));
        recordingTime.setBounds(area.removeFromTop(34));
        area.removeFromTop(8);
        details.setBounds(area.removeFromTop(266));
        area.removeFromTop(8);
        outputPath.setBounds(area.removeFromTop(24));
        takePath.setBounds(area.removeFromTop(24));
    }

private:
    void chooseOutputFolder()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose DAW Streamer recording folder",
            engine.getOutputRoot());

        const auto flags = juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectDirectories;

        fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            const auto selected = chooser.getResult();
            if (selected.getFullPathName().isNotEmpty())
            {
                engine.setOutputRoot(selected);
                persistSettings();
            }
            fileChooser.reset();
        });
    }

    void persistSettings()
    {
        if (settings == nullptr)
            return;

        settings->setValue("outputRoot", engine.getOutputRoot().getFullPathName());
        settings->setValue("sessionName", sessionEditor.getText());
        settings->saveIfNeeded();
    }

    static juce::String peakText(float peakLinear)
    {
        if (peakLinear <= 0.000001f)
            return "-inf dBFS";

        const auto db = 20.0 * std::log10(static_cast<double>(peakLinear));
        return juce::String(db, 1) + " dBFS";
    }

    void timerCallback() override
    {
        const auto snapshot = engine.getSnapshot();
        const auto now = juce::Time::getMillisecondCounterHiRes();

        int startedStreams = 0;
        for (std::size_t i = 0; i < snapshot.streams.size(); ++i)
        {
            const auto& stream = snapshot.streams[i];
            if (stream.writerOpen)
                ++startedStreams;

            if (stream.producerCallbacks != previousCallbacks[i])
            {
                previousCallbacks[i] = stream.producerCallbacks;
                lastCallbackChangeMs[i] = now;
            }
        }

        juce::String state = "IDLE";
        if (!snapshot.lastError.isEmpty())
        {
            state = "ERROR";
        }
        else if (snapshot.sessionActive && snapshot.waitingForStreams)
        {
            state = "RECORDING - " + juce::String(startedStreams)
                  + "/" + juce::String(static_cast<int>(dawstreamer::kStreamRoleCount))
                  + " STREAMS";
        }
        else if (snapshot.sessionActive)
        {
            state = "RECORDING";
        }

        status.setText(state, juce::dontSendNotification);
        recordingTime.setText("Recording time: "
                                  + juce::String(dawstreamer::formatRecordingTime(snapshot.takeFrames)),
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
                connection = (now - lastCallbackChangeMs[i] < 1000.0) ? "ACTIVE" : "CLAIMED";
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
                 << juce::String(stream.framesWritten);

            if (stream.duplicateClaims > 0)
                text << "   dup history=" << stream.duplicateClaims;

            text << "\n";
        }

        if (!snapshot.lastError.isEmpty())
            text << "\nError: " << snapshot.lastError;

        details.setText(text, juce::dontSendNotification);

        outputPath.setText("Base folder: " + snapshot.outputRoot, juce::dontSendNotification);
        takePath.setText(snapshot.takeDirectory.isEmpty()
                             ? juce::String("Current/last take: -")
                             : juce::String("Current/last take: ") + snapshot.takeDirectory,
                         juce::dontSendNotification);

        recordStopButton.setButtonText(snapshot.sessionActive ? "Stop" : "Record");
        recordStopButton.setEnabled(snapshot.lastError.isEmpty() || snapshot.sessionActive);
        sessionEditor.setEnabled(!snapshot.sessionActive);
        browseButton.setEnabled(!snapshot.sessionActive);
    }

    RecorderEngine engine;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::array<std::uint64_t, dawstreamer::kStreamRoleCount> previousCallbacks {};
    std::array<double, dawstreamer::kStreamRoleCount> lastCallbackChangeMs {};

    juce::Label title;
    juce::Label stage;
    juce::Label sessionLabel;
    juce::TextEditor sessionEditor;
    juce::Label folderLabel;
    juce::TextButton browseButton;
    juce::TextButton recordStopButton { "Record" };
    juce::Label status;
    juce::Label recordingTime;
    juce::Label details;
    juce::Label outputPath;
    juce::Label takePath;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(juce::String name)
        : DocumentWindow(std::move(name),
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(), true);
        setResizable(true, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class DAWStreamerRecorderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "DAW Streamer Recorder";
    }

    const juce::String getApplicationVersion() override
    {
        return "0.2.0-stage8a";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override
    {
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace

START_JUCE_APPLICATION(DAWStreamerRecorderApplication)
