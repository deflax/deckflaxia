if(NOT DEFINED DECKFLAXIA_APP_PATH OR DECKFLAXIA_APP_PATH STREQUAL "")
    message(FATAL_ERROR "DECKFLAXIA_APP_PATH is required for plugin sandbox helper packaging checks")
endif()

if(NOT DEFINED DECKFLAXIA_HELPER_PATH OR DECKFLAXIA_HELPER_PATH STREQUAL "")
    message(FATAL_ERROR "DECKFLAXIA_HELPER_PATH is required for plugin sandbox helper packaging checks")
endif()

if(NOT EXISTS "${DECKFLAXIA_APP_PATH}")
    message(FATAL_ERROR "Deckflaxia executable was not built at ${DECKFLAXIA_APP_PATH}")
endif()

if(NOT EXISTS "${DECKFLAXIA_HELPER_PATH}")
    message(FATAL_ERROR "DeckflaxiaPluginSandboxHelper executable was not built at ${DECKFLAXIA_HELPER_PATH}")
endif()

get_filename_component(deckflaxia_app_dir "${DECKFLAXIA_APP_PATH}" DIRECTORY)
get_filename_component(deckflaxia_helper_dir "${DECKFLAXIA_HELPER_PATH}" DIRECTORY)

if(NOT deckflaxia_app_dir STREQUAL deckflaxia_helper_dir)
    message(FATAL_ERROR "DeckflaxiaPluginSandboxHelper must be locatable beside Deckflaxia for smoke tests: app dir=${deckflaxia_app_dir}, helper dir=${deckflaxia_helper_dir}")
endif()

execute_process(
    COMMAND "${DECKFLAXIA_HELPER_PATH}" --helper-smoke
    RESULT_VARIABLE deckflaxia_helper_result
    OUTPUT_VARIABLE deckflaxia_helper_output
    ERROR_VARIABLE deckflaxia_helper_error
)

if(NOT deckflaxia_helper_result EQUAL 0)
    message(FATAL_ERROR "DeckflaxiaPluginSandboxHelper --helper-smoke failed with exit ${deckflaxia_helper_result}: ${deckflaxia_helper_error}")
endif()

if(NOT deckflaxia_helper_output MATCHES "DeckflaxiaPluginSandboxHelper: deterministic helper executable available")
    message(FATAL_ERROR "DeckflaxiaPluginSandboxHelper --helper-smoke did not emit the expected readiness line: ${deckflaxia_helper_output}")
endif()

message(STATUS "DeckflaxiaPluginSandboxHelper packaging check passed: ${DECKFLAXIA_HELPER_PATH}")
