# DAW Streamer — актуальная архитектура (v0.1)

> **Действующая архитектура на 2026-10-12.** Для передачи разработки и текущих дефектов см. [current-handoff.md](current-handoff.md) и [issue #34](https://github.com/vladleng/DAW-Streamer/issues/34). Документы прежних Stage и выпусков со standalone Recorder — исторические.

## Компоненты

| Код | Действующая роль |
| --- | --- |
| `fender/Source/FenderPluginProcessor.cpp`, `FenderPluginEditor.cpp` | Один универсальный VST3 (`Sender` или `Master Recorder`); имя каталога `fender` историческое |
| `recorder/Source/RecorderEngine.{h,cpp}` | Общий Recorder backend, встроен в Master VST3, собственный worker thread и WAV writers |
| `recorder/Source/MidiFileExporter.*` | Экспорт одного `MIDI.mid` |
| `shared/Source/SharedAudioTransport.*` | Четыре SPSC shared-memory audio rings, метаданные блока и счётчики |
| `shared/Source/SharedMidiTransport.*` | MIDI events на общей take timeline |
| `shared/Source/SharedRecorderControl.*` | Команды Record/Stop и authoritative Recorder state |
| `shared/Source/SharedStreamMetadata.*` | Пользовательские имена и metadata Sender |
| `tests/` | Unit/integration regression tests |

Корневой `CMakeLists.txt` собирает `shared`, `fender`, `tests`. Папка `plugin/` и прежний `recorder/Source/Main.cpp` существуют как старый standalone workflow, но **не входят в основной v0.1 release**. События/проблемы реальной эксплуатации: #31 и #34.

## Текущий live workflow

- Четыре Sender на нужных точках сигнального пути: Vocal / Guitar / Keys / Playback (имена настраиваемые `Inst 1`–`Inst 4`).
- Один Master Recorder на постоянной Main/Master. Master не записывает Main и прозрачно пропускает его аудио.
- Audio Sender передаёт копию float32 в локальную Windows shared memory; Decoder/Recorder в не-realtime worker сохраняет 24-bit PCM WAV / 48 kHz.
- Клавиши пишутся после виртуального инструмента до последующих обработок; прочие живые потоки — из выбранной до-эффектной точки.
- MIDI поступает в один из действующих Sender, отдельно передаётся/экспортируется в SMF Type 0, 960 PPQ. Tempo/time-signature map не сохраняются.
- Record/Stop не зависит от DAW Play/Stop и доступен из VST3 UI и через `Recording` VST3 parameter; Recorder остаётся единственным источником истины.

## Общая временная шкала и целостность

Каждый Sender формирует непрерывный локальный `producerFrameCounter` и посылает `producerFrameStart` для каждого callback. Реальный callback size принимается динамически, не приравнивается к 128 device samples. Host `timeInSamples`, если доступен, используется для **первоначального anchor** при начале take/присоединении потока, а не для пересчёта каждого последующего аудиоблока.

В RecorderEngine:
- первый блок потока фиксирует `producerAnchorFrame` и `takeBaseOffset`;
- аудиоблок рассчитывается в координатах take;
- `globalTakeFrontier`, `syncFloor`, `writeSilence()` и overlap handling пытаются удерживать общую timeline и восстанавливать интервалы потерянных данных тишиной;
- при Stop writers догоняют общий конец, WAV должны оставаться синхронны.

**Этот алгоритм находится под расследованием в #34** после повторного общего разрыва 2–3 s всех четырёх WAV. Совпадающая длительность файлов не является доказательством отсутствия потерь и искажений. Не менять алгоритм без детерминированного теста с проверкой реальных samples.

## Realtime safety и сбои

- `processBlock()`: не допускать дискового I/O, mutex, выделений, ожиданий и любых неопределённо долгих действий; звук при ошибке записи должен продолжать проходить.
- IPC ring ограничен (64 blocks, до 4096 frames/block, максимум 2 channels); переполнение обнаруживается и увеличивает `Drop`; oversized отдельно.
- Запись и экспорт файлов, GUI/техническая диагностика — вне критического audio callback.
- Любую невосстановимую потерю отмечать диагностикой; нельзя просто удалять пропущенный интервал и сжимать время.
- Изменения IPC ABI требуют отдельного обоснования, версионирования/регрессионной проверки; не объединять с функциональным v0.2.

## Будущая архитектура

После стабильной v0.1.x: #32 — Dynamic Instances (без фиксированного лимита 4) → #20 — универсальный GUI → позднее #6 (шаблоны Show) / #21 (опциональные host extensions). Базовое ядро остаётся host-agnostic. Текущий блокер — #34, стабильность подтверждает #31.
