# Stage 5A — Record/Stop from the plugin

## Goal

Control the single DAW Streamer Recorder session directly from any sender VST3 instance in Fender Studio / Show Page.

Recorder remains the authoritative owner of recording state. The DAW transport remains independent from Recorder Record/Stop.

## Control channel

Stage 5A adds a dedicated Windows shared-memory control mapping that is separate from the four audio ring buffers.

Any plugin instance can submit one of two idempotent desired-state commands:

- `Record`
- `Stop`

Commands use one atomic packed command word containing a monotonic sequence and command type. If several plugin instances issue commands close together, the latest desired state wins without locks or audio-thread work.

The Recorder polls the command word from its worker thread and is the only component that actually starts or stops a take.

## Shared Recorder state

Recorder publishes:

- `OFFLINE`
- `IDLE`
- `WAITING` (Record requested; waiting for the four first stream blocks)
- `RECORDING`
- `ERROR`
- current take frame count
- heartbeat counter

Plugin editors watch the heartbeat. If it stops changing for approximately one second, the Recorder is presented as `OFFLINE` even if stale shared memory still exists because one or more VST instances remain loaded.

## Plugin UI

Every sender editor contains:

- stream-role selector;
- `Record` button;
- `Stop` button;
- current Recorder state;
- current take frame count;
- existing audio/host diagnostics.

`Record` is enabled while the live Recorder reports `IDLE`; `Stop` is enabled while it reports `WAITING` or `RECORDING`.

## Recorder UI

The existing Recorder `Record` and `Stop` buttons remain functional. Plugin control and Recorder-window control both operate the same internal recording engine.

## Audio-thread safety

Stage 5A adds no control work to `processBlock()`. Button commands are sent from the plugin UI thread. Audio transport and pass-through behavior remain unchanged from validated Stage 4.

## Automated validation

`SharedRecorderControlTests` verifies:

- multiple clients open the same control mapping;
- state + heartbeat publication;
- Record command submission and readback;
- take-frame publication;
- Stop command submission and readback;
- OFFLINE publication.

## Manual target-system test

1. Start Fender Studio with the four sender instances and launch Recorder.
2. Open one sender and confirm Recorder state becomes `IDLE`.
3. Press `Record` in that sender.
4. Confirm Recorder starts a take and every opened sender shows `WAITING` then `RECORDING`.
5. Press `Stop` from a different sender.
6. Confirm Recorder closes the take and all senders return to `IDLE`.
7. Repeat while DAW transport is playing and stopped.
8. Switch Show Page Song while Recorder remains active.
9. Import the four WAV files and verify synchronization remains unchanged from Stage 4.
10. Confirm `Dropped blocks = 0` in the normal test.

## Acceptance

Stage 5A is complete when Record/Stop can be reliably controlled from any sender plugin instance, all instances show the same actual Recorder state, and the resulting four-track take remains synchronized and drop-free on the target system.
