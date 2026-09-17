# Stage 5A1 — startup-drop hotfix

## Problem

Stage 5A waited until the Recorder had captured the first audio block from all four roles before opening the four WAV writers. After a role supplied its first block, Recorder temporarily stopped draining that role while waiting for the remaining roles.

With a 64-block shared-memory ring, an early Vocal/Guitar/Keys sender could therefore fill its queue while Playback was still late and start incrementing `Dropped blocks` before the common take had actually started.

The Stage 4/5 timeline padding kept the resulting files aligned, but dropped audio is still real lost audio and must not be created by Recorder startup policy.

## Stage 5A1 behavior

On Record:

1. Recorder clears pending pre-take blocks and captures per-take drop/oversize baselines.
2. The first available stream establishes take origin.
3. That stream opens its WAV and starts writing immediately.
4. Every other available stream does the same; Recorder never stops draining an early stream just because another role has not appeared yet.
5. A late stream uses host `timeInSamples` relative to the first stream's host origin to calculate its leading offset.
6. Recorder writes leading silence before the late stream's first audio block.
7. If common host timing is unavailable for a late stream, it joins at the current Recorder frontier rather than being moved to take zero.
8. Stop still pads every started writer to the same final take length.

`WAITING` now means the take is already recording but fewer than four WAV writers have joined. The Recorder UI therefore displays `RECORDING · N/4 STREAMS` instead of implying that no audio is being written.

## Drop diagnostics

Recorder `Drop` now shows the increase since the current/most recent Record action. The shared transport still keeps its lifetime counter internally, but old historical drops no longer make a clean take look faulty.

## Automated regression test

`recorder-stage5a1-startup` simulates Vocal/Guitar/Keys producing 100 blocks while Playback is absent. This exceeds the 64-block ring capacity and reproduces the Stage 5A failure if Recorder stops draining early streams.

Acceptance requires:

- no drop increase on the first three streams;
- their WAV writers are active while Playback is still absent;
- Playback can join later;
- leading silence aligns Playback to the common host timeline;
- all four streams finish at the same frame count;
- four WAV files are produced.

## Manual target test

1. Launch Recorder and the four senders.
2. Press Record from any plugin instance.
3. Observe `Queue` and `Drop` during the first seconds.
4. Normal startup should keep `Drop = 0`; transient queue depth is acceptable as long as it drains.
5. Stop from any plugin instance.
6. Import the four WAV files at the same origin and verify synchronization.
7. Repeat with DAW Play/Stop and a Song switch.
