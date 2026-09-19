#include "SharedMidiTransport.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace
{
bool require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main()
{
    using namespace dawstreamer;

    SharedMidiTransport transport(L"Local\\DAWStreamer.Tests.MidiTransport");
    if (!require(transport.isOpen(), "shared MIDI mapping should open"))
        return 1;

    constexpr std::uint64_t owner = 0x12345678u;
    if (!require(transport.claimProducer(owner), "producer claim should succeed"))
        return 1;

    transport.discardPending();

    const std::array<std::uint8_t, 3> noteOn { 0x90, 52, 69 };
    if (!require(transport.push(noteOn.data(),
                                static_cast<std::uint32_t>(noteOn.size()),
                                1000,
                                83,
                                owner),
                 "note-on push should succeed"))
        return 1;

    MidiEvent event;
    if (!require(transport.pop(event), "pushed MIDI event should be readable"))
        return 1;

    bool ok = true;
    ok &= require(event.blockProducerFrameStart == 1000, "block frame start should round-trip");
    ok &= require(event.sampleOffset == 83, "sample offset should round-trip");
    ok &= require(event.producerFrame == 1083, "absolute producer frame should be block + offset");
    ok &= require(event.size == 3, "MIDI size should round-trip");
    ok &= require(event.data[0] == 0x90 && event.data[1] == 52 && event.data[2] == 69,
                  "MIDI bytes should round-trip");
    ok &= require(transport.pendingEvents() == 0, "queue should be empty after pop");

    const std::array<std::uint8_t, kMaxMidiMessageBytes + 1> tooLarge {};
    const auto oversizedBefore = transport.oversizedEvents();
    ok &= require(!transport.push(tooLarge.data(),
                                  static_cast<std::uint32_t>(tooLarge.size()),
                                  2000,
                                  0,
                                  owner),
                  "oversized event should be rejected");
    ok &= require(transport.oversizedEvents() == oversizedBefore + 1,
                  "oversized counter should increment");

    transport.releaseProducer(owner);
    ok &= require(transport.producerOwner() == 0, "producer release should clear owner");

    return ok ? 0 : 1;
}
