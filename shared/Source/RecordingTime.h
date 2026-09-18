#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace dawstreamer
{
inline std::string formatRecordingTime(std::uint64_t frames, std::uint32_t sampleRate = 48000)
{
    if (sampleRate == 0)
        return "00:00:00";

    const auto totalSeconds = frames / sampleRate;
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto seconds = totalSeconds % 60;

    char buffer[32] {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02llu:%02llu:%02llu",
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds));
    return buffer;
}
} // namespace dawstreamer
