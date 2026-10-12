# DAW Streamer — задачи и зависимости

[GitHub Project — DAW Streamer](https://github.com/users/vladleng/projects/9/views/1). Актуальные чек-листы и статус исполнения ведутся в GitHub Issues; эта страница — индекс, не повторение всей доски.

## Текущий приоритет

- **[#34 — BUG v0.1.1: общий разрыв всех WAV и аудиоартефакты](https://github.com/vladleng/DAW-Streamer/issues/34)** — текущая блокирующая задача. Пользователь наблюдал повторный разрыв на всех четырёх потоках.
- **[#31 — Эксплуатационный 60-минутный тест](https://github.com/vladleng/DAW-Streamer/issues/31)** — 60-минутная запись выполнена, но gate не принят из-за #34 и незакрытых sync/metrics проверок.
- **[#33 — Актуальный roadmap](https://github.com/vladleng/DAW-Streamer/issues/33)** — основной порядок этапов и версионирование.
- **[Техническая передача для нового чата](current-handoff.md)** — контекст, файлы кода, гипотезы, ожидаемые тесты и ограничения realtime.

## Дальнейшие этапы

1. После надёжной v0.1.x — [#32 v0.2 Dynamic Instances](https://github.com/vladleng/DAW-Streamer/issues/32).
2. После v0.2 — [#20 v0.3 Universal GUI](https://github.com/vladleng/DAW-Streamer/issues/20).
3. Позднее по необходимости — [#6 Show configurations](https://github.com/vladleng/DAW-Streamer/issues/6) и [#21 Optional Fender/PreSonus integration](https://github.com/vladleng/DAW-Streamer/issues/21).

## История, не текущий backlog

[#1–#5](https://github.com/vladleng/DAW-Streamer/issues), #7–#11, #15, #17, #25, #29 — завершённые/исторические этапы старой последовательности, включая прежние версии с standalone Recorder. Текущая публичная v0.1 = embedded Master Recorder. Не трактовать старые описания «MIDI позже» и «отдельный Recorder.exe обязателен» как актуальные.
