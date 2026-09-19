#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "SharedAudioTransport.h"

namespace dawstreamer
{
inline constexpr std::uint32_t kMidiProtocolMagic = 0x44534D49u; // DSMI
inline constexpr std::uint32_t kMidiProtocolVersion = 1;
inline constexpr std::uint32_t kMidiRingCapacity = 4096;
inline constexpr std::uint32_t kMaxMidiMessageBytes = 16;

struct MidiEvent
{
    std::uint64_t sequence = 0;
    std::uint64_t blockProducerFrameStart = 0;
    std::uint64_t producerFrame = 0;
    std::uint32_t sampleOffset = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, kMaxMidiMessageBytes> data {};
};

const wchar_t* midiMappingNameForRole(StreamRole role) noexcept;

class SharedMidiTransport final
{
public:
    explicit SharedMidiTransport(StreamRole role);
    explicit SharedMidiTransport(const wchar_t* mappingName);
    ~SharedMidiTransport();

    SharedMidiTransport(const SharedMidiTransport&) = delete;
    SharedMidiTransport& operator=(const SharedMidiTransport&) = delete;

    bool isOpen() const noexcept;

    bool claimProducer(std::uint64_t ownerToken) noexcept;
    void releaseProducer(std::uint64_t ownerToken) noexcept;
    bool producerClaimedBy(std::uint64_t ownerToken) const noexcept;
    std::uint64_t producerOwner() const noexcept;
    std::uint64_t duplicateClaims() const noexcept;

    // Audio-thread producer side. Fixed-size copy only: no locks, waits,
    // allocation, disk I/O, or system calls.
    bool push(const std::uint8_t* data,
              std::uint32_t size,
              std::uint64_t blockProducerFrameStart,
              std::uint32_t sampleOffset,
              std::uint64_t ownerToken) noexcept;

    // Recorder-thread consumer side.
    bool pop(MidiEvent& destination) noexcept;
    void discardPending() noexcept;

    std::uint64_t pendingEvents() const noexcept;
    std::uint64_t droppedEvents() const noexcept;
    std::uint64_t oversizedEvents() const noexcept;
    std::uint64_t producerEvents() const noexcept;

private:
    struct Impl;
    Impl* impl = nullptr;
};
} // namespace dawstreamer
