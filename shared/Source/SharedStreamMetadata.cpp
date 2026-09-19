#include "SharedStreamMetadata.h"

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
constexpr std::uint32_t kMetadataMagic = 0x44534d44u; // DSMD
constexpr std::uint32_t kMetadataVersion = 1;
constexpr wchar_t kVocalMetadata[] = L"Local\\DAWStreamer.StreamMetadata.V1.Vocal";
constexpr wchar_t kGuitarMetadata[] = L"Local\\DAWStreamer.StreamMetadata.V1.Guitar";
constexpr wchar_t kKeysMetadata[] = L"Local\\DAWStreamer.StreamMetadata.V1.Keys";
constexpr wchar_t kPlaybackMetadata[] = L"Local\\DAWStreamer.StreamMetadata.V1.Playback";

const wchar_t* metadataMappingName(StreamRole role) noexcept
{
    switch (role)
    {
        case StreamRole::Vocal: return kVocalMetadata;
        case StreamRole::Guitar: return kGuitarMetadata;
        case StreamRole::Keys: return kKeysMetadata;
        case StreamRole::Playback: return kPlaybackMetadata;
    }
    return kVocalMetadata;
}

struct alignas(64) SharedMetadataMemory
{
    alignas(4) std::uint32_t magic;
    std::uint32_t version;
    alignas(8) std::uint64_t revision;
    alignas(8) std::uint64_t ownerToken;
    char label[kStreamLabelMaxBytes + 1];
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
              "Stream metadata requires lock-free 64-bit atomics on Windows x64");
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free,
              "Stream metadata requires lock-free 32-bit atomics on Windows x64");

template <typename T>
std::atomic_ref<T> atomicRef(T& value) noexcept
{
    return std::atomic_ref<T>(value);
}

void closeMapping(HANDLE& mapping, SharedMetadataMemory*& memory) noexcept
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

std::size_t utf8SafeLength(std::string_view text) noexcept
{
    auto length = std::min<std::size_t>(text.size(), kStreamLabelMaxBytes);
    while (length > 0 && length < text.size()
           && (static_cast<unsigned char>(text[length]) & 0xc0u) == 0x80u)
        --length;
    return length;
}
}

struct SharedStreamMetadata::Impl
{
    explicit Impl(StreamRole role)
    {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                     nullptr,
                                     PAGE_READWRITE,
                                     0,
                                     static_cast<DWORD>(sizeof(SharedMetadataMemory)),
                                     metadataMappingName(role));
        if (mapping == nullptr)
            return;

        const auto createdNew = GetLastError() != ERROR_ALREADY_EXISTS;
        memory = static_cast<SharedMetadataMemory*>(MapViewOfFile(mapping,
                                                                  FILE_MAP_ALL_ACCESS,
                                                                  0,
                                                                  0,
                                                                  sizeof(SharedMetadataMemory)));
        if (memory == nullptr)
        {
            closeMapping(mapping, memory);
            return;
        }

        if (createdNew)
        {
            std::memset(memory, 0, sizeof(SharedMetadataMemory));
            memory->version = kMetadataVersion;
            atomicRef(memory->magic).store(kMetadataMagic, std::memory_order_release);
            return;
        }

        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (atomicRef(memory->magic).load(std::memory_order_acquire) != 0)
                break;
            Sleep(1);
        }

        if (atomicRef(memory->magic).load(std::memory_order_acquire) != kMetadataMagic
            || memory->version != kMetadataVersion)
            closeMapping(mapping, memory);
    }

    ~Impl()
    {
        closeMapping(mapping, memory);
    }

    HANDLE mapping = nullptr;
    SharedMetadataMemory* memory = nullptr;
};

SharedStreamMetadata::SharedStreamMetadata(StreamRole role)
    : impl(new (std::nothrow) Impl(role))
{
}

SharedStreamMetadata::~SharedStreamMetadata()
{
    delete impl;
}

bool SharedStreamMetadata::isOpen() const noexcept
{
    return impl != nullptr && impl->memory != nullptr;
}

bool SharedStreamMetadata::publishLabel(std::uint64_t ownerToken,
                                        std::string_view utf8Label) noexcept
{
    if (!isOpen() || ownerToken == 0)
        return false;

    auto& memory = *impl->memory;
    auto revision = atomicRef(memory.revision);
    revision.fetch_add(1, std::memory_order_acq_rel); // odd: write in progress

    atomicRef(memory.ownerToken).store(ownerToken, std::memory_order_relaxed);
    std::memset(memory.label, 0, sizeof(memory.label));
    const auto length = utf8SafeLength(utf8Label);
    if (length > 0)
        std::memcpy(memory.label, utf8Label.data(), length);
    memory.label[length] = '\0';

    revision.fetch_add(1, std::memory_order_release); // even: stable snapshot
    return true;
}

void SharedStreamMetadata::clearLabel(std::uint64_t ownerToken) noexcept
{
    if (!isOpen() || ownerToken == 0)
        return;

    auto& memory = *impl->memory;
    if (atomicRef(memory.ownerToken).load(std::memory_order_acquire) != ownerToken)
        return;

    auto revision = atomicRef(memory.revision);
    revision.fetch_add(1, std::memory_order_acq_rel);
    atomicRef(memory.ownerToken).store(0, std::memory_order_relaxed);
    std::memset(memory.label, 0, sizeof(memory.label));
    revision.fetch_add(1, std::memory_order_release);
}

StreamMetadataSnapshot SharedStreamMetadata::snapshot() const noexcept
{
    StreamMetadataSnapshot result;
    result.open = isOpen();
    if (!result.open)
        return result;

    auto& memory = *impl->memory;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto begin = atomicRef(memory.revision).load(std::memory_order_acquire);
        if ((begin & 1u) != 0)
            continue;

        const auto owner = atomicRef(memory.ownerToken).load(std::memory_order_relaxed);
        char copy[kStreamLabelMaxBytes + 1] {};
        std::memcpy(copy, memory.label, sizeof(copy));
        copy[kStreamLabelMaxBytes] = '\0';

        const auto end = atomicRef(memory.revision).load(std::memory_order_acquire);
        if (begin == end && (end & 1u) == 0)
        {
            result.ownerToken = owner;
            result.revision = end;
            result.label = copy;
            return result;
        }
    }

    return result;
}
} // namespace dawstreamer
