# shared/ — рабочие IPC-компоненты v0.1

**Реализованы и используются действующим VST3**, а не только запланированы:

- `SharedAudioTransport.*` — четыре bounded SPSC shared-memory audio rings, sequence/producer-frame metadata, Drop/oversize;
- `SharedMidiTransport.*` — отдельный MIDI event transport;
- `SharedRecorderControl.*` — Record/Stop и authoritative shared state;
- `SharedStreamMetadata.*` — пользовательские имена Sender;
- `RecordingTime.h` — общее форматирование длительности.

Текущая работа #34 касается в том числе транспортных потерь и временной шкалы. Не менять shared ABI и realtime path без теста на обратную совместимость и подтверждённой причины.

[Актуальная архитектура](../docs/architecture.md) · [Технический handoff](../docs/current-handoff.md)
