#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawstreamer
{
enum class StreamRole : std::uint32_t
{
    Vocal = 0,
    Guitar,
    Keys,
    Playback
};

inline constexpr std::size_t kStreamRoleCount = 4;
inline constexpr std::uint32_t kProtocolMagic = 0x44535452u; // DSTR
inline constexpr std::uint32_t kProtocolVersion = 2;
inline constexpr std::uint32_t kMaxChannels = 2;
inline constexpr std::uint32_t kMaxFramesPerBlock = 4096;
inline constexpr std::uint32_t kRingCapacity = 64;

const wchar_t* mappingNameForRole(StreamRole role) noexcept;
const char* streamRoleName(StreamRole role) noexcept;

struct AudioBlock
{
    std::uint64_t sequence = 0;
    std::uint64_t producerFrameStart = 0;
    std::int64_t hostTimeInSamples = 0;
    std::uint32_t hostTimeValid = 0;
    std::uint32_t numFrames = 0;
    std::uint32_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::array<std::array<float, kMaxFramesPerBlock>, kMaxChannels> samples {};
};

class SharedAudioTransport final
{
public:
    explicit SharedAudioTransport(const wchar_t* mappingName);
    explicit SharedAudioTransport(StreamRole role);
    ~SharedAudioTransport();

    SharedAudioTransport(const SharedAudioTransport&) = delete;
    SharedAudioTransport& operator=(const SharedAudioTransport&) = delete;

    bool isOpen() const noexcept;

    // Producer ownership is claimed outside the audio thread. It protects the
    // SPSC ring from accidentally receiving two active sender instances.
    bool claimProducer(std::uint64_t ownerToken) noexcept;
    void releaseProducer(std::uint64_t ownerToken) noexcept;
    bool producerClaimedBy(std::uint64_t ownerToken) const noexcept;
    std::uint64_t producerOwner() const noexcept;
    std::uint64_t duplicateClaims() const noexcept;

    // Producer side. Safe for the DAW audio thread: fixed-size copy only,
    // no locks, waits, allocations, file I/O, or system calls.
    bool push(const float* const* channelData,
              std::uint32_t numChannels,
              std::uint32_t numFrames,
              std::uint32_t sampleRate,
              std::uint64_t producerFrameStart,
              bool hostTimeValid,
              std::int64_t hostTimeInSamples,
              std::uint64_t ownerToken) noexcept;

    // Consumer side. Copies the oldest available block and advances the read cursor.
    bool pop(AudioBlock& destination) noexcept;

    // Consumer side. Used when Record starts so frame zero begins at fresh audio.
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
