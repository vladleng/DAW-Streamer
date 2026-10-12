# DAW Streamer — актуальная передача разработки (v0.1.1 stability)

> **Актуально на 2026-10-12.** Это рабочая точка входа для нового чата/Codex. Репозиторий: `vladleng/DAW-Streamer`, ветка `main`. Сначала читать этот файл, `README.md`, `docs/architecture.md`, `docs/build.md`, затем Issues #34, #31, #33. Старые `docs/stage*.md`, `docs/release-v0.2.0.md` и `docs/release-v0.3.0.md` — история прежней архитектуры и нумерации, не текущий план.

## Зафиксированная версия и архитектура

- **Текущий основной релиз — v0.1**, tag `v0.1`, commit `109ec2fa6100b8d9bd457fba0d8216d0e639dcdc`, PR #30 merged.
- **Один `DAW Streamer.vst3` в двух режимах:** `Sender` на выбранных каналах и единственный `Master Recorder` на постоянной Main/Master шине. Master пропускает аудио, но не записывает мастер-шину.
- Четыре фиксированных аудиослота, автоматически назначаемые Sender; отображаемые имена `Inst 1`–`Inst 4` пользователь может менять, они сохраняются в Show и используются для WAV. Общая MIDI-дорожка `MIDI.mid` формируется при наличии событий.
- **Sender → shared-memory SPSC ring buffers → RecorderEngine (background thread) → 24-bit PCM WAV / 48 kHz.** `SharedRecorderControl` — независимые команды/статус, `SharedMidiTransport` — отдельный поток MIDI.
- Управление Record/Stop возможно в Sender, Master и через стандартный VST3 параметр `Recording` с обратной связью при поддержке хостом.
- Основная проверенная среда: Windows 11 x64, Fender Studio Pro / Show Page, 48 kHz, device buffer 128; наблюдались фактические callbacks 512 frames — **не хардкодить размер блока**.
- Реально собираемый плагин находится в `fender/Source/`, цель `DAWStreamerFenderPlugin` в `fender/CMakeLists.txt`. Название каталога историческое, код продукта не должен становиться host-specific.
- В корневом `CMakeLists.txt` подключены `shared`, `fender`, `tests`. Папки `plugin/`, standalone `recorder/Source/Main.cpp` и старые документы существуют исторически; **отдельный Recorder.exe не входит в текущий релиз**.

## Текущий приоритет: issue #34 — дефекты после длинных записей

Пользователь сообщил:
1. **Общий разрыв/тишина ~2–3 секунды одновременно на всех четырёх WAV** приблизительно на 15-й минуте длинного дубля. Аналогичный разрыв отмечен в более раннем домашнем дубле длительностью около 25 минут (точная позиция не установлена). Дефект повторный, но точные условия пока не локализованы.
2. Периодические щелчки на Guitar.
3. Сильные искажения на Keys, иногда на всю песню, при обычном уровне без клипирования и после прогрузки инструментов.
4. Редкие тихие артефакты на Playback.
5. Vocal записан чисто; четыре WAV в целом синхронны (это **не** доказывает отсутствие потерянных данных).
6. Полная проверка синхронизации MIDI↔audio на 0/30/55–60 мин, RAM/CPU/Queue/Drop/Gaps по #31 ещё не закрыта.

**Не объединять эти симптомы в одну доказанную причину.** В первую очередь диагностировать общий разрыв, отдельно исследовать качество Keys/Guitar/Playback, включая возможность искажений на источнике до Sender.

## Куда смотреть в коде в первую очередь

- `recorder/Source/RecorderEngine.cpp`: `run()`, `handleAudioBlock()`, `startStreamFromFirstBlock()`, `writeSilence()`, `closeWriters()`, `publishSnapshot()`, `drainPendingAudioForStop()`.
- В `handleAudioBlock()`: `desiredStart = takeBaseOffset + (producerFrameStart - producerAnchorFrame)`; затем `syncFloor = globalTakeFrontier - block.numFrames` при положительной разности и `desiredStart = max(desiredStart, syncFloor)`. Проверить, не распространяет ли сдвиг/отставание **одного** потока gap/overlap на остальные и не пропускает ли валидные блоки.
- `run()` читает максимум **один аудиоблок на поток за итерацию** и выполняет WAV writer синхронно на своём worker thread; оценить влияние задержек диска, длительного queue backlog, обработки всех streams и MIDI.
- `shared/Source/SharedAudioTransport.{h,cpp}`: `kRingCapacity = 64`, `kMaxFramesPerBlock = 4096`, счётчики `droppedBlocks`, `oversizedBlocks`, `producerCallbacks`, `producerFrameStart`, sequence.
- `fender/Source/FenderPluginProcessor.cpp`: `processBlock()`, `producerFrameCounter += numFrames`, `hostTimeInSamples`, режим Sender, передача MIDI. Host time участвует в **первоначальной привязке**; не считать доказанным, что скачок host timestamp посреди уже начатого take создаёт текущий gap.
- `recorder/Source/RecorderEngine.h`: per-stream `gapFrames`, `gapEvents`, `framesWritten`, `globalTakeFrontier`.
- Сопоставить аудио до/после Sender и результат WAV: host/VSTi проблему нельзя автоматически считать дефектом Recorder.
- История Stage 5A1: `docs/stage5a1-startup-drop-fix.md` и test `tests/RecorderStartupTests.cpp`. Исправление старта уже было; не регрессировать его.

## Диагностический план для рабочего чата (без угадывания исправления)

1. Зафиксировать baseline v0.1 и записать чёткие гипотезы. Проверить, действительно ли участок WAV — цифровой ноль, отсутствующая последовательность или искажённые отсчёты; точные frame offsets и длительности **пока неизвестны**.
2. Добавить **лёгкую диагностику вне realtime audio path**: метки причин вставки тишины/пропуска overlap, per-stream expected vs actual `producerFrameStart`, sequence gaps, сдвиг `globalTakeFrontier`, максимумы Queue, Drop/Gaps/oversize, состояние writer, паузы worker, длительность write operations, статусы host transport/Song по возможности. Не логировать на диск в `processBlock()`.
3. Детерминированные regression tests: задержка одного producer при продолжающихся остальных; один stream получает очередь с backlog; другой stream уходит вперёд; пропущенные блоки; временная остановка Recorder worker; поздний join; Stop/drain; разные block sizes; повторный take; MIDI alignment после gaps. Каждый тест проверяет и **содержимое samples**, а не только равную длину WAV.
4. Если доказано, что вставка тишины ошибочна — исправить локально, не изменяя общий контракт синхронизации и не подменяя потерю аудио искусственно сжатым временем. При реальной потере в источнике сохранять честную диагностику.
5. Windows Release configure/build/CTest по `docs/build.md`; ручной тест с четырьмя Sender + MIDI, Song switching и 60+ минутами записи. Сравнить начало/середину/конец аудио и MIDI. Подтверждение пользователя требуется до закрытия issues и публикации релиза.

## Данные, которые ещё нужны от пользователя при следующем тесте

- Точные file/sample offsets общего разрыва и действительно ли там цифровой ноль; сравнение сигналов непосредственно в Fender Studio в этот момент.
- `Details` в Master/Senders (`Drop`, `Gaps`, `Queue`, callbacks/format), особенно до и после события.
- Совпадал ли инцидент со сменой Song, патча VSTi, Play/Stop, изменением нагрузки или фоновой записью на диск.
- Параметры диска, свободное место и пиковая нагрузка. Не требовать всё до начала code audit и воспроизводимых unit tests.

## Версии и порядок работ

- **Сейчас:** maintenance `v0.1.1` → issue #34 + gate #31. Только диагностические и исправляющие изменения.
- **Не начинать пока:** #32 — `v0.2 Dynamic Instances`; затем #20 — `v0.3 Universal GUI`.
- Позже #6 (Show templates) и #21 (optional Fender integration).
- Issue #33 — актуальная карта этапов. Старые исторические версии `v0.2.0`/`v0.3.0` относятся к предыдущему циклу *до* нового публичного `v0.1`. Не переименовывать текущий roadmap по старым файлам.
- **Не менять** tags, releases, GitHub Actions release workflow, API/формат IPC и realtime thread без обоснования и проверок. Любые изменения — в отдельной ветке/PR рабочего чата, в `main` только после проверки.

## Проверка сборки

```powershell
git clone --recurse-submodules https://github.com/vladleng/DAW-Streamer.git
cd DAW-Streamer
cmake --preset windows-x64
cmake --build --preset windows-x64-release
ctest --preset windows-x64-release --output-on-failure
```

Проверенные автотесты в `tests/CMakeLists.txt`: `shared-audio-transport`, `shared-midi-transport`, `shared-recorder-control`, `recording-time-format`, `midi-file-export`, `recorder-stage5a1-startup`. Реальный endurance test ими **не покрывается**.

## Режим работы

Этот документ не поручает автоматически реализовывать исправление при первом чтении: новый рабочий чат должен сначала провести аудит исходников и предложить минимальный проверяемый план `v0.1.1`, после чего вносить кодовые изменения в рамках поручения пользователя.