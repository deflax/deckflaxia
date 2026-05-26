# Alpha Architecture Boundaries

This document defines the alpha architecture boundary for the DJ application. It is intentionally enforceable: source contracts live in `src/audio/AudioGraphContracts.h`, `src/app/EngineCommandQueue.h`, and `src/core/BackgroundWorkerContracts.h`, with CTest coverage in `tests/RealTimeSafetyContracts.cpp`.

## Alpha Must Have

- macOS audio devices default to CoreAudio.
- Linux audio devices default to JACK when available and fall back to ALSA.
- Four deck slots exist in the product architecture, with each future deck typed as either an audio-file deck or a MIDI step-sequencer deck.
- A global master clock drives audio decks, sequencer decks, and plugin MIDI timing.
- Plugin hosting is VST3-only, in-process, with background cancellable scan, cache, and blacklist surfaces.
- Library persistence uses SQLite with migrations, playlists/crates, beatgrid/key metadata, and persistent analysis jobs.
- Routing supports deck output assignments, cue bus, master bus, and future multi-output devices.
- MIDI Learn provides generic target mapping with persistence.
- Audio callback boundaries are hard real-time safe and compile-visible.

## Alpha Must NOT Have

- No AU, LV2, VST2, or CLAP hosting in alpha.
- No plugin sandboxing in alpha.
- No hardware-profile or hardware-specific controller profile system in alpha.
- No Windows verification promise, and Windows CI is not an alpha blocker.
- No production-grade time-stretch, pitch-lock, DVS, streaming services, cloud sync, accounts, marketplace, telemetry, or crash upload in alpha.
- No UI work, disk/DB I/O, plugin scan or instantiation, blocking locks, logging, heap-heavy allocation, or analysis work from audio callbacks.
- No implementation task may be split from its tests and QA.

## Layers and Ownership

- `src/app` owns process bootstrap, command-line smoke behavior, and message-thread command producers. It may enqueue engine commands but does not own audio callback state.
- `src/audio` owns callback-facing contracts: immutable snapshots, audio graph commands, audio graph command queues, and callback dependency whitelists. It must remain JUCE-light or JUCE-free where practical.
- `src/core` owns cross-cutting background worker contracts and cancellable job tokens. Workers are producers of state changes, never callback dependencies.
- Future `src/decks`, `src/plugins`, `src/library`, `src/analysis`, `src/midi`, and `src/rendering` modules consume these contracts but must not bypass them.
- Runtime state flows toward the audio thread as immutable snapshots and small graph commands. Ownership flows away from the callback: the callback observes snapshots and consumes commands, but it does not own UI services, persistence services, plugin scanners, filesystem importers, loggers, or worker pools.

## Threading Model

- Audio thread: the real-time device callback. It reads the active immutable snapshot, consumes bounded/non-blocking audio graph commands, renders audio, and publishes only lock-free meter-style data in later tasks.
- JUCE message thread: owns UI event handling, user gestures, and command production. It may push commands into the UI-to-engine command queue and may request background jobs.
- Analysis pool: background workers for beatgrid/key/waveform-adjacent analysis jobs. It writes results through persistence or snapshot rebuild paths outside the audio callback.
- Plugin scan worker: a cancellable background worker for VST3 discovery, cache updates, and blacklist changes. It never runs in or blocks the audio callback.
- DB worker: a background persistence worker for SQLite migrations, library changes, playlists/crates, and analysis job persistence. It never runs in or blocks the audio callback.
- Waveform worker: a background worker for waveform summary generation and cache refresh. It never runs in or blocks the audio callback.

## Message Passing

- UI-to-engine commands use `djapp::app::UiToEngineCommandQueue`, which wraps `djapp::audio::AudioGraphCommandQueue` and exposes `trySendFromMessageThread`.
- Audio graph commands are plain C++17 trivially copyable values: command kind, target id, numeric value, and auxiliary integer payload.
- Audio callbacks receive `djapp::audio::AudioCallbackContract`, which exposes only `ImmutableAudioSnapshot` and `AudioGraphCommandQueue`.
- Background workers use `djapp::core::BackgroundJobTicket` and `CancellableBackgroundWorker` contracts. They may request snapshot rebuilds through future non-real-time coordinators, not by calling audio callback objects.
- Cross-thread communication must be one-way at the boundary: message/background threads produce commands or snapshots; the audio thread consumes already-prepared data.

## Device Backend Defaults

- macOS: CoreAudio is the alpha default.
- Linux: JACK is preferred; ALSA is the fallback when JACK is unavailable.
- Windows: the codebase remains portable, but Windows verification is not promised for alpha and is not an alpha completion blocker.

## Sample-Rate and Buffer Matrix

The alpha verification matrix is encoded in `djapp::audio::kAlphaSampleRateBufferMatrix` and covers:

| Sample rate | Buffer frames |
| --- | --- |
| 44.1 kHz | 64 |
| 44.1 kHz | 128 |
| 44.1 kHz | 512 |
| 48 kHz | 64 |
| 48 kHz | 128 |
| 48 kHz | 512 |

## Persistence Surfaces

- SQLite library DB with migrations is the persistence boundary for tracks, playlists/crates, beatgrid/key metadata, and persistent analysis jobs.
- Plugin discovery cache and blacklist are persisted by non-real-time plugin scan workers.
- MIDI Learn mappings are persisted outside the audio callback and applied through immutable snapshot rebuilds or graph commands.
- Device preferences and routing assignments are persisted by app/library services outside the audio callback.
- Audio callbacks never open files, query SQLite, write logs, update caches, or trigger migrations.

## Audio-Thread Forbidden Operations

The audio callback must not perform or directly depend on:

- UI/message-thread service access.
- Disk or filesystem import work.
- SQLite, DB worker, migrations, or persistence calls.
- Plugin scan, plugin instantiation, plugin cache, or blacklist mutation.
- Logging, telemetry, crash upload, or diagnostics emission.
- Blocking mutexes, condition variables, sleeps, joins, futures waiting, or other blocking synchronization.
- Heap-heavy allocation, unbounded container growth, or large object construction.
- Track analysis, waveform generation, beatgrid/key detection, or worker-pool scheduling.
- Network, cloud, account, marketplace, streaming, or remote-service calls.

The compile-visible whitelist is `djapp::audio::isAudioCallbackDependencyAllowed`. `AudioCallbackBoundary` statically accepts only immutable snapshots, audio graph command queues, and callback contracts.
