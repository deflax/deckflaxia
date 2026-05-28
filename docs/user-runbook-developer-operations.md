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

## Active CI Contract

The active GitHub Actions workflow runs on `push`, `pull_request`, and `workflow_dispatch`. Routine push and pull request runs include two Ubuntu 24.04 jobs:

- `linux-fallback` configures without JUCE and runs the fallback build, CTest, smoke, plugin sandbox, performance, packaging, and license gates. This is infrastructure coverage only. It doesn't prove native JUCE, native VST3, native editor windows, macOS runtime, screenshots, WAV output, Rubber Band DSP, or system SQLite persistence.
- `linux-juce-required` installs the Ubuntu native GUI and media dependencies, checks out `juce-framework/JUCE` at pinned `JUCE_REF=8.0.10` into `third_party/JUCE`, verifies `third_party/JUCE/CMakeLists.txt`, configures with `DECKFLAXIA_REQUIRE_JUCE=ON` and `DECKFLAXIA_USE_VENDORED_JUCE=ON`, builds with `cmake --build build-juce --parallel 1`, builds `DeckflaxiaRealVst3Fixture` with `cmake --build build-juce --target DeckflaxiaRealVst3Fixture --parallel 1`, then runs static analysis, `plugin-sandbox-helper-packaging-check`, CTest, playable smoke, plugin sandbox smoke, performance, and license gates. The JUCE-required CTest suite includes `VST3Processing.AppSmoke`, `VST3Processing.RealFixture`, `VST3Processing.RealProcessing`, `VST3Processing.RealParameters`, and `VST3Processing.RealState` after the real fixture target has been built.

The `macos-juce-required` job is optional and high-cost for routine presubmit. It runs only for manual `workflow_dispatch` executions or `main` branch runs through `if: github.event_name == 'workflow_dispatch' || github.ref == 'refs/heads/main'`. When it runs, it uses the same pinned JUCE checkout, checkout verification, required JUCE configure, low-memory build, real VST3 fixture build, `plugin-sandbox-helper-packaging-check`, CTest, playable smoke, plugin sandbox smoke, performance, and license contract as the Linux JUCE job, excluding Linux-only static analysis.

CTest owns fixture setup through `Fixtures.Generate` in fallback and JUCE-required jobs. Don't add a separate fixture generation step before CTest unless a direct binary command needs generated files outside CTest.

## Native JUCE Setup

Use one of these supported JUCE setup paths:

- Provide an installed or exported JUCE CMake package with `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`.
- Provide a licensed local checkout at `third_party/JUCE` and configure with `-DDECKFLAXIA_USE_VENDORED_JUCE=ON`.

CI uses the second path by checking out JUCE at `JUCE_REF=8.0.10` during the workflow. JUCE is not stored in this repository.

To mirror the pinned CI checkout locally, run from the repository root:

```sh
mkdir -p third_party
git clone --branch 8.0.10 --depth 1 https://github.com/juce-framework/JUCE.git third_party/JUCE
test -f third_party/JUCE/CMakeLists.txt
```

This uses JUCE's stable tagged release source from `juce-framework/JUCE`, which the upstream CMake documentation supports through `add_subdirectory`. Treat `third_party/JUCE` as a local licensed checkout only: `.gitignore` excludes `third_party/`, and the checkout must not be committed or treated as a vendored dependency. Review the `AGPL-3.0-only OR LicenseRef-JUCE-Commercial` choice before distributing a JUCE build. If the project updates `JUCE_REF`, update this local checkout tag at the same time.

On Ubuntu 24.04, install the native build and GUI/media development packages before configuring a JUCE-required build:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

If the distro only packages WebKitGTK 4.0, use `libwebkit2gtk-4.0-dev` instead of `libwebkit2gtk-4.1-dev`. A `gtk/gtk.h` compiler error means JUCE's Linux GUI module cannot find GTK development headers, usually because `libgtk-3-dev` is missing. WebKitGTK errors from `juce_gui_extra` mean the WebKit development package or distro equivalent is missing.

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
test -f third_party/JUCE/CMakeLists.txt
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-juce
ctest --test-dir build-juce --output-on-failure
```

CTest materializes deterministic DJ workflow audio fixtures through `Fixtures.Generate` before tests that require `tests/fixtures/dj-workflow`. If you run individual binaries directly or use an existing build tree from before that CTest fixture setup, generate them once first:

```sh
./build-juce/FixtureTests generate tests/fixtures/dj-workflow
```

On low-memory machines, compile the JUCE build with limited parallelism. `c++: fatal error: Killed signal terminated program cc1plus` while compiling files such as `third_party/JUCE/modules/juce_graphics/juce_graphics.cpp` or `juce_gui_basics.cpp` means the OS killed the compiler for memory pressure, not that the source file is invalid:

```sh
cmake --build build-juce --parallel 1
ctest --test-dir build-juce --output-on-failure
```

The source-built real VST3 gain fixture uses the same low-memory rule. Once the `DeckflaxiaRealVst3Fixture` target exists, build it in the JUCE tree with one compile job:

```sh
cmake --build build-juce --target DeckflaxiaRealVst3Fixture --parallel 1
```

That target writes the real fixture manifest to `${CMAKE_BINARY_DIR}/generated/real-vst3-fixture/manifest.json`; for the default local tree, expect `build-juce/generated/real-vst3-fixture/manifest.json`. The manifest points at the generated `.vst3` bundle in the build tree. The bundle is a build artifact only. Do not commit, vendor, install as app payload, or upload generated `.vst3` bundles as CI artifacts.

If one job is unnecessarily slow and the machine has enough RAM, try `--parallel 2`. Avoid broad `-j` builds in constrained containers because JUCE module translation units are large.

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

Real VST3 processing smoke, after configuring a JUCE-required build and building `DeckflaxiaRealVst3Fixture`:

```sh
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-juce --target DeckflaxiaRealVst3Fixture --parallel 1
ctest --test-dir build-juce -R "VST3Processing\.(AppSmoke|RealFixture|RealProcessing|RealParameters|RealState)" --output-on-failure
./build-juce/Deckflaxia --vst3-processing-smoke-test --chain deck-a --fixtures build-juce/generated/real-vst3-fixture --exit-after-init
```

Once the generated manifest exists at `build-juce/generated/real-vst3-fixture/manifest.json`, the real VST3 CTest gate should report `backend=juce-vst3` and `real-vst3-instantiated=1`. The deterministic fixture alone must keep reporting fallback or unavailable status and must not satisfy the real marker. If the app binary is under a generator-specific output directory, run the same arguments against that binary, for example `./build-juce/Deckflaxia_artefacts/Debug/Deckflaxia`.

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

`ctest` also runs `Fixtures.Generate` before fixture-dependent fallback tests. If you run binaries directly instead of CTest, use `./build/FixtureTests generate tests/fixtures/dj-workflow` first.

## Troubleshooting

If CMake reports that `CMakeCache.txt` was created from a different source directory, the build tree is stale. Remove the affected build directory or choose a fresh one before reconfiguring:

```sh
rm -rf build-juce
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

If `DECKFLAXIA_REQUIRE_JUCE=ON` configure fails, install or export JUCE and pass `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`, or add a licensed `third_party/JUCE` checkout and pass `-DDECKFLAXIA_USE_VENDORED_JUCE=ON`.

If `third_party/JUCE/CMakeLists.txt` is missing, recreate the local checkout from the repository root:

```sh
mkdir -p third_party
git clone --branch 8.0.10 --depth 1 https://github.com/juce-framework/JUCE.git third_party/JUCE
```

If `third_party/JUCE` already exists but is the wrong version, remove or move that local ignored directory first, then clone the pinned tag again. Do not commit `third_party/JUCE` or add it as a vendored dependency.

If a JUCE-required build reaches `gtk/gtk.h` and fails, install `libgtk-3-dev`. If it reaches WebKitGTK package or header errors through `juce_gui_extra`, install `libwebkit2gtk-4.1-dev` or the distro equivalent. These are system dependency failures, not fallback-mode successes.

If a JUCE-required build fails with `c++: fatal error: Killed signal terminated program cc1plus` while compiling `third_party/JUCE/modules/...`, rerun the build with fewer jobs:

```sh
cmake --build build-juce --parallel 1
```

This is the expected low-RAM failure mode for large JUCE module translation units. Increase to `--parallel 2` only if the machine has enough memory.

If Rubber Band is missing, local fallback uses the Signalsmith-compatible boundary and must not be treated as Rubber Band DSP verification.

If VST3 plugins or native editors are missing, the fallback processor and generic parameter surface can still validate command paths, but they do not prove native VST3 instantiation or native editor windows. Fallback and no-JUCE runs also do not prove native VST3, native editor windows, screenshots, or real JUCE plugin hosting.

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
