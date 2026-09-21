cmake_minimum_required(VERSION 3.24)
project(RhinoNative VERSION 0.1.0 LANGUAGES C CXX)

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

juce_add_gui_app(RhinoNative
    # The binary is deliberately not called Rhino.exe. Graphics drivers
    # carry per-executable profiles keyed on that name, and NVIDIA ships
    # one for Rhinoceros 3D, whose settings are applied to anything with
    # the same filename -- which corrupts this app's Direct2D repaints.
    # The product is still Rhino: the name shown to the user comes from
    # getApplicationName(), not from here.
    PRODUCT_NAME "RhinoDAW"
    BUNDLE_ID "com.rhino.native"
    ICON_BIG assets/rhino_app_icon_1024.png
    ICON_SMALL assets/rhino_app_icon_256.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Record audio into your songs"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)
# Only what the UI needs before it can read anything from disk is embedded.
# JUCE expands an asset to roughly three bytes of C++ per byte of data and
# recompiles all of it on a clean build, so audio content belongs in the
# content library under library/ instead -- see src/core/ContentLibrary.h.
juce_add_binary_data(RhinoNativeAssets
    SOURCES
        assets/fonts/InterRegular.ttf
        assets/fonts/InterSemiBold.ttf)
add_subdirectory(src/core)
add_subdirectory(src/devices)
# Sources are listed explicitly: a new .cpp must be added here or it will not compile.
# A class may span several translation units (Session, Arrangement); keep those grouped.
target_sources(RhinoNative PRIVATE
    # Session
    src/Session.cpp
    src/SessionInternal.cpp
    src/SessionNotes.cpp
    src/SessionPresets.cpp
    src/SessionPatches.cpp
    src/SessionDevices.cpp
    src/SessionTracks.cpp
    src/SessionGroups.cpp
    src/SessionSlots.cpp
    src/SessionMixer.cpp
    src/SessionClips.cpp
    src/SessionAudioClips.cpp
    src/SessionRegion.cpp
    src/SessionSamples.cpp
    src/SessionPreview.cpp
    src/SessionRecording.cpp
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
    src/ArrangementSelection.cpp
    src/ArrangementGroups.cpp
    src/ArrangementRename.cpp
    src/ArrangementAutomation.cpp
    src/ArrangementDrops.cpp
    src/SessionView.cpp
    src/SessionViewPainter.cpp
    src/SessionViewGestures.cpp
    src/BrowserPanel.cpp
    src/AudioClipPanel.cpp
    src/DeviceEditorPanel.cpp
    src/DeviceRack.cpp
    src/Playhead.cpp
    src/CountInClick.cpp
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
target_include_directories(RhinoNative PRIVATE src)
target_compile_definitions(RhinoNative PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0
    RHINO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JUCE_MODAL_LOOPS_PERMITTED=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)
target_link_libraries(RhinoNative PRIVATE
    RhinoNativeAssets
    RhinoCore
    RhinoDevices
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    juce::juce_audio_utils juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(RhinoNative PRIVATE /bigobj)
endif()
enable_testing()
add_test(NAME native_arrangement_geometry COMMAND RhinoNative --arrangement-geometry-test)
set_tests_properties(native_arrangement_geometry PROPERTIES TIMEOUT 10)
add_test(NAME native_device_correctness COMMAND RhinoNative --self-test)
set_tests_properties(native_device_correctness PROPERTIES TIMEOUT 30)
# The two workflow runs take 20-26 seconds each on this machine, so a 30 second
# limit is a 15% margin and a busy machine trips it. The limit is here to catch
# a render that never returns - which hangs outright rather than running slowly -
# so it can be generous without giving that up.
add_test(NAME native_pattern_workflow COMMAND RhinoNative --pattern-test)
set_tests_properties(native_pattern_workflow PROPERTIES TIMEOUT 120)
add_test(NAME native_arrangement_workflow COMMAND RhinoNative --arrangement-test)
set_tests_properties(native_arrangement_workflow PROPERTIES TIMEOUT 120)
add_test(NAME native_startup_lifecycle COMMAND RhinoNative --startup-test)
set_tests_properties(native_startup_lifecycle PROPERTIES TIMEOUT 30)
