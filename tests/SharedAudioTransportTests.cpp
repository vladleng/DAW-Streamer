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
    constexpr wchar_t testMappingName[] = L"Local\\DAWStreamer.Stage4.TransportTests";
    constexpr std::uint64_t ownerToken = 0x1111u;

    dawstreamer::SharedAudioTransport consumer(testMappingName);
    dawstreamer::SharedAudioTransport producer(testMappingName);
    dawstreamer::SharedAudioTransport duplicateProducer(testMappingName);

    if (!consumer.isOpen() || !producer.isOpen())
    {
        std::cerr << "shared mapping did not open\n";
        return 1;
    }

    if (!producer.claimProducer(ownerToken))
    {
        std::cerr << "producer claim failed\n";
        return 2;
    }

    const auto duplicateClaimsBefore = duplicateProducer.duplicateClaims();
    if (duplicateProducer.claimProducer(0x2222u))
    {
        std::cerr << "duplicate producer claim unexpectedly succeeded\n";
        return 3;
    }

    if (duplicateProducer.duplicateClaims() != duplicateClaimsBefore + 1)
    {
        std::cerr << "duplicate claim counter mismatch\n";
        return 4;
    }

    consumer.discardPending();

    const float monoA[] { 0.10f, 0.20f, 0.30f, 0.40f };
    const float* monoChannels[] { monoA };

    if (!producer.push(monoChannels, 1, 4, 48000, 1024, true, 8000, ownerToken))
    {
        std::cerr << "first push failed\n";
        return 5;
    }

    dawstreamer::AudioBlock block;
    if (!consumer.pop(block))
    {
        std::cerr << "first pop failed\n";
        return 6;
    }

    if (block.numChannels != 1 || block.numFrames != 4 || block.sampleRate != 48000
        || block.producerFrameStart != 1024 || block.hostTimeValid == 0
        || block.hostTimeInSamples != 8000)
    {
        std::cerr << "block metadata mismatch\n";
        return 7;
    }

    for (std::uint32_t i = 0; i < block.numFrames; ++i)
    {
        if (!nearlyEqual(block.samples[0][i], monoA[i]))
        {
            std::cerr << "sample mismatch\n";
            return 8;
        }
    }

    const float monoB[] { 1.0f };
    const float monoC[] { 2.0f };
    const float* channelsB[] { monoB };
    const float* channelsC[] { monoC };

    if (!producer.push(channelsB, 1, 1, 48000, 2000, false, 0, ownerToken)
        || !producer.push(channelsC, 1, 1, 48000, 2001, false, 0, ownerToken))
    {
        std::cerr << "ordering pushes failed\n";
        return 9;
    }

    if (!consumer.pop(block) || !nearlyEqual(block.samples[0][0], 1.0f)
        || !consumer.pop(block) || !nearlyEqual(block.samples[0][0], 2.0f))
    {
        std::cerr << "FIFO ordering failed\n";
        return 10;
    }

    consumer.discardPending();
    const auto droppedBefore = producer.droppedBlocks();

    for (std::uint32_t i = 0; i < dawstreamer::kRingCapacity; ++i)
    {
        if (!producer.push(channelsB, 1, 1, 48000, 3000 + i, false, 0, ownerToken))
        {
            std::cerr << "ring filled too early\n";
            return 11;
        }
    }

    if (producer.push(channelsB, 1, 1, 48000, 9999, false, 0, ownerToken))
    {
        std::cerr << "overflow push unexpectedly succeeded\n";
        return 12;
    }

    if (producer.droppedBlocks() != droppedBefore + 1)
    {
        std::cerr << "drop counter mismatch\n";
        return 13;
    }

    std::uint32_t drained = 0;
    while (consumer.pop(block))
        ++drained;

    if (drained != dawstreamer::kRingCapacity || consumer.pendingBlocks() != 0)
    {
        std::cerr << "ring drain mismatch\n";
        return 14;
    }

    producer.releaseProducer(ownerToken);
    if (!duplicateProducer.claimProducer(0x2222u))
    {
        std::cerr << "producer release/reclaim failed\n";
        return 15;
    }
    duplicateProducer.releaseProducer(0x2222u);

    // Four independent role transports must not leak samples into one another.
    dawstreamer::SharedAudioTransport vocalConsumer(dawstreamer::StreamRole::Vocal);
    dawstreamer::SharedAudioTransport guitarConsumer(dawstreamer::StreamRole::Guitar);
    dawstreamer::SharedAudioTransport keysConsumer(dawstreamer::StreamRole::Keys);
    dawstreamer::SharedAudioTransport playbackConsumer(dawstreamer::StreamRole::Playback);

    dawstreamer::SharedAudioTransport* roleConsumers[] {
        &vocalConsumer, &guitarConsumer, &keysConsumer, &playbackConsumer
    };

    for (std::size_t i = 0; i < dawstreamer::kStreamRoleCount; ++i)
    {
        const auto role = static_cast<dawstreamer::StreamRole>(i);
        dawstreamer::SharedAudioTransport roleProducer(role);
        const auto token = 0x10000u + static_cast<std::uint64_t>(i);
        roleConsumers[i]->discardPending();

        if (!roleProducer.claimProducer(token))
        {
            std::cerr << "role producer claim failed\n";
            return 16;
        }

        const float sample[] { static_cast<float>(i + 1) };
        const float* channels[] { sample };
        if (!roleProducer.push(channels, 1, 1, 48000, 0, true, 0, token))
        {
            std::cerr << "role push failed\n";
            return 17;
        }

        if (!roleConsumers[i]->pop(block) || !nearlyEqual(block.samples[0][0], sample[0]))
        {
            std::cerr << "role isolation failed\n";
            return 18;
        }

        roleProducer.releaseProducer(token);
    }

    std::cout << "SharedAudioTransport Stage 4 tests passed\n";
    return 0;
}
