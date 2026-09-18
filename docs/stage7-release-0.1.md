# Stage 7 — Release 0.1

## Status

Stage 6 has been accepted after real long-session testing. No recording defects were found in the target live workflow.

Stage 7 is release preparation only: no new features are added here.

## Release goal

Prepare the first stable DAW Streamer release for regular rehearsals and live performances.

Target environment validated during development:

- Windows 11 x64
- Fender Studio / Show Page
- 48 kHz
- VST3 sender + standalone Recorder
- four audio roles: Vocal, Guitar, Keys, Playback
- 24-bit PCM WAV output

## Release checklist

- [ ] Freeze the stable code state used for 0.1.
- [ ] Document VST3 installation and manual Recorder startup.
- [ ] Document sender placement:
  - Vocal — before processing
  - Guitar — before processing
  - Keys — after virtual instrument, before processing
  - Playback — before processing
- [ ] Verify persistence of recording folder and Show/session settings after restart.
- [ ] Record Stage 6 validation results and known 0.1 limitations.
- [ ] Prepare release notes / changelog.
- [ ] Build the final Windows artifact and run CI.
- [ ] Perform one final target-system smoke test.
- [ ] Create tag `v0.1.0` and GitHub Release.

## 0.1 scope boundary

The following are intentionally outside 0.1:

- MIDI capture
- Fender Studio Performance Mode integration
- visual redesign
- Fender/PreSonus host-specific enhanced integration
- recording templates for different Show configurations

## Release principle

0.1 must preserve the exact recording behavior validated in Stage 6. Any feature idea discovered during release preparation is deferred to later versions unless it fixes a release-blocking defect.
