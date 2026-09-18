# Stage 8B - Performance Mode control

## Goal

Expose DAW Streamer's existing global Recorder Record/Stop control as a generic VST3 parameter that can be mapped by a host such as Fender Studio Performance Mode, without adding Fender-specific code.

## VST3 parameter

The plugin now exposes one boolean parameter:

- ID: `recording`
- Name: `Recording`
- OFF / 0 = Recorder should be stopped
- ON / 1 = Recorder should be recording

This is a desired-state control, not a blind toggle. Recorder remains the authoritative source of truth.

## Control path

`Host / Performance Mode -> VST3 Recording parameter -> SharedRecorderControl -> Recorder`

Authoritative feedback returns through:

`RecorderState -> every DAW Streamer instance -> Recording parameter -> host`

All instances use the same shared Recorder state. Mapping any one sender instance therefore controls the same global Recorder.

## Pending command handling

When the host changes `Recording`, the plugin sends the desired Record or Stop command immediately. While Recorder is acknowledging the command, the plugin does not overwrite the host parameter with the previous Recorder state. Once Recorder reaches the requested state, the parameter is synchronized to the authoritative state.

If Recorder is offline or enters ERROR, the host-facing parameter returns to OFF. Commands are ignored while Recorder is offline so a stale Record request cannot be left waiting for a later Recorder launch.

## Scope boundary

Stage 8B uses only normal VST3 parameter behavior. There is no Fender/PreSonus-specific API in this stage.

If Fender Studio accepts the `Recording` command but does not visually update a mapped Performance Mode button when Recorder state changes elsewhere, that host-feedback limitation is documented and the enhanced feedback path is deferred to version 0.4 / Fender Studio Enhanced VST3.

## Manual acceptance in Fender Studio

1. Install the Stage 8B VST3 and run the matching Recorder.
2. Open the target Show with the normal Vocal, Guitar, Keys and Playback sender instances.
3. Open one DAW Streamer instance and confirm `Host Recording` shows `OFF` while Recorder is idle.
4. In Fender Studio Performance Mode, map a toggle/button control to the DAW Streamer VST3 parameter named `Recording` from one sender instance.
5. Press the mapped control. Recorder must start a take, the mapped parameter must become ON, and all DAW Streamer instances must report the same Recorder state.
6. Press it again. Recorder must stop and the parameter must return to OFF.
7. Start recording from the standalone Recorder window. Verify the VST3 `Recording` state follows it. Observe whether the mapped Performance Mode control also updates visually.
8. Stop from a different DAW Streamer instance. Verify all instances return to OFF and observe Performance Mode feedback.
9. Repeat Record/Stop while DAW transport is stopped and while switching songs. Recorder control must remain independent of DAW Play/Stop.
10. Confirm normal audio recording remains synchronized and `Drop` remains 0.

## Acceptance

Stage 8B is accepted when generic VST3 control reliably starts/stops Recorder and all plugin instances follow Recorder's authoritative state. Bidirectional visual feedback inside Fender Studio Performance Mode is accepted if the host exposes it through standard VST3 parameter feedback; otherwise the limitation is recorded for Stage 10 / version 0.4.
