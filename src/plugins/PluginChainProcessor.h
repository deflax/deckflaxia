#pragma once

#include "core/DomainModels.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace deckflaxia::plugins {

enum class PluginProcessingBackendKind : std::uint8_t {
    Unavailable,
    DeterministicFallback,
    JuceVst3,
};

enum class PluginChainTargetKind : std::uint8_t {
    Deck,
    Master,
};

enum class PluginHostError : std::uint8_t {
    None,
    InvalidSlot,
    InvalidParameter,
    PluginUnavailable,
    HostUnavailable,
};

struct PluginHostResult final {
    PluginHostError error{PluginHostError::None};

    [[nodiscard]] constexpr bool ok() const noexcept { return error == PluginHostError::None; }
    [[nodiscard]] static constexpr PluginHostResult success() noexcept { return {}; }
    [[nodiscard]] static constexpr PluginHostResult failure(PluginHostError error) noexcept { return PluginHostResult{error}; }
};

struct PluginProcessingStatus final {
    PluginProcessingBackendKind backend{PluginProcessingBackendKind::Unavailable};
    bool juceAvailable{};
    bool realVst3Instantiated{};
    std::uint32_t latencyFrames{};
    std::size_t slotCount{};
    std::size_t activeSlotCount{};
    std::size_t unavailableSlotCount{};
    std::string unavailableReason;
};

struct PluginAudioMetrics final {
    double inputRms{};
    double outputRms{};
    float peakMagnitude{};
    bool changedAudio{};
};

struct PluginStateSnapshot final {
    std::vector<std::uint8_t> bytes;
};

enum class RealVst3FixtureManifestError : std::uint8_t {
    None,
    ManifestMissing,
    ManifestUnreadable,
    InvalidSchema,
    InvalidFixtureId,
    InvalidFormat,
    ExpectedNotTrue,
    MissingBundlePath,
    InvalidBundlePath,
    InvalidSource,
    InvalidLicenseNotice,
    InvalidGeneratedBy,
};

struct RealVst3FixtureManifest final {
    std::string fixtureId;
    std::filesystem::path bundlePath;
    std::filesystem::path binaryPathIfApplicable;
};

struct RealVst3FixtureManifestResult final {
    RealVst3FixtureManifest manifest;
    RealVst3FixtureManifestError error{RealVst3FixtureManifestError::None};
    std::string reason;

    [[nodiscard]] bool ok() const noexcept { return error == RealVst3FixtureManifestError::None; }
};

struct PluginEditorWindowStatus final {
    bool nativeEditorAvailable{};
    bool open{};
    bool genericParameterSurfaceAvailable{true};
    std::string statusText;
};

class OfflinePluginChainHost final {
public:
    OfflinePluginChainHost();
    ~OfflinePluginChainHost();
    OfflinePluginChainHost(OfflinePluginChainHost&&) noexcept;
    OfflinePluginChainHost& operator=(OfflinePluginChainHost&&) noexcept;
    OfflinePluginChainHost(const OfflinePluginChainHost&) = delete;
    OfflinePluginChainHost& operator=(const OfflinePluginChainHost&) = delete;

    [[nodiscard]] PluginHostResult configure(PluginChainTargetKind target,
                                             core::PluginChainDescriptor chain,
                                             double sampleRateHz,
                                             std::uint32_t maxBlockFrames);
    [[nodiscard]] PluginHostResult setSlotBypass(std::size_t slotIndex, bool bypassed) noexcept;
    [[nodiscard]] PluginHostResult setParameter(std::size_t slotIndex,
                                                const std::string& parameterId,
                                                double normalizedValue) noexcept;
    [[nodiscard]] double parameter(std::size_t slotIndex, const std::string& parameterId) const noexcept;
    [[nodiscard]] const core::PluginChainDescriptor& chainState() const noexcept;
    [[nodiscard]] PluginHostResult snapshotState(std::size_t slotIndex, PluginStateSnapshot& snapshot) const;
    [[nodiscard]] PluginHostResult restoreState(std::size_t slotIndex, const PluginStateSnapshot& snapshot);
    [[nodiscard]] PluginProcessingStatus status() const noexcept;
    [[nodiscard]] PluginEditorWindowStatus openSeparateEditorWindow(std::size_t slotIndex) const;
    [[nodiscard]] PluginEditorWindowStatus closeSeparateEditorWindow(std::size_t slotIndex) const;
    [[nodiscard]] PluginAudioMetrics processReplacing(float* interleavedStereo,
                                                      std::uint32_t frameCount,
                                                      bool forceBypass) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool isDeterministicTestPluginId(const std::string& pluginId) noexcept;
[[nodiscard]] core::PluginDescriptor makeDeterministicGainPlugin(double normalizedGain = 0.5, bool bypassed = false);
[[nodiscard]] core::PluginDescriptor makeRealVst3FixturePlugin(const RealVst3FixtureManifest& manifest, bool bypassed = false);
[[nodiscard]] RealVst3FixtureManifestResult loadRealVst3FixtureManifest(const std::filesystem::path& manifestPath);
[[nodiscard]] const char* toString(PluginProcessingBackendKind backend) noexcept;
[[nodiscard]] const char* toString(PluginHostError error) noexcept;
[[nodiscard]] const char* toString(RealVst3FixtureManifestError error) noexcept;

}
