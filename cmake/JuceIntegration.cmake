function(djapp_configure_juce target_name)
    set(DJAPP_HAS_JUCE OFF PARENT_SCOPE)

    if(DJAPP_USE_VENDORED_JUCE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
        add_subdirectory(third_party/JUCE)
        set(DJAPP_HAS_JUCE ON PARENT_SCOPE)
        juce_add_gui_app(${target_name}
            PRODUCT_NAME "DJApp"
            VERSION "${PROJECT_VERSION}"
        )
        target_link_libraries(${target_name}
            PRIVATE
                juce::juce_gui_extra
                juce::juce_audio_utils
        )
        return()
    endif()

    find_package(JUCE CONFIG QUIET)
    if(JUCE_FOUND)
        set(DJAPP_HAS_JUCE ON PARENT_SCOPE)
        juce_add_gui_app(${target_name}
            PRODUCT_NAME "DJApp"
            VERSION "${PROJECT_VERSION}"
        )
        target_link_libraries(${target_name}
            PRIVATE
                juce::juce_gui_extra
                juce::juce_audio_utils
        )
        return()
    endif()

    if(DJAPP_REQUIRE_JUCE)
        message(FATAL_ERROR "JUCE was not found, but DJAPP_REQUIRE_JUCE=ON. Provide JUCE by either installing/exporting the JUCE CMake package and configuring with -DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build, or by placing a licensed checkout at third_party/JUCE with CMakeLists.txt present. This project is AGPL-3.0-or-later and expects the JUCE AGPL/commercial license choice to be reviewed before distribution.")
    endif()

    message(STATUS "JUCE not found; configuring explicit bootstrap-only fallback target. Set DJAPP_REQUIRE_JUCE=ON to require JUCE, then provide -DCMAKE_PREFIX_PATH=/path/to/JUCE/install-or-build or a licensed third_party/JUCE checkout.")
endfunction()
