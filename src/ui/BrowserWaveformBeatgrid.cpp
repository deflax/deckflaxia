#include "ui/BrowserWaveformBeatgrid.h"

#include "analysis/WaveformCache.h"

#include <ostream>

namespace djapp::ui {

std::vector<BrowserTableRowModel> BrowserWaveformBeatgridModel::buildBrowserRows(const std::vector<library::AudioImportClassification>& imports) const {
    std::vector<BrowserTableRowModel> rows;
    rows.reserve(imports.size());
    for (const auto& import : imports) {
        rows.push_back(BrowserTableRowModel{library::trackIdFromPath(import.entry.path), library::titleFromPath(import.entry.path), import.format, import.importable(), import.error});
    }
    return rows;
}

persistence::PersistenceUnitResult BrowserWaveformBeatgridModel::saveBeatgridAndCues(persistence::TrackMetadataRepository metadata,
                                                                                    const BeatgridEditorModel& model) const {
    const auto savedMetadata = metadata.save(persistence::TrackMetadataRecord{model.trackId, model.beatgrid, core::MusicalKey::Unknown});
    if (!savedMetadata.ok()) {
        return savedMetadata;
    }
    return metadata.saveCueMarkers(model.trackId, model.cueMarkers);
}

persistence::PersistenceResult<BeatgridEditorModel> BrowserWaveformBeatgridModel::loadBeatgridAndCues(persistence::TrackMetadataRepository metadata,
                                                                                                      const std::string& trackId) const {
    const auto trackMetadata = metadata.load(trackId);
    if (!trackMetadata.ok()) {
        return persistence::PersistenceResult<BeatgridEditorModel>::failure(trackMetadata.error);
    }
    const auto cues = metadata.loadCueMarkers(trackId);
    if (!cues.ok()) {
        return persistence::PersistenceResult<BeatgridEditorModel>::failure(cues.error);
    }
    return persistence::PersistenceResult<BeatgridEditorModel>::success(BeatgridEditorModel{trackId, trackMetadata.value.beatgrid, cues.value});
}

int runBrowserWaveformSmokeTest(std::ostream& output, const std::vector<library::FilesystemEntry>& entries) {
    const BrowserWaveformBeatgridModel browserModel;
    const analysis::WaveformCacheModel waveformCache;
    const auto imports = library::classifyAudioImports(entries);
    const auto rows = browserModel.buildBrowserRows(imports);
    output << "browser-waveform-smoke-test: ok\n";
    output << "browser-table-rows: " << rows.size() << '\n';
    for (const auto& row : rows) {
        output << "import: " << row.title << " format=" << row.format << " error=" << library::toString(row.importError) << '\n';
    }
    if (!imports.empty()) {
        const auto waveform = waveformCache.buildPrimitiveMetadata(rows.front().trackId, imports.front());
        output << "waveform-cache-seam: summary-points=" << waveform.summaryPointCount
               << " juce-thumbnail=" << (waveform.juceThumbnailAvailable ? "available" : "unavailable") << '\n';
    }
    return rows.empty() ? 1 : 0;
}

int runBeatgridEditSmokeTest(std::ostream& output) {
    persistence::PersistenceService service;
    const BrowserWaveformBeatgridModel model;
    const auto beatgrid = core::BeatgridMetadata::fromBpm(127.75, 0.3125).value;
    const BeatgridEditorModel edited{"track:beatgrid-smoke", beatgrid, {persistence::CueMarkerRecord{"cue:1", 1.0, "Load"}}};
    if (!model.saveBeatgridAndCues(service.trackMetadata(), edited).ok()) {
        output << "beatgrid-edit-smoke-test: fail\n";
        return 1;
    }
    const auto reloaded = model.loadBeatgridAndCues(service.trackMetadata(), edited.trackId);
    if (!reloaded.ok()) {
        output << "beatgrid-edit-smoke-test: fail\n";
        return 1;
    }
    output << "beatgrid-edit-smoke-test: ok\n";
    output << "beatgrid: bpm=" << reloaded.value.beatgrid.bpm << " downbeat=" << reloaded.value.beatgrid.firstBeatSeconds << '\n';
    output << "cue-markers: " << reloaded.value.cueMarkers.size() << '\n';
    return 0;
}

}
