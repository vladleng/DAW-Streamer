#include <cmath>
#include <iostream>

#include "SharedAudioTransport.h"

namespace
{
bool nearlyEqual(float a, float b)
{
    return std::abs(a - b) < 0.000001f;
}
}

int main()
{
    constexpr wchar_t testMappingName[] = L"Local\\DAWStreamer.Stage3.TransportTests";

    dawstreamer::SharedAudioTransport consumer(testMappingName);
    dawstreamer::SharedAudioTransport producer(testMappingName);

    if (!consumer.isOpen() || !producer.isOpen())
    {
        std::cerr << "shared mapping did not open\n";
        return 1;
    }

    consumer.discardPending();

    const float monoA[] { 0.10f, 0.20f, 0.30f, 0.40f };
    const float* monoChannels[] { monoA };

    if (!producer.push(monoChannels, 1, 4, 48000))
    {
        std::cerr << "first push failed\n";
        return 2;
    }

    dawstreamer::AudioBlock block;
    if (!consumer.pop(block))
    {
        std::cerr << "first pop failed\n";
        return 3;
    }

    if (block.numChannels != 1 || block.numFrames != 4 || block.sampleRate != 48000)
    {
        std::cerr << "block metadata mismatch\n";
        return 4;
    }

    for (std::uint32_t i = 0; i < block.numFrames; ++i)
    {
        if (!nearlyEqual(block.samples[0][i], monoA[i]))
        {
            std::cerr << "sample mismatch\n";
            return 5;
        }
    }

    const float monoB[] { 1.0f };
    const float monoC[] { 2.0f };
    const float* channelsB[] { monoB };
    const float* channelsC[] { monoC };

    if (!producer.push(channelsB, 1, 1, 48000)
        || !producer.push(channelsC, 1, 1, 48000))
    {
        std::cerr << "ordering pushes failed\n";
        return 6;
    }

    if (!consumer.pop(block) || !nearlyEqual(block.samples[0][0], 1.0f)
        || !consumer.pop(block) || !nearlyEqual(block.samples[0][0], 2.0f))
    {
        std::cerr << "FIFO ordering failed\n";
        return 7;
    }

    consumer.discardPending();
    const auto droppedBefore = producer.droppedBlocks();

    for (std::uint32_t i = 0; i < dawstreamer::kRingCapacity; ++i)
    {
        if (!producer.push(channelsB, 1, 1, 48000))
        {
            std::cerr << "ring filled too early\n";
            return 8;
        }
    }

    if (producer.push(channelsB, 1, 1, 48000))
    {
        std::cerr << "overflow push unexpectedly succeeded\n";
        return 9;
    }

    if (producer.droppedBlocks() != droppedBefore + 1)
    {
        std::cerr << "drop counter mismatch\n";
        return 10;
    }

    std::uint32_t drained = 0;
    while (consumer.pop(block))
        ++drained;

    if (drained != dawstreamer::kRingCapacity || consumer.pendingBlocks() != 0)
    {
        std::cerr << "ring drain mismatch\n";
        return 11;
    }

    // A missing consumer must never block the producer. Filling the bounded ring
    // and dropping the next block is the expected safe failure mode.
    consumer.discardPending();
    for (std::uint32_t i = 0; i < dawstreamer::kRingCapacity; ++i)
        producer.push(channelsB, 1, 1, 48000);

    if (producer.push(channelsB, 1, 1, 48000))
    {
        std::cerr << "bounded no-consumer behaviour failed\n";
        return 12;
    }

    std::cout << "SharedAudioTransport Stage 3 tests passed\n";
    return 0;
}
