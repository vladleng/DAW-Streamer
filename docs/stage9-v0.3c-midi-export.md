# Stage 9 / v0.3c — MIDI file export

## Goal

Turn the validated in-memory MIDI capture from v0.3b into a standard `.mid` file saved next to the four WAV files for each take.

## Output

When a take contains MIDI events, Stop should produce:

```text
Take_YYYY-MM-DD_HH-MM-SS/
  Vocal.wav
  Guitar.wav
  Keys.wav
  Playback.wav
  MIDI.mid
```

If no MIDI was routed during the take, audio recording remains unchanged and no MIDI file is required.

## Current compatibility revision

The first v0.3c build wrote a single-track SMF Type 0 file using SMPTE absolute-time division. The file was created correctly and JUCE could read it back, but Studio One/Fender Studio did not accept it for normal project import.

The current test revision therefore writes the most conventional interchange form:

- Standard MIDI File Type 0;
- exactly one MIDI track;
- PPQ time division at 960 ticks per quarter note;
- no captured song tempo map;
- no time-signature map;
- original MIDI channel/message data is preserved.

For the compatibility probe, absolute `takeFrame` values are converted independently to PPQ ticks using a nominal 120 BPM conversion (1920 ticks/second, 25 audio samples/tick at 48 kHz). This keeps the file structurally conventional and avoids cumulative rounding drift inside the exported file.

Important: PPQ is beat-based. If a DAW ignores the file's nominal timing and reinterprets ticks using the current project tempo, absolute alignment with WAV can change. Therefore v0.3c is not accepted merely because the file opens; after compatibility is confirmed we must also validate start/middle/end timing. If Studio One requires PPQ and cannot preserve absolute timing without a tempo map, that limitation will be handled explicitly rather than silently sacrificing synchronization.

## Manual validation

1. Keep the normal four DAW Streamer instances: Vocal, Guitar, Keys and Playback.
2. Route the service MIDI Player to the same DAW Streamer instance that receives Keys audio.
3. Record a short take with several notes plus CC/Expression/Pitch Bend/Aftertouch and stop Recorder.
4. Confirm the take folder contains four WAV files plus `MIDI.mid`.
5. First compatibility check: confirm `MIDI.mid` can be previewed/opened/imported by Studio One/Fender Studio as a normal single-track MIDI file.
6. Confirm Note On/Off, velocity, CC, Pitch Bend and Aftertouch are present.
7. Then import WAV + MIDI at the same start point and compare note attacks near the beginning, middle and end.
8. Repeat with Song switches/different BPM values and check whether project-tempo interpretation moves the MIDI relative to WAV.
9. Record an audio-only take with MIDI routing disabled; four WAV files must remain unaffected.

## Acceptance

v0.3c is accepted only when:

- `MIDI.mid` is a normally importable single-track SMF file;
- musical MIDI messages survive the round trip;
- timing behavior relative to WAV is understood and validated;
- no cumulative drift is introduced;
- MIDI export failure remains separate from completed WAV files;
- audio-only recording remains unchanged.
