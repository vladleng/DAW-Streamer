# DAW Streamer v0.1

DAW Streamer v0.1 — первый полноценный релиз новой embedded-recorder архитектуры Moon River Studio.

## Что изменилось

Главное изменение — отдельное приложение Recorder больше не требуется для основного workflow. Один и тот же `DAW Streamer.vst3` работает в двух режимах:

- `Sender` на рабочих каналах;
- `Master Recorder` на постоянном Main/Master канале.

Master Recorder принимает четыре синхронных аудиопотока и один MIDI event stream через Windows shared memory и пишет итоговые WAV + `MIDI.mid`.

## Инстансы

Четыре внутренних слота пользователь видит как:

- `Inst 1`
- `Inst 2`
- `Inst 3`
- `Inst 4`

Свободный slot назначается автоматически. Имя каждого Sender можно изменить прямо на основном экране. Если имя не менять, Master и готовые WAV используют `Inst 1`, `Inst 2` и т. д. Пользовательские имена сохраняются в Show/project state и становятся именами WAV.

## Запись

- 4 синхронных 24-bit PCM WAV при 48 kHz;
- непрерывная временная шкала take независимо от host Play/Stop;
- переключение Songs не прерывает запись при постоянном Master instance;
- transparent audio pass-through;
- один MIDI stream без пятого обязательного audio sender;
- `MIDI.mid`: SMF Type 0, one track, 960 PPQ;
- Note On/Off, velocity, CC, pitch bend, aftertouch, program change и CC64 sustain;
- общий VST3 boolean parameter `Recording` для host/control-surface mapping.

## Интерфейс

Основной экран оставлен компактным: имя instance/session, состояние, время, Audio/MIDI/Master health и Record/Stop. Queue, Drop, Gaps, format и другие технические данные находятся в раскрываемом `Details`; редко меняемые параметры — в `Setup`.

## Проверено вручную

Релиз проверен на Windows 11 x64 в Fender Studio Pro / Show Page:

- четыре Sender + один Master Recorder;
- непрерывная audio + MIDI запись;
- переключение Songs;
- совпадение MIDI с записанным Keys audio после импорта;
- sustain pedal CC64;
- пользовательские имена Sender;
- восстановление состояния Show;
- пользовательские имена итоговых WAV.

Fender Studio является проверенной средой, но не обязательной зависимостью: рабочая часть использует обычный VST3 и общий Windows transport.

## Установка

Архив релиза содержит:

- `DAW Streamer.vst3`
- `README.md`

Скопируйте VST3 bundle в стандартную Windows VST3-папку и выполните rescan в host.

## Дальнейшие обновления

v0.1 принимается как основной релиз. Если реальные выступления или дальнейшее тестирование выявят баги, исправления будут выпускаться как v0.1.1, v0.1.2 и далее.
