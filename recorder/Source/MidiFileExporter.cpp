#include "MidiFileExporter.h"

#include <algorithm>

namespace dawstreamer
{
double takeFrameToMidiTick(std::uint64_t takeFrame) noexcept
{
    return static_cast<double>(takeFrame) / kMidiFileSamplesPerTick;
}

MidiFileExportResult writeMidiTakeFile(const juce::File& takeDirectory,
                                       const std::vector<RecordedMidiEvent>& events,
                                       std::uint64_t takeLengthFrames)
{
    MidiFileExportResult result;
    result.file = takeDirectory.getChildFile("MIDI.mid");

    if (events.empty())
        return result;

    if (!takeDirectory.isDirectory())
    {
        result.error = "Take directory is unavailable for MIDI export.";
        return result;
    }

    juce::MidiMessageSequence sequence;

    auto trackName = juce::MidiMessage::textMetaEvent(3, "DAW Streamer MIDI");
    trackName.setTimeStamp(0.0);
    sequence.addEvent(trackName);

    std::uint64_t lastEventFrame = 0;
    for (const auto& event : events)
    {
        if (event.size == 0 || event.size > kMaxMidiMessageBytes)
            continue;

        juce::MidiMessage message(event.data.data(),
                                  static_cast<int>(event.size),
                                  takeFrameToMidiTick(event.takeFrame));
        sequence.addEvent(message);
        lastEventFrame = std::max(lastEventFrame, event.takeFrame);
    }

    if (sequence.getNumEvents() <= 1)
    {
        result.error = "Captured MIDI events could not be converted to a MIDI track.";
        return result;
    }

    const auto finalFrame = std::max(takeLengthFrames, lastEventFrame);
    auto endOfTrack = juce::MidiMessage::endOfTrack();
    endOfTrack.setTimeStamp(takeFrameToMidiTick(finalFrame));
    sequence.addEvent(endOfTrack);
    sequence.sort();

    // Compatibility-first Standard MIDI File: one track, Type 0, conventional PPQ.
    // We deliberately do not write a song tempo map or time-signature events here.
    // Event positions are derived independently from the absolute take timeline.
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(kMidiFileTicksPerQuarterNote);
    midiFile.addTrack(sequence);

    result.file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> output(result.file.createOutputStream());
    if (output == nullptr)
    {
        result.error = "Cannot create MIDI file: " + result.file.getFullPathName();
        return result;
    }

    if (!midiFile.writeTo(*output, 0))
    {
        output.reset();
        result.file.deleteFile();
        result.error = "JUCE failed to write MIDI file: " + result.file.getFullPathName();
        return result;
    }

    output.reset();
    result.written = result.file.existsAsFile();
    if (!result.written)
        result.error = "MIDI export completed without creating the expected file.";

    return result;
}
} // namespace dawstreamer
