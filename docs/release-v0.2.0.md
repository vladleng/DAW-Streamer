# DAW Streamer v0.2.0

## Release summary

v0.2.0 is the first public-ready release of DAW Streamer after long-session validation and live workflow testing.

The release keeps the recording core from 0.1 and adds practical live control features without introducing Fender-specific code.

## Highlights

- Four synchronized audio streams: Vocal, Guitar, Keys and Playback.
- Separate 24-bit PCM WAV files at 48 kHz.
- Continuous take timeline independent from DAW Play/Stop and song switching.
- Recording time displayed in Recorder and every sender instance as `HH:MM:SS`.
- Global boolean VST3 parameter `Recording` for Record/Stop control.
- Fender Studio Performance Mode mapping validated.
- Bidirectional visual state feedback through standard VST3 parameter feedback validated.
- MIDI Captain hardware control and return LED/state feedback validated in both directions.
- Recording folder and Show/session settings persist between launches.
- Safe take naming prevents accidental overwrite.

## Validated environment

- Windows 11 x64
- Fender Studio Pro / Show Page
- 48 kHz
- VST3 sender + standalone Recorder
- 24-bit PCM WAV
- Long takes of approximately 40–60 minutes

## Sender placement used in testing

- Vocal: before processing
- Guitar: before processing
- Keys: after virtual instrument, before effects
- Playback: before processing

## Performance Mode

DAW Streamer exposes the boolean VST3 parameter `Recording`:

- `OFF` = Stop
- `ON` = Record

The Recorder remains the authoritative state. Commands can originate from the standalone Recorder, any DAW Streamer sender instance, Fender Studio Performance Mode or a mapped MIDI controller. The resulting state is synchronized back to the other control surfaces.

Validated command path:

`MIDI Captain -> Fender Studio Performance Mode -> VST3 Recording -> RecorderControl -> Recorder`

Validated feedback path:

`Recorder -> VST3 Recording -> Fender Studio Performance Mode -> MIDI Captain`

## Known scope limits

- Windows x64 only.
- Current validated live format is 48 kHz.
- Four fixed audio roles.
- No MIDI performance capture yet.
- No automatic recording start.
- No crash-recovery feature unless a real failure scenario justifies it.
- No multi-Show template system yet.
- UI is still technical; visual redesign is planned for v0.3.

## Installation

The Windows release artifact contains:

- `DAW Streamer.vst3`
- `DAW Streamer Recorder.exe`

Copy the VST3 bundle to a standard Windows VST3 folder, rescan plugins in the DAW, and run the Recorder executable before the session.

## Validation status

Stage 6 long-session testing completed without detected recording defects in the target workflow.

Stage 8A recording-time display passed manual validation.

Stage 8B Performance Mode control, VST3 bidirectional state feedback and MIDI Captain hardware round-trip all passed manual validation.

Windows CI for the final v0.2.0 code passed before merge.
