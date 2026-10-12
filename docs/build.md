# DAW Streamer — текущая сборка и тестирование

> **Актуально для основного релиза v0.1 / maintenance v0.1.1 (2026-10-12).** Прежние standalone цели оставлены в исходниках исторически. Для технического контекста см. [current-handoff.md](current-handoff.md).

## Платформа и toolchain

- Windows 11 x64; C++20; Visual Studio 2022 17.14 / MSVC v143; CMake (в прошлых испытаниях использовалась 4.4.3).
- CMake minimum 3.22 — как объявлено в текущем корневом `CMakeLists.txt`.
- JUCE 9.0.2 через закреплённый Git submodule `external/JUCE`.
- VST3, цель `DAWStreamerFenderPlugin` в `fender/CMakeLists.txt`; product name `DAW Streamer`.
- Fender Studio Pro / Show Page — целевая ручная среда: 48 kHz, live device buffer 128; фактический callback в исторических тестах был 512 frames. Не фиксировать callback size.
- Windows CI: `.github/workflows/build-windows.yml`; workflow публикации текущего `v0.1`: `.github/workflows/release-v0.1.yml`. **Не запускать публикацию/смену тегов при обычном bugfix.**

## Клонирование и обновление submodule

```powershell
git clone --recurse-submodules https://github.com/vladleng/DAW-Streamer.git
cd DAW-Streamer
# Для уже клонированного репозитория:
git submodule update --init --recursive
```

## Configure → build → tests

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
```

Для Debug: `cmake --build --preset windows-x64-debug` и `ctest --preset windows-x64-debug --output-on-failure`. Поскольку используется Visual Studio multi-config generator, `CMakePresets.json` задаёт конфигурацию в build/test preset.

Корневой CMake подключает **`shared`, `fender`, `tests`**. Не считать `plugin/` и `recorder/Source/Main.cpp` отдельными актуальными сборочными целями.

## Артефакт текущего релиза

- **`DAW Streamer.vst3`** — один plugin, режимы Sender / Master Recorder.
- Отдельный `DAW Streamer Recorder.exe` в основном release v0.1 не поставляется.
- Формат записи: 4 WAV, 24-bit PCM/48 kHz, mono/stereo плюс `MIDI.mid` (при наличии событий).
- Текущий tag/release: `v0.1`, Windows x64 ZIP. В `.github/workflows/build-windows.yml` есть CI artifact `DAW-Streamer-v0.1-windows-x64`.

## Автотесты

Список из `tests/CMakeLists.txt`:
- `shared-audio-transport`
- `shared-midi-transport`
- `shared-recorder-control`
- `recording-time-format`
- `midi-file-export`
- `recorder-stage5a1-startup`

**Недостаток покрытия**: end-to-end длинный аудиотест с неравномерными очередями, пропущенными блоками, считыванием фактического sample content и MIDI alignment на 60 мин. Требуется для #34/#31. CI green не означает отсутствие выявленного пользователем общего WAV dropout.

## Правила изменения

Новые исправления делать отдельной веткой/PR, воспроизводить дефект тестом, сохранять real-time безопасность VST3. Изменения toolchain/JUCE/IPC ABI не смешивать с целевым bugfix без необходимости. Release только после ручной приёмки.
