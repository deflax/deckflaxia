if(NOT DEFINED DECKFLAXIA_APP_PATH OR DECKFLAXIA_APP_PATH STREQUAL "")
    message(FATAL_ERROR "DECKFLAXIA_APP_PATH is required")
endif()

if(NOT DEFINED DECKFLAXIA_SCREENSHOT_PATH OR DECKFLAXIA_SCREENSHOT_PATH STREQUAL "")
    message(FATAL_ERROR "DECKFLAXIA_SCREENSHOT_PATH is required")
endif()

file(REMOVE "${DECKFLAXIA_SCREENSHOT_PATH}")

execute_process(
    COMMAND "${DECKFLAXIA_APP_PATH}"
        --juce-ui-smoke-test
        --screenshot "${DECKFLAXIA_SCREENSHOT_PATH}"
        --exit-after-init
    RESULT_VARIABLE screenshot_result
    OUTPUT_VARIABLE screenshot_output
    ERROR_VARIABLE screenshot_error
)

set(screenshot_combined_output "${screenshot_output}${screenshot_error}")

if(NOT screenshot_result EQUAL 0)
    message(FATAL_ERROR "JuceUi screenshot command failed with ${screenshot_result}:\n${screenshot_combined_output}")
endif()

if(NOT screenshot_combined_output MATCHES "screenshot-source: live-main-component")
    message(FATAL_ERROR "JuceUi screenshot output missing live component marker:\n${screenshot_combined_output}")
endif()

if(NOT screenshot_combined_output MATCHES "screenshot-size: 1920x1080")
    message(FATAL_ERROR "JuceUi screenshot output missing 1920x1080 size marker:\n${screenshot_combined_output}")
endif()

if(NOT EXISTS "${DECKFLAXIA_SCREENSHOT_PATH}")
    message(FATAL_ERROR "JuceUi screenshot PNG was not created at ${DECKFLAXIA_SCREENSHOT_PATH}")
endif()

file(SIZE "${DECKFLAXIA_SCREENSHOT_PATH}" screenshot_size)
if(screenshot_size LESS_EQUAL 0)
    message(FATAL_ERROR "JuceUi screenshot PNG is empty at ${DECKFLAXIA_SCREENSHOT_PATH}")
endif()

file(READ "${DECKFLAXIA_SCREENSHOT_PATH}" png_header HEX LIMIT 24)
string(SUBSTRING "${png_header}" 0 16 png_signature)
if(NOT png_signature STREQUAL "89504e470d0a1a0a")
    message(FATAL_ERROR "JuceUi screenshot is not a PNG: signature=${png_signature}")
endif()

string(SUBSTRING "${png_header}" 24 8 png_chunk_type)
if(NOT png_chunk_type STREQUAL "49484452")
    message(FATAL_ERROR "JuceUi screenshot first PNG chunk is not IHDR: chunk=${png_chunk_type}")
endif()

string(SUBSTRING "${png_header}" 32 8 png_width_hex)
string(SUBSTRING "${png_header}" 40 8 png_height_hex)
math(EXPR png_width "0x${png_width_hex}" OUTPUT_FORMAT DECIMAL)
math(EXPR png_height "0x${png_height_hex}" OUTPUT_FORMAT DECIMAL)

if(NOT png_width EQUAL 1920 OR NOT png_height EQUAL 1080)
    message(FATAL_ERROR "JuceUi screenshot PNG dimensions are ${png_width}x${png_height}, expected 1920x1080")
endif()

message(STATUS "JuceUi screenshot verified: live-main-component 1920x1080 ${screenshot_size} bytes")
