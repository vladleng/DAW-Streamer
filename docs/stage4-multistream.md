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

Stage 4 requires every sender instance to have a stable stream role. The sender UI exposes one of four fixed roles: `Vocal`, `Guitar`, `Keys`, `Playback`.

Each role maps to its own named shared-memory transport and ring buffer. Duplicate active roles are rejected rather than mixed into one stream.

## Audio format

Each sender reads the real host bus layout at runtime:

- mono host channel -> mono stream/WAV;
- stereo host channel -> stereo stream/WAV.

Callback size is always taken from `buffer.getNumSamples()` and is not hard-coded to the device buffer size. The target sample rate remains 48 kHz; no resampling is performed in v0.1.

## Shared timeline

The Recorder owns the take timeline. DAW Play/Stop does not start or stop recording.

Every audio block carries producer sequence/timeline metadata, frame count, format, and host timing information when available. Recorder uses these values to keep each stream on the same take timeline and inserts silence for missing intervals instead of compressing time.

Host timing is synchronization metadata, not Record/Stop authority. Stage 2 established that callbacks continue while DAW transport is stopped and stop completely when a sender itself is bypassed.

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

Take directory:

`Documents/DAW Streamer Recordings/YYYY-MM-DD_HH-MM-SS/`

Files:

- `Vocal.wav`
- `Guitar.wav`
- `Keys.wav`
- `Playback.wav`

All files from one take share the same logical start and end time.

## Diagnostics

Recorder Stage 4 UI shows one row per stream with:

- connection/producer state;
- mono/stereo;
- sample rate;
- current block size;
- producer callbacks;
- queue depth;
- dropped blocks;
- timeline gaps / inserted silence;
- frames written.

The duplicate-claim counter is cumulative diagnostic history. During the first target-system validation, `Vocal` showed historical duplicate claims caused while multiple new instances initially used the default Vocal role before their saved/selected roles were applied. This did not mix sources and did not affect the recorded take.

The post-take `MISSING` label is also diagnostic/UI state rather than a failure of the completed take: the validated files and callback counters confirmed that all four producers delivered audio during recording. Connection-state presentation should be refined separately so historical/stale producer ownership is not confused with take validity.

## Automated tests

The Stage 4 build keeps the bounded shared-memory transport tests and Windows CI build/test gate. Manual target-system validation is required for the actual four-instance synchronization path.

## Manual target-system validation — 2026-09-17

Validated in Fender Studio / Show Page on the target Windows system.

Result:

- four sender instances were assigned to Vocal, Guitar, Keys and Playback;
- Recorder received all four streams;
- actual mono/stereo formats were detected correctly;
- all streams ran at 48 kHz with 512-sample callbacks in the tested Show Page;
- one Record/Stop produced four separate WAV files;
- all four files ended at exactly `1,696,256` frames (`35.34 s`) in the test take;
- `Dropped blocks = 0` for all four streams;
- recorded files were imported together into the DAW and were synchronized without manual alignment;
- audio playback was correct.

The screenshot after the take showed one timeline gap/padding event per stream. The resulting files nevertheless had identical duration and correct alignment, confirming that the common-timeline/padding path preserved synchronization.

## Stage 4 acceptance

Stage 4 is functionally complete: one Recorder take reliably produced four 24-bit / 48 kHz WAV files named Vocal, Guitar, Keys and Playback, with a common timeline, correct mono/stereo handling, no dropped blocks in the manual test, and no manual alignment required after import.
