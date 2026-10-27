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
        assets/fonts/InterSemiBold.ttf
        assets/arrangement_view.svg
        assets/dj_view.svg)
add_subdirectory(src/core)
add_subdirectory(src/devices)
# Sources are listed explicitly: a new .cpp must be added here or it will not compile.
# A class may span several translation units (Session, Arrangement); keep those grouped.
target_sources(RhinoNative PRIVATE
    # Session
    src/model/Session.cpp
    src/model/SessionInternal.cpp
    src/model/SessionNotes.cpp
    src/model/SessionPatternPresets.cpp
    src/model/SessionPatches.cpp
    src/model/SessionDevices.cpp
    src/model/SessionDevicePresets.cpp
    src/model/SessionDrums.cpp
    src/model/SessionTracks.cpp
    src/model/SessionTrackClipboard.cpp
    src/model/SessionGroups.cpp
    src/model/SessionSlots.cpp
    src/model/SessionMixer.cpp
    src/model/SessionReturns.cpp
    src/model/SessionMeters.cpp
    src/model/SessionClips.cpp
    src/model/SessionAudioClips.cpp
    src/model/SessionClipEnvelopes.cpp
    src/model/SessionWarp.cpp
    src/model/SessionRegion.cpp
    src/model/SessionRegionMove.cpp
    src/model/SessionMerge.cpp
    src/model/SessionBounce.cpp
    src/model/SessionSliceToMidi.cpp
    src/model/SessionExport.cpp
    src/model/SessionSamples.cpp
    src/model/SessionPreview.cpp
    src/model/SessionNoteAudition.cpp
    src/model/SessionNoteChop.cpp
    src/model/SessionNoteTools.cpp
    src/model/SessionClipLoop.cpp
    src/model/SessionRecording.cpp
    src/model/SessionMidiInput.cpp
    src/model/SessionAudioInput.cpp
    src/model/SessionSidechain.cpp
    src/model/SessionAutomation.cpp
    src/model/SessionTransport.cpp
    src/model/SessionExternalPlugins.cpp
    src/model/SessionDj.cpp
    src/model/SessionDjSources.cpp
    src/model/SessionDeviceMacros.cpp
    src/model/SessionMappedParameters.cpp
    src/model/SessionControllers.cpp
    src/model/SessionKeyboards.cpp
    # UI
    src/ui/notes/StepGrid.cpp
    src/ui/notes/StepGridPainter.cpp
    src/ui/notes/StepGridGestures.cpp
    src/ui/notes/StepGridEditing.cpp
    src/ui/notes/StepGridMenu.cpp
    src/ui/notes/StepGridVelocity.cpp
    src/ui/notes/StepGridVelocityGestures.cpp
    src/ui/notes/StepGridVelocityPainter.cpp
    src/ui/notes/StepGridTools.cpp
    src/ui/notes/NoteToolsPanel.cpp
    src/ui/arrangement/Arrangement.cpp
    src/ui/arrangement/ArrangementGeometry.cpp
    src/ui/arrangement/ArrangementPainter.cpp
    src/ui/arrangement/ArrangementClipPainter.cpp
    src/ui/arrangement/ArrangementSync.cpp
    src/ui/arrangement/ArrangementGestures.cpp
    src/ui/arrangement/ArrangementSelection.cpp
    src/ui/arrangement/ArrangementTrackClipboard.cpp
    src/ui/arrangement/ArrangementBounce.cpp
    src/ui/arrangement/ArrangementGroups.cpp
    src/ui/arrangement/ArrangementRename.cpp
    src/ui/arrangement/ArrangementAutomation.cpp
    src/ui/arrangement/ArrangementAutomationChooser.cpp
    src/ui/arrangement/ArrangementAutomationPaint.cpp
    src/ui/arrangement/ArrangementDrops.cpp
    src/ui/arrangement/MonitorSelector.cpp
    src/ui/arrangement/SendKnob.cpp
    src/ui/dj/DjView.cpp
    src/ui/dj/DjDeckPanel.cpp
    src/ui/dj/DjDeckPanelLayout.cpp
    src/ui/dj/DjDeckPanelMenus.cpp
    src/ui/dj/DjClipGrid.cpp
    src/ui/dj/DjDeckDisplay.cpp
    src/ui/dj/DjDeckDisplayWave.cpp
    src/ui/dj/DjMixerPanel.cpp
    src/ui/dj/DjMixerPanelStrips.cpp
    src/ui/dj/DjMixerPanelMaster.cpp
    src/ui/dj/DjControls.cpp
    src/ui/dj/DjPad.cpp
    src/ui/dj/DjFader.cpp
    src/ui/dj/DjMeter.cpp
    src/ui/dj/DjJogWheel.cpp
    src/ui/shared/ShapedText.cpp
    src/ui/shared/LevelMeter.cpp
    src/ui/browser/BrowserDrops.cpp
    src/ui/browser/BrowserPanel.cpp
    src/ui/audioclip/AudioClipPanel.cpp
    src/ui/audioclip/AudioClipWarp.cpp
    src/ui/audioclip/AudioClipEnvelope.cpp
    src/ui/rack/DeviceEditorPanel.cpp
    src/ui/rack/DeviceEditorPanelArp.cpp
    src/ui/rack/DeviceEditorPanelAutoTune.cpp
    src/ui/rack/DeviceEditorPanelEq.cpp
    src/ui/rack/DeviceEditorPanelVocoder.cpp
    src/ui/rack/DeviceEditorPanelTuner.cpp
    src/ui/rack/DeviceEditorPanelSidechain.cpp
    src/ui/rack/DeviceEditorPanelGenerated.cpp
    src/ui/rack/DeviceEditorPanelDisplay.cpp
    src/ui/rack/DeviceEditorPanelPresets.cpp
    src/ui/rack/DeviceEditorPanelDrums.cpp
    src/ui/rack/DeviceEditorPanelDrumParts.cpp
    src/ui/rack/DeviceEditorPanelDrumGestures.cpp
    src/ui/rack/DeviceEditorPanelDrumMenus.cpp
    src/ui/rack/DeviceEditorPanelDrumSample.cpp
    src/ui/rack/DeviceEditorPanelDrumSampleControls.cpp
    src/ui/rack/DeviceRack.cpp
    src/ui/rack/DrumRackWindow.cpp
    src/ui/preferences/PreferencesPanel.cpp
    src/ui/preferences/MidiPreferencesPanel.cpp
    src/ui/shared/Playhead.cpp
    src/model/CountInClick.cpp
    src/app/ComputerKeyboard.cpp
    # App
    src/app/Main.cpp
    src/app/MainWindow.cpp
    src/app/MainComponent.cpp
    src/app/MainComponentLayout.cpp
    src/app/MainComponentControlBar.cpp
    src/app/MainComponentPanes.cpp
    src/app/MainComponentCommands.cpp
    src/app/MainComponentTimer.cpp
    src/app/AppPlatform.cpp
    src/app/TransportDisplay.cpp
    src/app/ControlBarFields.cpp
    src/app/ControlBarIcons.cpp
    src/app/ProjectFiles.cpp
    src/app/LoadingScreen.cpp
    src/app/PushController.cpp
    src/app/PushControllerControls.cpp
    src/app/PushControllerSurface.cpp
    # Tests
    src/tests/SelfTest.cpp
    src/tests/ArpTest.cpp
    src/tests/DeviceConformance.cpp
    src/tests/AutoTuneTest.cpp
    src/tests/EqTest.cpp
    src/tests/VocoderTest.cpp
    src/tests/DriveDevicesTest.cpp
    src/tests/AmpTest.cpp
    src/tests/TunerTest.cpp
    src/tests/FmTest.cpp
    src/tests/DrumRackTest.cpp
    src/tests/DjTest.cpp
    src/tests/PresetTest.cpp
    src/tests/UiProfile.cpp
    src/tests/StressProfile.cpp
    src/tests/LayerLintTest.cpp
    src/tests/TestRunners.cpp
    src/tests/Pattern/DeviceRackTest.cpp
    src/tests/Pattern/DrumRackWindowTest.cpp
    src/tests/Pattern/WorkflowTest.cpp
    src/tests/Arrangement/ClipGeometryTest.cpp
    src/tests/PushProtocolTest.cpp
    src/tests/VelocityCurveTest.cpp
    src/tests/ClipEnvelopeTest.cpp
    src/tests/VelocityLaneTest.cpp
    src/tests/Arrangement/WorkflowTest.cpp)
target_include_directories(RhinoNative PRIVATE src)
target_compile_definitions(RhinoNative PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0
    RHINO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JUCE_MODAL_LOOPS_PERMITTED=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1)
target_link_libraries(RhinoNative PRIVATE
    RhinoNativeAssets
    RhinoCore
    RhinoDevices
    tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    juce::juce_audio_utils juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(RhinoNative PRIVATE /bigobj)
    # The main thread's stack, reserved at 8 MB as macOS gives it, rather than
    # Windows' 1 MB. The test runners are this binary, and each one holds every
    # scenario's locals in a single frame: two panels added to one scenario
    # overflowed it, which CTest reports as a SegFault before anything prints.
    # Reserved address space only; nothing is committed until it is touched.
    target_link_options(RhinoNative PRIVATE /STACK:8388608)
endif()
enable_testing()
add_test(NAME native_arrangement_geometry COMMAND RhinoNative --arrangement-geometry-test)
set_tests_properties(native_arrangement_geometry PROPERTIES TIMEOUT 10)
add_test(NAME native_device_correctness COMMAND RhinoNative --self-test)
set_tests_properties(native_device_correctness PROPERTIES TIMEOUT 30)
add_test(NAME native_device_conformance COMMAND RhinoNative --device-test)
set_tests_properties(native_device_conformance PROPERTIES TIMEOUT 120)
# The limit is here to catch a render that never returns - which hangs outright
# rather than running slowly - so it can be generous without giving that up.
# The pattern run takes about 36 seconds on this machine; the arrangement run,
# which carries every DJ scenario and their inline bounces, took 114 seconds on
# 2026-10-11, so its limit leaves room for the work still landing in it.
add_test(NAME native_pattern_workflow COMMAND RhinoNative --pattern-test)
set_tests_properties(native_pattern_workflow PROPERTIES TIMEOUT 120)
add_test(NAME native_arrangement_workflow COMMAND RhinoNative --arrangement-test)
set_tests_properties(native_arrangement_workflow PROPERTIES TIMEOUT 300)
add_test(NAME native_startup_lifecycle COMMAND RhinoNative --startup-test)
set_tests_properties(native_startup_lifecycle PROPERTIES TIMEOUT 30)
