# CMake Integration

The default production integration expects an installed JUCE CMake package and uses `find_package(JUCE CONFIG)`. A licensed local checkout can also be placed at `third_party/JUCE`, where this project will use JUCE's documented `add_subdirectory` flow.

This repository does not vendor JUCE or any other unknown third-party dependency. When JUCE is unavailable, CMake builds an explicit bootstrap-only fallback executable so CI and smoke tests can validate repository infrastructure without claiming JUCE functionality.

## License Report

Run `cmake --build build --target license-report` after configure. The target writes `build/license-report.spdx.txt`, an SPDX-style inventory covering project code, the JUCE commercial/GPL release gate, the system SQLite C API target, and the current no-vendored-dependency posture.

## Static Analysis

Run `cmake --build build --target static-analysis` after configure. The target checks for `clang-format` and `clang-tidy` and documents missing prerequisites without failing bootstrap-only environments. Full linting should use `.clang-format`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and locally installed LLVM tooling.
