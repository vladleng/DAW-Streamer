# Stage 4 — four audio streams and shared timeline

## Goal

Extend the proven Stage 3 single-stream path to four simultaneous sender instances:

- Vocal
- Guitar
- Keys
- Playback

Each sender remains a transparent VST3 pass-through and automatically follows the mono/stereo bus layout supplied by Fender Studio. MIDI is out of scope for Stage 4; Keys are recorded as audio.

One Recorder `Record` action must create one take containing four independent PCM WAV 24-bit / 48 kHz files that can be imported together into a DAW without manual alignment.

## Stream identity

Stage 4 requires every sender instance to have a stable stream role. The sender UI will expose one of four fixed roles: `Vocal`, `Guitar`, `Keys`, `Playback`.

Each role maps to its own named shared-memory transport and ring buffer. Duplicate active roles are an error condition and must be visible in the UI rather than silently mixing two sources into one stream.

## Audio format

Each sender reads the real host bus layout at runtime:

- mono host channel -> mono stream/WAV;
- stereo host channel -> stereo stream/WAV.

Callback size is always taken from `buffer.getNumSamples()` and must not be hard-coded to the device buffer size. The target sample rate remains 48 kHz; no resampling is performed in v0.1.

## Shared timeline

The Recorder owns the take timeline. DAW Play/Stop does not start or stop recording.

Every audio block sent from a plugin carries enough metadata to preserve continuity and detect gaps: producer sequence, frame count, format, and host timing information when available. Recorder uses these values to keep each stream on the same take timeline and inserts silence for missing intervals instead of compressing time.

Host timing is treated as synchronization metadata, not as the Record/Stop authority. This is important because Stage 2 showed that callbacks continue while transport is stopped and stop completely when the sender itself is bypassed.

## Record / Stop

On `Record`:

1. Recorder starts one take and establishes take frame 0.
2. Four output files are prepared under the same take name.
3. Incoming blocks are mapped onto the common take timeline.
4. A stream that appears late receives leading silence to preserve alignment.
5. A missing block or temporary stream interruption produces silence and a diagnostic gap counter; later audio must not move earlier in time.

On `Stop`:

1. Recorder stops accepting take audio.
2. All active writers are flushed and closed.
3. All four files end on the same take duration; shorter/missing streams are padded with silence as required.

## Output layout

Planned take directory:

`Documents/DAW Streamer Recordings/YYYY-MM-DD_HH-MM-SS/`

Files:

- `Vocal.wav`
- `Guitar.wav`
- `Keys.wav`
- `Playback.wav`

All files from one take share the same logical start and end time.

## Diagnostics

Recorder Stage 4 UI should show one row per stream with:

- Connected / Missing / Duplicate
- mono/stereo
- sample rate
- current block size
- producer callbacks
- queue depth
- dropped blocks
- timeline gaps / inserted silence
- frames written

## Tests

Automated tests should cover:

- four independent transports;
- unique role mapping and duplicate-role detection;
- mono and stereo streams together;
- common take start/end length;
- late stream connection -> leading silence;
- missing block -> silence insertion without timeline compression;
- disconnect/reconnect;
- queue overflow/drop counters;
- stopped DAW transport while Recorder continues;
- song changes without ending the take.

Manual target-system test:

1. Insert one sender on Vocal, Guitar, Keys and Playback.
2. Assign the four roles.
3. Verify Recorder sees all four streams and their actual mono/stereo formats.
4. Record a short take with an obvious simultaneous transient/impulse on multiple tracks.
5. Include DAW Stop -> Play and a Song switch while Recorder remains recording.
6. Stop Recorder.
7. Import all four WAVs at the same origin and verify the start/end alignment and transient coincidence.
8. Confirm no unexpected dropped blocks and no audible glitches.

## Stage 4 acceptance

Stage 4 is complete when one Recorder take reliably produces four 24-bit / 48 kHz WAV files named Vocal, Guitar, Keys and Playback, with a common timeline, no manual alignment required, correct mono/stereo handling, and explicit gap/drop diagnostics.
