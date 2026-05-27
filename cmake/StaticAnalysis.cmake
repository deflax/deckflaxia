if(NOT DEFINED DECKFLAXIA_SOURCE_DIR)
    message(FATAL_ERROR "DECKFLAXIA_SOURCE_DIR is required")
endif()

find_program(DECKFLAXIA_CLANG_FORMAT clang-format)
find_program(DECKFLAXIA_CLANG_TIDY clang-tidy)

if(DECKFLAXIA_CLANG_FORMAT)
    message(STATUS "clang-format found: ${DECKFLAXIA_CLANG_FORMAT}")
else()
    message(STATUS "clang-format not found; install clang-format to run formatting checks with .clang-format.")
endif()

if(DECKFLAXIA_CLANG_TIDY)
    message(STATUS "clang-tidy found: ${DECKFLAXIA_CLANG_TIDY}")
else()
    message(STATUS "clang-tidy not found; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON and install clang-tidy for source linting.")
endif()

if(NOT DECKFLAXIA_CLANG_FORMAT OR NOT DECKFLAXIA_CLANG_TIDY)
    message(STATUS "Static-analysis target documents missing prerequisites and remains non-failing in bootstrap CI environments.")
endif()
