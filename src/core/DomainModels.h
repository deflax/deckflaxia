#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deckflaxia::core {

enum class DomainError {
    None,
    InvalidDeckId,
    InvalidRouting,
    InvalidBpm,
    InvalidSeconds,
    InvalidProgress,
    InvalidIdentifier,
    InvalidTempoPitchSettings,
};

template <typename T>
struct DomainResult {
    T value{};
    DomainError error{DomainError::None};

    bool ok() const { return error == DomainError::None; }

    static DomainResult success(T resultValue) { return DomainResult{resultValue, DomainError::None}; }
    static DomainResult failure(DomainError resultError) { return DomainResult{T{}, resultError}; }
};

struct UnitResult {
    DomainError error{DomainError::None};

    bool ok() const { return error == DomainError::None; }
    static UnitResult success() { return UnitResult{DomainError::None}; }
    static UnitResult failure(DomainError resultError) { return UnitResult{resultError}; }
};

class DeckId {
public:
    DeckId() = default;

    static DomainResult<DeckId> fromIndex(std::size_t deckIndex);

    std::size_t index() const { return index_; }

    friend bool operator==(DeckId left, DeckId right) { return left.index_ == right.index_; }
    friend bool operator!=(DeckId left, DeckId right) { return !(left == right); }

private:
    explicit DeckId(std::size_t deckIndex) : index_(deckIndex) {}

    std::size_t index_{};
};

std::array<DeckId, 4> allDeckIds();

enum class DeckType {
    AudioFile,
    MidiStepSequencer,
};

enum class OutputBus {
    Master,
    Cue,
    Output1,
    Output2,
    Output3,
    Output4,
};

struct RoutingAssignment {
    OutputBus mainOutput{OutputBus::Master};
    bool cueEnabled{false};

    static DomainResult<RoutingAssignment> deckOutput(OutputBus mainOutput, bool cueEnabled);
};

struct TransportState {
    bool playing{false};
    double positionBeats{0.0};
};

struct DeckState {
    DeckId id{};
    DeckType type{DeckType::AudioFile};
    RoutingAssignment routing{};
    TransportState transport{};
};

class FourDeckCollection {
public:
    static FourDeckCollection createDefault();

    std::size_t size() const { return decks_.size(); }
    DomainResult<DeckState*> deck(DeckId id);
    DomainResult<const DeckState*> deck(DeckId id) const;
    DomainResult<DeckState*> deckByIndex(std::size_t deckIndex);
    DomainResult<const DeckState*> deckByIndex(std::size_t deckIndex) const;
    UnitResult setDeckType(DeckId id, DeckType type);
    UnitResult assignRouting(DeckId id, const RoutingAssignment& routing);

private:
    std::array<DeckState, 4> decks_{};
};

struct MasterClockState {
    double bpm{120.0};
    bool playing{false};
    double positionBeats{0.0};

    static MasterClockState stopped(double bpm);
    UnitResult setBpm(double nextBpm);
    void start();
    void stop();
    UnitResult advanceSeconds(double seconds);
};

struct BeatgridMetadata {
    double bpm{120.0};
    double firstBeatSeconds{0.0};

    static DomainResult<BeatgridMetadata> fromBpm(double bpm, double firstBeatSeconds);
};

struct TempoPitchSettings {
    double sourceBpm{120.0};
    double targetBpm{120.0};
    bool tempoSyncEnabled{false};
    bool pitchLockEnabled{true};
    double pitchShiftCents{0.0};
    bool bypass{false};

    static DomainResult<TempoPitchSettings> fromValues(double sourceBpm,
                                                       double targetBpm,
                                                       bool tempoSyncEnabled,
                                                       bool pitchLockEnabled,
                                                       double pitchShiftCents,
                                                       bool bypass);
    [[nodiscard]] double playbackRate() const noexcept;
    [[nodiscard]] double effectiveTempoBpm() const noexcept;
    [[nodiscard]] double pitchDriftCents() const noexcept;
};

enum class MusicalKey {
    Unknown,
    Camelot8A,
    Camelot8B,
};

struct PluginDescriptor {
    std::string identifier;
    std::string displayName;
    bool bypassed{false};
    struct ParameterState {
        std::string identifier;
        std::string displayName;
        double normalizedValue{};
    };
    std::vector<ParameterState> parameters;
    std::uint32_t latencyFrames{};
};

struct PluginChainDescriptor {
    std::string identifier;
    std::vector<PluginDescriptor> plugins;
};

struct MidiStep {
    bool enabled{false};
    int note{60};
    int velocity{100};
    double lengthBeats{0.25};
};

struct MidiStepPattern {
    std::array<MidiStep, 16> steps{};

    static MidiStepPattern sixteenStepDefault(int note);
};

struct LibraryTrack {
    std::string id;
    std::string title;
    std::string artist;
    BeatgridMetadata beatgrid;
    MusicalKey key{MusicalKey::Unknown};
};

struct Crate {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
};

struct Playlist {
    std::string id;
    std::string name;
    std::vector<std::string> trackIds;
};

enum class AnalysisJobStatus {
    Queued,
    Running,
    Complete,
    Failed,
};

struct AnalysisJob {
    std::string id;
    std::string trackId;
    AnalysisJobStatus status{AnalysisJobStatus::Queued};
    double progress{0.0};

    static AnalysisJob queued(std::string id, std::string trackId);
    UnitResult updateProgress(double nextProgress);
};

struct MidiLearnTarget {
    std::string id;
    std::string displayName;
};

struct MidiMessageDescriptor {
    int channel{0};
    int controller{0};
};

struct MidiLearnMapping {
    MidiLearnTarget target;
    MidiMessageDescriptor message;

    static DomainResult<MidiLearnMapping> bind(MidiLearnTarget target, MidiMessageDescriptor message);
};

} 
