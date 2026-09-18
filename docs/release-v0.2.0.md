# DAW Streamer v0.2.0

## Release summary

v0.2.0 is the first public-ready release of DAW Streamer after long-session validation and live workflow testing.

DAW Streamer is a host-agnostic VST3 sender plus a standalone Recorder for capturing several synchronized audio streams directly from a live plugin host or DAW without running a second recording DAW. The sender passes audio through unchanged and sends a copy to the Recorder on the same Windows computer.

The current build was validated in Fender Studio Pro / Show Page, but the recording core and control path use standard VST3 behavior and do not depend on Fender-specific APIs.

## Highlights

- Four synchronized audio streams with separate 24-bit PCM WAV files at 48 kHz.
- Transparent audio pass-through: the sender copies audio without changing the host signal path.
- Continuous take timeline independent from host Play/Stop and song switching.
- Recording time displayed in Recorder and every sender instance as `HH:MM:SS`.
- Global boolean VST3 parameter `Recording` for Record/Stop control.
- Bidirectional state feedback through standard VST3 parameter feedback.
- Recording can be controlled from the standalone Recorder, a DAW Streamer instance, or a compatible host/control surface that can map VST3 parameters.
- Hardware control and return LED/state feedback were validated with MIDI Captain through the tested host mapping.
- Recording folder and Show/session settings persist between launches.
- Safe take naming prevents accidental overwrite.

## Typical use case

DAW Streamer is intended for live hosts and DAWs where you want separate stems or signals from chosen points in the plugin chain without opening a second DAW just for recording.

Insert a DAW Streamer sender at the point you want to capture, choose one of the four stream slots, and run the standalone Recorder. The resulting WAV files share the same timeline and can be imported into a DAW later for mixing or editing.

## Validated environment

The following is the environment used for development and manual acceptance; it is a validation target, not a host requirement:

- Windows 11 x64
- Fender Studio Pro / Show Page
- 48 kHz
- VST3 sender + standalone Recorder
- 24-bit PCM WAV
- Long takes of approximately 40–60 minutes
- MIDI Captain mapped through the host for Record/Stop and return state feedback

Other VST3 hosts have not yet been formally validated and may differ in routing, parameter mapping and control-surface feedback behavior.

## Stream placement used in testing

The four stream slots in v0.2.0 are currently named:

- Vocal
- Guitar
- Keys
- Playback

In the tested setup they were placed before processing, except Keys, which was captured after the virtual instrument and before effects. These names describe the current test configuration rather than a requirement of the recording architecture.

Custom user-defined stream names are planned for v0.2.1.

## Host control

DAW Streamer exposes the boolean VST3 parameter `Recording`:

- `OFF` = Stop
- `ON` = Record

The Recorder remains the authoritative state. If a compatible host maps this parameter to a button, MIDI controller or control surface, the command can start or stop the same global Recorder state. If the host supports standard VST3 parameter feedback, external state changes can also be reflected back to the mapped control.

In the validation setup, this complete round-trip was confirmed with Fender Studio Performance Mode and MIDI Captain. No Fender-specific code is used by DAW Streamer v0.2.0.

## Known scope limits

- Windows x64 only.
- Current validated live format is 48 kHz.
- Four fixed stream slots in v0.2.0; custom names are planned for v0.2.1.
- No MIDI performance capture yet.
- No automatic recording start.
- No crash-recovery feature unless a real failure scenario justifies it.
- No multi-configuration/template system yet.
- UI is still technical; visual redesign is planned for v0.3.

## Installation

The Windows release archive contains:

- `DAW Streamer.vst3`
- `DAW Streamer Recorder.exe`

Copy the VST3 bundle to a standard Windows VST3 folder, rescan plugins in your host, and run the Recorder executable before the session.

## Validation status

Long-session recording, synchronized four-stream capture, recording-time display, global VST3 Record/Stop control, bidirectional parameter feedback and hardware control were all manually validated in the target test environment.

Windows CI passed for the final v0.2.0 code before publication.
