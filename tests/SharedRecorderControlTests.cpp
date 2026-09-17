#include <iostream>

#include "SharedRecorderControl.h"

int main()
{
    dawstreamer::SharedRecorderControl recorder;
    dawstreamer::SharedRecorderControl pluginA;
    dawstreamer::SharedRecorderControl pluginB;

    if (!recorder.isOpen() || !pluginA.isOpen() || !pluginB.isOpen())
    {
        std::cerr << "recorder control mapping did not open\n";
        return 1;
    }

    recorder.publishState(dawstreamer::RecorderState::idle, 0);
    const auto idle = pluginA.snapshot();
    if (idle.state != dawstreamer::RecorderState::idle || idle.heartbeat == 0)
    {
        std::cerr << "idle state publish failed\n";
        return 2;
    }

    const auto initialWord = idle.commandWord;
    if (!pluginA.sendCommand(dawstreamer::RecorderCommand::record))
    {
        std::cerr << "record command send failed\n";
        return 3;
    }

    std::uint64_t recordWord = initialWord;
    dawstreamer::RecorderCommand command = dawstreamer::RecorderCommand::none;
    if (!recorder.readCommandAfter(initialWord, recordWord, command)
        || command != dawstreamer::RecorderCommand::record
        || recordWord == initialWord)
    {
        std::cerr << "record command read failed\n";
        return 4;
    }

    recorder.publishState(dawstreamer::RecorderState::recording, 48000);
    const auto recording = pluginB.snapshot();
    if (recording.state != dawstreamer::RecorderState::recording
        || recording.takeFrames != 48000
        || recording.heartbeat <= idle.heartbeat)
    {
        std::cerr << "recording state publish failed\n";
        return 5;
    }

    if (!pluginB.sendCommand(dawstreamer::RecorderCommand::stop))
    {
        std::cerr << "stop command send failed\n";
        return 6;
    }

    std::uint64_t stopWord = recordWord;
    command = dawstreamer::RecorderCommand::none;
    if (!recorder.readCommandAfter(recordWord, stopWord, command)
        || command != dawstreamer::RecorderCommand::stop
        || stopWord == recordWord)
    {
        std::cerr << "stop command read failed\n";
        return 7;
    }

    recorder.publishOffline();
    if (pluginA.snapshot().state != dawstreamer::RecorderState::offline)
    {
        std::cerr << "offline state publish failed\n";
        return 8;
    }

    std::cout << "SharedRecorderControl Stage 5A tests passed\n";
    return 0;
}
