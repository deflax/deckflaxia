# DJApp

DJApp is an AGPL-3.0-or-later native JUCE DJ application workflow. The current target is a four-deck app for macOS 15 via `macos-latest` and Ubuntu 24.04 LTS via `ubuntu-24.04`, with local fallback checks for environments that don't have JUCE or system media dependencies installed.

Start with the runbook for exact setup, build, launch, smoke, sandbox, license, troubleshooting, and deferred-scope commands:

```sh
cmake -S . -B build-juce -DDJAPP_REQUIRE_JUCE=ON
```

Full guide: [docs/user-runbook-developer-operations.md](docs/user-runbook-developer-operations.md)

Related references:

- [Architecture boundaries](docs/architecture/alpha-architecture.md)
- [CI, license, and static analysis hardening](docs/compliance/ci-license-static-analysis.md)
- [CMake integration notes](cmake/README.md)

## Local Fallback Check

This container path is fallback-only when JUCE, Rubber Band, native VST3/editor surfaces, system SQLite, fallback WAV encoding, and clangd are missing. It doesn't prove native JUCE success.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/DJApp --production-dj-workflow-smoke-test --fixtures tests/fixtures/dj-workflow --exit-after-init
```

## Deferred Scope

These features are intentionally out of scope for this workflow: Windows, recording, smart playlists, samplers, streaming, DVS/timecode, Rekordbox/Serato import, cloud/accounts/marketplace, per-plugin sandboxing, and embedded plugin editors.
