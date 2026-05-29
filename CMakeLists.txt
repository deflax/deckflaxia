cmake_minimum_required(VERSION 3.22)

project(Deckflaxia VERSION 0.1.0 LANGUAGES C CXX)

option(DECKFLAXIA_REQUIRE_JUCE "Fail configuration when JUCE is unavailable; required for real playable JUCE validation" OFF)
option(DECKFLAXIA_USE_VENDORED_JUCE "Use a licensed local third_party/JUCE checkout when present" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/JuceIntegration.cmake)

add_custom_target(license-report
    COMMAND ${CMAKE_COMMAND}
        -DDECKFLAXIA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DDECKFLAXIA_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DDECKFLAXIA_LICENSE_REPORT=${CMAKE_CURRENT_BINARY_DIR}/license-report.spdx.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LicenseReport.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/license-report.spdx.txt
    COMMENT "Writing SPDX-style dependency license inventory"
    VERBATIM
)

add_custom_target(static-analysis
    COMMAND ${CMAKE_COMMAND}
        -DDECKFLAXIA_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StaticAnalysis.cmake
    COMMENT "Checking static-analysis tool prerequisites"
    VERBATIM
)

add_library(DeckflaxiaCore STATIC
    src/core/DomainModels.cpp
)
target_include_directories(DeckflaxiaCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaCore PUBLIC cxx_std_17)

add_library(DeckflaxiaAudio STATIC
    src/audio/AudioEngine.cpp
    src/audio/MixerControls.cpp
    src/audio/TimeStretchEngine.cpp
    src/audio/routing/AudioRoutingGraph.cpp
)
target_include_directories(DeckflaxiaAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaAudio PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaAudio PUBLIC DeckflaxiaCore)
find_path(DECKFLAXIA_RUBBERBAND_INCLUDE_DIR rubberband/RubberBandStretcher.h)
find_library(DECKFLAXIA_RUBBERBAND_LIBRARY NAMES rubberband RubberBand)
if(DECKFLAXIA_RUBBERBAND_INCLUDE_DIR AND DECKFLAXIA_RUBBERBAND_LIBRARY)
    target_compile_definitions(DeckflaxiaAudio PUBLIC DECKFLAXIA_HAS_RUBBERBAND=1)
    target_include_directories(DeckflaxiaAudio PUBLIC ${DECKFLAXIA_RUBBERBAND_INCLUDE_DIR})
    target_link_libraries(DeckflaxiaAudio PUBLIC ${DECKFLAXIA_RUBBERBAND_LIBRARY})
    message(STATUS "Rubber Band found; enabling guarded primary real-time time-stretch boundary")
else()
    target_compile_definitions(DeckflaxiaAudio PUBLIC DECKFLAXIA_HAS_RUBBERBAND=0)
    message(STATUS "Rubber Band not found; enabling Signalsmith-compatible guarded fallback time-stretch boundary")
endif()

add_library(DeckflaxiaMidi STATIC
    src/midi/MidiBuffer.cpp
    src/midi/MidiLearn.cpp
)
target_include_directories(DeckflaxiaMidi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaMidi PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaMidi PUBLIC DeckflaxiaCore)

add_library(DeckflaxiaDecks STATIC
    src/audio/AudioDeckSmoke.cpp
    src/audio/JuceAudioDeviceEngine.cpp
    src/decks/AudioDeck.cpp
    src/decks/FourDeckPlaybackCore.cpp
    src/decks/SequencerDeck.cpp
)
target_include_directories(DeckflaxiaDecks
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaDecks PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaDecks PUBLIC DeckflaxiaAudio DeckflaxiaMidi)

find_package(SQLite3 QUIET)

add_library(DeckflaxiaPersistence STATIC
    src/persistence/Persistence.cpp
)
target_include_directories(DeckflaxiaPersistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaPersistence PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaPersistence PUBLIC DeckflaxiaCore)
if(SQLite3_FOUND)
    target_compile_definitions(DeckflaxiaPersistence PUBLIC DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE=1)
    target_link_libraries(DeckflaxiaPersistence PUBLIC SQLite::SQLite3)
else()
    target_compile_definitions(DeckflaxiaPersistence PUBLIC DECKFLAXIA_PERSISTENCE_SYSTEM_SQLITE_AVAILABLE=0)
endif()
target_link_libraries(DeckflaxiaDecks PUBLIC DeckflaxiaPersistence)

add_library(DeckflaxiaLibrary STATIC
    src/library/AudioImport.cpp
    src/library/LibraryModel.cpp
)
target_include_directories(DeckflaxiaLibrary
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaLibrary PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaLibrary PUBLIC DeckflaxiaPersistence)

add_library(DeckflaxiaAnalysis STATIC
    src/analysis/AnalysisPipeline.cpp
    src/analysis/WaveformCache.cpp
)
target_include_directories(DeckflaxiaAnalysis
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaAnalysis PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaAnalysis PUBLIC DeckflaxiaLibrary)

add_library(DeckflaxiaPlugins STATIC
    src/plugins/PluginManager.cpp
    src/plugins/PluginChainProcessor.cpp
    src/plugins/PluginSandbox.cpp
)
target_include_directories(DeckflaxiaPlugins
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaPlugins PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaPlugins PUBLIC DeckflaxiaAudio DeckflaxiaPersistence)
target_link_libraries(DeckflaxiaDecks PUBLIC DeckflaxiaPlugins)

add_executable(DeckflaxiaPluginSandboxHelper
    src/ipc/PluginSandboxHelperMain.cpp
)
target_include_directories(DeckflaxiaPluginSandboxHelper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(DeckflaxiaPluginSandboxHelper PRIVATE cxx_std_17)

add_library(DeckflaxiaRendering STATIC
    src/rendering/WaveformRenderer.cpp
)
target_include_directories(DeckflaxiaRendering
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaRendering PUBLIC cxx_std_17)

add_library(DeckflaxiaUiShell STATIC
    src/app/UiShell.cpp
    src/ui/BrowserWaveformBeatgrid.cpp
    src/ui/JuceComponentTree.cpp
    src/ui/JuceUiCommandAdapter.cpp
    src/ui/VST3EditorUi.cpp
)
target_include_directories(DeckflaxiaUiShell
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaUiShell PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaUiShell PUBLIC DeckflaxiaAnalysis DeckflaxiaAudio DeckflaxiaDecks DeckflaxiaLibrary DeckflaxiaMidi DeckflaxiaPlugins DeckflaxiaRendering)

add_library(DeckflaxiaAlphaSmoke STATIC
    src/app/AlphaSmoke.cpp
    src/app/PlayableSmoke.cpp
)
target_include_directories(DeckflaxiaAlphaSmoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaAlphaSmoke PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaAlphaSmoke PUBLIC DeckflaxiaAnalysis DeckflaxiaDecks DeckflaxiaPlugins DeckflaxiaUiShell)

add_library(DeckflaxiaBootstrap STATIC
    src/app/Bootstrap.cpp
)
target_include_directories(DeckflaxiaBootstrap
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(DeckflaxiaBootstrap PUBLIC cxx_std_17)
target_link_libraries(DeckflaxiaBootstrap PUBLIC DeckflaxiaAudio)

deckflaxia_configure_juce(Deckflaxia)

target_compile_definitions(DeckflaxiaAudio
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

target_compile_definitions(DeckflaxiaLibrary
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

target_compile_definitions(DeckflaxiaAnalysis
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

target_compile_definitions(DeckflaxiaDecks
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

target_compile_definitions(DeckflaxiaUiShell
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

target_compile_definitions(DeckflaxiaPlugins
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
        JUCE_PLUGINHOST_VST3=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

if(DECKFLAXIA_HAS_JUCE)
    target_link_libraries(DeckflaxiaDecks PUBLIC juce::juce_audio_utils)
    target_link_libraries(DeckflaxiaPlugins PUBLIC juce::juce_audio_processors juce::juce_gui_extra)
    target_link_libraries(DeckflaxiaUiShell PUBLIC juce::juce_gui_extra juce::juce_audio_utils)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(DeckflaxiaDecks PUBLIC juce::pkgconfig_JUCE_CURL_LINUX_DEPS)
        target_link_libraries(DeckflaxiaPlugins PUBLIC juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS juce::pkgconfig_JUCE_CURL_LINUX_DEPS)
        target_link_libraries(DeckflaxiaUiShell PUBLIC juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS juce::pkgconfig_JUCE_CURL_LINUX_DEPS)
    endif()
endif()

if(DECKFLAXIA_HAS_JUCE AND TARGET juce::juce_audio_plugin_client_VST3)
    set(DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/real-vst3-fixture)
    set(DECKFLAXIA_REAL_VST3_FIXTURE_MANIFEST_PATH ${CMAKE_CURRENT_BINARY_DIR}/generated/real-vst3-fixture/manifest.json)
    set(DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME DeckflaxiaRealVst3Fixture)
    set(DECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR}/VST3/${DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME}.vst3)
    set(DECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH "")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(DECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH ${DECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH}/Contents/${JUCE_TARGET_ARCHITECTURE}-linux/${DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME}.so)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(JUCE_TARGET_ARCHITECTURE STREQUAL "x86_64")
            set(DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH x86_64)
        elseif(JUCE_TARGET_ARCHITECTURE STREQUAL "i386")
            set(DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH x86)
        elseif(JUCE_TARGET_ARCHITECTURE STREQUAL "arm64ec")
            set(DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH arm64ec)
        elseif(JUCE_TARGET_ARCHITECTURE STREQUAL "aarch64")
            set(DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH arm64)
        endif()

        if(DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH)
            set(DECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH ${DECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH}/Contents/${DECKFLAXIA_REAL_VST3_FIXTURE_WINDOWS_ARCH}-win/${DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME}.vst3)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(DECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH ${DECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH}/Contents/MacOS/${DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME})
    endif()

    juce_add_plugin(DeckflaxiaRealVst3FixturePlugin
        COMPANY_NAME Deckflaxia
        BUNDLE_ID app.deckflaxia.tests.real-vst3-fixture
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE Dflx
        PLUGIN_CODE Rvg1
        FORMATS VST3
        PRODUCT_NAME ${DECKFLAXIA_REAL_VST3_FIXTURE_PRODUCT_NAME}
    )
    target_sources(DeckflaxiaRealVst3FixturePlugin
        PRIVATE
            tests/fixtures/plugins/real-vst3-fixture/RealVst3GainFixture.cpp
            tests/fixtures/plugins/real-vst3-fixture/RealVst3GainFixture.h
    )
    target_compile_definitions(DeckflaxiaRealVst3FixturePlugin
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )
    target_link_libraries(DeckflaxiaRealVst3FixturePlugin
        PRIVATE
            juce::juce_audio_processors
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(DeckflaxiaRealVst3FixturePlugin PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        LIBRARY_OUTPUT_DIRECTORY ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR}
    )
    set_target_properties(DeckflaxiaRealVst3FixturePlugin_VST3 DeckflaxiaRealVst3FixturePlugin_All PROPERTIES
        EXCLUDE_FROM_ALL TRUE
    )

    add_custom_command(TARGET DeckflaxiaRealVst3FixturePlugin_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DDECKFLAXIA_REAL_VST3_FIXTURE_MANIFEST=${DECKFLAXIA_REAL_VST3_FIXTURE_MANIFEST_PATH}
            -DDECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH=${DECKFLAXIA_REAL_VST3_FIXTURE_BUNDLE_PATH}
            -DDECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH=${DECKFLAXIA_REAL_VST3_FIXTURE_BINARY_PATH}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugins/real-vst3-fixture/GenerateManifest.cmake
        BYPRODUCTS ${DECKFLAXIA_REAL_VST3_FIXTURE_MANIFEST_PATH}
        COMMENT "Writing real VST3 fixture manifest"
        VERBATIM
    )
    add_custom_target(DeckflaxiaRealVst3Fixture
        DEPENDS DeckflaxiaRealVst3FixturePlugin_VST3
    )
else()
    message(STATUS "DeckflaxiaRealVst3Fixture disabled; JUCE VST3 plugin build support is unavailable")
endif()

if(DECKFLAXIA_HAS_JUCE)
    target_sources(Deckflaxia PRIVATE src/app/Main.cpp)
    target_link_libraries(Deckflaxia PRIVATE DeckflaxiaBootstrap DeckflaxiaUiShell DeckflaxiaAlphaSmoke DeckflaxiaDecks)
else()
    add_executable(Deckflaxia src/app/Main.cpp)
    target_link_libraries(Deckflaxia PRIVATE DeckflaxiaBootstrap DeckflaxiaUiShell DeckflaxiaAlphaSmoke DeckflaxiaDecks)
endif()

target_compile_features(Deckflaxia PRIVATE cxx_std_17)
add_dependencies(Deckflaxia DeckflaxiaPluginSandboxHelper)

add_custom_command(TARGET Deckflaxia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>
        $<TARGET_FILE_DIR:Deckflaxia>
    COMMENT "Copying DeckflaxiaPluginSandboxHelper beside Deckflaxia for sandbox smoke tests"
    VERBATIM
)

add_custom_target(plugin-sandbox-helper-packaging-check
    COMMAND ${CMAKE_COMMAND}
        -DDECKFLAXIA_APP_PATH=$<TARGET_FILE:Deckflaxia>
        -DDECKFLAXIA_HELPER_PATH=$<TARGET_FILE_DIR:Deckflaxia>/$<TARGET_FILE_NAME:DeckflaxiaPluginSandboxHelper>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginSandboxPackagingCheck.cmake
    DEPENDS Deckflaxia DeckflaxiaPluginSandboxHelper
    COMMENT "Checking DeckflaxiaPluginSandboxHelper build output and app-relative location"
    VERBATIM
)

target_compile_definitions(DeckflaxiaBootstrap
    PUBLIC
        DECKFLAXIA_HAS_JUCE=$<BOOL:${DECKFLAXIA_HAS_JUCE}>
)

add_executable(DeckflaxiaTests
    tests/BootstrapTests.cpp
)
target_link_libraries(DeckflaxiaTests PRIVATE DeckflaxiaBootstrap)
target_compile_features(DeckflaxiaTests PRIVATE cxx_std_17)

add_test(NAME DeckflaxiaTests COMMAND DeckflaxiaTests)

add_executable(RealTimeSafetyContracts
    tests/RealTimeSafetyContracts.cpp
)
target_link_libraries(RealTimeSafetyContracts PRIVATE DeckflaxiaBootstrap)
target_compile_features(RealTimeSafetyContracts PRIVATE cxx_std_17)

add_test(NAME RealTimeSafetyContracts COMMAND RealTimeSafetyContracts)

add_executable(CoreDomainTests
    tests/core/CoreDomainTests.cpp
)
target_link_libraries(CoreDomainTests PRIVATE DeckflaxiaCore)
target_compile_features(CoreDomainTests PRIVATE cxx_std_17)

add_test(NAME CoreDomain.All COMMAND CoreDomainTests)
add_test(NAME CoreDomain.Decks COMMAND CoreDomainTests decks)
add_test(NAME CoreDomain.InvalidTransitions COMMAND CoreDomainTests invalid)
add_test(NAME CoreDomain.Clock COMMAND CoreDomainTests clock)
add_test(NAME CoreDomain.Descriptors COMMAND CoreDomainTests descriptors)

add_executable(AudioEngineTests
    tests/audio/AudioEngineTests.cpp
)
target_link_libraries(AudioEngineTests PRIVATE DeckflaxiaAudio)
target_compile_features(AudioEngineTests PRIVATE cxx_std_17)

add_test(NAME AudioEngine.All COMMAND AudioEngineTests)
add_test(NAME AudioEngine.Matrix COMMAND AudioEngineTests matrix)
add_test(NAME AudioEngine.DevicePolicy COMMAND AudioEngineTests policy)
add_test(NAME AudioEngine.DeviceEvents COMMAND AudioEngineTests events)
add_test(NAME AudioEngine.NoDevice COMMAND AudioEngineTests no-device)

add_executable(AudioRoutingGraphTests
    tests/audio/AudioRoutingGraphTests.cpp
)
target_link_libraries(AudioRoutingGraphTests PRIVATE DeckflaxiaAudio)
target_compile_features(AudioRoutingGraphTests PRIVATE cxx_std_17)

add_test(NAME AudioGraph.All COMMAND AudioRoutingGraphTests)
add_test(NAME AudioGraph.MultiOutput COMMAND AudioRoutingGraphTests multiout)
add_test(NAME AudioGraph.Errors COMMAND AudioRoutingGraphTests errors)
add_test(NAME AudioGraph.PendingUpdate COMMAND AudioRoutingGraphTests pending)

add_executable(AudioDeckTests
    tests/decks/AudioDeckTests.cpp
)
target_link_libraries(AudioDeckTests PRIVATE DeckflaxiaDecks)
target_compile_features(AudioDeckTests PRIVATE cxx_std_17)

add_test(NAME AudioDeck.All COMMAND AudioDeckTests)
add_test(NAME AudioDeck.ValidFile COMMAND AudioDeckTests valid)
add_test(NAME AudioDeck.CorruptFile COMMAND AudioDeckTests corrupt)
add_test(NAME AudioDeck.Transport COMMAND AudioDeckTests transport)

set(DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dj-workflow)

add_executable(JuceAudioDeckTests
    tests/decks/JuceAudioDeckTests.cpp
)
target_link_libraries(JuceAudioDeckTests PRIVATE DeckflaxiaDecks)
target_compile_features(JuceAudioDeckTests PRIVATE cxx_std_17)

add_test(NAME JuceAudioDeck.All COMMAND JuceAudioDeckTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.FourDeckCore COMMAND JuceAudioDeckTests core ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.Errors COMMAND JuceAudioDeckTests errors ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME JuceAudioDeck.FixtureRender COMMAND JuceAudioDeckTests fixture ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})

add_executable(TimeStretchTests
    tests/audio/TimeStretchTests.cpp
)
target_link_libraries(TimeStretchTests PRIVATE DeckflaxiaDecks DeckflaxiaPersistence)
target_compile_features(TimeStretchTests PRIVATE cxx_std_17)

add_test(NAME TimeStretch.All COMMAND TimeStretchTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.TempoSync COMMAND TimeStretchTests tempo ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.PitchLock COMMAND TimeStretchTests pitch ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.OverloadFallback COMMAND TimeStretchTests overload ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME TimeStretch.Persistence COMMAND TimeStretchTests persistence ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})

if(DECKFLAXIA_HAS_JUCE)
    add_test(NAME JuceAudioDeck.AppSmoke COMMAND Deckflaxia --audio-deck-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
endif()
add_test(NAME TimeStretch.TempoSyncSmoke COMMAND Deckflaxia --tempo-sync-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
add_test(NAME TimeStretch.OverloadSmoke COMMAND Deckflaxia --time-stretch-overload-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)

add_executable(MixerMidiTests
    tests/audio/MixerMidiTests.cpp
)
target_link_libraries(MixerMidiTests PRIVATE DeckflaxiaDecks DeckflaxiaPersistence)
target_compile_features(MixerMidiTests PRIVATE cxx_std_17)

add_test(NAME MixerMidi.All COMMAND MixerMidiTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.Crossfader COMMAND MixerMidiTests crossfader ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.PersistenceDispatch COMMAND MixerMidiTests midi ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.SmokeSurface COMMAND MixerMidiTests smoke ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME MixerMidi.AppSmoke COMMAND Deckflaxia --mixer-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-9-crossfade.wav --exit-after-init)

add_executable(SequencerTests
    tests/midi/SequencerTests.cpp
)
target_link_libraries(SequencerTests PRIVATE DeckflaxiaDecks)
target_compile_features(SequencerTests PRIVATE cxx_std_17)

add_test(NAME Sequencer.All COMMAND SequencerTests)
add_test(NAME Sequencer.DeterministicOffsets COMMAND SequencerTests offsets)
add_test(NAME Sequencer.Mute COMMAND SequencerTests mute)
add_test(NAME Sequencer.TempoChange COMMAND SequencerTests tempo)

add_executable(MidiLearnTests
    tests/midi/MidiLearnTests.cpp
)
target_link_libraries(MidiLearnTests PRIVATE DeckflaxiaMidi DeckflaxiaPersistence)
target_compile_features(MidiLearnTests PRIVATE cxx_std_17)

add_test(NAME MidiLearn.All COMMAND MidiLearnTests)
add_test(NAME MidiLearn.TargetRegistry COMMAND MidiLearnTests registry)
add_test(NAME MidiLearn.PersistMapping COMMAND MidiLearnTests persist)
add_test(NAME MidiLearn.DuplicateConflict COMMAND MidiLearnTests conflict)
add_test(NAME MidiLearn.ReconnectDispatch COMMAND MidiLearnTests reconnect)

add_executable(PersistenceTests
    tests/persistence/PersistenceTests.cpp
)
target_link_libraries(PersistenceTests PRIVATE DeckflaxiaPersistence)
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
target_link_libraries(LibraryAnalysisTests PRIVATE DeckflaxiaAnalysis)
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
target_link_libraries(UiShellTests PRIVATE DeckflaxiaUiShell)
target_compile_features(UiShellTests PRIVATE cxx_std_17)

add_test(NAME UiShell.All COMMAND UiShellTests)
add_test(NAME UiShell.EmptyLibrary COMMAND UiShellTests empty)

add_executable(JuceUiSmokeTests
    tests/ui/JuceUiSmokeTests.cpp
)
target_link_libraries(JuceUiSmokeTests PRIVATE DeckflaxiaUiShell)
target_compile_features(JuceUiSmokeTests PRIVATE cxx_std_17)

add_test(NAME JuceUi.ModelSnapshot COMMAND JuceUiSmokeTests snapshot)
add_test(NAME JuceUi.UnavailableSurface COMMAND JuceUiSmokeTests unavailable)
add_test(NAME JuceUi.ComponentInventory COMMAND JuceUiSmokeTests inventory)
add_test(NAME JuceUi.DeckPanelStateContent COMMAND JuceUiSmokeTests deck-panels)
add_test(NAME JuceUi.DeckMixerControls COMMAND JuceUiSmokeTests deck-mixer)
add_test(NAME JuceUi.BrowserControls COMMAND JuceUiSmokeTests browser)
add_test(NAME JuceUi.PluginControls COMMAND JuceUiSmokeTests plugin)
add_test(NAME JuceUi.RefreshHonesty COMMAND JuceUiSmokeTests refresh)
set_tests_properties(JuceUi.BrowserControls PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(JuceUiCommandAdapterTests
    tests/ui/JuceUiCommandAdapterTests.cpp
)
target_link_libraries(JuceUiCommandAdapterTests PRIVATE DeckflaxiaUiShell DeckflaxiaDecks DeckflaxiaPlugins)
target_compile_features(JuceUiCommandAdapterTests PRIVATE cxx_std_17)

add_test(NAME JuceUiCommandAdapter.All COMMAND JuceUiCommandAdapterTests)
set_tests_properties(JuceUiCommandAdapter.All PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(BrowserWaveformBeatgridTests
    tests/ui/BrowserWaveformBeatgridTests.cpp
)
target_link_libraries(BrowserWaveformBeatgridTests PRIVATE DeckflaxiaUiShell DeckflaxiaAnalysis)
target_compile_features(BrowserWaveformBeatgridTests PRIVATE cxx_std_17)

add_test(NAME BrowserWaveformBeatgrid.All COMMAND BrowserWaveformBeatgridTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.ImportClassification COMMAND BrowserWaveformBeatgridTests import ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.WaveformPrimitive COMMAND BrowserWaveformBeatgridTests waveform ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.BeatgridPersistence COMMAND BrowserWaveformBeatgridTests beatgrid ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME BrowserWaveformBeatgrid.InvalidImportRows COMMAND BrowserWaveformBeatgridTests invalid ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})

if(DECKFLAXIA_HAS_JUCE)
    add_test(NAME JuceUi.ComponentTree COMMAND Deckflaxia --juce-ui-smoke-test --dump-components --exit-after-init)
    add_test(NAME JuceUi.Screenshot COMMAND ${CMAKE_COMMAND}
        -DDECKFLAXIA_APP_PATH=$<TARGET_FILE:Deckflaxia>
        -DDECKFLAXIA_SCREENSHOT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-6-ui.png
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/VerifyJuceScreenshot.cmake
    )
else()
    add_test(NAME JuceUi.ComponentTreeUnavailable COMMAND Deckflaxia --juce-ui-smoke-test --dump-components --exit-after-init)
    add_test(NAME JuceUi.ScreenshotUnavailable COMMAND Deckflaxia --juce-ui-smoke-test --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-6-ui.png --exit-after-init)
    set_tests_properties(JuceUi.ScreenshotUnavailable PROPERTIES WILL_FAIL TRUE)
endif()

add_executable(PluginManagerTests
    tests/plugins/PluginManagerTests.cpp
)
target_link_libraries(PluginManagerTests PRIVATE DeckflaxiaPlugins DeckflaxiaDecks)
target_compile_features(PluginManagerTests PRIVATE cxx_std_17)

add_test(NAME Plugin.All COMMAND PluginManagerTests)
add_test(NAME Plugin.ScanCache COMMAND PluginManagerTests scan-cache)
add_test(NAME Plugin.Blacklist COMMAND PluginManagerTests blacklist)
add_test(NAME Plugin.SequencerMidiRouting COMMAND PluginManagerTests midi-routing)

set(DECKFLAXIA_PLUGIN_FIXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugins)

add_executable(VST3ProcessingTests
    tests/plugins/VST3ProcessingTests.cpp
)
target_link_libraries(VST3ProcessingTests PRIVATE DeckflaxiaDecks DeckflaxiaPlugins DeckflaxiaPersistence)
target_compile_features(VST3ProcessingTests PRIVATE cxx_std_17)

add_test(NAME VST3Processing.All COMMAND VST3ProcessingTests all ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.DeckProcessingChangesAudio COMMAND VST3ProcessingTests deck ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.ParameterLatency COMMAND VST3ProcessingTests parameters ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.MissingRealFixtureManifest COMMAND VST3ProcessingTests missing-real-manifest ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.MasterChainStateReload COMMAND VST3ProcessingTests master-state ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3Processing.SmokeSurface COMMAND VST3ProcessingTests smoke ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
if(DECKFLAXIA_HAS_JUCE AND TARGET DeckflaxiaRealVst3Fixture)
    add_test(NAME VST3Processing.AppSmoke COMMAND Deckflaxia --vst3-processing-smoke-test --chain deck-a --fixtures ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR} --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-vst3-fixture/task-7-real-vst3.wav --exit-after-init)
    add_test(NAME VST3Processing.RealFixture COMMAND VST3ProcessingTests real-fixture ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR})
    add_test(NAME VST3Processing.RealProcessing COMMAND VST3ProcessingTests real-processing ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR})
    add_test(NAME VST3Processing.RealParameters COMMAND VST3ProcessingTests real-parameters ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR})
    add_test(NAME VST3Processing.RealState COMMAND VST3ProcessingTests real-state ${DECKFLAXIA_REAL_VST3_FIXTURE_OUTPUT_DIR})
elseif(NOT DECKFLAXIA_HAS_JUCE)
    add_test(NAME VST3Processing.AppSmokeUnavailable COMMAND Deckflaxia --vst3-processing-smoke-test --chain deck-a --fixtures ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-10-deck-vst3.wav --exit-after-init)
endif()

add_executable(VST3EditorUiTests
    tests/ui/VST3EditorUiTests.cpp
)
target_link_libraries(VST3EditorUiTests PRIVATE DeckflaxiaUiShell DeckflaxiaDecks DeckflaxiaPlugins DeckflaxiaPersistence)
target_compile_features(VST3EditorUiTests PRIVATE cxx_std_17)

add_test(NAME VST3EditorUi.All COMMAND VST3EditorUiTests all ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.ModelControls COMMAND VST3EditorUiTests model ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.NoEditorGenericFallback COMMAND VST3EditorUiTests no-editor ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.EmptyPluginChain COMMAND VST3EditorUiTests empty ${DECKFLAXIA_PLUGIN_FIXTURE_DIR})
add_test(NAME VST3EditorUi.AppSmoke COMMAND Deckflaxia --vst3-editor-smoke-test --fixtures ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-11-editor.png --exit-after-init)


add_executable(PluginSandboxTests
    tests/plugins/PluginSandboxTests.cpp
)
target_link_libraries(PluginSandboxTests PRIVATE DeckflaxiaPlugins DeckflaxiaPersistence)
target_compile_features(PluginSandboxTests PRIVATE cxx_std_17)

add_test(NAME PluginSandbox.All COMMAND PluginSandboxTests all ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.HelperLimit COMMAND PluginSandboxTests limit ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.CrashRestartBlacklist COMMAND PluginSandboxTests crash ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.ControlIpc COMMAND PluginSandboxTests control ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.AudioRoundtrip COMMAND PluginSandboxTests audio ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.SmokeSurface COMMAND PluginSandboxTests smoke ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} $<TARGET_FILE:DeckflaxiaPluginSandboxHelper>)
add_test(NAME PluginSandbox.AppSmoke COMMAND Deckflaxia --plugin-sandbox-smoke-test --kill-helper-after-ms 500 --fixtures ${DECKFLAXIA_PLUGIN_FIXTURE_DIR} --exit-after-init)
add_test(NAME PluginSandbox.HelperPackaging COMMAND ${CMAKE_COMMAND}
    -DDECKFLAXIA_APP_PATH=$<TARGET_FILE:Deckflaxia>
    -DDECKFLAXIA_HELPER_PATH=$<TARGET_FILE_DIR:Deckflaxia>/$<TARGET_FILE_NAME:DeckflaxiaPluginSandboxHelper>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginSandboxPackagingCheck.cmake)

add_executable(PlayableSmokeTests
    tests/PlayableSmokeTests.cpp
)
target_link_libraries(PlayableSmokeTests PRIVATE DeckflaxiaAlphaSmoke)
target_compile_features(PlayableSmokeTests PRIVATE cxx_std_17)

add_test(NAME PlayableSmoke.All COMMAND PlayableSmokeTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME PlayableSmoke.Workflow COMMAND PlayableSmokeTests workflow ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME PlayableSmoke.Restart COMMAND PlayableSmokeTests restart ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-13-restart-test.db)
add_test(NAME ProductionWorkflowSmoke.Library COMMAND PlayableSmokeTests production ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-16-restart-test.db)
add_test(NAME ScopeAudit.Library COMMAND PlayableSmokeTests scope-audit ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/task-16-restart-test.db)
add_test(NAME PlayableSmoke.AppSmoke COMMAND Deckflaxia --playable-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --screenshot ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-playable.png --render ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-mix.wav --exit-after-init)
add_test(NAME PlayableSmoke.AppRestart COMMAND Deckflaxia --playable-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --db ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-restart.db --exit-after-init)
add_test(NAME PlayableSmoke.AppRestartRestore COMMAND Deckflaxia --playable-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --db ${CMAKE_CURRENT_SOURCE_DIR}/.omo/evidence/real-playable-juce/task-13-restart.db --expect-restored-session --exit-after-init)
set_tests_properties(PlayableSmoke.AppRestartRestore PROPERTIES DEPENDS PlayableSmoke.AppRestart)
add_test(NAME ProductionWorkflowSmoke.AppSmoke COMMAND Deckflaxia --production-dj-workflow-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --exit-after-init)
add_test(NAME ScopeAudit.AppSmoke COMMAND Deckflaxia --scope-audit --forbid "Windows,recording,smart playlists,samplers,streaming,DVS,timecode,Rekordbox,Serato,cloud,accounts,marketplace,per-plugin sandbox,embedded plugin editor" --paths src tests docs cmake)
set_tests_properties(ProductionWorkflowSmoke.Library ScopeAudit.Library ProductionWorkflowSmoke.AppSmoke ScopeAudit.AppSmoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(PerformanceHardeningTests
    tests/PerformanceHardeningTests.cpp
)
target_link_libraries(PerformanceHardeningTests PRIVATE DeckflaxiaAlphaSmoke DeckflaxiaDecks DeckflaxiaPlugins DeckflaxiaPersistence)
target_compile_features(PerformanceHardeningTests PRIVATE cxx_std_17)

add_test(NAME PerformanceHardening.All COMMAND PerformanceHardeningTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.Budgets COMMAND PerformanceHardeningTests budgets ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.Faults COMMAND PerformanceHardeningTests faults ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME PerformanceHardening.AppSmoke COMMAND Deckflaxia --performance-smoke-test --fixtures ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR} --sample-rate 48000 --buffer-size 512 --exit-after-init)
set_tests_properties(PerformanceHardening.All PerformanceHardening.Budgets PerformanceHardening.Faults PerformanceHardening.AppSmoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(AlphaSmokeTests
    tests/AlphaSmokeTests.cpp
)
target_link_libraries(AlphaSmokeTests PRIVATE DeckflaxiaAlphaSmoke)
target_compile_features(AlphaSmokeTests PRIVATE cxx_std_17)

add_test(NAME AlphaSmoke.All COMMAND AlphaSmokeTests)
add_test(NAME AlphaSmoke.EndToEnd COMMAND AlphaSmokeTests end-to-end)

add_executable(EdgeCaseTests
    tests/EdgeCaseTests.cpp
)
target_link_libraries(EdgeCaseTests PRIVATE DeckflaxiaAlphaSmoke)
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

add_test(NAME Fixtures.All COMMAND FixtureTests all ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME Fixtures.Generate COMMAND FixtureTests generate ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})
add_test(NAME Fixtures.CorruptAudio COMMAND FixtureTests corrupt ${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_DIR})

set_tests_properties(Fixtures.Generate PROPERTIES FIXTURES_SETUP DeckflaxiaDjWorkflowFixtures)

set(DECKFLAXIA_DJ_WORKFLOW_FIXTURE_TESTS
    JuceAudioDeck.All
    JuceAudioDeck.Errors
    JuceAudioDeck.FixtureRender
    TimeStretch.All
    TimeStretch.TempoSync
    TimeStretch.PitchLock
    TimeStretch.OverloadFallback
    TimeStretch.TempoSyncSmoke
    TimeStretch.OverloadSmoke
    MixerMidi.All
    MixerMidi.Crossfader
    MixerMidi.SmokeSurface
    MixerMidi.AppSmoke
    BrowserWaveformBeatgrid.All
    BrowserWaveformBeatgrid.ImportClassification
    BrowserWaveformBeatgrid.WaveformPrimitive
    BrowserWaveformBeatgrid.BeatgridPersistence
    BrowserWaveformBeatgrid.InvalidImportRows
    JuceUiCommandAdapter.All
    JuceUi.BrowserControls
    PlayableSmoke.All
    PlayableSmoke.Workflow
    PlayableSmoke.Restart
    ProductionWorkflowSmoke.Library
    PlayableSmoke.AppSmoke
    PlayableSmoke.AppRestart
    PlayableSmoke.AppRestartRestore
    ProductionWorkflowSmoke.AppSmoke
    PerformanceHardening.All
    PerformanceHardening.Budgets
    PerformanceHardening.Faults
    PerformanceHardening.AppSmoke
)
if(DECKFLAXIA_HAS_JUCE)
    list(APPEND DECKFLAXIA_DJ_WORKFLOW_FIXTURE_TESTS JuceAudioDeck.AppSmoke)
endif()
set_tests_properties(${DECKFLAXIA_DJ_WORKFLOW_FIXTURE_TESTS} PROPERTIES FIXTURES_REQUIRED DeckflaxiaDjWorkflowFixtures)
