#include <iostream>
#include <string>

#include "RecordingTime.h"

namespace
{
bool expectEqual(const std::string& actual, const std::string& expected, const char* label)
{
    if (actual == expected)
        return true;

    std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
    return false;
}
}

int main()
{
    constexpr std::uint64_t sampleRate = 48000;
    bool ok = true;

    ok &= expectEqual(dawstreamer::formatRecordingTime(0, sampleRate),
                      "00:00:00",
                      "zero");
    ok &= expectEqual(dawstreamer::formatRecordingTime(sampleRate - 1, sampleRate),
                      "00:00:00",
                      "sub-second");
    ok &= expectEqual(dawstreamer::formatRecordingTime(sampleRate, sampleRate),
                      "00:00:01",
                      "one second");
    ok &= expectEqual(dawstreamer::formatRecordingTime(sampleRate * 60, sampleRate),
                      "00:01:00",
                      "one minute");
    ok &= expectEqual(dawstreamer::formatRecordingTime(sampleRate * (3600 + 2 * 60 + 3), sampleRate),
                      "01:02:03",
                      "hour minute second");
    ok &= expectEqual(dawstreamer::formatRecordingTime(sampleRate * 100 * 3600, sampleRate),
                      "100:00:00",
                      "more than 99 hours");
    ok &= expectEqual(dawstreamer::formatRecordingTime(96000, 96000),
                      "00:00:01",
                      "explicit sample rate");
    ok &= expectEqual(dawstreamer::formatRecordingTime(12345, 0),
                      "00:00:00",
                      "zero sample rate");

    return ok ? 0 : 1;
}
