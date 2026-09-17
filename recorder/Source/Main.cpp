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

        stage.setText("Stage 3 — one shared-memory audio stream to 24-bit WAV",
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
        details.setFont(juce::FontOptions(14.0f));
        addAndMakeVisible(details);

        filePath.setJustificationType(juce::Justification::topLeft);
        filePath.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(filePath);

        setSize(680, 470);
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
        details.setBounds(area.removeFromTop(190));
        area.removeFromTop(6);
        filePath.setBounds(area);
    }

private:
    void timerCallback() override
    {
        const auto snapshot = engine.getSnapshot();

        juce::String state = "IDLE";
        if (!snapshot.lastError.isEmpty())
            state = "ERROR";
        else if (snapshot.sessionActive && snapshot.writerOpen)
            state = "RECORDING";
        else if (snapshot.sessionActive)
            state = "WAITING FOR AUDIO";

        status.setText(state, juce::dontSendNotification);

        const auto sourceFormat = snapshot.sourceSampleRate == 0
            ? juce::String("N/A")
            : juce::String(snapshot.sourceSampleRate) + " Hz, "
                + juce::String(snapshot.sourceChannels) + " ch, block "
                + juce::String(snapshot.sourceBlockFrames);

        double durationSeconds = 0.0;
        if (snapshot.fileSampleRate != 0)
            durationSeconds = static_cast<double>(snapshot.framesWritten)
                            / static_cast<double>(snapshot.fileSampleRate);

        juce::String text;
        text << "Shared transport: " << (snapshot.transportOpen ? "OPEN" : "CLOSED") << "\n"
             << "Producer callbacks: " << snapshot.producerCallbacks << "\n"
             << "Source format: " << sourceFormat << "\n"
             << "Queued blocks: " << snapshot.pendingBlocks << "\n"
             << "Dropped blocks: " << snapshot.droppedBlocks << "\n"
             << "Oversized blocks: " << snapshot.oversizedBlocks << "\n"
             << "Frames written: " << snapshot.framesWritten
             << "  (" << juce::String(durationSeconds, 2) << " s)";

        if (!snapshot.lastError.isEmpty())
            text << "\nError: " << snapshot.lastError;

        details.setText(text, juce::dontSendNotification);

        const auto pathText = snapshot.lastFilePath.isEmpty()
            ? juce::String("Output: Documents\\DAW Streamer Recordings\\ (file is created after the first audio block)")
            : juce::String("Output: ") + snapshot.lastFilePath;
        filePath.setText(pathText, juce::dontSendNotification);

        recordButton.setEnabled(!snapshot.sessionActive);
        stopButton.setEnabled(snapshot.sessionActive || snapshot.writerOpen);
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
        return "0.1.0-stage3";
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
