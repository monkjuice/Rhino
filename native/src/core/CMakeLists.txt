# Foundation shared by the application and the device library.  Nothing here
# may depend on Session, the UI, or Tracktion -- only JUCE -- so that both of
# those can link it without depending on each other.
#
# Three folders, by responsibility: dsp/ holds the engines behind Rhino's own
# devices, dj/ the booth's engine, content/ the library and its file formats.
# Only this root is public, so an include names the folder it wants:
# #include "dsp/EqFilter.h", #include "dj/DjEngine.h".  Sources are listed
# explicitly, as in the parent CMakeLists.
add_library(RhinoCore STATIC
    SystemUsage.cpp
    content/ContentLibrary.cpp
    content/DrumKitFile.cpp
    content/DevicePreset.cpp
    dsp/EqEngine.cpp
    dsp/SpectrumAnalyser.cpp
    dsp/PitchTracker.cpp
    dsp/PsolaShifter.cpp
    dsp/AutoTuneEngine.cpp
    dsp/VocoderEngine.cpp
    dsp/FmEngine.cpp
    dsp/DrumSynth.cpp
    dsp/DrumRackEngine.cpp
    dsp/DrumSlicer.cpp
    dsp/Oversampler.cpp
    dsp/SaturatorEngine.cpp
    dsp/AmpEngine.cpp
    dsp/ReduxEngine.cpp
    dsp/ErosionEngine.cpp
    dsp/TunerEngine.cpp
    dj/DjAnalysis.cpp
    dj/DjReleaseQueue.cpp
    dj/DjTrackPort.cpp
    dj/DjDeck.cpp
    dj/DjDeckTransport.cpp
    dj/DjDeckLoops.cpp
    dj/DjDeckRender.cpp
    dj/DjMixer.cpp
    dj/DjEngine.cpp
    dj/DjEngineClock.cpp
    dj/DjEngineMix.cpp)
target_include_directories(RhinoCore PUBLIC .)
target_link_libraries(RhinoCore
    PUBLIC juce::juce_core
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(RhinoCore PRIVATE RHINO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
