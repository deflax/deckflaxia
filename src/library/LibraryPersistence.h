#pragma once

#include "persistence/Persistence.h"

namespace deckflaxia::library {

using LibraryTracksRepository = persistence::LibraryTracksRepository;
using CratesRepository = persistence::CratesRepository;
using PlaylistsRepository = persistence::PlaylistsRepository;
using TrackMetadataRepository = persistence::TrackMetadataRepository;
using AnalysisJobsRepository = persistence::AnalysisJobsRepository;

}
