# Stage 9 / v0.3a — MIDI source probe

## Goal

Determine whether a normal DAW Streamer VST3 sender can receive MIDI events directly from the host with sample offsets suitable for later sample-accurate synchronization to the existing audio take timeline.

This build does **not** record MIDI and does **not** write `.mid` files. It is diagnostics only.

## Changes in v0.3a

- DAW Streamer now declares VST3 MIDI input capability.
- `acceptsMidi()` returns `true`.
- Incoming `juce::MidiBuffer` events are inspected inside the existing audio callback.
- The audio thread only updates atomic diagnostic counters/values; it does not allocate, write to disk, lock, or wait.
- Existing audio pass-through, shared audio transport, Recorder control and `Recording` VST3 parameter are unchanged.

## MIDI diagnostics shown in the VST3 window

- `MIDI input` — `WAITING` until at least one event has been received, then `RECEIVED`.
- `MIDI events total` — total number of MIDI events seen by this plugin instance.
- `Events in last MIDI block` — event count from the most recent block that contained MIDI.
- `Last MIDI` — decoded summary for Note On/Off, CC, pitch wheel, channel pressure, poly aftertouch, program change, or other MIDI.
- `MIDI sample offset` — the JUCE/VST3 sample offset of the last event inside its audio callback.

## Manual test — first pass

Use one DAW Streamer instance on the keyboard/instrument path where MIDI is expected to reach the plug-in.

1. Start the host and open the Show/live project normally.
2. Open DAW Streamer v0.3a. Confirm the title says `DAW Streamer v0.3a - MIDI source probe`.
3. Confirm audio still passes normally and existing Recorder control still works.
4. Before playing MIDI, `MIDI input` should show `WAITING` and `MIDI events total` should be `0`.
5. Play several notes on the keyboard/controller.
6. If host MIDI reaches the VST3, `MIDI input` changes to `RECEIVED`, the counter increases, `Last MIDI` changes, and `MIDI sample offset` shows a value in samples.
7. Move controls that generate CC, pitch bend and aftertouch if available and verify their message types appear.

## Required host-behaviour checks

After MIDI reception is confirmed, repeat the test in these states:

- host transport stopped;
- host transport playing;
- after switching Song;
- after switching the keyboard/instrument patch or preset;
- several rapid Song/patch changes.

For each case record whether:

- audio callbacks continue;
- MIDI events continue arriving;
- event counters continue increasing;
- `MIDI sample offset` remains within the current audio block range (`0 <= offset < Current block`).

## Interpretation

### Success

If MIDI arrives directly in the VST3 with valid per-block sample offsets through the live workflow, the host MIDI path becomes the preferred source for v0.3b.

The later canonical event position will be:

`eventTakeFrame = audioBlockTakeFrame + midiSampleOffset`

No BPM, PPQ or tempo map is required for audio/MIDI synchronization.

### Host does not send MIDI to the sender

If the plug-in works normally but `MIDI input` remains `WAITING`, this is a valid v0.3a result. The next experiment will use an independent MIDI input / virtual MIDI port in Recorder and map its timestamp to the same take timeline.

## Acceptance criteria for v0.3a

v0.3a is complete when we know, from a real host test, whether the normal VST3 sender path can provide the MIDI stream required for capture, and whether its sample offsets remain usable across transport and Song/patch switching.
