# Stage 8B - Performance Mode control

## Goal

Expose DAW Streamer's existing global Recorder Record/Stop control as a generic VST3 parameter that can be mapped by a host such as Fender Studio Performance Mode, without adding Fender-specific code.

## VST3 parameter

The plugin exposes one boolean parameter:

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

## Manual validation in Fender Studio

Windows CI passed after the JUCE 9 `AudioParameterBool` accessor fix.

Manual validation in the target Show Page passed:

- [x] Fender Studio exposes `Recording` as an assignable DAW Streamer parameter.
- [x] `Recording` can be assigned to a Performance Mode button.
- [x] Performance Mode starts and stops DAW Streamer recording.
- [x] Recorder remains the authoritative state.
- [x] All DAW Streamer instances follow the shared Recorder state.
- [x] The mapped Performance Mode button visually changes between OFF and ON from standard VST3 parameter feedback.
- [x] DAW Play/Stop remains independent from DAW Streamer Record/Stop.

This confirms that the software-side bidirectional Performance Mode path works without Fender-specific extensions.

## Final hardware validation - MIDI Captain

The mapped `Recording` control was tested with MIDI Captain using the user's two-way feedback firmware/configuration.

Acceptance checklist:

- [x] Pressing the MIDI Captain button changes the Performance Mode `Recording` control and starts Recorder.
- [x] Pressing it again stops Recorder.
- [x] MIDI Captain LED/display turns ON after the recording state is acknowledged.
- [x] MIDI Captain LED/display turns OFF after Stop is acknowledged.
- [x] Starting/stopping directly from the standalone Recorder propagates through Performance Mode back to MIDI Captain.
- [x] Starting/stopping from another DAW Streamer instance produces the same controller feedback.
- [x] Several quick Record/Stop operations do not leave MIDI Captain showing the opposite state.

Confirmed command chain:

`MIDI Captain -> Fender Studio Performance Mode -> VST3 Recording -> RecorderControl -> Recorder`

Confirmed feedback chain:

`Recorder -> authoritative state -> VST3 Recording -> Fender Studio Performance Mode -> MIDI Captain`

The full bidirectional path therefore works through standard VST3 parameter feedback in the tested Fender Studio + MIDI Captain setup. No Fender-specific workaround is required for version 0.2.

## Acceptance

Stage 8B is accepted. Generic VST3 control reliably starts/stops Recorder, all plugin instances and Performance Mode follow Recorder's authoritative state, and MIDI Captain two-way feedback is confirmed. Version 0.2.0 is ready for release.
