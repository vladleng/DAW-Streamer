# Stage 5B — Recorder UI, recording folder and safe takes

Stage 5B turns the working multistream recorder into a practical day-to-day/live workflow.

## Goals

- Choose a base recording folder instead of forcing recordings onto the system drive.
- Persist the selected folder and Show/session name between Recorder launches.
- Create a separate session folder and a unique take folder for every Record action.
- Never overwrite an earlier take.
- Use one stateful Record/Stop button in the sender plugin, driven by the authoritative Recorder state.
- Keep four-stream diagnostics visible: role, connection activity, format, peak, queue, drop, gaps and written frames.
- Preserve Stage 5A1 late-stream alignment and per-take drop counters.

## Output layout

Example:

```text
<selected base folder>/
  Kitchen Lab/
    Take_2026-09-17_20-45-12/
      Vocal.wav
      Guitar.wav
      Keys.wav
      Playback.wav
    Take_2026-09-17_21-32-04/
      ...
```

If a take with the same timestamp already exists, a numeric suffix is added. Session names are sanitized for Windows file names.

## Recorder controls

- `Show / session` — name used for the session subfolder.
- `Choose...` — selects the base recording folder.
- `Record` / `Stop` — one stateful button in Recorder.
- Folder and session controls are disabled while a take is active.
- Settings are stored in the Recorder properties file and restored on the next launch.

## Plugin controls

The previous separate Record and Stop buttons are replaced by one stateful button:

- Recorder `IDLE` -> `Record`
- Recorder `WAITING` or `RECORDING` -> `Stop recording`
- Recorder offline/error -> button disabled

The plugin never invents its own recording state; it follows the shared Recorder control state.

## Diagnostics

Recorder reports for every role:

- claimed/active status;
- mono/stereo source format;
- latest peak in dBFS;
- callback count;
- queue depth;
- per-take dropped blocks;
- timeline gaps/silence inserted;
- frames written.

`duplicate claims` is displayed as historical diagnostic information rather than as a current connection state.

## Stage 5A1 follow-up fixed here

The first post-merge `main` CI run exposed a race in the automated delayed-start test: Stop could be processed before the Recorder polling loop consumed the final queued block. Stage 5B now drains already queued blocks at the Stop boundary before closing/padding WAV files. This prevents the final callback from being lost and makes the delayed-start test deterministic.

## Manual acceptance test

1. Start Recorder and choose a folder on the desired recording disk.
2. Enter a Show/session name and restart Recorder; confirm both settings are restored.
3. Open four sender instances: Vocal, Guitar, Keys, Playback.
4. Press the single Record button in any plugin instance.
5. Confirm Recorder enters recording and the same plugin button becomes Stop.
6. Record 20–30 seconds, including DAW Stop/Play and a Song switch.
7. Press Stop from a different plugin instance.
8. Confirm four WAV files were created in a new take folder and remain synchronized after import.
9. Start a second take immediately and verify a different take folder is created without overwriting the first.
10. Confirm `Drop = 0` during normal operation and that the selected base folder is not the system drive unless explicitly chosen.

Long 40–60 minute validation remains Stage 6.
