#pragma once

#include "library/AudioImport.h"
#include "persistence/Persistence.h"

#include <string>
#include <vector>
#include <iosfwd>

namespace deckflaxia::ui {

struct BrowserTableRowModel final {
    std::string trackId;
    std::string title;
    std::string format;
    bool importable{};
    library::AudioImportError importError{library::AudioImportError::None};
};

struct BeatgridEditorModel final {
    std::string trackId;
    core::BeatgridMetadata beatgrid{};
    std::vector<persistence::CueMarkerRecord> cueMarkers;
};

class BrowserWaveformBeatgridModel final {
public:
    [[nodiscard]] std::vector<BrowserTableRowModel> buildBrowserRows(const std::vector<library::AudioImportClassification>& imports) const;
    [[nodiscard]] persistence::PersistenceUnitResult saveBeatgridAndCues(persistence::TrackMetadataRepository metadata,
                                                                         const BeatgridEditorModel& model) const;
    [[nodiscard]] persistence::PersistenceResult<BeatgridEditorModel> loadBeatgridAndCues(persistence::TrackMetadataRepository metadata,
                                                                                          const std::string& trackId) const;
};

[[nodiscard]] int runBrowserWaveformSmokeTest(std::ostream& output, const std::vector<library::FilesystemEntry>& entries);
[[nodiscard]] int runBeatgridEditSmokeTest(std::ostream& output);

}
