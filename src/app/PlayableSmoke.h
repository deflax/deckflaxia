#pragma once

#include <filesystem>
#include <iosfwd>
#include <cstdint>
#include <string>
#include <vector>

namespace deckflaxia::app {

struct PlayableSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/dj-workflow"};
    std::filesystem::path screenshotPath{};
    std::filesystem::path renderPath{};
    std::filesystem::path databasePath{};
    std::filesystem::path sandboxHelperPath{};
    bool expectRestoredSession{};
    bool writeEvidenceLog{true};
};

struct PerformanceSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/dj-workflow"};
    std::filesystem::path evidencePath{".omo/evidence/real-playable-juce/task-15-performance.json"};
    std::uint32_t sampleRateHz{48000};
    std::uint32_t bufferFrames{512};
};

struct ProductionDjWorkflowSmokeOptions final {
    std::filesystem::path fixtureDirectory{"tests/fixtures/dj-workflow"};
    std::filesystem::path evidencePath{".omo/evidence/real-playable-juce/task-16-production-smoke.log"};
    std::filesystem::path databasePath{".omo/evidence/real-playable-juce/task-16-restart.db"};
    std::uint32_t sampleRateHz{48000};
    std::uint32_t bufferFrames{512};
};

struct ScopeAuditOptions final {
    std::vector<std::string> forbiddenTerms;
    std::vector<std::filesystem::path> paths{std::filesystem::path{"src"}, std::filesystem::path{"tests"}, std::filesystem::path{"docs"}, std::filesystem::path{"cmake"}};
    std::filesystem::path evidencePath{".omo/evidence/real-playable-juce/task-16-scope-audit.log"};
};

[[nodiscard]] int runPlayableSmokeTest(std::ostream& output, const PlayableSmokeOptions& options);
[[nodiscard]] int runPerformanceSmokeTest(std::ostream& output, const PerformanceSmokeOptions& options);
[[nodiscard]] int runProductionDjWorkflowSmokeTest(std::ostream& output, const ProductionDjWorkflowSmokeOptions& options);
[[nodiscard]] int runScopeAudit(std::ostream& output, const ScopeAuditOptions& options);
[[nodiscard]] std::vector<std::string> splitScopeAuditTerms(const std::string& csv);

}
