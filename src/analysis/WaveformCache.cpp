#include "analysis/WaveformCache.h"

namespace djapp::analysis {

WaveformPrimitiveMetadata WaveformCacheModel::buildPrimitiveMetadata(const std::string& trackId,
                                                                     const library::AudioImportClassification& classification) const noexcept {
    WaveformPrimitiveMetadata metadata;
    metadata.trackId = trackId;
    metadata.importError = classification.error;
    if (!classification.importable()) {
        return metadata;
    }
#if DJAPP_HAS_JUCE
    metadata.juceThumbnailAvailable = true;
#endif
    metadata.durationSeconds = classification.format == "wav" ? 2.0 : 0.0;
    metadata.summaryPointCount = classification.format == "wav" ? 128U : 64U;
    return metadata;
}

}
