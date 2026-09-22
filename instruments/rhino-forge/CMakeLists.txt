cmake_minimum_required(VERSION 3.24)
project(RhinoForge VERSION 0.1.0 LANGUAGES CXX)

# Sources are explicit. Suppressing ZERO_CHECK avoids a CMake/MSBuild generated
# project-reference failure seen on this Windows toolchain after the check
# succeeds, before the actual plugin target is invoked.
set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RHINO_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../native/.deps/juce" CACHE PATH "JUCE source checkout")
if (NOT EXISTS "${RHINO_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set RHINO_JUCE_DIR to a JUCE checkout, or fetch Rhino's native dependencies first.")
endif()

add_subdirectory("${RHINO_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

# Forge's own sources compile against a precompiled header, so splitting a file
# in two costs roughly what the new code costs rather than another full parse of
# JUCE. See src/ForgePch.h for what is in it and what is deliberately not.
#
# Two things have to be kept away from it. JUCE compiles its module .cpp files
# straight into every target that links it, and each one refuses to build if a
# JUCE module header has already been included — which a forced-include
# precompiled header does by definition. And the header is generated once per
# enabled language: JUCE enables C here, so without the CXX guard CMake builds a
# C one too, which fails JUCE's own "requires C++17" check.
#
# The module sources arrive through the JUCE targets' INTERFACE_SOURCES rather
# than through target_sources, so they are named here by where they live: one
# unity file per module directory, which is JUCE's own layout.
file(GLOB _juce_unity_sources
    "${RHINO_JUCE_DIR}/modules/*/*.cpp"
    "${RHINO_JUCE_DIR}/modules/*/*.c"
    "${RHINO_JUCE_DIR}/modules/*/*.mm")
set_source_files_properties(${_juce_unity_sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

function(forge_use_precompiled_header target)
    target_precompile_headers(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/ForgePch.h>")
endfunction()

juce_add_plugin(RhinoForge
    COMPANY_NAME "Rhino"
    BUNDLE_ID "com.rhino.forge"
    PLUGIN_MANUFACTURER_CODE Rhno
    PLUGIN_CODE Frge
    FORMATS VST3 Standalone
    PRODUCT_NAME "Rhino Forge"
    IS_SYNTH TRUE
    ICON_BIG ui/assets/forge_app_icon_512.png
    ICON_SMALL ui/assets/forge_app_icon_256.png
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    # The pinned JUCE/CMake Visual Studio generator's manifest-helper project
    # fails before invoking the VST3 target. A VST3 manifest is optional for
    # development hosts; re-enable it explicitly for release packaging once
    # that upstream generator issue is resolved.
    VST3_AUTO_MANIFEST FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

# What the panel needs before it can read anything from disk: its wordmark, its
# four typefaces, the screws and the performance wheel skin.
#
# forge_logo.png is stored at the size the panel draws it so paint() blits
# rather than rescaling a master on every repaint; the full-resolution
# originals sit beside it for packaging and store art. The screws are 64px
# because that is comfortably more than the ~13px they are ever drawn at, even
# on a scaled display. The typefaces are subsets — see ui/assets/fonts/README.md
# for what was cut and how to re-cut them; unsubsetted they are 988 KB, and
# juce_add_binary_data expands an asset to roughly three bytes of C++ per byte.
juce_add_binary_data(RhinoForgeAssets SOURCES
    ui/assets/forge_logo.png
    ui/assets/screw_cross.png
    ui/assets/screw_dome.png
    ui/assets/performance_wheel.png
    ui/assets/fonts/ChakraPetchSemiBold.ttf
    ui/assets/fonts/RajdhaniMedium.ttf
    ui/assets/fonts/RajdhaniSemiBold.ttf
    ui/assets/fonts/IBMPlexMonoMedium.ttf)

# The synth itself, compiled into the plugin and into the test binary alike.
# Named once so the two cannot drift: a file the tests do not know about is a
# file nothing checks.
set(forge_sources
    core/ForgeVoiceParts.h
    core/ForgeLfoTable.h
    core/ForgeArp.h
    core/ForgeShapes.h
    core/ForgeMatrix.h
    core/ForgePatch.h
    core/ForgeCore.h
    core/ForgeFx.h
    core/ForgeFxDsp.h
    core/ForgeTableStore.h
    core/ForgeWarp.h
    core/ForgeWavetable.h
    ui/ForgeChrome.h
    ui/ForgeFxDisplay.h
    ui/ForgeModule.h
    ui/ForgeModules.h
    ui/ForgePlacement.h
    ui/ForgeControlBlock.h
    ui/ForgeLayout.h
    ui/ForgePanels.h
    ui/ForgePerformanceWheel.h
    ui/ForgeTablePanel.h
    ui/ForgeTooltips.h
    ui/ForgeType.h
    ui/ForgeStyle.h
    ui/ForgeLookAndFeel.h
    ui/ForgeDisplays.h
    ui/ForgeControls.h
    ui/ForgeMacroCell.h
    ui/ForgeFxVisuals.h
    ui/ForgeEnvelopeVisuals.h
    ui/ForgeFilterVisuals.h
    ui/ForgeVisuals.h
    src/ForgePch.h
    src/ForgeProcessor.cpp
    src/ForgeProcessor.h
    src/ForgeParameters.cpp
    src/ForgeProcessorState.cpp
    src/ForgeProcessorLfo.cpp
    src/ForgeEditor.cpp
    src/ForgeEditor.h
    src/ForgeEditorInternal.h
    src/ForgeEditorFx.cpp
    src/ForgeEditorInput.cpp
    src/ForgeEditorLayout.cpp
    src/ForgeEditorLfo.cpp
    src/ForgeEditorModulation.cpp
    src/ForgeEditorPages.cpp
    src/ForgeEditorPaint.cpp
    src/ForgeEditorWarp.cpp)

target_sources(RhinoForge PRIVATE ${forge_sources})
forge_use_precompiled_header(RhinoForge)
target_link_libraries(RhinoForge PRIVATE
    RhinoForgeAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(RhinoForge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Forge ships VST3 only. JUCE's VST2/VST3 collision guard is therefore not
    # applicable, while the explicit ParameterID version hints above retain
    # stable host automation IDs.
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

include(CTest)
if (BUILD_TESTING)
    juce_add_console_app(RhinoForgeTests PRODUCT_NAME "Rhino Forge Tests")
    set_target_properties(RhinoForgeTests PROPERTIES EXCLUDE_FROM_ALL FALSE)

    # One area per file, and the file's name is the argument that selects it and
    # the CTest case that runs it. Working on the effects means editing
    # tests/ForgeTestsFx.cpp and running `ctest -R forge_fx`: one translation
    # unit rebuilt, one area run. A milestone runs the lot, and `ctest -j 8`
    # runs them at once because no two areas share a Processor.
    #
    # Keep this list in step with tests/ForgeSuites.h, which declares each entry
    # point, and with the table in tests/ForgeTestMain.cpp, which maps a name to
    # a function. Adding an area is those three lines and the new file.
    set(forge_test_areas
        layout
        displays
        presets
        engine
        oscillator
        warp
        filter
        mixer
        fx
        modulation
        envelope
        lfo
        table
        voicing
        arp)

    target_sources(RhinoForgeTests PRIVATE
        tests/ForgeTestMain.cpp
        tests/ForgeSuites.h
        tests/ForgeTestSupport.cpp
        tests/ForgeTestSupport.h
        tests/ForgeTestSpectrum.cpp
        tests/ForgeTestSpectrum.h
        tests/ForgeTestRender.cpp
        tests/ForgeTestTools.cpp
        tests/ForgeTestTools.h
        tests/ForgeTestsLayout.cpp
        tests/ForgeTestsDisplays.cpp
        tests/ForgeTestsPresets.cpp
        tests/ForgeTestsEngine.cpp
        tests/ForgeTestsOscillator.cpp
        tests/ForgeTestsWarp.cpp
        tests/ForgeTestsFilter.cpp
        tests/ForgeTestsMixer.cpp
        tests/ForgeTestsFx.cpp
        tests/ForgeTestsModulation.cpp
        tests/ForgeTestsEnvelope.cpp
        tests/ForgeTestsLfo.cpp
        tests/ForgeTestsTable.cpp
        tests/ForgeTestsVoicing.cpp
        tests/ForgeTestsArp.cpp
        ${forge_sources})
    forge_use_precompiled_header(RhinoForgeTests)
    target_link_libraries(RhinoForgeTests PRIVATE
        RhinoForgeAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(RhinoForgeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        RHINO_FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

    foreach(area IN LISTS forge_test_areas)
        add_test(NAME forge_${area} COMMAND RhinoForgeTests --${area})
    endforeach()
endif()
