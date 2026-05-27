# CI, License, and Static Analysis Hardening

## Local CI Parity

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

Ubuntu JUCE-required validation needs Linux GUI/media development packages before configure and build. A `gtk/gtk.h` failure means `libgtk-3-dev` is missing; WebKitGTK failures from `juce_gui_extra` usually mean `libwebkit2gtk-4.1-dev` or the distro equivalent is missing. Keep fallback CI useful for infrastructure checks, but do not report fallback output as native JUCE success. See `docs/user-runbook-developer-operations.md` for the full Linux package list.

The GitHub Actions workflow intentionally has no Windows job. The platform gates are `ubuntu-24.04` and `macos-latest`, and both check out `juce-framework/JUCE` at the pinned `JUCE_REF` into `third_party/JUCE` before running the JUCE-required configure/build/CTest sequence, `plugin-sandbox-helper-packaging-check`, playable smoke, plugin sandbox smoke, current performance-equivalent tempo/overload smokes, and `license-report`. This is a CI-only checkout under JUCE AGPL/commercial terms; JUCE is not vendored in this repository.

`plugin-sandbox-helper-packaging-check` verifies that `DeckflaxiaPluginSandboxHelper` was built beside `Deckflaxia` and that `DeckflaxiaPluginSandboxHelper --helper-smoke` emits its readiness line. It does not claim OS-level sandbox hardening, notarization, or code signing.

## macOS Evidence Requirement

The GitHub Actions macOS job configures, builds, runs helper packaging, CTest, playable smoke, plugin sandbox smoke, current performance-equivalent smokes, and the license report on `macos-latest`. If a local macOS agent is used instead, run the same `build-juce` command sequence from the workflow when JUCE is available. A fallback-only infrastructure check can use:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/Deckflaxia --smoke-test --exit-after-init
ctest --test-dir build --output-on-failure
```

Save Task 14 macOS output to `.omo/evidence/real-playable-juce/task-14-macos-ci.log`. A Linux-hosted placeholder may document these instructions but must not claim macOS execution.

## Static Analysis Prerequisites

`cmake --build build --target static-analysis` checks whether `clang-format` and `clang-tidy` are installed. This target intentionally stays non-failing when those tools are absent so bootstrap CI remains green in constrained containers. Full static analysis requires LLVM tooling, `.clang-format`, and a configure step with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

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

The repository license is AGPL-3.0-or-later. This document and the generated SPDX-style report are engineering compliance aids, not legal advice. The current repository does not add telemetry, crash upload, accounts, cloud sync, marketplace, or remote logging infrastructure; local logs are for developer diagnostics only and are not uploaded.
