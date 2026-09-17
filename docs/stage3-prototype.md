# Этап 3 — один аудиопоток VST3 → shared memory → WAV

## Цель

Получить первый сквозной прототип: один экземпляр `DAW Streamer.vst3` передаёт mono или stereo float32-аудио в отдельный `DAW Streamer Recorder`, а Recorder по своим кнопкам Record/Stop пишет валидный PCM WAV 24-bit / 48 kHz.

На Этапе 3 используется **ровно один sender-плагин**. Идентификация четырёх независимых экземпляров начинается на Этапе 4.

## Транспорт

Windows named shared memory:

`Local\\DAWStreamer.Stage3.Stream0`

Протокол имеет `magic` и `version = 1`. В общей памяти находится ограниченный SPSC ring buffer:

- `64` блока;
- максимум `4096` frames на блок;
- максимум `2` канала;
- float32 planar audio;
- монотонные `writeSequence` / `readSequence`;
- `droppedBlocks`, `oversizedBlocks`, `producerCallbacks`;
- фактические sample rate / channel count / block size последнего callback.

Размер callback не предполагается равным device buffer. Sender каждый раз использует реальный `buffer.getNumSamples()`.

## Правила real-time потока

В `processBlock()` разрешены только:

1. чтение готового аудиобуфера;
2. memcpy в заранее выделенный shared-memory slot;
3. lock-free atomic операции с индексами и счётчиками.

В audio thread нет:

- mutex/critical section;
- ожиданий;
- дискового I/O;
- выделений памяти;
- взаимодействия с GUI Recorder.

Если ring buffer заполнен, sender **не ждёт Recorder**, а пропускает блок и увеличивает `droppedBlocks`. Звук DAW остаётся pass-through.

Если Recorder вообще не запущен, sender после заполнения ограниченного буфера начинает считать drop, но не блокирует Fender Studio.

## Recorder

Recorder держит отдельный background thread, который постоянно вычитывает shared memory. Когда запись не включена, полученные блоки просто отбрасываются, поэтому очередь остаётся свежей.

При нажатии Record:

1. отбрасывается всё, что накопилось до момента Record;
2. внутренний sample counter начинается с нуля;
3. после первого аудиоблока создаётся WAV;
4. файл пишется до нажатия Stop независимо от Play/Stop транспорта Fender Studio.

При Stop writer закрывается/flush, после чего WAV должен быть валидным.

Формат Stage 3 строго:

- 48,000 Hz;
- PCM 24-bit;
- mono или stereo как у sender;
- без resampling.

Если source sample rate отличается от 48 kHz или формат меняется посреди дубля, Recorder останавливает запись с ошибкой.

Папка вывода:

`Documents\\DAW Streamer Recordings\\`

Имя:

`Stage3_YYYY-MM-DD_HH-MM-SS.wav`

## Автоматические тесты

`DAWStreamerSharedTests` проверяет:

- создание/открытие общей памяти;
- точность отсчётов;
- FIFO-порядок блоков;
- ограничение ring buffer;
- увеличение drop counter при переполнении;
- безопасное поведение, когда consumer не освобождает очередь.

## Ручная проверка Stage 3

Для первого теста использовать только один sender, например Guitar.

1. Закрыть старый Recorder и заменить VST3/EXE свежей Stage 3 сборкой.
2. Запустить Fender Studio и открыть Show Page.
3. Оставить `DAW Streamer` включённым на одном канале.
4. Запустить `DAW Streamer Recorder.exe`.
5. Проверить, что `Producer callbacks` растёт и Source format показывает `48000 Hz` и реальный block size (на текущей системе ожидается 512).
6. Нажать Record.
7. Записать 10–20 секунд, включая при желании Stop → Play в DAW.
8. Нажать Stop в Recorder.
9. Открыть созданный WAV из `Documents\\DAW Streamer Recordings`.
10. Проверить длительность, отсутствие щелчков/пропусков и значение `Dropped blocks`.
11. Импортировать WAV в DAW и убедиться, что звук соответствует исходному каналу.

## Критерий завершения

Этап 3 завершён, когда на целевой Windows/Fender Studio системе один sender стабильно создаёт через Recorder валидный 24-bit/48 kHz WAV, а автоматические и ручные проверки подтверждают порядок блоков, счётчики переполнения и безопасную работу без Recorder.
