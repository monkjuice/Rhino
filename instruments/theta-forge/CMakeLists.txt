cmake_minimum_required(VERSION 3.24)
project(ThetaForge VERSION 0.1.0 LANGUAGES CXX)

# Sources are explicit. Suppressing ZERO_CHECK avoids a CMake/MSBuild generated
# project-reference failure seen on this Windows toolchain after the check
# succeeds, before the actual plugin target is invoked.
set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(THETA_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../native/.deps/juce" CACHE PATH "JUCE source checkout")
if (NOT EXISTS "${THETA_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set THETA_JUCE_DIR to a JUCE checkout, or fetch Theta's native dependencies first.")
endif()

add_subdirectory("${THETA_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

juce_add_plugin(ThetaForge
    COMPANY_NAME "Theta"
    BUNDLE_ID "com.theta.forge"
    PLUGIN_MANUFACTURER_CODE ThTa
    PLUGIN_CODE Frge
    FORMATS VST3 Standalone
    PRODUCT_NAME "Theta Forge"
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

# The panel's wordmark. forge_logo.png is stored at the size the panel draws it
# so paint() blits rather than rescaling a master on every repaint; the
# full-resolution originals sit beside it for packaging and store art.
juce_add_binary_data(ThetaForgeAssets SOURCES ui/assets/forge_logo.png)

target_sources(ThetaForge PRIVATE
    core/ForgeCore.h
    ui/ForgeLayout.h
    ui/ForgeVisuals.h
    src/ForgeProcessor.cpp
    src/ForgeProcessor.h
    src/ForgeEditor.cpp
    src/ForgeEditor.h)
target_link_libraries(ThetaForge PRIVATE
    ThetaForgeAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(ThetaForge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Forge ships VST3 only. JUCE's VST2/VST3 collision guard is therefore not
    # applicable, while the explicit ParameterID version hints above retain
    # stable host automation IDs.
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)

include(CTest)
if (BUILD_TESTING)
    juce_add_console_app(ThetaForgeTests PRODUCT_NAME "Theta Forge Tests")
    set_target_properties(ThetaForgeTests PROPERTIES EXCLUDE_FROM_ALL FALSE)
    target_sources(ThetaForgeTests PRIVATE
        tests/ForgeTests.cpp
        core/ForgeCore.h
        ui/ForgeLayout.h
        ui/ForgeVisuals.h
        src/ForgeProcessor.cpp
        src/ForgeProcessor.h
        src/ForgeEditor.cpp
        src/ForgeEditor.h)
    target_link_libraries(ThetaForgeTests PRIVATE
        ThetaForgeAssets
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    target_compile_definitions(ThetaForgeTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        THETA_FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)
    # One binary, three cases selected by argument, as Theta's own tests do.
    add_test(NAME forge_layout COMMAND ThetaForgeTests --layout)
    add_test(NAME forge_presets COMMAND ThetaForgeTests --presets)
    add_test(NAME forge_engine COMMAND ThetaForgeTests --engine)
endif()
