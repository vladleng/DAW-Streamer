#pragma once

#include <array>
#include <cstdint>

namespace dawstreamer
{
inline constexpr wchar_t kSharedMemoryName[] = L"Local\\DAWStreamer.Stage3.Stream0";
inline constexpr std::uint32_t kProtocolMagic = 0x44535452u; // DSTR
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFramesPerBlock = 4096;
inline constexpr std::uint32_t kRingCapacity = 64;

struct AudioBlock
{
    std::uint64_t sequence = 0;
    std::uint32_t numFrames = 0;
    std::uint32_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::array<std::array<float, kMaxFramesPerBlock>, kMaxChannels> samples {};
};

class SharedAudioTransport final
{
public:
    explicit SharedAudioTransport(const wchar_t* mappingName = kSharedMemoryName);
    ~SharedAudioTransport();

    SharedAudioTransport(const SharedAudioTransport&) = delete;
    SharedAudioTransport& operator=(const SharedAudioTransport&) = delete;

    bool isOpen() const noexcept;

    // Producer side. Safe for the DAW audio thread: fixed-size copy only,
    // no locks, waits, allocations, file I/O, or system calls.
    bool push(const float* const* channelData,
              std::uint32_t numChannels,
              std::uint32_t numFrames,
              std::uint32_t sampleRate) noexcept;

    // Consumer side. Copies the oldest available block and advances the read cursor.
    bool pop(AudioBlock& destination) noexcept;

    // Consumer side. Used when Record starts so frame zero begins at the newest block.
    void discardPending() noexcept;

    std::uint64_t pendingBlocks() const noexcept;
    std::uint64_t droppedBlocks() const noexcept;
    std::uint64_t oversizedBlocks() const noexcept;
    std::uint64_t producerCallbacks() const noexcept;
    std::uint32_t lastSampleRate() const noexcept;
    std::uint32_t lastNumChannels() const noexcept;
    std::uint32_t lastNumFrames() const noexcept;

private:
    struct Impl;
    Impl* impl = nullptr;
};
} // namespace dawstreamer
