#include "SharedMidiTransport.h"

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
constexpr wchar_t kMidiVocalMapping[] = L"Local\\DAWStreamer.Midi.Vocal";
constexpr wchar_t kMidiGuitarMapping[] = L"Local\\DAWStreamer.Midi.Guitar";
constexpr wchar_t kMidiKeysMapping[] = L"Local\\DAWStreamer.Midi.Keys";
constexpr wchar_t kMidiPlaybackMapping[] = L"Local\\DAWStreamer.Midi.Playback";

struct alignas(64) SharedMidiControl
{
    alignas(4) std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t capacity;
    std::uint32_t maxMessageBytes;

    alignas(8) std::uint64_t writeSequence;
    alignas(8) std::uint64_t readSequence;
    alignas(8) std::uint64_t droppedEvents;
    alignas(8) std::uint64_t oversizedEvents;
    alignas(8) std::uint64_t producerEvents;
    alignas(8) std::uint64_t producerOwner;
    alignas(8) std::uint64_t duplicateClaims;
};

struct alignas(64) SharedMidiSlot
{
    std::uint64_t sequence;
    std::uint64_t blockProducerFrameStart;
    std::uint64_t producerFrame;
    std::uint32_t sampleOffset;
    std::uint32_t size;
    std::uint8_t data[kMaxMidiMessageBytes];
};

struct SharedMidiMemory
{
    SharedMidiControl control;
    SharedMidiSlot slots[kMidiRingCapacity];
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
              "v0.3b requires lock-free 64-bit atomics on Windows x64");
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free,
              "v0.3b requires lock-free 32-bit atomics on Windows x64");

template <typename T>
std::atomic_ref<T> atomicRef(T& value) noexcept
{
    return std::atomic_ref<T>(value);
}

void closeMapping(HANDLE& mapping, SharedMidiMemory*& memory) noexcept
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

const wchar_t* midiMappingNameForRole(StreamRole role) noexcept
{
    switch (role)
    {
        case StreamRole::Vocal: return kMidiVocalMapping;
        case StreamRole::Guitar: return kMidiGuitarMapping;
        case StreamRole::Keys: return kMidiKeysMapping;
        case StreamRole::Playback: return kMidiPlaybackMapping;
    }

    return kMidiKeysMapping;
}

struct SharedMidiTransport::Impl
{
    explicit Impl(const wchar_t* mappingName)
    {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                     nullptr,
                                     PAGE_READWRITE,
                                     0,
                                     static_cast<DWORD>(sizeof(SharedMidiMemory)),
                                     mappingName);

        if (mapping == nullptr)
            return;

        const auto createdNewMapping = GetLastError() != ERROR_ALREADY_EXISTS;
        memory = static_cast<SharedMidiMemory*>(MapViewOfFile(mapping,
                                                              FILE_MAP_ALL_ACCESS,
                                                              0,
                                                              0,
                                                              sizeof(SharedMidiMemory)));
        if (memory == nullptr)
        {
            closeMapping(mapping, memory);
            return;
        }

        auto& control = memory->control;

        if (createdNewMapping)
        {
            std::memset(memory, 0, sizeof(SharedMidiMemory));
            control.version = kMidiProtocolVersion;
            control.capacity = kMidiRingCapacity;
            control.maxMessageBytes = kMaxMidiMessageBytes;
            atomicRef(control.magic).store(kMidiProtocolMagic, std::memory_order_release);
            return;
        }

        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (atomicRef(control.magic).load(std::memory_order_acquire) != 0)
                break;
            Sleep(1);
        }

        if (atomicRef(control.magic).load(std::memory_order_acquire) != kMidiProtocolMagic
            || control.version != kMidiProtocolVersion
            || control.capacity != kMidiRingCapacity
            || control.maxMessageBytes != kMaxMidiMessageBytes)
        {
            closeMapping(mapping, memory);
        }
    }

    ~Impl()
    {
        closeMapping(mapping, memory);
    }

    HANDLE mapping = nullptr;
    SharedMidiMemory* memory = nullptr;
};

SharedMidiTransport::SharedMidiTransport(StreamRole role)
    : SharedMidiTransport(midiMappingNameForRole(role))
{
}

SharedMidiTransport::SharedMidiTransport(const wchar_t* mappingName)
    : impl(new (std::nothrow) Impl(mappingName))
{
}

SharedMidiTransport::~SharedMidiTransport()
{
    delete impl;
}

bool SharedMidiTransport::isOpen() const noexcept
{
    return impl != nullptr && impl->memory != nullptr;
}

bool SharedMidiTransport::claimProducer(std::uint64_t ownerToken) noexcept
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

void SharedMidiTransport::releaseProducer(std::uint64_t ownerToken) noexcept
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

bool SharedMidiTransport::producerClaimedBy(std::uint64_t ownerToken) const noexcept
{
    return ownerToken != 0 && producerOwner() == ownerToken;
}

std::uint64_t SharedMidiTransport::producerOwner() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.producerOwner).load(std::memory_order_acquire);
}

std::uint64_t SharedMidiTransport::duplicateClaims() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.duplicateClaims).load(std::memory_order_relaxed);
}

bool SharedMidiTransport::push(const std::uint8_t* data,
                               std::uint32_t size,
                               std::uint64_t blockProducerFrameStart,
                               std::uint32_t sampleOffset,
                               std::uint64_t ownerToken) noexcept
{
    if (!isOpen() || !producerClaimedBy(ownerToken))
        return false;

    auto& control = impl->memory->control;
    atomicRef(control.producerEvents).fetch_add(1, std::memory_order_relaxed);

    if (data == nullptr || size == 0 || size > kMaxMidiMessageBytes)
    {
        atomicRef(control.oversizedEvents).fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_relaxed);
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_acquire);

    if (writeSequence - readSequence >= kMidiRingCapacity)
    {
        atomicRef(control.droppedEvents).fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto& slot = impl->memory->slots[writeSequence % kMidiRingCapacity];
    slot.sequence = writeSequence;
    slot.blockProducerFrameStart = blockProducerFrameStart;
    slot.producerFrame = blockProducerFrameStart + sampleOffset;
    slot.sampleOffset = sampleOffset;
    slot.size = size;
    std::memcpy(slot.data, data, size);
    if (size < kMaxMidiMessageBytes)
        std::memset(slot.data + size, 0, kMaxMidiMessageBytes - size);

    atomicRef(control.writeSequence).store(writeSequence + 1, std::memory_order_release);
    return true;
}

bool SharedMidiTransport::pop(MidiEvent& destination) noexcept
{
    if (!isOpen())
        return false;

    auto& control = impl->memory->control;
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_relaxed);
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);

    if (readSequence == writeSequence)
        return false;

    const auto& slot = impl->memory->slots[readSequence % kMidiRingCapacity];
    if (slot.size == 0 || slot.size > kMaxMidiMessageBytes)
    {
        atomicRef(control.readSequence).store(readSequence + 1, std::memory_order_release);
        return false;
    }

    destination.sequence = slot.sequence;
    destination.blockProducerFrameStart = slot.blockProducerFrameStart;
    destination.producerFrame = slot.producerFrame;
    destination.sampleOffset = slot.sampleOffset;
    destination.size = slot.size;
    std::copy_n(slot.data, slot.size, destination.data.begin());

    atomicRef(control.readSequence).store(readSequence + 1, std::memory_order_release);
    return true;
}

void SharedMidiTransport::discardPending() noexcept
{
    if (!isOpen())
        return;

    auto& control = impl->memory->control;
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);
    atomicRef(control.readSequence).store(writeSequence, std::memory_order_release);
}

std::uint64_t SharedMidiTransport::pendingEvents() const noexcept
{
    if (!isOpen())
        return 0;

    auto& control = impl->memory->control;
    const auto writeSequence = atomicRef(control.writeSequence).load(std::memory_order_acquire);
    const auto readSequence = atomicRef(control.readSequence).load(std::memory_order_acquire);
    return std::min<std::uint64_t>(writeSequence - readSequence, kMidiRingCapacity);
}

std::uint64_t SharedMidiTransport::droppedEvents() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.droppedEvents).load(std::memory_order_relaxed);
}

std::uint64_t SharedMidiTransport::oversizedEvents() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.oversizedEvents).load(std::memory_order_relaxed);
}

std::uint64_t SharedMidiTransport::producerEvents() const noexcept
{
    if (!isOpen())
        return 0;
    return atomicRef(impl->memory->control.producerEvents).load(std::memory_order_relaxed);
}
} // namespace dawstreamer
