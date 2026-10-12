# DAW Streamer — instructions for a new coding agent

Read `docs/current-handoff.md` FIRST, then `README.md`, `docs/architecture.md`, `docs/build.md`, and GitHub issues #34, #31, #33.

- Current release is **v0.1 (embedded Master Recorder)**, not the historical standalone v0.2.0/v0.3.0. The active plugin sources are in `fender/Source` despite the directory's old name.
- Current priority is diagnosis/fix of issue #34 in the **v0.1.1 maintenance line**. The common ~2–3 s simultaneous four-WAV dropout is repeat-observed. The individual Keys/Guitar/Playback artifacts may be separate issues. Do not claim root cause without evidence.
- Preserve realtime safety: no locks, file I/O, dynamic allocations, sleeps or potentially blocking operations inside VST3 `processBlock()`. Diagnostics and WAV writing must remain on non-realtime paths.
- Preserve four-track and MIDI synchronization, recorded samples, common-timeline behavior, audio pass-through, and host-independent VST3 design. Do not silently hide genuine missing data by compressing time.
- Add tests for long-run/timeline conditions and inspect sample contents, not only equal WAV durations. First review `RecorderEngine::handleAudioBlock` (`globalTakeFrontier`/`syncFloor`) and draining behavior; these are hypotheses, not established faults.
- No `v0.2 Dynamic Instances` or `v0.3 GUI` work until #31 stability gate and #34 fix are accepted. See #33.
- Build/test with `cmake --preset windows-x64`, `cmake --build --preset windows-x64-release`, `ctest --preset windows-x64-release --output-on-failure`. User manually tests in Fender Studio Pro Show Page, Windows 11 x64, 48 kHz.
- Keep documentation and issue status aligned. Do not close an issue, merge a PR, change release tags or publish a release on behalf of the user without their explicit acceptance.
- Stage-specific documents, old release documents, `plugin/` and standalone `recorder/Source/Main.cpp` are historical references; verify active CMake targets before editing.
