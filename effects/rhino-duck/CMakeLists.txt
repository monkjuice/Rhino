cmake_minimum_required(VERSION 3.24)
project(RhinoDuck VERSION 0.1.0 LANGUAGES CXX)

# Sources are explicit. Suppressing ZERO_CHECK avoids a CMake/MSBuild generated
# project-reference failure seen on this Windows toolchain, as Forge's project
# does; re-run the configure line after editing this file.
set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RHINO_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../native/.deps/juce" CACHE PATH "JUCE source checkout")
if (NOT EXISTS "${RHINO_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set RHINO_JUCE_DIR to a JUCE checkout, or fetch Rhino's native dependencies first.")
endif()

add_subdirectory("${RHINO_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

# Every Duck source compiles against a precompiled header of JUCE and the
# standard library (src/DuckPch.h). JUCE's own module sources are kept away
# from it: they refuse to build with a module header already included, and
# they arrive through the JUCE targets' interface sources rather than
# target_sources, so they are named by where they live.
file(GLOB _juce_unity_sources
    "${RHINO_JUCE_DIR}/modules/*/*.cpp"
    "${RHINO_JUCE_DIR}/modules/*/*.c"
    "${RHINO_JUCE_DIR}/modules/*/*.mm")
set_source_files_properties(${_juce_unity_sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

function(duck_use_precompiled_header target)
    target_precompile_headers(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/DuckPch.h>")
endfunction()

juce_add_plugin(RhinoDuck
    COMPANY_NAME "Rhino"
    BUNDLE_ID "com.rhino.duck"
    PLUGIN_MANUFACTURER_CODE Rhno
    PLUGIN_CODE Duck
    FORMATS VST3 Standalone
    PRODUCT_NAME "Rhino Duck"
    IS_SYNTH FALSE
    # The MIDI trigger: a note restarts the curve.
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics
    # The pinned JUCE/CMake Visual Studio generator's manifest-helper project
    # fails before invoking the VST3 target; the manifest is optional for
    # development hosts and goes back on for release packaging.
    VST3_AUTO_MANIFEST FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

# The three typefaces, the same subsets Forge carries (ui/assets/fonts/README.md).
juce_add_binary_data(RhinoDuckAssets SOURCES
    ui/assets/fonts/ChakraPetchSemiBold.ttf
    ui/assets/fonts/RajdhaniSemiBold.ttf
    ui/assets/fonts/IBMPlexMonoMedium.ttf)

# The plugin itself, compiled into the plugin and into the test binary alike,
# named once so the two cannot drift.
set(duck_sources
    core/DuckCurves.h
    core/DuckEngine.h
    ui/DuckTheme.h
    ui/DuckLayout.h
    ui/DuckControls.h
    src/DuckPch.h
    src/DuckProcessor.h
    src/DuckProcessor.cpp
    src/DuckParameters.cpp
    src/DuckScopeView.h
    src/DuckScopeView.cpp
    src/DuckEditor.h
    src/DuckEditor.cpp)

target_sources(RhinoDuck PRIVATE ${duck_sources})
duck_use_precompiled_header(RhinoDuck)
target_link_libraries(RhinoDuck PRIVATE
    RhinoDuckAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(RhinoDuck PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

include(CTest)
if (BUILD_TESTING)
    juce_add_console_app(RhinoDuckTests PRODUCT_NAME "Rhino Duck Tests")
    set_target_properties(RhinoDuckTests PROPERTIES EXCLUDE_FROM_ALL FALSE)
    if (MSVC)
        target_link_options(RhinoDuckTests PRIVATE /STACK:8388608)
    endif()

    # One area per file, and the file's name is the argument that selects it
    # and the CTest case that runs it. Keep this list in step with
    # tests/DuckSuites.h and the table in tests/DuckTestMain.cpp.
    set(duck_test_areas
        curves
        engine
        processor
        layout)

    target_sources(RhinoDuckTests PRIVATE
        tests/DuckTestMain.cpp
        tests/DuckSuites.h
        tests/DuckTestSupport.h
        tests/DuckTestSupport.cpp
        tests/DuckTestTools.h
        tests/DuckTestTools.cpp
        tests/DuckTestsCurves.cpp
        tests/DuckTestsEngine.cpp
        tests/DuckTestsProcessor.cpp
        tests/DuckTestsLayout.cpp
        ${duck_sources})
    duck_use_precompiled_header(RhinoDuckTests)
    target_link_libraries(RhinoDuckTests PRIVATE
        RhinoDuckAssets
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(RhinoDuckTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

    foreach(area IN LISTS duck_test_areas)
        add_test(NAME duck_${area} COMMAND RhinoDuckTests --${area})
    endforeach()
endif()
