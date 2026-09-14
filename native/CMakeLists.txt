cmake_minimum_required(VERSION 3.24)
project(ThetaNative VERSION 0.1.0 LANGUAGES C CXX)

# Every application source is explicitly listed below, so Visual Studio does
# not need CMake's generated ZERO_CHECK target to discover new files.  Recent
# CMake/MSBuild combinations in this workspace report a parent-target failure
# immediately after ZERO_CHECK despite the check itself succeeding.
set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/juce/CMakeLists.txt")
    message(FATAL_ERROR "Run python native/scripts/fetch-dependencies.py first")
endif()
add_subdirectory(.deps/juce EXCLUDE_FROM_ALL)
add_subdirectory(.deps/tracktion EXCLUDE_FROM_ALL)

juce_add_gui_app(ThetaNative
    PRODUCT_NAME "Theta"
    BUNDLE_ID "com.theta.native"
    ICON_BIG assets/theta_app_icon_1024.png
    ICON_SMALL assets/theta_app_icon_256.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record audio into your songs"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
juce_add_binary_data(ThetaNativeAssets
    SOURCES
        assets/HandClap-01_09.flac
        assets/TR808/TR808Kick.wav
        assets/TR808/TR808Snare.wav
        assets/TR808/TR808ClosedHat.wav
        assets/TR808/TR808OpenHat.wav
        assets/TR808/TR808LowTom.wav
        assets/TR808/TR808MidTom.wav
        assets/TR808/TR808HighTom.wav)
# Sources are listed explicitly: a new .cpp must be added here or it will not compile.
# A class may span several translation units (Session, Arrangement); keep those grouped.
target_sources(ThetaNative PRIVATE
    # Session
    src/Session.cpp
    src/SessionInternal.cpp
    src/SessionNotes.cpp
    src/SessionPresets.cpp
    src/SessionPatches.cpp
    src/SessionDevices.cpp
    src/SessionTracks.cpp
    src/SessionSlots.cpp
    src/SessionMixer.cpp
    src/SessionClips.cpp
    src/SessionSamples.cpp
    src/SessionAutomation.cpp
    src/SessionTransport.cpp
    src/SessionExternalPlugins.cpp
    src/DeviceMacros.cpp
    # UI
    src/StepGrid.cpp
    src/StepGridPainter.cpp
    src/StepGridGestures.cpp
    src/StepGridEditing.cpp
    src/Arrangement.cpp
    src/ArrangementGeometry.cpp
    src/ArrangementPainter.cpp
    src/ArrangementSync.cpp
    src/ArrangementGestures.cpp
    src/ArrangementDrops.cpp
    src/SessionView.cpp
    src/SessionViewPainter.cpp
    src/SessionViewGestures.cpp
    src/BrowserPanel.cpp
    src/DeviceEditorPanel.cpp
    src/DeviceRack.cpp
    src/Playhead.cpp
    # Devices
    src/UtilityDevice.cpp
    src/DrumDevice.cpp
    src/ThetaArpDevice.cpp
    src/ThetaBloomDevice.cpp
    src/ThetaSpaceDevice.cpp
    src/ThetaWaveDevice.cpp
    # App
    src/Main.cpp
    src/TransportDisplay.cpp
    src/ProjectFiles.cpp
    # Tests
    src/tests/SelfTest.cpp
    src/tests/Pattern/DeviceRackTest.cpp
    src/tests/Pattern/WorkflowTest.cpp
    src/tests/Arrangement/ClipGeometryTest.cpp
    src/tests/Arrangement/WorkflowTest.cpp)
target_include_directories(ThetaNative PRIVATE src)
target_compile_definitions(ThetaNative PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0
    THETA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JUCE_MODAL_LOOPS_PERMITTED=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(ThetaNative PRIVATE
    ThetaNativeAssets
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    juce::juce_audio_utils juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(ThetaNative PRIVATE /bigobj)
endif()
enable_testing()
add_test(NAME native_arrangement_geometry COMMAND ThetaNative --arrangement-geometry-test)
set_tests_properties(native_arrangement_geometry PROPERTIES TIMEOUT 10)
add_test(NAME native_device_correctness COMMAND ThetaNative --self-test)
set_tests_properties(native_device_correctness PROPERTIES TIMEOUT 30)
add_test(NAME native_pattern_workflow COMMAND ThetaNative --pattern-test)
set_tests_properties(native_pattern_workflow PROPERTIES TIMEOUT 30)
add_test(NAME native_arrangement_workflow COMMAND ThetaNative --arrangement-test)
set_tests_properties(native_arrangement_workflow PROPERTIES TIMEOUT 30)
add_test(NAME native_startup_lifecycle COMMAND ThetaNative --startup-test)
set_tests_properties(native_startup_lifecycle PROPERTIES TIMEOUT 30)
