# DAW Streamer

DAW Streamer — VST3-плагин и отдельное приложение Recorder для многодорожечной записи живого выступления напрямую из Show Page без второй DAW.

Проект появился из практической задачи: во время выступления не хочется запускать Reaper, настраивать ReaStream и контролировать вторую DAW только ради записи. DAW Streamer оставляет рабочий Show Page основным окружением: sender-плагины прозрачно пропускают звук дальше и одновременно передают копию аудио в отдельный Recorder на том же Windows-компьютере.

## Текущая версия

**v0.2.0 — готовая протестированная версия для Windows x64.**

Проверенный рабочий сценарий:

- Windows 11 x64;
- Fender Studio Pro / Show Page;
- 48 kHz;
- отдельные 24-bit PCM WAV;
- четыре роли: `Vocal`, `Guitar`, `Keys`, `Playback`;
- длительные концертные дубли 40–60 минут;
- Record/Stop из Recorder, из любого экземпляра DAW Streamer или из Fender Studio Performance Mode;
- двусторонняя индикация состояния через стандартный VST3 parameter feedback;
- аппаратная двусторонняя проверка с MIDI Captain также пройдена.

DAW transport и запись DAW Streamer независимы: Play/Stop и смена песен в Show Page не останавливают текущий дубль.

## Что нового в v0.2.0

Версия 0.2 добавляет две функции поверх стабильного аудиоядра 0.1:

1. **Recording time** — Recorder и все экземпляры VST3 показывают одну фактическую длительность текущего дубля в формате `HH:MM:SS`.
2. **Performance Mode control** — DAW Streamer публикует boolean VST3-параметр `Recording` (`OFF = Stop`, `ON = Record`). Fender Studio Performance Mode может назначить его на кнопку и получает обратное состояние Recorder.

Recorder остаётся единственным authoritative state: если запись запущена из standalone Recorder или другого экземпляра DAW Streamer, состояние синхронизируется обратно во все sender-плагины и mapped control Performance Mode.

## Установка

В релизном архиве находятся:

- `DAW Streamer.vst3` — sender-плагин;
- `DAW Streamer Recorder.exe` — standalone Recorder.

Скопируйте VST3 в стандартную пользовательскую или системную папку VST3 Windows и выполните пересканирование плагинов в DAW. Recorder можно хранить в любой удобной папке и запускать вручную перед репетицией или выступлением.

Для Fender Studio / Show Page sender рекомендуется ставить в следующих точках:

- **Vocal** — до обработки;
- **Guitar** — до обработки;
- **Keys** — после виртуального инструмента, но до эффектов;
- **Playback** — до обработки.

В каждом экземпляре DAW Streamer выберите соответствующую роль. Одна роль должна быть занята только одним активным sender-инстансом.

## Запись

1. Запустите `DAW Streamer Recorder.exe`.
2. Выберите базовую папку записи и укажите имя Show/session.
3. Откройте Show Page. У четырёх sender-инстансов должны появиться активные роли.
4. Запустите запись кнопкой Record в Recorder, в VST3 или mapped-кнопкой `Recording` в Performance Mode.
5. Stop закрывает текущий take. Следующий Record создаёт новый take и не перезаписывает предыдущий.

Структура результата:

```text
Recording folder/
└── Show or session/
    └── Take_YYYY-MM-DD_HH-MM-SS/
        ├── Vocal.wav
        ├── Guitar.wav
        ├── Keys.wav
        └── Playback.wav
```

Все четыре файла сохраняют общую временную шкалу. При временном исчезновении sender callback Recorder сохраняет timeline, вставляя соответствующий разрыв как тишину, а не сжимая время.

## Performance Mode

В Fender Studio Performance Mode найдите параметр DAW Streamer **`Recording`** и назначьте его на кнопку/toggle.

Подтверждённая цепочка управления:

`Performance Mode / MIDI Captain -> VST3 Recording -> RecorderControl -> Recorder`

Подтверждённая обратная связь:

`Recorder -> authoritative state -> VST3 Recording -> Performance Mode -> MIDI Captain`

Для v0.2.0 Fender-specific API не используется: протестированная двусторонняя связь работает стандартным механизмом VST3 parameter feedback.

## Ограничения v0.2.0

- только Windows x64;
- рабочий формат текущего live-сценария — 48 kHz;
- фиксированные четыре аудиороли;
- MIDI performance capture пока не реализован;
- автоматический запуск записи намеренно отсутствует;
- recovery после аварийного завершения будет разрабатываться только при появлении реального проблемного сценария;
- шаблоны разных конфигураций Show отложены на поздний этап;
- визуальный интерфейс пока технический и будет переработан в версии 0.3.

## Сборка из исходников

Стек проекта:

- C++20;
- JUCE 9.0.2;
- CMake;
- Visual Studio 2022 / MSVC;
- VST3.

Подробности: [docs/build.md](docs/build.md).

## Документация и разработка

- [Требования](docs/requirements.md)
- [Архитектура](docs/architecture.md)
- [Roadmap](docs/roadmap.md)
- [Сборка](docs/build.md)
- [Stage 8B / Performance Mode](docs/stage8b-performance-mode.md)
- [Доска разработки](https://github.com/users/vladleng/projects/9/views/1)
- [Issues](https://github.com/vladleng/DAW-Streamer/issues)

Следующий крупный этап roadmap — **v0.3: переработка визуального интерфейса**, без изменения уже проверенного аудиоядра.
