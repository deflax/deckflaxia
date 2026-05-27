cmake_minimum_required(VERSION 3.22)

project(DJApp VERSION 0.1.0 LANGUAGES C CXX)

option(DJAPP_REQUIRE_JUCE "Fail configuration when JUCE is unavailable; required for real playable JUCE validation" OFF)
option(DJAPP_USE_VENDORED_JUCE "Use a licensed local third_party/JUCE checkout when present" ON)

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
    src/audio/MixerControls.cpp
    src/audio/TimeStretchEngine.cpp
    src/audio/routing/AudioRoutingGraph.cpp
)
target_include_directories(DJAppAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppAudio PUBLIC cxx_std_17)
target_link_libraries(DJAppAudio PUBLIC DJAppCore)
find_path(DJAPP_RUBBERBAND_INCLUDE_DIR rubberband/RubberBandStretcher.h)
find_library(DJAPP_RUBBERBAND_LIBRARY NAMES rubberband RubberBand)
if(DJAPP_RUBBERBAND_INCLUDE_DIR AND DJAPP_RUBBERBAND_LIBRARY)
    target_compile_definitions(DJAppAudio PUBLIC DJAPP_HAS_RUBBERBAND=1)
    target_include_directories(DJAppAudio PUBLIC ${DJAPP_RUBBERBAND_INCLUDE_DIR})
    target_link_libraries(DJAppAudio PUBLIC ${DJAPP_RUBBERBAND_LIBRARY})
    message(STATUS "Rubber Band found; enabling guarded primary real-time time-stretch boundary")
else()
    target_compile_definitions(DJAppAudio PUBLIC DJAPP_HAS_RUBBERBAND=0)
    message(STATUS "Rubber Band not found; enabling Signalsmith-compatible guarded fallback time-stretch boundary")
endif()

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
    src/audio/AudioDeckSmoke.cpp
    src/audio/JuceAudioDeviceEngine.cpp
    src/decks/AudioDeck.cpp
    src/decks/FourDeckPlaybackCore.cpp
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
    target_link_libraries(DJAppPersistence PUBLIC SQLite::SQLite3)
else()
    target_compile_definitions(DJAppPersistence PUBLIC DJAPP_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE=0)
endif()
target_link_libraries(DJAppDecks PUBLIC DJAppPersistence)

add_library(DJAppLibrary STATIC
    src/library/AudioImport.cpp
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
    src/analysis/WaveformCache.cpp
)
target_include_directories(DJAppAnalysis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppAnalysis PUBLIC cxx_std_17)
target_link_libraries(DJAppAnalysis PUBLIC DJAppLibrary)

add_library(DJAppPlugins STATIC
    src/plugins/PluginManager.cpp
    src/plugins/PluginChainProcessor.cpp
    src/plugins/PluginSandbox.cpp
)
target_include_directories(DJAppPlugins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppPlugins PUBLIC cxx_std_17)
target_link_libraries(DJAppPlugins PUBLIC DJAppAudio DJAppPersistence)
target_link_libraries(DJAppDecks PUBLIC DJAppPlugins)

add_executable(DJAppPluginSandboxHelper
    src/ipc/PluginSandboxHelperMain.cpp
)
target_include_directories(DJAppPluginSandboxHelper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(DJAppPluginSandboxHelper PRIVATE cxx_std_17)

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
    src/ui/BrowserWaveformBeatgrid.cpp
    src/ui/JuceComponentTree.cpp
    src/ui/VST3EditorUi.cpp
)
target_include_directories(DJAppUiShell
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DJAppUiShell PUBLIC cxx_std_17)
target_link_libraries(DJAppUiShell PUBLIC DJAppAudio DJAppDecks DJAppLibrary DJAppMidi DJAppPlugins DJAppRendering)

add_library(DJAppAlphaSmoke STATIC
    src/app/AlphaSmoke.cpp
    src/app/PlayableSmoke.cpp
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

target_compile_definitions(DJAppAudio
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

target_compile_definitions(DJAppLibrary
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

target_compile_definitions(DJAppAnalysis
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

target_compile_definitions(DJAppDecks
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

target_compile_definitions(DJAppUiShell
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

target_compile_definitions(DJAppPlugins
    PUBLIC
        DJAPP_HAS_JUCE=$<BOOL:${DJAPP_HAS_JUCE}>
)

if(DJAPP_HAS_JUCE)
    target_link_libraries(DJAppDecks PUBLIC juce::juce_audio_utils)
    target_link_libraries(DJAppPlugins PUBLIC juce::juce_audio_processors juce::juce_gui_extra)
    target_link_libraries(DJAppUiShell PUBLIC juce::juce_gui_extra juce::juce_audio_utils)
endif()

if(DJAPP_HAS_JUCE)
    target_sources(DJApp PRIVATE src/app/Main.cpp)
    target_link_libraries(DJApp PRIVATE DJAppBootstrap DJAppUiShell DJAppAlphaSmoke DJAppDecks)
else()
    add_executable(DJApp src/app/Main.cpp)
    target_link_libraries(DJApp PRIVATE DJAppBootstrap DJAppUiShell DJAppAlphaSmoke DJAppDecks)
endif()

target_compile_features(DJApp PRIVATE cxx_std_17)
add_dependencies(DJApp DJAppPluginSandboxHelper)

add_custom_command(TARGET DJApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DJAppPluginSandboxHelper>
        $<TARGET_FILE_DIR:DJApp>
    COMMENT "Copying DJAppPluginSandboxHelper beside DJApp for sandbox smoke tests"
    VERBATIM
)

add_custom_target(plugin-sandbox-helper-packaging-check
    COMMAND ${CMAKE_COMMAND}
        -DDJAPP_APP_PATH=$<TARGET_FILE:DJApp>
        -DDJAPP_HELPER_PATH=$<TARGET_FILE:DJAppPluginSandboxHelper>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginSandboxPackagingCheck.cmake
    DEPENDS DJApp DJAppPluginSandboxHelper
    COMMENT "Checking DJAppPluginSandboxHelper build output and app-relative location"
    VERBATIM
)

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

set(DJAPP_DJ_WORKFLOW_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dj-workflow)

add_executable(JuceAudioDeckTests
    tests/decks/JuceAudioDeckTests.cpp
)
target_link_libraries(JuceAudioDeckTests PRIVATE DJAppDecks)
target_compile_features(JuceAudioDeckTests PRIVATE cxx_std_17)

add_test(NAME JuceAudioDeck.All COMMAND JuceAudioDeckTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.FourDeckCore COMMAND JuceAudioDeckTests core ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.Errors COMMAND JuceAudioDeckTests errors ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.FixtureRender COMMAND JuceAudioDeckTests fixture ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})

add_executable(TimeStretchTests
    tests/audio/TimeStretchTests.cpp
)
target_link_libraries(TimeStretchTests PRIVATE DJAppDecks DJAppPersistence)
target_compile_features(TimeStretchTests PRIVATE cxx_std_17)

add_test(NAME TimeStretch.All COMMAND TimeStretchTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.TempoSync COMMAND TimeStretchTests tempo ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.PitchLock COMMAND TimeStretchTests pitch ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.OverloadFallback COMMAND TimeStretchTests overload ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.Persistence COMMAND TimeStretchTests persistence ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})

if(DJAPP_HAS_JUCE)
    add_test(NAME JuceAudioDeck.AppSmoke COMMAND DJApp --audio-deck-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
endif()
add_test(NAME TimeStretch.TempoSyncSmoke COMMAND DJApp --tempo-sync-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
add_test(NAME TimeStretch.OverloadSmoke COMMAND DJApp --time-stretch-overload-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)

add_executable(MixerMidiTests
    tests/audio/MixerMidiTests.cpp
)
target_link_libraries(MixerMidiTests PRIVATE DJAppDecks DJAppPersistence)
target_compile_features(MixerMidiTests PRIVATE cxx_std_17)

add_test(NAME MixerMidi.All COMMAND MixerMidiTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.Crossfader COMMAND MixerMidiTests crossfader ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.PersistenceDispatch COMMAND MixerMidiTests midi ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.SmokeSurface COMMAND MixerMidiTests smoke ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.AppSmoke COMMAND DJApp --mixer-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-9-crossfade.wav --exit-after-init)

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
add_test(NAME SQLitePersistence.RestartSmoke COMMAND PersistenceTests sqlite-restart ${CMAKE_CURRENT_BINARY_DIR}/sqlite-persistence-smoke.db)
add_test(NAME SQLitePersistence.MigrationFailure COMMAND PersistenceTests sqlite-failure ${CMAKE_CURRENT_BINARY_DIR}/sqlite-persistence-invalid.db)
add_test(NAME SQLitePersistence.LockedDatabase COMMAND PersistenceTests sqlite-locked ${CMAKE_CURRENT_BINARY_DIR}/sqlite-persistence-locked.db)

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

add_executable(JuceUiSmokeTests
    tests/ui/JuceUiSmokeTests.cpp
)
target_link_libraries(JuceUiSmokeTests PRIVATE DJAppUiShell)
target_compile_features(JuceUiSmokeTests PRIVATE cxx_std_17)

add_test(NAME JuceUi.ModelSnapshot COMMAND JuceUiSmokeTests snapshot)
add_test(NAME JuceUi.UnavailableSurface COMMAND JuceUiSmokeTests unavailable)

add_executable(BrowserWaveformBeatgridTests
    tests/ui/BrowserWaveformBeatgridTests.cpp
)
target_link_libraries(BrowserWaveformBeatgridTests PRIVATE DJAppUiShell DJAppAnalysis)
target_compile_features(BrowserWaveformBeatgridTests PRIVATE cxx_std_17)

add_test(NAME BrowserWaveformBeatgrid.All COMMAND BrowserWaveformBeatgridTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.ImportClassification COMMAND BrowserWaveformBeatgridTests import ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.WaveformPrimitive COMMAND BrowserWaveformBeatgridTests waveform ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.BeatgridPersistence COMMAND BrowserWaveformBeatgridTests beatgrid ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})

if(DJAPP_HAS_JUCE)
    add_test(NAME JuceUi.ComponentTree COMMAND DJApp --juce-ui-smoke-test --dump-components --exit-after-init)
    add_test(NAME JuceUi.Screenshot COMMAND DJApp --juce-ui-smoke-test --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-6-ui.png --exit-after-init)
else()
    add_test(NAME JuceUi.ComponentTreeUnavailable COMMAND DJApp --juce-ui-smoke-test --dump-components --exit-after-init)
    add_test(NAME JuceUi.ScreenshotUnavailable COMMAND DJApp --juce-ui-smoke-test --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-6-ui.png --exit-after-init)
    set_tests_properties(JuceUi.ScreenshotUnavailable PROPERTIES WILL_FAIL TRUE)
endif()

add_executable(PluginManagerTests
    tests/plugins/PluginManagerTests.cpp
)
target_link_libraries(PluginManagerTests PRIVATE DJAppPlugins DJAppDecks)
target_compile_features(PluginManagerTests PRIVATE cxx_std_17)

add_test(NAME Plugin.All COMMAND PluginManagerTests)
add_test(NAME Plugin.ScanCache COMMAND PluginManagerTests scan-cache)
add_test(NAME Plugin.Blacklist COMMAND PluginManagerTests blacklist)
add_test(NAME Plugin.SequencerMidiRouting COMMAND PluginManagerTests midi-routing)

set(DJAPP_PLUGIN_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugins)

add_executable(VST3ProcessingTests
    tests/plugins/VST3ProcessingTests.cpp
)
target_link_libraries(VST3ProcessingTests PRIVATE DJAppDecks DJAppPlugins DJAppPersistence)
target_compile_features(VST3ProcessingTests PRIVATE cxx_std_17)

add_test(NAME VST3Processing.All COMMAND VST3ProcessingTests all ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.DeckProcessingChangesAudio COMMAND VST3ProcessingTests deck ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.ParameterLatency COMMAND VST3ProcessingTests parameters ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.MasterChainStateReload COMMAND VST3ProcessingTests master-state ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.SmokeSurface COMMAND VST3ProcessingTests smoke ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.AppSmoke COMMAND DJApp --vst3-processing-smoke-test --chain deck-a --fixtures ${DJAPP_PLUGIN_FIXTURE_DIR} --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-10-deck-vst3.wav --exit-after-init)

add_executable(VST3EditorUiTests
    tests/ui/VST3EditorUiTests.cpp
)
target_link_libraries(VST3EditorUiTests PRIVATE DJAppUiShell DJAppDecks DJAppPlugins DJAppPersistence)
target_compile_features(VST3EditorUiTests PRIVATE cxx_std_17)

add_test(NAME VST3EditorUi.All COMMAND VST3EditorUiTests all ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.ModelControls COMMAND VST3EditorUiTests model ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.NoEditorGenericFallback COMMAND VST3EditorUiTests no-editor ${DJAPP_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.AppSmoke COMMAND DJApp --vst3-editor-smoke-test --fixtures ${DJAPP_PLUGIN_FIXTURE_DIR} --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-11-editor.png --exit-after-init)


add_executable(PluginSandboxTests
    tests/plugins/PluginSandboxTests.cpp
)
target_link_libraries(PluginSandboxTests PRIVATE DJAppPlugins DJAppPersistence)
target_compile_features(PluginSandboxTests PRIVATE cxx_std_17)

add_test(NAME PluginSandbox.All COMMAND PluginSandboxTests all ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.HelperLimit COMMAND PluginSandboxTests limit ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.CrashRestartBlacklist COMMAND PluginSandboxTests crash ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.ControlIpc COMMAND PluginSandboxTests control ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.AudioRoundtrip COMMAND PluginSandboxTests audio ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.SmokeSurface COMMAND PluginSandboxTests smoke ${DJAPP_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DJAppPluginSandboxHelper>)
add_test(NAME PluginSandbox.AppSmoke COMMAND DJApp --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures ${DJAPP_PLUGIN_FIXTURE_DIR} --exit-after-init)
add_test(NAME PluginSandbox.HelperPackaging COMMAND ${CMAKE_COMMAND}
    -DDJAPP_APP_PATH=$<TARGET_FILE:DJApp>
    -DDJAPP_HELPER_PATH=$<TARGET_FILE:DJAppPluginSandboxHelper>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginSandboxPackagingCheck.cmake)

add_executable(PlayableSmokeTests
    tests/PlayableSmokeTests.cpp
)
target_link_libraries(PlayableSmokeTests PRIVATE DJAppAlphaSmoke)
target_compile_features(PlayableSmokeTests PRIVATE cxx_std_17)

add_test(NAME PlayableSmoke.All COMMAND PlayableSmokeTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME PlayableSmoke.Workflow COMMAND PlayableSmokeTests workflow ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME PlayableSmoke.Restart COMMAND PlayableSmokeTests restart ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME ProductionWorkflowSmoke.Library COMMAND PlayableSmokeTests production ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-16-restart-test.db)
add_test(NAME ScopeAudit.Library COMMAND PlayableSmokeTests scope-audit ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-16-restart-test.db)
add_test(NAME PlayableSmoke.AppSmoke COMMAND DJApp --playable-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-playable.png --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-mix.wav --exit-after-init)
add_test(NAME PlayableSmoke.AppRestart COMMAND DJApp --playable-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --db ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-restart.db --exit-after-init)
add_test(NAME PlayableSmoke.AppRestartRestore COMMAND DJApp --playable-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --db ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-restart.db --expect-restored-session --exit-after-init)
set_tests_properties(PlayableSmoke.AppRestartRestore PROPERTIES DEPENDS PlayableSmoke.AppRestart)
add_test(NAME ProductionWorkflowSmoke.AppSmoke COMMAND DJApp --production-dj-workflow-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
add_test(NAME ScopeAudit.AppSmoke COMMAND DJApp --scope-audit --forbid "Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor" --paths src tests docs cmake)
set_tests_properties(ProductionWorkflowSmoke.Library ScopeAudit.Library ProductionWorkflowSmoke.AppSmoke ScopeAudit.AppSmoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(PerformanceHardeningTests
    tests/PerformanceHardeningTests.cpp
)
target_link_libraries(PerformanceHardeningTests PRIVATE DJAppAlphaSmoke DJAppDecks DJAppPlugins DJAppPersistence)
target_compile_features(PerformanceHardeningTests PRIVATE cxx_std_17)

add_test(NAME PerformanceHardening.All COMMAND PerformanceHardeningTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.Budgets COMMAND PerformanceHardeningTests budgets ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.Faults COMMAND PerformanceHardeningTests faults ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.AppSmoke COMMAND DJApp --performance-smoke-test --fixtures ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR} --sample-rate 48000 --buffer-size 512 --exit-after-init)
set_tests_properties(PerformanceHardening.All PerformanceHardening.Budgets PerformanceHardening.Faults PerformanceHardening.AppSmoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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

add_executable(FixtureTests
    tests/FixtureTests.cpp
)
target_compile_features(FixtureTests PRIVATE cxx_std_17)

add_test(NAME Fixtures.All COMMAND FixtureTests all ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME Fixtures.Generate COMMAND FixtureTests generate ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME Fixtures.CorruptAudio COMMAND FixtureTests corrupt ${DJAPP_DJ_WORKFLOW_FIXTURE_DIR})
