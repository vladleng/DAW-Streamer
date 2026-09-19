# Stage 9 / v0.3b — MIDI capture on the shared take timeline

## Goal

Capture MIDI events in Recorder memory on the same absolute take timeline as the four audio streams. This stage deliberately does **not** export a `.mid` file yet.

## Validated routing

Keep the four normal DAW Streamer instances:

- Vocal — audio
- Guitar — audio
- Keys — audio + routed MIDI in the current Show
- Playback — audio

A service MIDI Player sends the controller stream to the same DAW Streamer instance that already carries Keys audio.

Internally the product still has four audio streams plus one logical MIDI event stream. MIDI does not count toward `4/4 streams ready` and cannot block audio recording.

## Timing model

Each sender callback publishes the audio block first and then MIDI events for that same callback.

Each MIDI event contains:

- producer block frame start;
- per-event sample offset;
- absolute producer frame;
- raw MIDI bytes.

Recorder maps the event to the take timeline using the audio stream anchor:

`eventTakeFrame = takeBaseOffset + (producerFrame - producerAnchorFrame)`

No BPM, PPQ, tempo map, or tempo automation participates in synchronization.

## Recorder diagnostics

During a take the Recorder shows:

- MIDI source role;
- received and captured event counts;
- current IPC queue depth;
- dropped and oversized event counts;
- events ignored from a second MIDI-producing role;
- last raw MIDI message;
- first and last captured event time.

The first role that actually produces MIDI during a take becomes the single MIDI source for that take. This prevents accidental duplication if MIDI is routed to more than one DAW Streamer instance.

## Manual acceptance test

1. Install the v0.3b VST3 and launch the matching Recorder.
2. Keep the four existing audio instances in their normal positions.
3. Keep the service MIDI Player routed to the Keys DAW Streamer instance.
4. Before Record, play MIDI and confirm the plug-in reports MIDI input and its MIDI IPC is `CLAIMED`.
5. Start Record.
6. Confirm Recorder reaches normal `4/4` audio recording without waiting for MIDI.
7. Play notes plus CC/Expression, sustain, pitch bend and aftertouch.
8. Confirm Recorder shows `source=Keys` (or whichever role receives MIDI), and `captured` increases.
9. Confirm `drop=0`, `oversized=0`, `other-role=0` in the intended routing.
10. Stop and start host transport while Recorder remains recording; MIDI capture must continue when the host continues delivering callbacks/events.
11. Switch Song and instrument/patch and verify captured events continue increasing.
12. Stop Recorder and confirm the final captured count remains visible.
13. Start a second take and confirm MIDI counters/timing reset for the new take.
14. Make one take with no MIDI routing at all and confirm the four WAV streams still record normally.

## Expected result

v0.3b is accepted when Recorder reliably owns one continuous in-memory MIDI event stream whose timestamps are expressed on the same absolute take timeline as the WAV files, with no MIDI-induced regression to the audio recorder.

The next stage, v0.3c, will serialize this captured event stream to a standard MIDI file while preserving the absolute-time relationship to the WAV files.
