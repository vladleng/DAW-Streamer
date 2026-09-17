#pragma once

#include <cstdint>

namespace dawstreamer
{
enum class RecorderCommand : std::uint8_t
{
    none = 0,
    record = 1,
    stop = 2
};

enum class RecorderState : std::uint32_t
{
    offline = 0,
    idle,
    waitingForStreams,
    recording,
    error
};

struct RecorderControlSnapshot
{
    bool open = false;
    std::uint64_t commandWord = 0;
    std::uint64_t heartbeat = 0;
    std::uint64_t takeFrames = 0;
    RecorderState state = RecorderState::offline;
};

class SharedRecorderControl final
{
public:
    SharedRecorderControl();
    ~SharedRecorderControl();

    SharedRecorderControl(const SharedRecorderControl&) = delete;
    SharedRecorderControl& operator=(const SharedRecorderControl&) = delete;

    bool isOpen() const noexcept;

    // UI/client side. Commands are idempotent desired-state requests. A single
    // atomic packed word preserves the latest command and a monotonic sequence.
    bool sendCommand(RecorderCommand command) noexcept;

    // Recorder side. Returns the newest command when it differs from lastSeenWord.
    bool readCommandAfter(std::uint64_t lastSeenWord,
                          std::uint64_t& currentWord,
                          RecorderCommand& command) const noexcept;

    // Recorder side. Publishes the authoritative recorder state and increments a
    // heartbeat used by plugin editors to distinguish a live Recorder from stale
    // shared memory.
    void publishState(RecorderState state, std::uint64_t takeFrames) noexcept;
    void publishOffline() noexcept;

    RecorderControlSnapshot snapshot() const noexcept;

private:
    struct Impl;
    Impl* impl = nullptr;
};

const char* recorderStateName(RecorderState state) noexcept;
} // namespace dawstreamer
