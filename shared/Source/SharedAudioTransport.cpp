#include "SharedAudioTransport.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dawstreamer
{
namespace
{
constexpr wchar_t kVocalMapping[] = L"Local\\DAWStreamer.Stage4.Vocal";
constexpr wchar_t kGuitarMapping[] = L"Local\\DAWStreamer.Stage4.Guitar";
constexpr wchar_t kKeysMapping[] = L"Local\\DAWStreamer.Stage4.Keys";
constexpr wchar_t kPlaybackMapping[] = L"Local\\DAWStreamer.Stage4.Playback";

struct alignas(64) SharedControl
{
    alignas(4) std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t capacity;
    std::uint32_t maxFramesPerBlock;
    std::uint32_t maxChannels;
    std::uint32_t reserved0;

    alignas(8) std::uint64_t writeSequence;
    alignas(8) std::uint64_t readSequence;
    alignas(8) std::uint64_t droppedBlocks;
    alignas(8) std::uint64_t oversizedBlocks;
    alignas(8) std::uint64_t producerCallbacks;
    alignas(8) std::uint64_t producerOwner;
    alignas(8) std::uint64_t duplicateClaims;

    alignas(4) std::uint32_t lastSampleRate;
    alignas(4) std::uint32_t lastNumChannels;
    alignas(4) std::uint32_t lastNumFrames;
    std::uint32_t reserved1;
};

struct alignas(64) SharedSlot
{
    std::uint64_t sequence;
    std::uint64_t producerFrameStart;
    std::int64_t hostTimeInSamples;
    std::uint32_t hostTimeValid;
    std::uint32_t numFrames;
    std::uint32_t numChannels;
    std::uint32_t sampleRate;
    std::uint32_t reserved;
    alignas(64) float samples[kMaxChannels][kMaxFramesPerBlock];
};

struct SharedMemory
{
    SharedControl control;
    SharedSlot slots[kRingCapacity];
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
              "Stage 4 requires lock-free 64-bit atomics on Windows x64");
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free,
              "Stage 4 requires lock-free 32-bit atomics on Windows x64");

template <typename T>
std::atomic_ref<T> atomicRef(T& value) noexcept
{
    return std::atomic_ref<T>(value);
}

void closeMapping(HANDLE& mapping, SharedMemory*& memory) noexcept
{
    if (memory != nullptr)
    {
        UnmapViewOfFile(memory);
        memory = nullptr;
    }

    if (mapping != nullptr)
    {
        CloseHandle(mapping);
        mapping = nullptr;
    }
}
} // namespace

const wchar_t* mappingNameForRole(StreamRole role) noexcept
{
    switch (role)
    {
        case StreamRole::Vocal: return kVocalMapping;
        case StreamRole::Guitar: return kGuitarMapping;
        case StreamRole::Keys: return kKeysMapping;
        case StreamRole::Playback: return kPlaybackMapping;
    }

    return kVocalMapping;
}

const char* streamRoleName(StreamRole role) noexcept
{
    switch (role)
    {
        case StreamRole::Vocal: return "Inst 1";
        case StreamRole::Guitar: return "Inst 2";
        case StreamRole::Keys: return "Inst 3";
        case StreamRole::Playback: return "Inst 4";
    }

    return "Inst";
}

struct SharedAudioTransport::Impl
{
    explicit Impl(const wchar_t* mappingName)
    {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                     nullptr,
                                     PAGE_READWRITE,
                                     0,
                                     static_cast<DWORD>(sizeof(SharedMemory)),
                                     mappingName);

        if (mapping == nullptr)
            return;

        const auto createdNewMapping = GetLastError() != ERROR_ALREADY_EXISTS;

        memory = static_cast<SharedMemory*>(MapViewOfFile(mapping,
                                                          FILE_MAP_ALL_ACCESS,
                                                          0,
                                                          0,
                                                          sizeof(SharedMemory)));

        if (memory == nullptr)
        {
            closeMapping(mapping, memory);
            return;
        }

        auto& control = memory->control;

        if (createdNewMapping)
        {
            std::memset(memory, 0, sizeof(SharedMemory));
            control.version = kProtocolVersion;
            control.capacity = kRingCapacity;
            control.maxFramesPerBlock = kMaxFramesPerBlock;
            control.maxChannels = kMaxChannels;
            atomicRef(control.magic).store(kProtocolMagic, std::memory_order_release);
            return;
        }

        // Another process may have created the mapping a few microseconds before
        // finishing initialisation. This wait occurs only during construction.
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (atomicRef(control.magic).load(std::memory_order_acquire) != 0)
                break;

            Sleep(1);
        }

        if (atomicRef(control.magic).load(std::memory_order_acquire) != kProtocolMagic
            || control.version != kProtocolVersion
            || control.capacity != kRingCapacity
            || control.maxFramesPerBlock != kMaxFramesPerBlock
            || control.maxChannels != kMaxChannels)
        {
            closeMapping(mapping, memory);
        }
    }

    ~Impl()
    {
        closeMapping(mapping, memory);
    }

    HANDLE mapping = nullptr;
    SharedMemory* memory = nullptr;
};

SharedAudioTransport::SharedAudioTransport(const wchar_t* mappingName)
    : impl(new (std::nothrow) Impl(mappingName))
{
}

SharedAudioTransport::SharedAudioTransport(StreamRole role)
    : SharedAudioTransport(mappingNameForRole(role))
{
}

SharedAudioTransport::~SharedAudioTransport()
{
    delete impl;
}

bool SharedAudioTransport::isOpen() const noexcept
{
    return impl != nullptr && impl->memory != nullptr;
}

bool SharedAudioTransport::claimProducer(std::uint64_t ownerToken) noexcept
{
    if (!isOpen() || ownerToken == 0)
        return false;

    auto& control = impl->memory->control;
    auto owner = atomicRef(control.producerOwner);
    auto expected = std::uint64_t { 0 };

    if (owner.compare_exchange_strong(expected,
                                      ownerToken,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire))
        return true;

    if (expected == ownerToken)
        return true;

    atomicRef(control.duplicateClaims).fetch_add(1, std::memory_order_relaxed);
    return false;
}

void SharedAudioTransport::releaseProducer(std::uint64_t ownerToken) noexcept
{
    if (!isOpen() || ownerToken == 0)
        return;

    auto& owner = impl->memory->control.producerOwner;
    auto expected = ownerToken;
    atomicRef(owner).compare_exchange_strong(expected,
                                             std::uint64_t { 0 },
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
}

bool SharedAudioTransport::producerClaimedBy(std::uint64_t ownerToken) const noexcept
{
    return ownerToken != 0 && producerOwner() == ownerToken;
}

std::uint64_t SharedAudioTransport::producerOwner() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.producerOwner).load(std::memory_order_acquire);
}

std::uint64_t SharedAudioTransport::duplicateClaims() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.duplicateClaims).load(std::memory_order_relaxed);
}

bool SharedAudioTransport::push(const float* const* channelData,
                                std::uint32_t numChannels,
                                std::uint32_t numFrames,
                                std::uint32_t sampleRate,
                                std::uint64_t producerFrameStart,
                                bool hostTimeValid,
                                std::int64_t hostTimeInSamples,
                                std::uint64_t ownerToken) noexcept
{
    if (!isOpen() || !producerClaimedBy(ownerToken))
        return false;

    auto& control = impl->memory->control;
    atomicRef(control.producerCallbacks).fetch_add(1, std::memory_order_relaxed);
    atomicRef(control.lastSampleRate).store(sampleRate, std::memory_order_relaxed);
    atomicRef(control.lastNumChannels).store(numChannels, std::memory_order_relaxed);
    atomicRef(control.lastNumFrames).store(numFrames, std::memory_order_relaxed);

    if (channelData == nullptr
        || numChannels == 0
        || numChannels > kMaxChannels
        || numFrames == 0
        || numFrames > kMaxFramesPerBlock)
    {
        atomicRef(control.oversizedBlocks).fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_relaxed);
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_acquire);

    if (writeSequence - readSequence >= kRingCapacity)
    {
        atomicRef(control.droppedBlocks).fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto& slot = impl->memory->slots[writeSequence % kRingCapacity];
    slot.sequence = writeSequence;
    slot.producerFrameStart = producerFrameStart;
    slot.hostTimeInSamples = hostTimeInSamples;
    slot.hostTimeValid = hostTimeValid ? 1u : 0u;
    slot.numFrames = numFrames;
    slot.numChannels = numChannels;
    slot.sampleRate = sampleRate;

    for (std::uint32_t channel = 0; channel < numChannels; ++channel)
    {
        if (channelData[channel] == nullptr)
        {
            atomicRef(control.oversizedBlocks).fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::memcpy(slot.samples[channel],
                    channelData[channel],
                    static_cast<std::size_t>(numFrames) * sizeof(float));
    }

    atomicRef(control.writeSequence).store(writeSequence + 1, std::memory_order_release);
    return true;
}

bool SharedAudioTransport::pop(AudioBlock& destination) noexcept
{
    if (!isOpen())
        return false;

    auto& control = impl->memory->control;
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_relaxed);
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);

    if (readSequence == writeSequence)
        return false;

    const auto& slot = impl->memory->slots[readSequence % kRingCapacity];

    if (slot.numChannels == 0
        || slot.numChannels > kMaxChannels
        || slot.numFrames == 0
        || slot.numFrames > kMaxFramesPerBlock)
    {
        atomicRef(control.readSequence).store(readSequence + 1, std::memory_order_release);
        return false;
    }

    destination.sequence = slot.sequence;
    destination.producerFrameStart = slot.producerFrameStart;
    destination.hostTimeInSamples = slot.hostTimeInSamples;
    destination.hostTimeValid = slot.hostTimeValid;
    destination.numFrames = slot.numFrames;
    destination.numChannels = slot.numChannels;
    destination.sampleRate = slot.sampleRate;

    for (std::uint32_t channel = 0; channel < slot.numChannels; ++channel)
    {
        std::memcpy(destination.samples[channel].data(),
                    slot.samples[channel],
                    static_cast<std::size_t>(slot.numFrames) * sizeof(float));
    }

    atomicRef(control.readSequence).store(readSequence + 1, std::memory_order_release);
    return true;
}

void SharedAudioTransport::discardPending() noexcept
{
    if (!isOpen())
        return;

    auto& control = impl->memory->control;
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);
    atomicRef(control.readSequence).store(writeSequence, std::memory_order_release);
}

std::uint64_t SharedAudioTransport::pendingBlocks() const noexcept
{
    if (!isOpen())
        return 0;

    auto& control = impl->memory->control;
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_acquire);
    return std::min<std::uint64_t>(writeSequence - readSequence, kRingCapacity);
}

std::uint64_t SharedAudioTransport::droppedBlocks() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.droppedBlocks).load(std::memory_order_relaxed);
}

std::uint64_t SharedAudioTransport::oversizedBlocks() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.oversizedBlocks).load(std::memory_order_relaxed);
}

std::uint64_t SharedAudioTransport::producerCallbacks() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.producerCallbacks).load(std::memory_order_relaxed);
}

std::uint32_t SharedAudioTransport::lastSampleRate() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.lastSampleRate).load(std::memory_order_relaxed);
}

std::uint32_t SharedAudioTransport::lastNumChannels() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.lastNumChannels).load(std::memory_order_relaxed);
}

std::uint32_t SharedAudioTransport::lastNumFrames() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.lastNumFrames).load(std::memory_order_relaxed);
}
} // namespace dawstreamer
