cmake_minimum_required(VERSION 3.22)

project(DJApp VERSION 0.1.0 LANGUAGES CXX)

option(DJAPP_REQUIRE_JUCE "Fail configuration when JUCE is unavailable" OFF)
option(DJAPP_USE_VENDORED_JUCE "Use a local third_party/JUCE checkout when present" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/JuceIntegration.cmake)

add_custom_target(license-report
    COMMAND ${CMAKE_COMMAND}
        -DDJAPP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DDJAPP_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DDJAPP_LICENSE_REPORT=${CMAKE_CURRENT_BINARY_DIR}/license-report.spdx.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LicenseReport.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/license-report.spdx.txt
    COMMENT "Writing SPDX-style dependency license inventory"
    VERBATIM
)

add_custom_target(static-analysis
    COMMAND ${CMAKE_COMMAND}
        -DDJAPP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StaticAnalysis.cmake
    COMMENT "Checking static-analysis tool prerequisites"
    VERBATIM
)

add_library(DJAppCore STATIC
    src/core/DomainModels.cpp
)
target_include_directories(DJAppCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppCore PUBLIC cxx_std_17)

add_library(DJAppAudio STATIC
    src/audio/AudioEngine.cpp
    src/audio/routing/AudioRoutingGraph.cpp
)
target_include_directories(DJAppAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppAudio PUBLIC cxx_std_17)
target_link_libraries(DJAppAudio PUBLIC DJAppCore)

add_library(DJAppMidi STATIC
    src/midi/MidiBuffer.cpp
    src/midi/MidiLearn.cpp
)
target_include_directories(DJAppMidi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppMidi PUBLIC cxx_std_17)
target_link_libraries(DJAppMidi PUBLIC DJAppCore)

add_library(DJAppDecks STATIC
    src/decks/AudioDeck.cpp
    src/decks/SequencerDeck.cpp
)
target_include_directories(DJAppDecks
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppDecks PUBLIC cxx_std_17)
target_link_libraries(DJAppDecks PUBLIC DJAppAudio DJAppMidi)

find_package(SQLite3 QUIET)

add_library(DJAppPersistence STATIC
    src/persistence/Persistence.cpp
)
target_include_directories(DJAppPersistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppPersistence PUBLIC cxx_std_17)
target_link_libraries(DJAppPersistence PUBLIC DJAppCore)
if(SQLite3_FOUND)
    target_compile_definitions(DJAppPersistence PUBLIC DJAPP_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE=1)
else()
    target_compile_definitions(DJAppPersistence PUBLIC DJAPP_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE=0)
endif()

add_library(DJAppLibrary STATIC
    src/library/LibraryModel.cpp
)
target_include_directories(DJAppLibrary
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppLibrary PUBLIC cxx_std_17)
target_link_libraries(DJAppLibrary PUBLIC DJAppPersistence)

add_library(DJAppAnalysis STATIC
    src/analysis/AnalysisPipeline.cpp
)
target_include_directories(DJAppAnalysis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppAnalysis PUBLIC cxx_std_17)
target_link_libraries(DJAppAnalysis PUBLIC DJAppLibrary)

add_library(DJAppPlugins STATIC
    src/plugins/PluginManager.cpp
)
target_include_directories(DJAppPlugins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppPlugins PUBLIC cxx_std_17)
target_link_libraries(DJAppPlugins PUBLIC DJAppAudio DJAppPersistence)

add_library(DJAppRendering STATIC
    src/rendering/WaveformRenderer.cpp
)
target_include_directories(DJAppRendering
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppRendering PUBLIC cxx_std_17)

add_library(DJAppUiShell STATIC
    src/app/UiShell.cpp
)
target_include_directories(DJAppUiShell
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppUiShell PUBLIC cxx_std_17)
target_link_libraries(DJAppUiShell PUBLIC DJAppAudio DJAppLibrary DJAppMidi DJAppRendering)

add_library(DJAppAlphaSmoke STATIC
    src/app/AlphaSmoke.cpp
)
target_include_directories(DJAppAlphaSmoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppAlphaSmoke PUBLIC cxx_std_17)
target_link_libraries(DJAppAlphaSmoke PUBLIC DJAppAnalysis DJAppDecks DJAppPlugins DJAppUiShell)

add_library(DJAppBootstrap STATIC
    src/app/Bootstrap.cpp
)
target_include_directories(DJAppBootstrap
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppBootstrap PUBLIC cxx_std_17)
target_link_libraries(DJAppBootstrap PUBLIC DJAppAudio)

djapp_configure_juce(DJApp)

if(DJAPP_HAS_JUCE)
    target_sources(DJApp PRIVATE src/app/Main.cpp)
    target_link_libraries(DJApp PRIVATE DJAppBootstrap DJAppUiShell DJAppAlphaSmoke)
else()
    add_executable(DJApp src/app/Main.cpp)
    target_link_libraries(DJApp PRIVATE DJAppBootstrap DJAppUiShell DJAppAlphaSmoke)
endif()

target_compile_features(DJApp PRIVATE cxx_std_17)

target_compile_definitions(DJAppBootstrap
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

add_executable(DJAppTests
    tests/BootstrapTests.cpp
)
target_link_libraries(DJAppTests PRIVATE DJAppBootstrap)
target_compile_features(DJAppTests PRIVATE cxx_std_17)

add_test(NAME DJAppTests COMMAND DJAppTests)

add_executable(RealTimeSafetyContracts
    tests/RealTimeSafetyContracts.cpp
)
target_link_libraries(RealTimeSafetyContracts PRIVATE DJAppBootstrap)
target_compile_features(RealTimeSafetyContracts PRIVATE cxx_std_17)

add_test(NAME RealTimeSafetyContracts COMMAND RealTimeSafetyContracts)

add_executable(CoreDomainTests
    tests/core/CoreDomainTests.cpp
)
target_link_libraries(CoreDomainTests PRIVATE DJAppCore)
target_compile_features(CoreDomainTests PRIVATE cxx_std_17)

add_test(NAME CoreDomain.All COMMAND CoreDomainTests)
add_test(NAME CoreDomain.Decks COMMAND CoreDomainTests decks)
add_test(NAME CoreDomain.InvalidTransitions COMMAND CoreDomainTests invalid)
add_test(NAME CoreDomain.Clock COMMAND CoreDomainTests clock)
add_test(NAME CoreDomain.Descriptors COMMAND CoreDomainTests descriptors)

add_executable(AudioEngineTests
    tests/audio/AudioEngineTests.cpp
)
target_link_libraries(AudioEngineTests PRIVATE DJAppAudio)
target_compile_features(AudioEngineTests PRIVATE cxx_std_17)

add_test(NAME AudioEngine.All COMMAND AudioEngineTests)
add_test(NAME AudioEngine.Matrix COMMAND AudioEngineTests matrix)
add_test(NAME AudioEngine.DevicePolicy COMMAND AudioEngineTests policy)
add_test(NAME AudioEngine.DeviceEvents COMMAND AudioEngineTests events)
add_test(NAME AudioEngine.NoDevice COMMAND AudioEngineTests no-device)

add_executable(AudioRoutingGraphTests
    tests/audio/AudioRoutingGraphTests.cpp
)
target_link_libraries(AudioRoutingGraphTests PRIVATE DJAppAudio)
target_compile_features(AudioRoutingGraphTests PRIVATE cxx_std_17)

add_test(NAME AudioGraph.All COMMAND AudioRoutingGraphTests)
add_test(NAME AudioGraph.MultiOutput COMMAND AudioRoutingGraphTests multiout)
add_test(NAME AudioGraph.Errors COMMAND AudioRoutingGraphTests errors)
add_test(NAME AudioGraph.PendingUpdate COMMAND AudioRoutingGraphTests pending)

add_executable(AudioDeckTests
    tests/decks/AudioDeckTests.cpp
)
target_link_libraries(AudioDeckTests PRIVATE DJAppDecks)
target_compile_features(AudioDeckTests PRIVATE cxx_std_17)

add_test(NAME AudioDeck.All COMMAND AudioDeckTests)
add_test(NAME AudioDeck.ValidFile COMMAND AudioDeckTests valid)
add_test(NAME AudioDeck.CorruptFile COMMAND AudioDeckTests corrupt)
add_test(NAME AudioDeck.Transport COMMAND AudioDeckTests transport)

add_executable(SequencerTests
    tests/midi/SequencerTests.cpp
)
target_link_libraries(SequencerTests PRIVATE DJAppDecks)
target_compile_features(SequencerTests PRIVATE cxx_std_17)

add_test(NAME Sequencer.All COMMAND SequencerTests)
add_test(NAME Sequencer.DeterministicOffsets COMMAND SequencerTests offsets)
add_test(NAME Sequencer.Mute COMMAND SequencerTests mute)
add_test(NAME Sequencer.TempoChange COMMAND SequencerTests tempo)

add_executable(MidiLearnTests
    tests/midi/MidiLearnTests.cpp
)
target_link_libraries(MidiLearnTests PRIVATE DJAppMidi DJAppPersistence)
target_compile_features(MidiLearnTests PRIVATE cxx_std_17)

add_test(NAME MidiLearn.All COMMAND MidiLearnTests)
add_test(NAME MidiLearn.TargetRegistry COMMAND MidiLearnTests registry)
add_test(NAME MidiLearn.PersistMapping COMMAND MidiLearnTests persist)
add_test(NAME MidiLearn.DuplicateConflict COMMAND MidiLearnTests conflict)
add_test(NAME MidiLearn.ReconnectDispatch COMMAND MidiLearnTests reconnect)

add_executable(PersistenceTests
    tests/persistence/PersistenceTests.cpp
)
target_link_libraries(PersistenceTests PRIVATE DJAppPersistence)
target_compile_features(PersistenceTests PRIVATE cxx_std_17)

add_test(NAME Persistence.All COMMAND PersistenceTests)
add_test(NAME Persistence.Migrations COMMAND PersistenceTests migrations)
add_test(NAME Persistence.LockedDatabase COMMAND PersistenceTests locked)
add_test(NAME Persistence.FailedMigration COMMAND PersistenceTests failed)
add_test(NAME Persistence.Repositories COMMAND PersistenceTests repositories)

add_executable(LibraryAnalysisTests
    tests/library/LibraryAnalysisTests.cpp
)
target_link_libraries(LibraryAnalysisTests PRIVATE DJAppAnalysis)
target_compile_features(LibraryAnalysisTests PRIVATE cxx_std_17)

add_test(NAME Library.All COMMAND LibraryAnalysisTests)
add_test(NAME Library.ImportCrate COMMAND LibraryAnalysisTests import-crate)
add_test(NAME Library.Metadata COMMAND LibraryAnalysisTests metadata)
add_test(NAME Library.DeletedTrack COMMAND LibraryAnalysisTests deleted)
add_test(NAME Library.HugeScanCancellation COMMAND LibraryAnalysisTests cancel)
add_test(NAME Analysis.ResumeJob COMMAND LibraryAnalysisTests analysis-resume)

add_executable(UiShellTests
    tests/ui/UiShellTests.cpp
)
target_link_libraries(UiShellTests PRIVATE DJAppUiShell)
target_compile_features(UiShellTests PRIVATE cxx_std_17)

add_test(NAME UiShell.All COMMAND UiShellTests)
add_test(NAME UiShell.EmptyLibrary COMMAND UiShellTests empty)

add_executable(PluginManagerTests
    tests/plugins/PluginManagerTests.cpp
)
target_link_libraries(PluginManagerTests PRIVATE DJAppPlugins DJAppDecks)
target_compile_features(PluginManagerTests PRIVATE cxx_std_17)

add_test(NAME Plugin.All COMMAND PluginManagerTests)
add_test(NAME Plugin.ScanCache COMMAND PluginManagerTests scan-cache)
add_test(NAME Plugin.Blacklist COMMAND PluginManagerTests blacklist)
add_test(NAME Plugin.SequencerMidiRouting COMMAND PluginManagerTests midi-routing)

add_executable(AlphaSmokeTests
    tests/AlphaSmokeTests.cpp
)
target_link_libraries(AlphaSmokeTests PRIVATE DJAppAlphaSmoke)
target_compile_features(AlphaSmokeTests PRIVATE cxx_std_17)

add_test(NAME AlphaSmoke.All COMMAND AlphaSmokeTests)
add_test(NAME AlphaSmoke.EndToEnd COMMAND AlphaSmokeTests end-to-end)

add_executable(EdgeCaseTests
    tests/EdgeCaseTests.cpp
)
target_link_libraries(EdgeCaseTests PRIVATE DJAppAlphaSmoke)
target_compile_features(EdgeCaseTests PRIVATE cxx_std_17)

add_test(NAME EdgeCases.All COMMAND EdgeCaseTests)
add_test(NAME EdgeCases.DisconnectedDevices COMMAND EdgeCaseTests devices)
add_test(NAME EdgeCases.UnavailableOutputs COMMAND EdgeCaseTests outputs)
add_test(NAME EdgeCases.MissingPlugins COMMAND EdgeCaseTests plugins)
add_test(NAME EdgeCases.DeletedTracks COMMAND EdgeCaseTests deleted-tracks)
add_test(NAME EdgeCases.CancelledScans COMMAND EdgeCaseTests cancelled-scans)
add_test(NAME EdgeCases.UnavailableWaveform COMMAND EdgeCaseTests waveform)
add_test(NAME EdgeCases.GraphUpdateDuringRender COMMAND EdgeCaseTests graph-render)
