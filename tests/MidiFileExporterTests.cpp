#include <cmath>
#include <iostream>
#include <vector>

#include "MidiFileExporter.h"

namespace
{
bool near(double actual, double expected, double tolerance = 0.0006)
{
    return std::abs(actual - expected) <= tolerance;
}

int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}
}

int main()
{
    if (!near(dawstreamer::takeFrameToMidiTick(48000), 1920.0, 0.000001))
        return fail("48 kHz frame-to-PPQ-tick conversion is incorrect");

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("DAWStreamerMidiExportTest_" + juce::Uuid().toString());
    if (directory.createDirectory().failed())
        return fail("Could not create temporary MIDI export test directory");

    std::vector<dawstreamer::RecordedMidiEvent> events;

    dawstreamer::RecordedMidiEvent noteOn;
    noteOn.takeFrame = 24000;
    noteOn.size = 3;
    noteOn.data[0] = 0x90;
    noteOn.data[1] = 60;
    noteOn.data[2] = 100;
    events.push_back(noteOn);

    dawstreamer::RecordedMidiEvent controller;
    controller.takeFrame = 48000;
    controller.size = 3;
    controller.data[0] = 0xB0;
    controller.data[1] = 11;
    controller.data[2] = 96;
    events.push_back(controller);

    dawstreamer::RecordedMidiEvent noteOff;
    noteOff.takeFrame = 72000;
    noteOff.size = 3;
    noteOff.data[0] = 0x80;
    noteOff.data[1] = 60;
    noteOff.data[2] = 0;
    events.push_back(noteOff);

    const auto exportResult = dawstreamer::writeMidiTakeFile(directory, events, 96000);
    if (!exportResult.written || !exportResult.error.isEmpty() || !exportResult.file.existsAsFile())
        return fail("MIDI exporter did not create MIDI.mid");

    std::unique_ptr<juce::FileInputStream> input(exportResult.file.createInputStream());
    if (input == nullptr)
        return fail("Could not reopen exported MIDI file");

    juce::MidiFile midiFile;
    int midiFileType = -1;
    if (!midiFile.readFrom(*input, false, &midiFileType))
        return fail("JUCE could not read exported MIDI file");

    if (midiFileType != 0)
        return fail("Exported MIDI file is not SMF type 0");

    if (midiFile.getNumTracks() != 1)
        return fail("Exported MIDI file is not single-track");

    if (midiFile.getTimeFormat() != dawstreamer::kMidiFileTicksPerQuarterNote)
        return fail("Exported MIDI file is not using the expected PPQ timebase");

    midiFile.convertTimestampTicksToSeconds();
    const auto* track = midiFile.getTrack(0);
    if (track == nullptr)
        return fail("Exported MIDI file has no track");

    std::vector<double> musicalEventTimes;
    double endOfTrackTime = -1.0;

    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto& message = track->getEventPointer(i)->message;
        if (message.isEndOfTrackMetaEvent())
        {
            endOfTrackTime = message.getTimeStamp();
            continue;
        }

        if (!message.isMetaEvent())
            musicalEventTimes.push_back(message.getTimeStamp());
    }

    if (musicalEventTimes.size() != 3)
        return fail("Unexpected number of musical MIDI events after round trip");

    if (!near(musicalEventTimes[0], 0.5)
        || !near(musicalEventTimes[1], 1.0)
        || !near(musicalEventTimes[2], 1.5))
        return fail("Exported MIDI event times do not match take-frame positions at the nominal SMF tempo");

    if (!near(endOfTrackTime, 2.0))
        return fail("End-of-track time does not match audio take length at the nominal SMF tempo");

    directory.deleteRecursively();
    return 0;
}
