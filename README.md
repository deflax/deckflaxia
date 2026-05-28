# Deckflaxia

Deckflaxia is an AGPL-3.0-or-later native JUCE DJ application workflow. The current target is a four-deck app for macOS 15 via `macos-latest` and Ubuntu 24.04 LTS via `ubuntu-24.04`, with local fallback checks for environments that don't have JUCE or system media dependencies installed.

Start with the runbook for exact setup, build, launch, smoke, sandbox, license, troubleshooting, and deferred-scope commands:

- [User runbook and developer operations](docs/user-runbook-developer-operations.md)

For a local JUCE checkout, see the runbook's `third_party/JUCE` setup commands; JUCE is not vendored in this repository and the local checkout must not be committed.

Related references:

- [Architecture boundaries](docs/architecture/alpha-architecture.md)
- [CI, license, and static analysis hardening](docs/compliance/ci-license-static-analysis.md)
- [CMake integration notes](cmake/README.md)

## Deferred Scope

These features are intentionally out of scope for this workflow: Windows, recording, smart playlists, samplers, streaming, DVS/timecode, Rekordbox/Serato import, cloud/accounts/marketplace, per-plugin sandboxing, and embedded plugin editors.
