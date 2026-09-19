# DAW Streamer v0.3.0

## Release summary

v0.3.0 adds MIDI performance capture to the existing synchronized multi-stream audio recorder while keeping the audio workflow unchanged.

DAW Streamer remains a host-agnostic VST3 sender plus a standalone Windows Recorder. The project originated from the practical limitation of recording separate live-performance signals in Studio One / Fender Studio Show Page without running a second DAW, but the architecture does not depend on Fender-specific APIs and can also be useful in other Windows VST3 live hosts with compatible audio and MIDI routing.

## What is new in v0.3.0

- MIDI input support in the VST3 sender.
- One existing sender instance can carry both its normal audio stream and MIDI, so no fifth audio sender is required.
- MIDI is transported to Recorder through a separate lock-free shared-memory event path.
- MIDI never participates in the `4/4 audio streams ready` condition and cannot block normal audio recording.
- Recorder captures one continuous MIDI event stream alongside the four WAV streams.
- `MIDI.mid` is written automatically next to the WAV files when a take containing MIDI is stopped.
- Export format is Standard MIDI File Type 0 with one track and 960 PPQ for broad DAW compatibility.
- MIDI message data is preserved, including Note On/Off, velocity, control changes, pitch bend, channel/poly aftertouch and program changes.
- Sustain pedal CC64 was manually validated after re-import.
- MIDI diagnostics show source, received/captured counts, queue, drops and export status.

## Typical MIDI-enabled take

```text
Take_YYYY-MM-DD_HH-MM-SS/
  Vocal.wav
  Guitar.wav
  Keys.wav
  Playback.wav
  MIDI.mid
```

## Validated routing

The tested setup keeps the same four DAW Streamer instances used for audio:

- Vocal
- Guitar
- Keys
- Playback

A service MIDI Player sends a copy of the keyboard/controller MIDI to the same DAW Streamer instance that already captures Keys audio. Recorder receives four audio streams plus one logical MIDI event stream.

This allows the virtual instrument routing to continue normally while DAW Streamer records a parallel MIDI copy.

## Timing and musical metadata

DAW Streamer captures MIDI against the same take timeline used by the audio recorder, then exports a conventional single-track MIDI file for DAW compatibility.

v0.3.0 intentionally does not write:

- tempo map;
- tempo meta events;
- time-signature map;
- automatic BPM detection;
- song markers or song-specific MIDI segmentation.

The goal is to preserve the continuous performance for later editing. If musical-grid editing is required, the desired song tempo can be set after import in the target DAW.

## Existing audio features retained

- Four synchronized 24-bit PCM WAV streams at 48 kHz.
- Transparent audio pass-through.
- Common take timeline independent from host Play/Stop and Song switching.
- Global Record/Stop from Recorder or any DAW Streamer instance.
- Boolean VST3 `Recording` parameter for host/control-surface mapping.
- Bidirectional standard VST3 parameter feedback where supported by the host.
- Recording time shown as `HH:MM:SS`.
- Persistent recording folder and Show/session settings.
- Safe unique take folders.

## Manual validation completed

The v0.3 development sequence validated:

- host MIDI routing into DAW Streamer;
- Note On/Off, velocity, CC, pitch bend and aftertouch reception;
- sample offsets inside host processing blocks;
- zero dropped MIDI events during Song switching tests;
- `received == captured` in Recorder diagnostics;
- creation and normal import of `MIDI.mid`;
- note timing matching the recorded Keys audio in the tested session excerpt;
- sustain pedal CC64 surviving capture, export and re-import.

One isolated dirty Playback WAV was observed on an early test while the live setup was still loading; it did not repeat in subsequent recordings and is not currently reproduced as a DAW Streamer defect. Continued real-session testing will determine whether any follow-up fix is needed.

## Validated environment

- Windows 11 x64
- Fender Studio Pro / Show Page
- 48 kHz
- VST3 sender + standalone Recorder
- Four audio streams + MIDI
- MIDI Captain for host-mapped Record/Stop feedback

Other hosts may differ in how they route MIDI to VST3 effects. The audio and Recorder architecture itself remains host-agnostic.

## Installation

The Windows release archive contains:

- `DAW Streamer.vst3`
- `DAW Streamer Recorder.exe`
- `README.md`

Copy the VST3 bundle to a standard Windows VST3 folder, rescan plugins in the host, and run the Recorder application before recording.

## Release policy

v0.3.0 is considered ready for practical use. Further issues found during continued live testing will be handled as maintenance releases such as v0.3.1, v0.3.2 and so on.

The published Windows package is built and tested from `main` by GitHub Actions before the GitHub Release is created.
