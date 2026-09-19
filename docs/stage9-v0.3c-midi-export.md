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

## Timing model

MIDI remains independent of BPM, PPQ, tempo maps and Song tempo changes.

The source of truth is the absolute `takeFrame` captured in v0.3b. The MIDI file uses Standard MIDI File SMPTE timing:

- 30 SMPTE frames per second;
- 200 subframes per SMPTE frame;
- 6000 MIDI ticks per second;
- at the fixed 48 kHz project sample rate this equals exactly 8 audio samples per MIDI tick.

Each event is converted independently from its absolute take frame, so quantisation is bounded to at most half a MIDI tick (4 audio samples, about 0.083 ms at 48 kHz) and cannot accumulate as drift over a long take.

A track-name meta event is written at time zero and End Of Track is placed at the final audio take length. No tempo or time-signature events are written.

## Manual validation

1. Keep the normal four DAW Streamer instances: Vocal, Guitar, Keys and Playback.
2. Route the service MIDI Player to the same DAW Streamer instance that receives Keys audio, as validated in v0.3a/v0.3b.
3. Start Recorder and record a short take with several notes plus CC/Expression/Pitch Bend/Aftertouch.
4. Stop Recorder.
5. Confirm Recorder shows `MIDI file:` followed by the path to `MIDI.mid`.
6. Confirm the take folder contains four WAV files plus `MIDI.mid`.
7. Import all four WAV files and `MIDI.mid` into a DAW with every file aligned to the same zero/start position.
8. Compare MIDI note attacks with the Keys WAV near the beginning, middle and end of the take.
9. Confirm CC, Pitch Bend and Aftertouch survived the round trip.
10. Repeat with several Song switches and different song BPM values. MIDI timing must remain aligned because it is absolute-time based.
11. Record an audio-only take with MIDI routing disabled. Four WAV files must still record normally and absence of `MIDI.mid` must not be treated as an audio-recording error.

## Acceptance

v0.3c is accepted when:

- `MIDI.mid` is created automatically on Stop whenever MIDI was captured;
- imported MIDI preserves Note On/Off, velocity, CC, pitch bend and aftertouch;
- the beginning, middle and end remain aligned with the WAV timeline;
- Song switching and different BPM values do not create drift;
- MIDI export failure is reported separately and does not invalidate already completed WAV files;
- audio-only recording remains unchanged.
