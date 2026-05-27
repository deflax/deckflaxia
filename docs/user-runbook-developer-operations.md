# User Runbook and Developer Operations Guide

This guide tells an agent or developer how to build, run, and verify the real playable JUCE DJ workflow without overstating what a fallback environment proves.

## Licensing and Dependencies

The repository is `AGPL-3.0-or-later`. This is an engineering compliance note, not legal advice.

Dependency posture:

- JUCE is used under `AGPL-3.0-only OR LicenseRef-JUCE-Commercial` terms. The repo doesn't vendor JUCE.
- GitHub Actions checks out `juce-framework/JUCE` at pinned `JUCE_REF=8.0.10` into `third_party/JUCE` before `DECKFLAXIA_REQUIRE_JUCE=ON` configure. That checkout is CI-only and must not be committed.
- Steinberg VST3 SDK is listed as `MIT`.
- Rubber Band is the guarded primary stretch engine and is listed as `GPL-2.0-or-later` when present as a system dependency.
- Signalsmith is a non-vendored `MIT` fallback boundary.
- SoundTouch is `LGPL-2.1-only` legacy fallback only.
- Persistence targets the system SQLite C API when CMake finds it.

Run the local license report after configure:

```sh
cmake --build build --target license-report
```

The report is written to `build/license-report.spdx.txt`.

## Native JUCE Setup

Use one of these supported JUCE setup paths:

- Provide an installed or exported JUCE CMake package with `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`.
- Provide a licensed local checkout at `third_party/JUCE` and configure with `-DDECKFLAXIA_USE_VENDORED_JUCE=ON`.

CI uses the second path by checking out JUCE at `JUCE_REF=8.0.10` during the workflow. JUCE is not stored in this repository.

## Build on macOS or Linux with JUCE

Run from the repository root on macOS 15 or Ubuntu 24.04 with JUCE available:

```sh
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-juce
ctest --test-dir build-juce --output-on-failure
```

If using `third_party/JUCE`, use:

```sh
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-juce
ctest --test-dir build-juce --output-on-failure
```

## Run the App

After a JUCE build:

```sh
./build-juce/Deckflaxia
```

For CI or headless setup checks:

```sh
./build-juce/Deckflaxia --juce-shell-smoke-test --exit-after-init
./build-juce/Deckflaxia --juce-ui-smoke-test --dump-components --exit-after-init
```

## Fixture Smoke Commands

Playable smoke:

```sh
./build-juce/Deckflaxia --playable-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
```

Production smoke:

```sh
./build-juce/Deckflaxia --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
```

Plugin sandbox smoke:

```sh
cmake --build build-juce --target plugin-sandbox-helper-packaging-check
./build-juce/Deckflaxia --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures tests/fixtures/plugins --exit-after-init
```

Performance smoke at the plan budget surface:

```sh
./build-juce/Deckflaxia --performance-smoke-test --fixtures tests/fixtures/dj-workflow --sample-rate 48000 --buffer-size 512 --exit-after-init
```

Scope audit:

```sh
./build-juce/Deckflaxia --scope-audit --forbid "Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor" --paths src tests docs cmake # deferred scope audit
```

## Plugin Sandbox Behavior

The sandbox MVP uses exactly five chain helpers: deck A, deck B, deck C, deck D, and master. `plugin-sandbox-helper-packaging-check` verifies that `DeckflaxiaPluginSandboxHelper` is built beside `Deckflaxia` and that `DeckflaxiaPluginSandboxHelper --helper-smoke` emits its readiness line.

Smoke behavior covers helper heartbeat, control IPC, audio roundtrip, crash detection, restart, repeat-crash blacklist, and persisted sandbox health. Per-plugin sandboxing remains deferred. Embedded plugin editors also remain deferred; this workflow uses separate plugin editor windows when native JUCE/VST3 support is available and generic parameter/status surfaces in fallback mode.

## Local Fallback Checks

Use this path only when the machine doesn't have JUCE or native dependencies. It verifies deterministic core behavior and infrastructure. It does not prove native JUCE, native VST3, native editor, macOS, screenshot, WAV encoding, Rubber Band, or system SQLite success.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
cmake --build build --target plugin-sandbox-helper-packaging-check
ctest --test-dir build --output-on-failure
./build/Deckflaxia --smoke-test --exit-after-init
./build/Deckflaxia --playable-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
./build/Deckflaxia --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
./build/Deckflaxia --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures tests/fixtures/plugins --exit-after-init
./build/Deckflaxia --performance-smoke-test --fixtures tests/fixtures/dj-workflow --sample-rate 48000 --buffer-size 512 --exit-after-init
./build/Deckflaxia --scope-audit --forbid "Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor" --paths src tests docs cmake # deferred scope audit
cmake --build build --target license-report
```

Fallback logs should state `juce=unavailable`, deterministic or generic plugin behavior, and SQLite fallback state if system SQLite is unavailable.

## Troubleshooting

If `DECKFLAXIA_REQUIRE_JUCE=ON` configure fails, install or export JUCE and pass `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`, or add a licensed `third_party/JUCE` checkout and pass `-DDECKFLAXIA_USE_VENDORED_JUCE=ON`.

If Rubber Band is missing, local fallback uses the Signalsmith-compatible boundary and must not be treated as Rubber Band DSP verification.

If VST3 plugins or native editors are missing, the fallback processor and generic parameter surface can still validate command paths, but they do not prove native VST3 instantiation or native editor windows.

If system SQLite is missing, restart smoke may write an honest fallback state file. That file isn't a SQLite database.

If screenshots or WAV renders are missing in fallback mode, don't create fake artifacts. The smoke logs should explain the blocked surface.

If `clangd` or Markdown/YAML language servers are unavailable, record that diagnostics couldn't run and rely on CMake, CTest, command evidence, and read or grep review.

## Accepted Local Limitations

This Linux container lacks JUCE, Rubber Band, native VST3/editor surfaces, system SQLite, fallback WAV encoding, and clangd. Local evidence from this environment is fallback-only. It can prove command syntax, deterministic core behavior, helper packaging, smoke surfaces, scope audit behavior, and documentation coverage. It does not claim native JUCE app success, native VST3 success, native editor windows, macOS runtime, screenshots, WAV output, Rubber Band DSP, or system SQLite persistence.

## Deferred Features

These features are intentionally out of scope: Windows, recording, smart playlists, samplers, streaming, DVS/timecode, Rekordbox/Serato import, cloud/accounts/marketplace.

Related implementation limits are also deferred: per-plugin sandboxing and embedded plugin editors.

## Task 17 Evidence Commands

Required-JUCE command capture for a JUCE-equipped runner, or an honest local missing-JUCE failure:

```sh
cmake -S . -B build-juce-runbook -DDECKFLAXIA_REQUIRE_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-juce-runbook
./build-juce-runbook/Deckflaxia --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
```

Fallback-only verification and deferred docs audit in this container:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/Deckflaxia --scope-audit --forbid "Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor" --paths src tests docs cmake # deferred scope audit
python3 - <<'PY'
from pathlib import Path
text = Path('README.md').read_text() + '\n' + Path('docs/user-runbook-developer-operations.md').read_text()
terms = ['Windows', 'recording', 'smart playlists', 'samplers', 'streaming', 'DVS/timecode', 'Rekordbox/Serato import', 'cloud/accounts/marketplace']  # deferred docs audit
missing = [term for term in terms if term not in text]
print('missing=' + ','.join(missing) if missing else 'deferred-docs-audit: PASS')
raise SystemExit(1 if missing else 0)
PY
```
