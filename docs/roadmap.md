# DAW Streamer — актуальный roadmap

> **Состояние на 2026-10-12.** Это новый roadmap после публичного `v0.1` с embedded Master Recorder. Старые номера версий `v0.2.0` (host control) и `v0.3.0` (MIDI) — *исторические релизы прежнего цикла*; MIDI и control уже входят в текущую публичную `v0.1`. Старые Stage-файлы сохраняются только как история реализации.

| Очерёдность | Версия / issue | Содержание | Состояние |
| --- | --- | --- | --- |
| 1 | `v0.1.x` / [#31](https://github.com/vladleng/DAW-Streamer/issues/31) | Эксплуатационная и часовая проверка | Идёт, gate **не пройден** |
| 1a | `v0.1.1` / [#34](https://github.com/vladleng/DAW-Streamer/issues/34) | Общий WAV dropout, искажения и диагностика | **Блокер, требуется исправление** |
| 2 | `v0.2` / [#32](https://github.com/vladleng/DAW-Streamer/issues/32) | Dynamic Instances, Audio / MIDI / Audio+MIDI capabilities | Не начинать до #31/#34 |
| 3 | `v0.3` / [#20](https://github.com/vladleng/DAW-Streamer/issues/20) | Компактный универсальный GUI, meters, diagnostics | После #32 |
| Позже | [#6](https://github.com/vladleng/DAW-Streamer/issues/6), [#21](https://github.com/vladleng/DAW-Streamer/issues/21) | Show templates; optional host extensions | Отложено |

Актуальная общая задача: [#33](https://github.com/vladleng/DAW-Streamer/issues/33). Передача новому чату и технический план текущего багфикса: [docs/current-handoff.md](current-handoff.md).

## Следующая работа

1. Аудит `RecorderEngine` timeline/worker behavior, shared transport и входящего host audio.
2. Регрессионный тест общего разрыва и проверка sample contents, воспроизводимая диагностика.
3. Минимальный bugfix maintenance v0.1.1 и Windows CI.
4. Ручной длинный тест/приёмка пользователя по #31. Не закрывать #31 лишь потому, что часовой дубль был записан: в нём уже выявлены дефекты.
5. Только после успешной проверки начать Dynamic Instances v0.2.

**Правило:** не смешивать новую функциональность и устранение надёжности в одном PR/релизе. Базовая архитектура — host-agnostic VST3.
