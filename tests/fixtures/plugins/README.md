# Plugin Fixture Contracts

This directory currently contains deterministic fallback plugin fixtures. Task 1 of the real VST3 fixture validation work defines the contract below for downstream tasks; it does not implement the VST3 fixture target or commit any generated `.vst3` output.

## Deterministic Fallback Fixture

`deterministic_gain.fixture.json` describes the fallback-only plugin `deterministic:gain`. It is not a VST3 binary, is not loaded through a JUCE VST3 plugin instance, and must never be treated as real VST3 success.

Fallback and no-JUCE tests may keep using `deterministic_gain.fixture.json` to cover deterministic processing, bypass, parameter state, and persistence. Those tests do not require a generated real VST3 fixture, and they must never emit `real-vst3-instantiated=1` from `deterministic:gain` or any deterministic JSON fixture.

## Real VST3 Fixture Manifest

The real fixture is a source-built, test-only VST3 gain effect generated into the build tree by a future target. The generated manifest path is fixed as:

```text
${CMAKE_BINARY_DIR}/generated/real-vst3-fixture/manifest.json
```

For the default JUCE build tree, downstream tests and smoke commands should look for:

```text
build-juce/generated/real-vst3-fixture/manifest.json
```

The manifest is generated build output and must not be committed. Generated `.vst3` bundles also must not be committed, installed, packaged, or uploaded as CI artifacts.

The manifest schema is:

```json
{
  "schema": "deckflaxia-real-vst3-fixture-v1",
  "fixture_id": "real-vst3-fixture:deterministic_gain",
  "format": "vst3",
  "expected": true,
  "bundle_path": "<absolute-or-build-tree-relative path to the generated .vst3 bundle>",
  "binary_path_if_applicable": "<platform-specific loadable binary inside the bundle, or null when not applicable>",
  "source": "build-tree",
  "license_notice": "source-built test-only fixture; no proprietary/commercial binaries; VST3 SDK/sample code notices reviewed before copying any third-party implementation",
  "generated_by": "DeckflaxiaRealVst3Fixture"
}
```

Field contract:

- `schema` must be exactly `deckflaxia-real-vst3-fixture-v1` until a migration is intentionally introduced.
- `fixture_id` identifies this source-built real fixture and must not be `deterministic:gain`.
- `format` must be `vst3`.
- `expected` must be `true` in JUCE-required jobs/tests that require the real fixture.
- `bundle_path` names the platform-correct generated `.vst3` bundle path in the build tree.
- `binary_path_if_applicable` names the platform-specific loadable binary inside the bundle when downstream code needs it; otherwise it is `null`.
- `source` must be `build-tree`.
- `license_notice` records that the fixture is source-built for tests and that any third-party VST3 SDK or sample implementation material has been license-reviewed and attributed before use.
- `generated_by` names the build target or helper that produced the manifest.

## Success And Failure Policy

Real VST3 success means Deckflaxia's production host/chain path instantiates the generated fixture and reports both of these machine-checkable markers in the relevant test or smoke output:

```text
backend=juce-vst3
real-vst3-instantiated=1
```

The real fixture must be represented as a VST3 descriptor or `.vst3` bundle from the generated manifest, not as `deterministic:gain`, and it must exercise the same production host path surfaced by `PluginProcessingStatus::backend` and `PluginProcessingStatus::realVst3Instantiated`.

JUCE-required CI/tests must fail when the real fixture is expected and any of these conditions occur:

- The generated manifest at `${CMAKE_BINARY_DIR}/generated/real-vst3-fixture/manifest.json` is missing or invalid.
- `bundle_path` is missing, outside the build tree, not a `.vst3` bundle, or unloadable by the production host.
- The plugin is bypassed, no-op, or otherwise fails the required deterministic audio-change assertion.
- Parameter control or state serialization/restoration fails to roundtrip the gain behavior.
- The host falls back to `deterministic:gain`, reports a non-VST3 fixture, or omits `real-vst3-instantiated=1` when the real fixture is required.

Fallback and no-JUCE builds may skip real-fixture-only checks or run deterministic fallback coverage only. They should report honest unavailable or fallback status, such as `real-vst3-instantiated=0`, rather than converting deterministic fallback success into real VST3 success.

## Licensing Guardrails

The fixture must be built from source in the test build and used only as test infrastructure. Do not commit generated `.vst3` bundles, proprietary plugins, commercial plugins, downloaded plugin binaries, or generated binary artifacts.

Do not copy Steinberg sample implementation files unless every copied file has been license-reviewed, the applicable license permits the repository use, and attribution/notices are added in the source and manifest. Prefer a minimal source-built fixture through existing JUCE/VST3 support, with `license_notice` documenting the reviewed source basis.

## Evidence Baseline

Current JUCE evidence is fallback-honest and not real VST3 success:

- `.omo/evidence/real-playable-juce/task-10-deck-vst3.log` reports `backend=juce-vst3 juce=1 real-vst3-instantiated=0` with unavailable reason `no VST3 plugin instantiated`.
- `.omo/evidence/real-playable-juce/task-13-playable.log` reports deck and master `backend=juce-vst3 real-vst3-instantiated=0` while using the deterministic fixture path.

Downstream tasks should preserve that honesty until the generated real fixture exists and the production host path emits `backend=juce-vst3` plus `real-vst3-instantiated=1` from the real manifest-backed VST3 fixture only.
