# DAW Streamer for Fender Studio v0.1a

## Purpose

This branch adds a Fender Studio-oriented workflow without replacing or forking the universal DAW Streamer core.

The same shared-memory audio/MIDI transports and the same `RecorderEngine` are used. The difference is where the recorder frontend lives:

- Universal workflow: track senders + standalone `DAW Streamer Recorder.exe`.
- Fender workflow: track senders + one `Master Recorder` VST3 instance on the persistent Main/Master channel.

## Plugin

The new VST3 is named:

`DAW Streamer Fender.vst3`

Each instance has two modes:

### Sender

Use on the normal Show channels. Select one stream slot:

- Vocal
- Guitar
- Keys
- Playback

Sender mode behaves like the universal DAW Streamer sender: audio is passed through unchanged while a copy is written to the shared-memory transport. MIDI received by that sender is also copied to the shared MIDI event transport.

### Master Recorder

Use exactly one instance on Fender Studio's persistent Main/Master channel.

Master mode:

- does not claim Vocal/Guitar/Keys/Playback;
- does not record the Main bus by default;
- passes Main audio through unchanged;
- owns the same `RecorderEngine` used by the standalone Recorder;
- shows recording folder, session name, Record/Stop, time, all four stream diagnostics and MIDI diagnostics inside the plugin window;
- writes the same take output: four WAV files plus `MIDI.mid` when MIDI was captured.

## Recorder backend ownership

`RecorderEngine` now claims a single backend ownership lock.

This prevents two embedded Master instances, or an embedded Master plus the standalone Recorder, from both acting as active recorders. A second backend reports a conflict and does not publish recorder state or write files.

This is intentionally separate from the sender role ownership mechanism.

## State

The Fender plugin stores in the Show/project state:

- mode (`Sender` / `Master Recorder`);
- sender stream role;
- Master recording folder;
- Master session name.

A saved Show should therefore reopen with its Main instance still in Master Recorder mode and track instances restored to their sender roles.

## Record control

The existing global `Recording` VST3 parameter remains available.

Record/Stop can therefore come from:

- Master plugin UI;
- Sender plugin UI / parameter mapping;
- Fender Studio Performance Mode mapping;
- MIDI Captain or another control surface routed through the host.

All instances observe the same authoritative recorder state through `SharedRecorderControl`.

## v0.1a manual test

1. Install `DAW Streamer Fender.vst3` from the CI artifact.
2. Do not run the standalone Recorder for the first test.
3. Put one Fender plugin instance on each required track in `Sender` mode and assign Vocal/Guitar/Keys/Playback.
4. Keep the existing service MIDI routing into the Keys sender.
5. Put one Fender plugin instance on Main/Master and choose `Master Recorder`.
6. Select a recording folder and session name in the Master window.
7. Confirm all four rows become ACTIVE.
8. Start Record from the Master window.
9. Play audio and MIDI, switch Songs, use sustain pedal, and optionally stop/start host transport while DAW Streamer continues recording.
10. Stop from the Master window.
11. Confirm the take contains `Vocal.wav`, `Guitar.wav`, `Keys.wav`, `Playback.wav` and `MIDI.mid`.
12. Confirm `drop=0` and that MIDI still aligns with Keys audio.
13. Save and reopen the Show; confirm Master/Sender modes and roles restore correctly.
14. Optional ownership test: while the embedded Master is active, open standalone Recorder or create a second Master. The second backend must show a conflict and must not create competing files.

## Not in v0.1a

This first build proves the embedded-recorder architecture. It does not yet use proprietary Fender/PreSonus context metadata.

Later iterations can add document/show name, channel name/color/type and document folder through the published host extension APIs after the Master/Sender recording workflow has been manually accepted.
