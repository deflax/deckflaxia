#include "analysis/WaveformCache.h"
#include "library/AudioImport.h"
#include "persistence/Persistence.h"
#include "ui/BrowserWaveformBeatgrid.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return 1;
    }
    return 0;
}

std::vector<deckflaxia::library::FilesystemEntry> fixtureEntries(const std::filesystem::path& fixtureDir) {
    return {{(fixtureDir / "track_120bpm.wav").string(), true},
            {(fixtureDir / "track_128bpm.wav").string(), true},
            {(fixtureDir / "track_95bpm.mp3").string(), true},
            {(fixtureDir / "corrupt_audio.wav").string(), true},
            {(fixtureDir / "not_audio.txt").string(), true},
            {(fixtureDir / "folder").string(), false}};
}

int testImportClassification(const std::filesystem::path& fixtureDir) {
    const auto imports = deckflaxia::library::classifyAudioImports(fixtureEntries(fixtureDir));
    if (expect(imports.size() == 6U, "fixture import classification should cover all entries") != 0) {
        return 1;
    }
    if (expect(imports[0].importable() && imports[0].format == "wav", "WAV fixture should be importable") != 0) {
        return 1;
    }
    if (expect(imports[3].error == deckflaxia::library::AudioImportError::CorruptAudio, "corrupt WAV should return typed corrupt error") != 0) {
        return 1;
    }
    if (expect(imports[4].error == deckflaxia::library::AudioImportError::UnsupportedFormat, "text fixture should return typed unsupported error") != 0) {
        return 1;
    }
    if (expect(imports[5].error == deckflaxia::library::AudioImportError::NotRegularFile, "folder entry should return typed regular-file error") != 0) {
        return 1;
    }
#if DECKFLAXIA_HAS_JUCE
    if (expect(imports[2].format == "mp3", "JUCE MP3 entry should reach platform codec path") != 0) {
        return 1;
    }
#else
    if (expect(imports[2].error == deckflaxia::library::AudioImportError::ExternalToolRequired, "fallback MP3 should remain external-tool-required") != 0) {
        return 1;
    }
#endif
    const deckflaxia::ui::BrowserWaveformBeatgridModel model;
    const auto rows = model.buildBrowserRows(imports);
    if (expect(rows.size() == imports.size() && rows[0].trackId.find("track:") == 0U, "browser table rows should expose deterministic track ids") != 0) {
        return 1;
    }
    std::cout << "BrowserWaveformBeatgrid.ImportClassification wav=importable mp3=" << deckflaxia::library::toString(imports[2].error)
              << " corrupt=" << deckflaxia::library::toString(imports[3].error) << " text=" << deckflaxia::library::toString(imports[4].error) << '\n';
    return 0;
}

int testWaveformPrimitive(const std::filesystem::path& fixtureDir) {
    const auto imports = deckflaxia::library::classifyAudioImports(fixtureEntries(fixtureDir));
    const deckflaxia::analysis::WaveformCacheModel cache;
    const auto wav = cache.buildPrimitiveMetadata("track:wav", imports[0]);
    const auto corrupt = cache.buildPrimitiveMetadata("track:corrupt", imports[3]);
    if (expect(wav.summaryPointCount == 128U && std::abs(wav.durationSeconds - 2.0) < 0.000001, "WAV waveform primitive metadata should be deterministic") != 0) {
        return 1;
    }
    if (expect(corrupt.summaryPointCount == 0U && corrupt.importError == deckflaxia::library::AudioImportError::CorruptAudio, "corrupt waveform should not fake primitives") != 0) {
        return 1;
    }
    std::cout << "BrowserWaveformBeatgrid.WaveformPrimitive points=" << wav.summaryPointCount << " juce-thumbnail=" << (wav.juceThumbnailAvailable ? "available" : "unavailable") << '\n';
    return 0;
}

int testBeatgridPersistence(const std::filesystem::path&) {
    using namespace deckflaxia::core;
    using namespace deckflaxia::persistence;
    using namespace deckflaxia::ui;

    PersistenceService service;
    const auto track = LibraryTrack{"track:beatgrid-edit", "Beatgrid Edit", "Fixture", BeatgridMetadata::fromBpm(120.0, 0.0).value, MusicalKey::Unknown};
    if (expect(service.libraryTracks().upsert(track).ok(), "track seed should persist") != 0) {
        return 1;
    }
    const BrowserWaveformBeatgridModel model;
    const BeatgridEditorModel edited{"track:beatgrid-edit",
                                     BeatgridMetadata::fromBpm(127.75, 0.3125).value,
                                     {CueMarkerRecord{"cue:hot-1", 4.0, "Load"}, CueMarkerRecord{"cue:hot-2", 16.5, "Drop"}}};
    if (expect(model.saveBeatgridAndCues(service.trackMetadata(), edited).ok(), "beatgrid and cue edit should save through metadata repository") != 0) {
        return 1;
    }
    const BrowserWaveformBeatgridModel restartedModel;
    const auto reloaded = restartedModel.loadBeatgridAndCues(service.trackMetadata(), "track:beatgrid-edit");
    if (expect(reloaded.ok(), "beatgrid and cue edit should reload after model restart") != 0) {
        return 1;
    }
    if (expect(std::abs(reloaded.value.beatgrid.bpm - 127.75) < 0.000001 && std::abs(reloaded.value.beatgrid.firstBeatSeconds - 0.3125) < 0.000001,
               "beatgrid BPM/downbeat should reload exact values") != 0) {
        return 1;
    }
    if (expect(reloaded.value.cueMarkers.size() == 2U && reloaded.value.cueMarkers[1].label == "Drop", "cue markers should reload exact edit model") != 0) {
        return 1;
    }
    std::cout << "BrowserWaveformBeatgrid.BeatgridPersistence bpm=" << reloaded.value.beatgrid.bpm
              << " downbeat=" << reloaded.value.beatgrid.firstBeatSeconds << " cues=" << reloaded.value.cueMarkers.size() << '\n';
    return 0;
}

}

int main(int argc, char* argv[]) {
    const std::string filter = argc > 1 ? argv[1] : "all";
    const std::filesystem::path fixtureDir = argc > 2 ? argv[2] : "tests/fixtures/dj-workflow";
    if (filter == "import") {
        return testImportClassification(fixtureDir);
    }
    if (filter == "waveform") {
        return testWaveformPrimitive(fixtureDir);
    }
    if (filter == "beatgrid") {
        return testBeatgridPersistence(fixtureDir);
    }
    if (filter != "all") {
        std::cerr << "FAILED: unknown BrowserWaveformBeatgrid filter " << filter << '\n';
        return 1;
    }
    if (testImportClassification(fixtureDir) != 0 || testWaveformPrimitive(fixtureDir) != 0 || testBeatgridPersistence(fixtureDir) != 0) {
        return 1;
    }
    std::cout << "Browser waveform beatgrid tests passed\n";
    return 0;
}
