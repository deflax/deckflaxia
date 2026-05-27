#include "app/PlayableSmoke.h"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

int runWorkflow(const std::filesystem::path& fixtures) {
    std::ostringstream output;
    const auto result = deckflaxia::app::runPlayableSmokeTest(output, deckflaxia::app::PlayableSmokeOptions{fixtures, {}, {}, {}, {}, false});
    const auto text = output.str();
    if (expect(result == 0, "playable workflow should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("four-decks-loaded=4") != std::string::npos && text.find("four-decks-playing=4") != std::string::npos, "workflow should load and play four decks") != 0) {
        return 1;
    }
    if (expect(text.find("deck-vst3-active=1") != std::string::npos && text.find("master-vst3-active=1") != std::string::npos, "workflow should activate deck and master plugin chains") != 0) {
        return 1;
    }
    if (expect(text.find("midi-command-dispatched=1") != std::string::npos && text.find("sandbox-status-active=1") != std::string::npos, "workflow should dispatch MIDI and activate sandbox status") != 0) {
        return 1;
    }
    std::cout << "PlayableSmoke.Workflow ok\n";
    return 0;
}

int runRestart(const std::filesystem::path& fixtures, const std::filesystem::path& dbPath) {
    std::error_code removeError;
    std::filesystem::remove(dbPath, removeError);

    std::ostringstream firstOutput;
    const auto first = deckflaxia::app::runPlayableSmokeTest(firstOutput, deckflaxia::app::PlayableSmokeOptions{fixtures, {}, {}, dbPath, {}, false});
    if (expect(first == 0, "first restart smoke pass should persist state") != 0) {
        std::cerr << firstOutput.str();
        return 1;
    }

    std::ostringstream secondOutput;
    const auto second = deckflaxia::app::runPlayableSmokeTest(secondOutput, deckflaxia::app::PlayableSmokeOptions{fixtures, {}, {}, dbPath, {}, true});
    const auto text = secondOutput.str();
    if (expect(second == 0, "second restart smoke pass should restore state") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("restart-restored=1") != std::string::npos, "restart report should mark restored session") != 0) {
        return 1;
    }
    std::cout << "PlayableSmoke.Restart ok db=" << dbPath << '\n';
    return 0;
}

int runProduction(const std::filesystem::path& fixtures, const std::filesystem::path& dbPath) {
    std::ostringstream output;
    const auto result = deckflaxia::app::runProductionDjWorkflowSmokeTest(output, deckflaxia::app::ProductionDjWorkflowSmokeOptions{fixtures, ".omo/evidence/real-playable-juce/task-16-production-smoke.log", dbPath, 48000, 512});
    const auto text = output.str();
    if (expect(result == 0, "production workflow smoke should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("system=four-decks status=PASS") != std::string::npos &&
                   text.find("system=performance budgets status=PASS") != std::string::npos &&
                   text.find("system=edge/fault hardening status=PASS") != std::string::npos,
               "production workflow should consolidate required PASS systems") != 0) {
        std::cerr << text;
        return 1;
    }
    std::cout << "PlayableSmoke.ProductionWorkflow ok\n";
    return 0;
}

int runScopeAudit() {
    std::ostringstream output;
    const auto forbidden = deckflaxia::app::splitScopeAuditTerms("Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor");
    const auto result = deckflaxia::app::runScopeAudit(output, deckflaxia::app::ScopeAuditOptions{forbidden, {"src", "tests", "docs", "cmake"}, ".omo/evidence/real-playable-juce/task-16-scope-audit.log"});
    const auto text = output.str();
    if (expect(result == 0, "scope audit should pass") != 0) {
        std::cerr << text;
        return 1;
    }
    if (expect(text.find("violations=0") != std::string::npos && text.find("scope-audit: PASS") != std::string::npos,
               "scope audit should report zero forbidden implementation violations") != 0) {
        std::cerr << text;
        return 1;
    }
    std::cout << "PlayableSmoke.ScopeAudit ok\n";
    return 0;
}

} 

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const std::filesystem::path fixtures = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures/dj-workflow");
    const std::filesystem::path dbPath = argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path(".omo/evidence/real-playable-juce/task-13-restart.db");

    if (filter == "all") {
        return runWorkflow(fixtures) + runRestart(fixtures, dbPath);
    }
    if (filter == "workflow") {
        return runWorkflow(fixtures);
    }
    if (filter == "restart") {
        return runRestart(fixtures, dbPath);
    }
    if (filter == "production") {
        return runProduction(fixtures, dbPath);
    }
    if (filter == "scope-audit") {
        return runScopeAudit();
    }
    std::cerr << "FAILED: unknown PlayableSmoke filter " << filter << '\n';
    return 1;
}
