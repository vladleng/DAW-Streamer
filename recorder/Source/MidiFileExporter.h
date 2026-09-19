#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "SharedMidiTransport.h"

namespace dawstreamer
{
inline constexpr std::uint32_t kMidiFileSourceSampleRate = 48000;
inline constexpr int kMidiFileSmpteFramesPerSecond = 30;
inline constexpr int kMidiFileSmpteSubframesPerFrame = 200;
inline constexpr double kMidiFileTicksPerSecond =
    static_cast<double>(kMidiFileSmpteFramesPerSecond * kMidiFileSmpteSubframesPerFrame);
inline constexpr double kMidiFileSamplesPerTick =
    static_cast<double>(kMidiFileSourceSampleRate) / kMidiFileTicksPerSecond;

struct RecordedMidiEvent
{
    std::uint64_t takeFrame = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, kMaxMidiMessageBytes> data {};
};

struct MidiFileExportResult
{
    bool written = false;
    juce::File file;
    juce::String error;
};

double takeFrameToMidiTick(std::uint64_t takeFrame) noexcept;

MidiFileExportResult writeMidiTakeFile(const juce::File& takeDirectory,
                                       const std::vector<RecordedMidiEvent>& events,
                                       std::uint64_t takeLengthFrames);
} // namespace dawstreamer
