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
# four typefaces and the two pieces of hardware it is screwed together with.
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
    ui/assets/fonts/ChakraPetchSemiBold.ttf
    ui/assets/fonts/RajdhaniMedium.ttf
    ui/assets/fonts/RajdhaniSemiBold.ttf
    ui/assets/fonts/IBMPlexMonoMedium.ttf)

target_sources(RhinoForge PRIVATE
    core/ForgeCore.h
    core/ForgeTableStore.h
    core/ForgeWavetable.h
    core/ForgeFx.h
    core/ForgeFxDsp.h
    core/ForgeWarp.h
    ui/ForgeLayout.h
    ui/ForgeChrome.h
    ui/ForgePanels.h
    ui/ForgeType.h
    ui/ForgeTablePanel.h
    ui/ForgeVisuals.h
    ui/ForgeFxDisplay.h
    src/ForgeProcessor.cpp
    src/ForgeProcessor.h
    src/ForgeEditor.cpp
    src/ForgeEditor.h)
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
    target_sources(RhinoForgeTests PRIVATE
        tests/ForgeTests.cpp
        core/ForgeCore.h
        core/ForgeTableStore.h
        core/ForgeWavetable.h
    core/ForgeFx.h
    core/ForgeFxDsp.h
    core/ForgeWarp.h
        ui/ForgeLayout.h
        ui/ForgeChrome.h
        ui/ForgePanels.h
        ui/ForgeType.h
        ui/ForgeTablePanel.h
        ui/ForgeVisuals.h
    ui/ForgeFxDisplay.h
        src/ForgeProcessor.cpp
        src/ForgeProcessor.h
        src/ForgeEditor.cpp
        src/ForgeEditor.h)
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
    # One binary, three cases selected by argument, as Rhino's own tests do.
    add_test(NAME forge_layout COMMAND RhinoForgeTests --layout)
    add_test(NAME forge_presets COMMAND RhinoForgeTests --presets)
    add_test(NAME forge_engine COMMAND RhinoForgeTests --engine)
endif()
