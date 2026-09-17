#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "SharedAudioTransport.h"

class RecorderEngine final : private juce::Thread
{
public:
    struct Snapshot
    {
        bool transportOpen = false;
        bool sessionActive = false;
        bool writerOpen = false;
        std::uint64_t producerCallbacks = 0;
        std::uint64_t pendingBlocks = 0;
        std::uint64_t droppedBlocks = 0;
        std::uint64_t oversizedBlocks = 0;
        std::uint64_t framesWritten = 0;
        std::uint32_t sourceSampleRate = 0;
        std::uint32_t sourceChannels = 0;
        std::uint32_t sourceBlockFrames = 0;
        std::uint32_t fileSampleRate = 0;
        std::uint32_t fileChannels = 0;
        juce::String lastFilePath;
        juce::String lastError;
    };

    RecorderEngine();
    ~RecorderEngine() override;

    void startRecording() noexcept;
    void stopRecording() noexcept;
    Snapshot getSnapshot() const;

private:
    enum class Command : int
    {
        none = 0,
        start,
        stop
    };

    void run() override;
    void handleCommand(Command command);
    void handleAudioBlock(const dawstreamer::AudioBlock& block);
    bool openWriterForBlock(const dawstreamer::AudioBlock& block);
    void closeWriter();
    void setError(const juce::String& message);

    dawstreamer::SharedAudioTransport transport;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    std::atomic<int> pendingCommand { static_cast<int>(Command::none) };
    std::atomic<bool> sessionActive { false };
    std::atomic<bool> writerOpen { false };
    std::atomic<std::uint64_t> framesWritten { 0 };
    std::atomic<std::uint32_t> fileSampleRate { 0 };
    std::atomic<std::uint32_t> fileChannels { 0 };

    mutable juce::CriticalSection textLock;
    juce::String lastFilePath;
    juce::String lastError;
};
