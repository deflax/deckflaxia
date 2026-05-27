# CMake Integration

The real playable JUCE workflow uses `DJAPP_REQUIRE_JUCE=ON` for presubmit and release-style configuration. The integration first accepts a licensed local checkout at `third_party/JUCE` when `DJAPP_USE_VENDORED_JUCE=ON`, then falls back to `find_package(JUCE CONFIG)` so installed/exported JUCE packages can be supplied with `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build`.

This repository does not vendor JUCE, the VST3 SDK, Rubber Band, Signalsmith, SoundTouch, SQLite wrappers, or any other third-party dependency. CMake may link an already-installed system Rubber Band library as the guarded primary time-stretch boundary; otherwise it builds the honest Signalsmith-compatible fallback boundary without claiming production stretch quality. When `DJAPP_REQUIRE_JUCE=OFF` and JUCE is unavailable, CMake builds an explicit bootstrap-only fallback executable so early CI and smoke tests can validate repository infrastructure without claiming JUCE functionality.

GitHub Actions may check out `juce-framework/JUCE` into `third_party/JUCE` at the pinned workflow `JUCE_REF` immediately before configure. That checkout is CI-only and relies on JUCE AGPL/commercial licensing terms; it must not be committed or treated as a vendored dependency.

## License Report

Run `cmake --build build --target license-report` after configure. The target writes `build/license-report.spdx.txt`, an SPDX-style inventory covering project source as `AGPL-3.0-or-later`, JUCE as `AGPL-3.0-only OR LicenseRef-JUCE-Commercial`, the VST3 SDK as `MIT`, Rubber Band as optional system `GPL-2.0-or-later`, Signalsmith as non-vendored `MIT` fallback boundary, SoundTouch as `LGPL-2.1-only`, the system SQLite C API target, and the current no-vendored-dependency posture.

The report is an engineering compliance aid, not legal advice. Any new dependency must be added to `cmake/LicenseReport.cmake` before integration and must remain compatible with the project AGPL-3.0-or-later posture.

## Required JUCE Configure

Use this command when validating the JUCE-required phase:

```sh
cmake -S . -B build-juce -DDJAPP_REQUIRE_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

When using a local or CI checkout at `third_party/JUCE`, keep `DJAPP_USE_VENDORED_JUCE=ON`:

```sh
cmake -S . -B build-juce -DDJAPP_REQUIRE_JUCE=ON -DDJAPP_USE_VENDORED_JUCE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

If JUCE is missing, configure fails with instructions to provide either `-DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build` or a licensed `third_party/JUCE` checkout. Use fallback mode only for early infrastructure checks that intentionally do not exercise JUCE:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Static Analysis

Run `cmake --build build --target static-analysis` after configure. The target checks for `clang-format` and `clang-tidy` and documents missing prerequisites without failing bootstrap-only environments. Full linting should use `.clang-format`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and locally installed LLVM tooling.
