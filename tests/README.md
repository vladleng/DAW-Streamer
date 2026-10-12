# DAW Streamer — существующие тесты и пробелы

> Актуальное состояние v0.1. Запуск: `cmake --preset windows-x64`, `cmake --build --preset windows-x64-release`, `ctest --preset windows-x64-release --output-on-failure`. См. [handoff](../docs/current-handoff.md).

Текущие зарегистрированные цели в `tests/CMakeLists.txt`:

- `shared-audio-transport` — ring, data/order/drop;
- `shared-midi-transport` — transport MIDI events;
- `shared-recorder-control` — управление;
- `recording-time-format` — формат длительности;
- `midi-file-export` — экспорт MIDI;
- `recorder-stage5a1-startup` — старт при запаздывании одного Sender.

**Нужно добавить для #34:** детерминированные RecorderEngine tests на queue backlog, скачок/разрыв producer-frame sequence, пропуск audio blocks, рассинхрон обработки нескольких Sender, уплотнение/перекрытие блоков, сохранение аудио после stop, качество фактических WAV samples и MIDI time alignment. Интеграционная проверка в реальном Fender Studio / Show Page обязательна для #31 и не заменяется CI. Не трактовать равную продолжительность четырёх WAV как доказательство целостности записи.
