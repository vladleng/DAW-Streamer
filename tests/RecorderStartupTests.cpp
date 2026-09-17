#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>

#include <juce_core/juce_core.h>

#include "RecorderEngine.h"
#include "SharedAudioTransport.h"

namespace
{
constexpr std::uint32_t kBlockFrames = 512;
constexpr std::uint32_t kSampleRate = 48000;

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + static_cast<double>(timeoutMs);
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        if (predicate())
            return true;
        juce::Thread::sleep(2);
    }
    return predicate();
}
}

int main()
{
    const auto testRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("DAWStreamer-Stage5A1-"
                                            + juce::String::toHexString(
                                                static_cast<juce::int64>(juce::Time::currentTimeMillis())));
    testRoot.deleteRecursively();

    int result = 0;

    {
        RecorderEngine engine(testRoot);

        std::array<std::unique_ptr<dawstreamer::SharedAudioTransport>, dawstreamer::kStreamRoleCount> producers;
        std::array<std::uint64_t, dawstreamer::kStreamRoleCount> tokens {};
        std::array<std::uint64_t, dawstreamer::kStreamRoleCount> droppedBefore {};

        for (std::size_t i = 0; i < producers.size(); ++i)
        {
            const auto role = static_cast<dawstreamer::StreamRole>(i);
            producers[i] = std::make_unique<dawstreamer::SharedAudioTransport>(role);
            tokens[i] = 0x5A100u + static_cast<std::uint64_t>(i);

            if (!producers[i]->isOpen() || !producers[i]->claimProducer(tokens[i]))
            {
                std::cerr << "producer setup failed for role " << i << "\n";
                result = 1;
                break;
            }

            droppedBefore[i] = producers[i]->droppedBlocks();
        }

        if (result == 0)
        {
            engine.startRecording();
            if (!waitUntil([&] { return engine.getSnapshot().sessionActive; }, 2000))
            {
                std::cerr << "recorder did not enter active state\n";
                result = 2;
            }
        }

        std::array<float, kBlockFrames> samples {};
        samples.fill(0.25f);
        const float* monoChannels[] { samples.data() };
        constexpr std::int64_t hostStart = 1000000;

        // Simulate Vocal/Guitar/Keys running for 100 callbacks before Playback
        // produces its first block. Stage 5A used to stop draining these three
        // queues after their first block, overflowing the 64-block ring.
        if (result == 0)
        {
            for (std::uint64_t blockIndex = 0; blockIndex < 100; ++blockIndex)
            {
                for (std::size_t roleIndex = 0; roleIndex < 3; ++roleIndex)
                {
                    if (!producers[roleIndex]->push(monoChannels,
                                                     1,
                                                     kBlockFrames,
                                                     kSampleRate,
                                                     blockIndex * kBlockFrames,
                                                     true,
                                                     hostStart + static_cast<std::int64_t>(blockIndex * kBlockFrames),
                                                     tokens[roleIndex]))
                    {
                        std::cerr << "early stream push failed at block " << blockIndex << "\n";
                        result = 3;
                        break;
                    }
                }

                if (result != 0)
                    break;

                juce::Thread::sleep(1);
            }
        }

        if (result == 0)
        {
            const auto snapshot = engine.getSnapshot();
            if (!snapshot.sessionActive || !snapshot.waitingForStreams
                || !snapshot.streams[0].writerOpen
                || !snapshot.streams[1].writerOpen
                || !snapshot.streams[2].writerOpen
                || snapshot.streams[3].writerOpen
                || snapshot.takeFrames == 0)
            {
                std::cerr << "early streams were not written while Playback was delayed\n";
                result = 4;
            }
        }

        if (result == 0)
        {
            for (std::size_t i = 0; i < 3; ++i)
            {
                if (producers[i]->droppedBlocks() != droppedBefore[i])
                {
                    std::cerr << "startup drop detected before Playback joined\n";
                    result = 5;
                    break;
                }
            }
        }

        // Playback joins at the current host sample-time. All streams then run
        // together for 20 more callbacks. Its WAV must receive leading silence.
        if (result == 0)
        {
            for (std::uint64_t jointIndex = 0; jointIndex < 20; ++jointIndex)
            {
                const auto hostFrameIndex = 100 + jointIndex;

                for (std::size_t roleIndex = 0; roleIndex < 3; ++roleIndex)
                {
                    if (!producers[roleIndex]->push(monoChannels,
                                                     1,
                                                     kBlockFrames,
                                                     kSampleRate,
                                                     hostFrameIndex * kBlockFrames,
                                                     true,
                                                     hostStart + static_cast<std::int64_t>(hostFrameIndex * kBlockFrames),
                                                     tokens[roleIndex]))
                    {
                        std::cerr << "joint early-stream push failed\n";
                        result = 6;
                        break;
                    }
                }

                if (result != 0)
                    break;

                if (!producers[3]->push(monoChannels,
                                        1,
                                        kBlockFrames,
                                        kSampleRate,
                                        jointIndex * kBlockFrames,
                                        true,
                                        hostStart + static_cast<std::int64_t>(hostFrameIndex * kBlockFrames),
                                        tokens[3]))
                {
                    std::cerr << "delayed Playback push failed\n";
                    result = 7;
                    break;
                }

                juce::Thread::sleep(1);
            }
        }

        if (result == 0
            && !waitUntil([&]
            {
                const auto snapshot = engine.getSnapshot();
                for (const auto& stream : snapshot.streams)
                {
                    if (!stream.writerOpen)
                        return false;
                }
                return !snapshot.waitingForStreams;
            }, 2000))
        {
            std::cerr << "Playback did not join the active take\n";
            result = 8;
        }

        if (result == 0)
        {
            engine.stopRecording();
            if (!waitUntil([&] { return !engine.getSnapshot().sessionActive; }, 2000))
            {
                std::cerr << "recorder did not stop\n";
                result = 9;
            }
        }

        if (result == 0)
        {
            const auto snapshot = engine.getSnapshot();
            const auto expectedFrames = std::uint64_t { 120 } * kBlockFrames;

            if (snapshot.takeFrames != expectedFrames)
            {
                std::cerr << "unexpected take length: " << snapshot.takeFrames << "\n";
                result = 10;
            }

            for (const auto& stream : snapshot.streams)
            {
                if (stream.framesWritten != expectedFrames || stream.droppedBlocks != 0)
                {
                    std::cerr << "stream alignment/drop mismatch\n";
                    result = 11;
                    break;
                }

                const auto file = juce::File(snapshot.takeDirectory)
                                      .getChildFile(juce::String(dawstreamer::streamRoleName(stream.role)) + ".wav");
                if (!file.existsAsFile())
                {
                    std::cerr << "expected WAV missing: " << file.getFullPathName() << "\n";
                    result = 12;
                    break;
                }
            }
        }

        for (std::size_t i = 0; i < producers.size(); ++i)
        {
            if (producers[i] != nullptr)
                producers[i]->releaseProducer(tokens[i]);
        }
    }

    testRoot.deleteRecursively();

    if (result == 0)
        std::cout << "Recorder Stage 5A1 delayed-start test passed\n";

    return result;
}
