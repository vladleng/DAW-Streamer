#include "SharedRecorderControl.h"

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
constexpr wchar_t kControlMappingName[] = L"Local\\DAWStreamer.Stage5A.RecorderControl";
constexpr std::uint32_t kControlMagic = 0x44534354u; // DSCT
constexpr std::uint32_t kControlVersion = 1;

struct alignas(64) SharedControlMemory
{
    alignas(4) std::uint32_t magic;
    std::uint32_t version;
    alignas(8) std::uint64_t commandWord;
    alignas(8) std::uint64_t heartbeat;
    alignas(8) std::uint64_t takeFrames;
    alignas(4) std::uint32_t recorderState;
    std::uint32_t reserved;
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free,
              "Stage 5A requires lock-free 64-bit atomics on Windows x64");
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free,
              "Stage 5A requires lock-free 32-bit atomics on Windows x64");

template <typename T>
std::atomic_ref<T> atomicRef(T& value) noexcept
{
    return std::atomic_ref<T>(value);
}

void closeMapping(HANDLE& mapping, SharedControlMemory*& memory) noexcept
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

std::uint64_t packCommand(std::uint64_t sequence, RecorderCommand command) noexcept
{
    return (sequence << 8u) | static_cast<std::uint64_t>(command);
}

RecorderCommand unpackCommand(std::uint64_t word) noexcept
{
    const auto value = static_cast<std::uint8_t>(word & 0xffu);
    if (value == static_cast<std::uint8_t>(RecorderCommand::record))
        return RecorderCommand::record;
    if (value == static_cast<std::uint8_t>(RecorderCommand::stop))
        return RecorderCommand::stop;
    return RecorderCommand::none;
}
}

struct SharedRecorderControl::Impl
{
    Impl()
    {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                     nullptr,
                                     PAGE_READWRITE,
                                     0,
                                     static_cast<DWORD>(sizeof(SharedControlMemory)),
                                     kControlMappingName);

        if (mapping == nullptr)
            return;

        const auto createdNewMapping = GetLastError() != ERROR_ALREADY_EXISTS;
        memory = static_cast<SharedControlMemory*>(MapViewOfFile(mapping,
                                                                 FILE_MAP_ALL_ACCESS,
                                                                 0,
                                                                 0,
                                                                 sizeof(SharedControlMemory)));

        if (memory == nullptr)
        {
            closeMapping(mapping, memory);
            return;
        }

        if (createdNewMapping)
        {
            std::memset(memory, 0, sizeof(SharedControlMemory));
            memory->version = kControlVersion;
            atomicRef(memory->recorderState).store(static_cast<std::uint32_t>(RecorderState::offline),
                                                   std::memory_order_relaxed);
            atomicRef(memory->magic).store(kControlMagic, std::memory_order_release);
            return;
        }

        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (atomicRef(memory->magic).load(std::memory_order_acquire) != 0)
                break;
            Sleep(1);
        }

        if (atomicRef(memory->magic).load(std::memory_order_acquire) != kControlMagic
            || memory->version != kControlVersion)
        {
            closeMapping(mapping, memory);
        }
    }

    ~Impl()
    {
        closeMapping(mapping, memory);
    }

    HANDLE mapping = nullptr;
    SharedControlMemory* memory = nullptr;
};

SharedRecorderControl::SharedRecorderControl()
    : impl(new (std::nothrow) Impl())
{
}

SharedRecorderControl::~SharedRecorderControl()
{
    delete impl;
}

bool SharedRecorderControl::isOpen() const noexcept
{
    return impl != nullptr && impl->memory != nullptr;
}

bool SharedRecorderControl::sendCommand(RecorderCommand command) noexcept
{
    if (!isOpen() || command == RecorderCommand::none)
        return false;

    auto word = atomicRef(impl->memory->commandWord);
    auto current = word.load(std::memory_order_acquire);

    for (;;)
    {
        const auto sequence = (current >> 8u) + 1u;
        const auto desired = packCommand(sequence, command);

        if (word.compare_exchange_weak(current,
                                       desired,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
            return true;
    }
}

bool SharedRecorderControl::readCommandAfter(std::uint64_t lastSeenWord,
                                             std::uint64_t& currentWord,
                                             RecorderCommand& command) const noexcept
{
    if (!isOpen())
        return false;

    currentWord = atomicRef(impl->memory->commandWord).load(std::memory_order_acquire);
    if (currentWord == lastSeenWord)
        return false;

    command = unpackCommand(currentWord);
    return command != RecorderCommand::none;
}

void SharedRecorderControl::publishState(RecorderState state, std::uint64_t takeFrames) noexcept
{
    if (!isOpen())
        return;

    atomicRef(impl->memory->takeFrames).store(takeFrames, std::memory_order_relaxed);
    atomicRef(impl->memory->recorderState).store(static_cast<std::uint32_t>(state),
                                                 std::memory_order_release);
    atomicRef(impl->memory->heartbeat).fetch_add(1, std::memory_order_release);
}

void SharedRecorderControl::publishOffline() noexcept
{
    if (!isOpen())
        return;

    atomicRef(impl->memory->recorderState).store(static_cast<std::uint32_t>(RecorderState::offline),
                                                 std::memory_order_release);
    atomicRef(impl->memory->heartbeat).fetch_add(1, std::memory_order_release);
}

RecorderControlSnapshot SharedRecorderControl::snapshot() const noexcept
{
    RecorderControlSnapshot result;
    result.open = isOpen();
    if (!result.open)
        return result;

    result.commandWord = atomicRef(impl->memory->commandWord).load(std::memory_order_acquire);
    result.heartbeat = atomicRef(impl->memory->heartbeat).load(std::memory_order_acquire);
    result.takeFrames = atomicRef(impl->memory->takeFrames).load(std::memory_order_acquire);

    const auto rawState = atomicRef(impl->memory->recorderState).load(std::memory_order_acquire);
    if (rawState <= static_cast<std::uint32_t>(RecorderState::error))
        result.state = static_cast<RecorderState>(rawState);
    else
        result.state = RecorderState::error;

    return result;
}

const char* recorderStateName(RecorderState state) noexcept
{
    switch (state)
    {
        case RecorderState::offline: return "OFFLINE";
        case RecorderState::idle: return "IDLE";
        case RecorderState::waitingForStreams: return "WAITING";
        case RecorderState::recording: return "RECORDING";
        case RecorderState::error: return "ERROR";
    }

    return "UNKNOWN";
}
} // namespace dawstreamer
