#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "SharedAudioTransport.h"
#include "SharedRecorderControl.h"

class RecorderEngine final : private juce::Thread
{
public:
    struct StreamSnapshot
    {
        dawstreamer::StreamRole role = dawstreamer::StreamRole::Vocal;
        bool transportOpen = false;
        bool producerPresent = false;
        bool writerOpen = false;
        std::uint64_t producerCallbacks = 0;
        std::uint64_t pendingBlocks = 0;
        std::uint64_t droppedBlocks = 0;
        std::uint64_t oversizedBlocks = 0;
        std::uint64_t duplicateClaims = 0;
        std::uint64_t framesWritten = 0;
        std::uint64_t gapFrames = 0;
        std::uint64_t gapEvents = 0;
        std::uint32_t sourceSampleRate = 0;
        std::uint32_t sourceChannels = 0;
        std::uint32_t sourceBlockFrames = 0;
    };

    struct Snapshot
    {
        bool sessionActive = false;
        bool waitingForStreams = false;
        std::uint64_t takeFrames = 0;
        std::array<StreamSnapshot, dawstreamer::kStreamRoleCount> streams {};
        juce::String takeDirectory;
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

    struct StreamState
    {
        dawstreamer::StreamRole role = dawstreamer::StreamRole::Vocal;
        std::unique_ptr<dawstreamer::SharedAudioTransport> transport;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        std::optional<dawstreamer::AudioBlock> firstBlock;
        std::uint64_t producerAnchorFrame = 0;
        std::uint64_t takeBaseOffset = 0;
        std::uint64_t framesWritten = 0;
        std::uint64_t gapFrames = 0;
        std::uint64_t gapEvents = 0;
        std::uint32_t fileChannels = 0;
    };

    void run() override;
    void handleCommand(Command command);
    void handleSharedControlCommand();
    dawstreamer::RecorderState currentSharedState() const noexcept;
    void beginTake();
    void finishTake();
    bool tryStartTakeFromFirstBlocks();
    bool openWriter(std::size_t streamIndex, const dawstreamer::AudioBlock& firstBlock);
    void handleAudioBlock(std::size_t streamIndex, const dawstreamer::AudioBlock& block);
    bool writeSilence(StreamState& stream, std::uint64_t frames);
    void closeWriters(bool padToCommonEnd);
    void failTake(const juce::String& message);
    void publishSnapshot();

    std::array<StreamState, dawstreamer::kStreamRoleCount> streams;
    std::array<float, dawstreamer::kMaxFramesPerBlock> silenceBuffer {};
    dawstreamer::SharedRecorderControl recorderControl;

    std::atomic<int> pendingCommand { static_cast<int>(Command::none) };
    std::uint64_t lastSharedCommandWord = 0;
    bool sessionActiveInternal = false;
    bool waitingForStreamsInternal = false;
    std::uint64_t globalTakeFrontier = 0;
    juce::File takeDirectory;
    juce::String lastError;

    mutable juce::CriticalSection snapshotLock;
    Snapshot publishedSnapshot;
};
