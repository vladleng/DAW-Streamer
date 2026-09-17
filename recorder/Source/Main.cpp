#include <juce_gui_extra/juce_gui_extra.h>

#include "RecorderEngine.h"

namespace
{
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent()
    {
        title.setText("DAW Streamer Recorder", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(title);

        stage.setText("Stage 5A1 — immediate stream recording + late-stream alignment",
                      juce::dontSendNotification);
        stage.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(stage);

        recordButton.setButtonText("Record");
        recordButton.onClick = [this] { engine.startRecording(); };
        addAndMakeVisible(recordButton);

        stopButton.setButtonText("Stop");
        stopButton.onClick = [this] { engine.stopRecording(); };
        addAndMakeVisible(stopButton);

        status.setJustificationType(juce::Justification::centred);
        status.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        addAndMakeVisible(status);

        details.setJustificationType(juce::Justification::topLeft);
        details.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(details);

        filePath.setJustificationType(juce::Justification::topLeft);
        filePath.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(filePath);

        setSize(960, 540);
        startTimerHz(5);
        timerCallback();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24);
        title.setBounds(area.removeFromTop(42));
        stage.setBounds(area.removeFromTop(30));
        area.removeFromTop(10);

        auto buttons = area.removeFromTop(42);
        const auto buttonWidth = 140;
        const auto gap = 16;
        const auto totalWidth = buttonWidth * 2 + gap;
        auto centredButtons = buttons.withSizeKeepingCentre(totalWidth, buttons.getHeight());
        recordButton.setBounds(centredButtons.removeFromLeft(buttonWidth));
        centredButtons.removeFromLeft(gap);
        stopButton.setBounds(centredButtons.removeFromLeft(buttonWidth));

        area.removeFromTop(12);
        status.setBounds(area.removeFromTop(34));
        area.removeFromTop(8);
        details.setBounds(area.removeFromTop(250));
        area.removeFromTop(8);
        filePath.setBounds(area);
    }

private:
    void timerCallback() override
    {
        const auto snapshot = engine.getSnapshot();

        int startedStreams = 0;
        for (const auto& stream : snapshot.streams)
        {
            if (stream.writerOpen)
                ++startedStreams;
        }

        juce::String state = "IDLE";
        if (!snapshot.lastError.isEmpty())
        {
            state = "ERROR";
        }
        else if (snapshot.sessionActive && snapshot.waitingForStreams)
        {
            state = "RECORDING · " + juce::String(startedStreams)
                  + "/" + juce::String(static_cast<int>(dawstreamer::kStreamRoleCount))
                  + " STREAMS";
        }
        else if (snapshot.sessionActive)
        {
            state = "RECORDING";
        }

        status.setText(state, juce::dontSendNotification);

        juce::String text;
        text << "Role       State       Format                  Callbacks   Queue  Drop  Gaps(frames)   Written\n";
        text << "---------------------------------------------------------------------------------------------\n";

        for (const auto& stream : snapshot.streams)
        {
            const auto role = juce::String(dawstreamer::streamRoleName(stream.role)).paddedRight(' ', 10);
            const auto connection = juce::String(stream.producerPresent ? "CONNECTED" : "MISSING").paddedRight(' ', 12);

            juce::String format = "N/A";
            if (stream.sourceSampleRate != 0)
            {
                format = juce::String(stream.sourceSampleRate) + " Hz "
                       + juce::String(stream.sourceChannels) + "ch b"
                       + juce::String(stream.sourceBlockFrames);
            }
            format = format.paddedRight(' ', 24);

            text << role << connection << format
                 << juce::String(stream.producerCallbacks).paddedRight(' ', 12)
                 << juce::String(stream.pendingBlocks).paddedRight(' ', 7)
                 << juce::String(stream.droppedBlocks).paddedRight(' ', 6)
                 << (juce::String(stream.gapEvents) + " (" + juce::String(stream.gapFrames) + ")").paddedRight(' ', 15)
                 << juce::String(stream.framesWritten);

            if (stream.duplicateClaims > 0)
                text << "   duplicate claims=" << stream.duplicateClaims;

            text << "\n";
        }

        const auto seconds = static_cast<double>(snapshot.takeFrames) / 48000.0;
        text << "\nTake timeline: " << snapshot.takeFrames << " frames ("
             << juce::String(seconds, 2) << " s)";

        if (!snapshot.lastError.isEmpty())
            text << "\nError: " << snapshot.lastError;

        details.setText(text, juce::dontSendNotification);

        const auto pathText = snapshot.takeDirectory.isEmpty()
            ? juce::String("Output: Documents\\DAW Streamer Recordings\\<take>\\")
            : juce::String("Output: ") + snapshot.takeDirectory;
        filePath.setText(pathText, juce::dontSendNotification);

        recordButton.setEnabled(!snapshot.sessionActive);
        stopButton.setEnabled(snapshot.sessionActive);
    }

    RecorderEngine engine;
    juce::Label title;
    juce::Label stage;
    juce::TextButton recordButton;
    juce::TextButton stopButton;
    juce::Label status;
    juce::Label details;
    juce::Label filePath;
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
        return "0.1.0-stage5a1";
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
