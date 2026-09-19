#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "SharedAudioTransport.h"
#include "SharedMidiTransport.h"
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
        float peakLinear = 0.0f;
    };

    struct MidiSnapshot
    {
        bool sourceSeen = false;
        dawstreamer::StreamRole sourceRole = dawstreamer::StreamRole::Keys;
        std::uint64_t receivedEvents = 0;
        std::uint64_t capturedEvents = 0;
        std::uint64_t pendingEvents = 0;
        std::uint64_t droppedEvents = 0;
        std::uint64_t oversizedEvents = 0;
        std::uint64_t ignoredOtherRoleEvents = 0;
        std::uint64_t firstTakeFrame = 0;
        std::uint64_t lastTakeFrame = 0;
        std::uint32_t lastMessageSize = 0;
        std::array<std::uint8_t, 3> lastMessageBytes {};
    };

    struct Snapshot
    {
        bool sessionActive = false;
        bool waitingForStreams = false;
        std::uint64_t takeFrames = 0;
        std::array<StreamSnapshot, dawstreamer::kStreamRoleCount> streams {};
        MidiSnapshot midi;
        juce::String takeDirectory;
        juce::String outputRoot;
        juce::String sessionName;
        juce::String lastError;
    };

    explicit RecorderEngine(juce::File outputRootOverride = {});
    ~RecorderEngine() override;

    void startRecording() noexcept;
    void stopRecording() noexcept;
    Snapshot getSnapshot() const;

    void setOutputRoot(juce::File directory);
    juce::File getOutputRoot() const;
    void setSessionName(juce::String name);
    juce::String getSessionName() const;

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
        std::unique_ptr<dawstreamer::SharedMidiTransport> midiTransport;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        std::uint64_t producerAnchorFrame = 0;
        std::uint64_t takeBaseOffset = 0;
        std::uint64_t framesWritten = 0;
        std::uint64_t gapFrames = 0;
        std::uint64_t gapEvents = 0;
        std::uint64_t droppedBaseline = 0;
        std::uint64_t oversizedBaseline = 0;
        std::uint64_t midiDroppedBaseline = 0;
        std::uint64_t midiOversizedBaseline = 0;
        std::uint64_t midiProducerEventsBaseline = 0;
        bool counterBaselineValid = false;
        bool midiCounterBaselineValid = false;
        std::uint32_t fileChannels = 0;
        float peakLinear = 0.0f;
    };

    struct CapturedMidiEvent
    {
        std::uint64_t takeFrame = 0;
        dawstreamer::StreamRole sourceRole = dawstreamer::StreamRole::Keys;
        std::uint32_t size = 0;
        std::array<std::uint8_t, dawstreamer::kMaxMidiMessageBytes> data {};
    };

    void run() override;
    void handleCommand(Command command);
    void handleSharedControlCommand();
    dawstreamer::RecorderState currentSharedState() const noexcept;
    void beginTake();
    void finishTake();
    void drainPendingAudioForStop();
    void drainPendingMidiForStop();
    void drainMidiEvents(std::size_t streamIndex, std::uint32_t maximumEvents);
    void handleMidiEvent(std::size_t streamIndex, const dawstreamer::MidiEvent& event);
    bool startStreamFromFirstBlock(std::size_t streamIndex, const dawstreamer::AudioBlock& firstBlock);
    bool allStreamsStarted() const noexcept;
    bool openWriter(std::size_t streamIndex, const dawstreamer::AudioBlock& firstBlock);
    void handleAudioBlock(std::size_t streamIndex, const dawstreamer::AudioBlock& block);
    void updatePeak(std::size_t streamIndex, const dawstreamer::AudioBlock& block) noexcept;
    bool writeSilence(StreamState& stream, std::uint64_t frames);
    void closeWriters(bool padToCommonEnd);
    void failTake(const juce::String& message);
    void publishSnapshot();
    static juce::String sanitiseSessionName(juce::String name);

    std::array<StreamState, dawstreamer::kStreamRoleCount> streams;
    std::array<float, dawstreamer::kMaxFramesPerBlock> silenceBuffer {};
    std::vector<CapturedMidiEvent> capturedMidiEvents;
    dawstreamer::SharedRecorderControl recorderControl;

    std::atomic<int> pendingCommand { static_cast<int>(Command::none) };
    std::uint64_t lastSharedCommandWord = 0;
    bool sessionActiveInternal = false;
    bool waitingForStreamsInternal = false;
    bool takeOriginEstablished = false;
    bool takeHostOriginValid = false;
    std::int64_t takeHostOriginSamples = 0;
    std::uint64_t globalTakeFrontier = 0;
    bool midiSourceSeenInternal = false;
    dawstreamer::StreamRole midiSourceRoleInternal = dawstreamer::StreamRole::Keys;
    std::uint64_t midiReceivedEventsInternal = 0;
    std::uint64_t midiIgnoredOtherRoleEventsInternal = 0;
    std::uint64_t midiFirstTakeFrameInternal = 0;
    std::uint64_t midiLastTakeFrameInternal = 0;
    std::uint32_t midiLastMessageSizeInternal = 0;
    std::array<std::uint8_t, 3> midiLastMessageBytesInternal {};
    juce::File takeDirectory;
    juce::String lastError;

    mutable juce::CriticalSection configLock;
    juce::File configuredOutputRoot;
    juce::String configuredSessionName;

    mutable juce::CriticalSection snapshotLock;
    Snapshot publishedSnapshot;
};
