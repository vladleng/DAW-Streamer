# Stage 9 / v0.3c — MIDI file export

## Goal

Turn the validated in-memory MIDI capture from v0.3b into a standard `.mid` file saved next to the four WAV files for each take.

## Output

When a take contains MIDI events, Stop produces:

```text
Take_YYYY-MM-DD_HH-MM-SS/
  Vocal.wav
  Guitar.wav
  Keys.wav
  Playback.wav
  MIDI.mid
```

If no MIDI was routed during the take, audio recording remains unchanged and no MIDI file is required.

## Compatibility format

The first experimental v0.3c build used SMPTE time division. JUCE could read it back, but the target DAW did not import it normally.

The accepted v0.3c format is therefore the conventional interchange format:

- Standard MIDI File Type 0;
- one track;
- 960 PPQ;
- no captured tempo map;
- no captured time-signature map;
- original channel MIDI messages preserved.

## Manual validation completed

- [x] `MIDI.mid` is created automatically on Stop whenever MIDI was captured.
- [x] The file imports normally in the target DAW.
- [x] MIDI notes match the recorded Keys audio in the tested session excerpt.
- [x] Sustain pedal CC64 survives capture, export and re-import.
- [x] Note/CC/Pitch/Aftertouch capture path was already validated in v0.3a/v0.3b.
- [x] Song switching test completed with zero dropped MIDI events before export.
- [x] Four WAV streams continue to record normally.

One isolated dirty Playback WAV was heard in an early test while the live setup was still loading. The issue did not repeat in subsequent recordings and is not currently reproducible as a DAW Streamer defect. It remains something to watch during continued real-session use.

## Acceptance

v0.3c is accepted. The MIDI feature is promoted to v0.3.0 for practical use. Any defects discovered during continued testing will be handled in maintenance releases (v0.3.1+).
