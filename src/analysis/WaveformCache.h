#pragma once

#include "library/AudioImport.h"

#include <cstddef>
#include <string>
#include <vector>

namespace djapp::analysis {

struct WaveformPrimitiveMetadata final {
    std::string trackId;
    std::size_t summaryPointCount{};
    double durationSeconds{};
    bool juceThumbnailAvailable{};
    library::AudioImportError importError{library::AudioImportError::None};
};

class WaveformCacheModel final {
public:
    [[nodiscard]] WaveformPrimitiveMetadata buildPrimitiveMetadata(const std::string& trackId,
                                                                  const library::AudioImportClassification& classification) const noexcept;
};

}
