# CI, License, and Static Analysis Hardening

## Local CI Parity

Run these commands from the repository root to match the Linux CI flow:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
cmake --build build --target static-analysis
ctest --test-dir build --output-on-failure
cmake --build build --target license-report
```

The license inventory is written to `build/license-report.spdx.txt`. Task evidence should copy that report to `.omo/evidence/task-13-license-report.txt` and capture command output in `.omo/evidence/task-13-local-ci.log`.

## macOS Evidence Requirement

The GitHub Actions macOS job configures, builds, runs the smoke test, and runs CTest on `macos-latest`. If a local macOS agent is used instead, run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
./build/DJApp --smoke-test --exit-after-init
ctest --test-dir build --output-on-failure
```

Save the macOS output to `.omo/evidence/task-13-macos-build-test.log` before alpha completion. A Linux-hosted placeholder may document these instructions but must not claim macOS execution.

## Static Analysis Prerequisites

`cmake --build build --target static-analysis` checks whether `clang-format` and `clang-tidy` are installed. This target intentionally stays non-failing when those tools are absent so bootstrap CI remains green in constrained containers. Full static analysis requires LLVM tooling, `.clang-format`, and a configure step with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## License Inventory Policy

`cmake --build build --target license-report` generates an SPDX-style inventory with:

- DJApp project source license status.
- JUCE framework status as `LicenseRef-JUCE-Commercial OR GPL-3.0-or-later`.
- System SQLite C API target as the commercial-compatible persistence dependency.
- No-vendored-third-party posture, except an optional local licensed `third_party/JUCE` checkout.

A documented JUCE commercial license or GPL-compatible release decision is a release gate before any distribution. The current alpha repository does not add telemetry, crash upload, accounts, cloud sync, marketplace, or remote logging infrastructure; local logs are for developer diagnostics only and are not uploaded.
