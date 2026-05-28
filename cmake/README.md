# CMake Integration

The real playable JUCE workflow uses `DECKFLAXIA_REQUIRE_JUCE=ON` for presubmit and release-style configuration. The integration first accepts a licensed local checkout at `third_party/JUCE` when `DECKFLAXIA_USE_VENDORED_JUCE=ON`, then falls back to `find_package(JUCE CONFIG)` so installed/exported JUCE packages can be supplied with `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`.

This repository does not vendor JUCE, the VST3 SDK, Rubber Band, Signalsmith, SoundTouch, SQLite wrappers, or any other third-party dependency. CMake may link an already-installed system Rubber Band library as the guarded primary time-stretch boundary; otherwise it builds the honest Signalsmith-compatible fallback boundary without claiming production stretch quality. When `DECKFLAXIA_REQUIRE_JUCE=OFF` and JUCE is unavailable, CMake builds an explicit bootstrap-only fallback executable so early CI and smoke tests can validate repository infrastructure without claiming JUCE functionality.

GitHub Actions checks out `juce-framework/JUCE` into `third_party/JUCE` at pinned `JUCE_REF=8.0.10` immediately before required-JUCE configure. The checkout is CI-only and relies on JUCE AGPL/commercial licensing terms; it must not be committed or treated as a vendored dependency. The active workflow runs `linux-fallback` and `linux-juce-required` on push and pull request. The `macos-juce-required` job is optional and high-cost, so it is gated to manual `workflow_dispatch` or `main` branch runs. JUCE-required jobs build `DeckflaxiaRealVst3Fixture` before the JUCE-required CTest suite so the real VST3 tests and app smoke can validate the generated host path.

For a matching local checkout, run from the repository root:

```sh
mkdir -p third_party
git clone --branch 8.0.10 --depth 1 https://github.com/juce-framework/JUCE.git third_party/JUCE
test -f third_party/JUCE/CMakeLists.txt
```

Keep this tag aligned with the workflow `JUCE_REF`. This is a local licensed checkout for `add_subdirectory(third_party/JUCE)`, not source-controlled vendoring; `third_party/` is ignored and must stay out of commits.

## License Report

Run `cmake --build build --target license-report` after configure. The target writes `build/license-report.spdx.txt`, an SPDX-style inventory covering project source as `AGPL-3.0-or-later`, JUCE as `AGPL-3.0-only OR LicenseRef-JUCE-Commercial`, the VST3 SDK as `MIT`, Rubber Band as optional system `GPL-2.0-or-later`, Signalsmith as non-vendored `MIT` fallback boundary, SoundTouch as `LGPL-2.1-only`, the system SQLite C API target, and the current no-vendored-dependency posture.

The report is an engineering compliance aid, not legal advice. Any new dependency must be added to `cmake/LicenseReport.cmake` before integration and must remain compatible with the project AGPL-3.0-or-later posture.

## Required JUCE Configure

Use this command when validating the JUCE-required phase:

```sh
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

When using a local or CI checkout at `third_party/JUCE`, keep `DECKFLAXIA_USE_VENDORED_JUCE=ON`. The Linux and macOS JUCE-required CI jobs both verify `third_party/JUCE/CMakeLists.txt` before configure:

```sh
test -f third_party/JUCE/CMakeLists.txt
cmake -S . -B build-juce -DDECKFLAXIA_REQUIRE_JUCE=ON -DDECKFLAXIA_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

If the compiler is killed while building large JUCE module sources such as `juce_graphics.cpp` or `juce_gui_basics.cpp`, the machine is out of RAM. Rebuild with constrained parallelism:

```sh
cmake --build build-juce --parallel 1
```

Build the source-built, test-only real VST3 fixture the same way once the target is present:

```sh
cmake --build build-juce --target DeckflaxiaRealVst3Fixture --parallel 1
```

The target must generate `${CMAKE_BINARY_DIR}/generated/real-vst3-fixture/manifest.json`; in the default tree, that is `build-juce/generated/real-vst3-fixture/manifest.json`. The manifest may name a platform-specific `.vst3` bundle under the build tree. That bundle is generated output, not source. Do not commit it, vendor it, install it as app payload, or upload it as a CI artifact.

Use `--parallel 2` only after confirming the host has enough memory for multiple concurrent JUCE translation units.

CTest uses `Fixtures.Generate` as the setup step for deterministic DJ workflow fixtures. Tests that load `tests/fixtures/dj-workflow/track_120bpm.wav`, `track_128bpm.wav`, `silence_10s.wav`, or `corrupt_audio.wav` require that setup when run through CTest. If you invoke test binaries directly, run the generated fixture step yourself first:

```sh
./build-juce/FixtureTests generate tests/fixtures/dj-workflow
```

If JUCE is missing, configure fails with instructions to provide either `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build` or a licensed `third_party/JUCE` checkout. Use fallback mode only for infrastructure checks that intentionally do not exercise JUCE. Fallback does not prove native JUCE, native VST3, native editor windows, macOS runtime, screenshots, WAV output, Rubber Band DSP, or system SQLite persistence:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

After the real fixture manifest exists, run the real VST3 CTest gate or the direct app smoke command:

```sh
ctest --test-dir build-juce -R "VST3Processing\.(AppSmoke|RealFixture|RealProcessing|RealParameters|RealState)" --output-on-failure
```

Or run the direct app smoke command:

```sh
./build-juce/Deckflaxia --vst3-processing-smoke-test --chain deck-a --fixtures build-juce/generated/real-vst3-fixture --exit-after-init
```

Expected real-fixture output includes `backend=juce-vst3` and `real-vst3-instantiated=1`. Without the manifest, or in fallback/no-JUCE builds, this command path must not treat `deterministic_gain.fixture.json` as real VST3. Fallback/no-JUCE validation still does not prove native VST3, native editor windows, screenshots, or real JUCE plugin hosting.

## JUCE Module Headers in Library Targets

Plain `add_library` targets such as `DeckflaxiaPlugins`, `DeckflaxiaUiShell`, and `DeckflaxiaDecks` should link their required JUCE modules, then include direct JUCE module headers when they compile JUCE-aware code:

```cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
```

Do not include `<JuceHeader.h>` from plain library targets, and do not add `juce_generate_juce_header` to those targets just to make that umbrella header available. Prefer direct module headers for the JUCE app target too unless `juce_generate_juce_header` is intentionally configured. If direct module headers compile far enough to fail on `gtk/gtk.h`, first confirm `pkg-config --cflags gtk+-x11-3.0` reports GTK include paths, then make sure plain targets compiling or publicly propagating `juce_gui_extra` on Linux link `juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS` with matching visibility so JUCE's GTK/WebKit package flags propagate to downstream targets. Targets compiling or publicly propagating JUCE code with `JUCE_USE_CURL=1` also need `juce::pkgconfig_JUCE_CURL_LINUX_DEPS` so libcurl link flags propagate. Do not switch back to `JuceHeader.h` to mask missing target flags. VST3 hosting code also requires `JUCE_PLUGINHOST_VST3=1` on the target that compiles `juce::VST3PluginFormat`; `DeckflaxiaPlugins` sets this when `DECKFLAXIA_HAS_JUCE` is true.

## Static Analysis

Run `cmake --build build --target static-analysis` after configure. The target checks for `clang-format` and `clang-tidy` and documents missing prerequisites without failing bootstrap-only environments. Full linting should use `.clang-format`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and locally installed LLVM tooling.
