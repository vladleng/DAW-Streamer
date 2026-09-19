#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "SharedAudioTransport.h"

namespace dawstreamer
{
inline constexpr std::size_t kStreamLabelMaxBytes = 95;

struct StreamMetadataSnapshot
{
    bool open = false;
    std::uint64_t ownerToken = 0;
    std::uint64_t revision = 0;
    std::string label;
};

class SharedStreamMetadata final
{
public:
    explicit SharedStreamMetadata(StreamRole role);
    ~SharedStreamMetadata();

    SharedStreamMetadata(const SharedStreamMetadata&) = delete;
    SharedStreamMetadata& operator=(const SharedStreamMetadata&) = delete;

    bool isOpen() const noexcept;

    // Non-audio-thread metadata path. The owner token must match the active
    // sender for the role before the Recorder accepts the label.
    bool publishLabel(std::uint64_t ownerToken, std::string_view utf8Label) noexcept;
    void clearLabel(std::uint64_t ownerToken) noexcept;
    StreamMetadataSnapshot snapshot() const noexcept;

private:
    struct Impl;
    Impl* impl = nullptr;
};
} // namespace dawstreamer
