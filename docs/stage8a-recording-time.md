# Stage 8A - Recording time

## Goal

Replace raw take-frame counters in the user-facing Recorder/VST3 UI with a readable recording duration while keeping the existing sample timeline as the internal source of truth.

## Scope

- Display `HH:MM:SS` recording time in every DAW Streamer VST3 instance.
- Display the same `HH:MM:SS` recording time prominently in Recorder.
- Keep `takeFrames` / the sample counter unchanged inside Core and shared control.
- Reset displayed time when a new take starts because the authoritative take timeline resets.
- Do not couple recording time to Fender Studio Play/Stop transport.
- Do not change audio transport, WAV writing, IPC, synchronization or Record/Stop behavior.

## Implementation

`shared/Source/RecordingTime.h` formats the authoritative take frame count as whole elapsed seconds. Version 0.2 still targets 48 kHz, so the UI converts the existing shared take timeline to wall-clock duration without introducing a second timer.

The VST3 reads `recorderTakeFrames` from the existing Recorder control snapshot. Recorder reads `snapshot.takeFrames` from `RecorderEngine`. Both therefore show the same timeline and cannot drift independently.

Raw frame counters remain available in Core and technical per-stream diagnostics where useful; the primary take-duration display no longer exposes frames to the user.

## Automated test

`recording-time-format` covers:

- zero and sub-second values;
- second/minute/hour transitions;
- durations over 99 hours;
- explicit alternate sample rates;
- zero sample-rate guard behavior.

## Manual acceptance

1. Start Recorder and open at least two DAW Streamer VST3 instances.
2. Confirm Recorder shows `Recording time: 00:00:00` and each online plugin shows `Recording time 00:00:00` before a new take.
3. Press Record from either Recorder or one plugin.
4. Let the take run for at least 70 seconds. Confirm Recorder and all plugin instances show the same `HH:MM:SS` value and cross `00:01:00` correctly.
5. Use Fender Studio Play/Stop and change Song while Recorder remains recording. Confirm the recording clock continues from the Recorder take timeline.
6. Stop recording. Confirm all UIs retain the final take duration.
7. Start a second take. Confirm all UIs reset to `00:00:00` and advance together.
8. Confirm WAV creation/synchronization and Drop behavior are unchanged.

Stage 8A is accepted only after Windows CI is green and the manual check passes on the target Fender Studio Show Page system.
