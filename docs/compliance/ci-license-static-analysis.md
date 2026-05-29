# Disabled CI, License, and Static Analysis Hardening

## GitHub Actions Policy

GitHub Actions CI is intentionally disabled for this repository. Do not add active `.yml` or `.yaml` workflow files under `.github/workflows`, do not restore `push`, `pull_request`, `workflow_dispatch`, `schedule`, `workflow_call`, `workflow_run`, or dispatch-style triggers, and do not reactivate CI in future feature plans unless the user explicitly requests that change. Historical workflow definitions may be kept only as disabled reference artifacts outside `.github/workflows`.

## Local Verification Parity

Run these commands from the repository root to match the Linux fallback infrastructure flow:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
cmake --build build --target static-analysis
cmake --build build --target plugin-sandbox-helper-packaging-check
ctest --test-dir build --output-on-failure
cmake --build build --target license-report
```

The license inventory is written to `build/license-report.spdx.txt`. Real playable JUCE workflow evidence should copy the generated report or command output to `.omo/evidence/real-playable-juce/task-1-license-report.txt` and capture missing-JUCE required configure output in `.omo/evidence/real-playable-juce/task-1-missing-juce.log`.

## Required JUCE Gate

Presubmit and release-style JUCE validation should configure with:

```sh
cmake -S . -B build-juce -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON
```

When JUCE is not available, this command must fail clearly and explain both supported setup paths: an installed/exported JUCE CMake package via `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`, or a licensed local `third_party/JUCE` checkout. Bootstrap fallback configuration remains available only with `DECKFLAXIA_REQUIRE_JUCE=OFF` for early CI/dev checks that do not claim JUCE functionality.

Ubuntu JUCE-required validation needs Linux GUI/media development packages before configure and build. A `gtk/gtk.h` failure means `libgtk-3-dev` is missing; WebKitGTK failures from `juce_gui_extra` usually mean `libwebkit2gtk-4.1-dev` or the distro equivalent is missing. Keep fallback checks useful for infrastructure validation, but do not report fallback output as native JUCE success. See `docs/user-runbook-developer-operations.md` for the full Linux package list.

When the real VST3 fixture target is available, local JUCE-required validation should build it before VST3 tests or app smokes:

```sh
cmake -S . -B build-juce -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON
cmake --build build-juce --target DeckflaxiaRealVst3Fixture --parallel 1
ctest --test-dir build-juce -R "VST3Processing\.(AppSmoke|RealFixture|RealProcessing|RealParameters|RealState)" --output-on-failure
./build-juce/Deckflaxia --vst3-processing-smoke-test --chain deck-a --fixtures build-juce/generated/real-vst3-fixture --exit-after-init
```

The fixture manifest is generated at `${CMAKE_BINARY_DIR}/generated/real-vst3-fixture/manifest.json`, with default local path `build-juce/generated/real-vst3-fixture/manifest.json`. Successful real fixture validation must include `backend=juce-vst3` and `real-vst3-instantiated=1` from the production host path. Fallback/no-JUCE output must not claim that marker from deterministic fixtures.

The disabled workflow reference intentionally had no Windows job. Local platform gates are still Ubuntu 24.04 and macOS latest-equivalent hosts, and both may check out `juce-framework/JUCE` at the pinned `JUCE_REF` into `third_party/JUCE` before running the JUCE-required configure/build/CTest sequence, `DeckflaxiaRealVst3Fixture`, `plugin-sandbox-helper-packaging-check`, playable smoke, plugin sandbox smoke, current performance-equivalent tempo/overload smokes, and `license-report`. This is a local-only licensed checkout under JUCE AGPL/commercial terms; JUCE is not vendored in this repository.

`plugin-sandbox-helper-packaging-check` verifies that `DeckflaxiaPluginSandboxHelper` was built beside `Deckflaxia` and that `DeckflaxiaPluginSandboxHelper --helper-smoke` emits its readiness line. It does not claim OS-level sandbox hardening, notarization, or code signing.

## macOS Evidence Requirement

For macOS evidence, use a local or explicitly provisioned macOS agent to configure, build, run helper packaging, CTest, playable smoke, plugin sandbox smoke, current performance-equivalent smokes, and the license report when JUCE is available. A fallback-only infrastructure check can use:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/Deckflaxia --smoke-test --exit-after-init
ctest --test-dir build --output-on-failure
```

Save Task 14 macOS output to `.omo/evidence/real-playable-juce/task-14-macos-ci.log`. A Linux-hosted placeholder may document these instructions but must not claim macOS execution.

## Static Analysis Prerequisites

`cmake --build build --target static-analysis` checks whether `clang-format` and `clang-tidy` are installed. This target intentionally stays non-failing when those tools are absent so bootstrap local checks remain usable in constrained containers. Full static analysis requires LLVM tooling, `.clang-format`, and a configure step with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## License Inventory Policy

`cmake --build build --target license-report` generates an SPDX-style inventory with:

- Deckflaxia project source as `AGPL-3.0-or-later`.
- JUCE framework as `AGPL-3.0-only OR LicenseRef-JUCE-Commercial`.
- Steinberg VST3 SDK as `MIT`.
- Rubber Band as optional system `GPL-2.0-or-later` for the guarded primary stretch engine; live playback uses the real-time boundary when present.
- Signalsmith Stretch as `MIT` for the non-vendored fallback adapter boundary.
- SoundTouch as `LGPL-2.1-only` for legacy fallback only.
- System SQLite C API target as the persistence dependency.
- No-vendored-third-party posture, except an optional local licensed `third_party/JUCE` checkout.

The source-built real VST3 fixture is test infrastructure only. Review and record the license for any VST3 SDK or sample code copied into the repository, and add attribution or notices before use. Do not add proprietary or commercial plugin binaries. Generated `.vst3` bundles and their generated bundle directories are build artifacts and must not be committed, vendored, installed as app payload, or uploaded as CI artifacts.

The repository license is AGPL-3.0-or-later. This document and the generated SPDX-style report are engineering compliance aids, not legal advice. The current repository does not add telemetry, crash upload, accounts, cloud sync, marketplace, or remote logging infrastructure; local logs are for developer diagnostics only and are not uploaded.
