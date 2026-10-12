# recorder/ — действующее ядро и исторический standalone frontend

**`Source/RecorderEngine.{h,cpp}` и `Source/MidiFileExporter.*` активно используются в v0.1** и компилируются внутрь Master Recorder VST3 через `fender/CMakeLists.txt`.

**`Source/Main.cpp` — исторический frontend отдельного Recorder.exe**, который не входит в основной релиз v0.1. Не отлаживать #34 только в standalone-окне: действующая среда — встроенный Master VST3.

Текущие проблемы многодорожечной записи: [issue #34](https://github.com/vladleng/DAW-Streamer/issues/34). См. [handoff](../docs/current-handoff.md), [архитектуру](../docs/architecture.md).
