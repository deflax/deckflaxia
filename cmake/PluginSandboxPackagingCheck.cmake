if(NOT DEFINED DJAPP_APP_PATH OR DJAPP_APP_PATH STREQUAL "")
    message(FATAL_ERROR "DJAPP_APP_PATH is required for plugin sandbox helper packaging checks")
endif()

if(NOT DEFINED DJAPP_HELPER_PATH OR DJAPP_HELPER_PATH STREQUAL "")
    message(FATAL_ERROR "DJAPP_HELPER_PATH is required for plugin sandbox helper packaging checks")
endif()

if(NOT EXISTS "${DJAPP_APP_PATH}")
    message(FATAL_ERROR "DJApp executable was not built at ${DJAPP_APP_PATH}")
endif()

if(NOT EXISTS "${DJAPP_HELPER_PATH}")
    message(FATAL_ERROR "DJAppPluginSandboxHelper executable was not built at ${DJAPP_HELPER_PATH}")
endif()

get_filename_component(djapp_app_dir "${DJAPP_APP_PATH}" DIRECTORY)
get_filename_component(djapp_helper_dir "${DJAPP_HELPER_PATH}" DIRECTORY)

if(NOT djapp_app_dir STREQUAL djapp_helper_dir)
    message(FATAL_ERROR "DJAppPluginSandboxHelper must be locatable beside DJApp for smoke tests: app dir=${djapp_app_dir}, helper dir=${djapp_helper_dir}")
endif()

execute_process(
    COMMAND "${DJAPP_HELPER_PATH}" --helper-smoke
    RESULT_VARIABLE djapp_helper_result
    OUTPUT_VARIABLE djapp_helper_output
    ERROR_VARIABLE djapp_helper_error
)

if(NOT djapp_helper_result EQUAL 0)
    message(FATAL_ERROR "DJAppPluginSandboxHelper --helper-smoke failed with exit ${djapp_helper_result}: ${djapp_helper_error}")
endif()

if(NOT djapp_helper_output MATCHES "DJAppPluginSandboxHelper: deterministic helper executable available")
    message(FATAL_ERROR "DJAppPluginSandboxHelper --helper-smoke did not emit the expected readiness line: ${djapp_helper_output}")
endif()

message(STATUS "DJAppPluginSandboxHelper packaging check passed: ${DJAPP_HELPER_PATH}")
