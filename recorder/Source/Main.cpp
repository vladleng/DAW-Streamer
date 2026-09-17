#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
class MainComponent final : public juce::Component
{
public:
    MainComponent()
    {
        title.setText("DAW Streamer Recorder", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(title);

        status.setText("Stage 2 skeleton\nAudio receiver is not connected yet.",
                       juce::dontSendNotification);
        status.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(status);

        setSize(520, 260);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24);
        title.setBounds(area.removeFromTop(56));
        status.setBounds(area);
    }

private:
    juce::Label title;
    juce::Label status;
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
        return "0.1.0-dev";
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
