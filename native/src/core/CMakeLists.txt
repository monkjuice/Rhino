# Foundation shared by the application and the device library.  Nothing here
# may depend on Session, the UI, or Tracktion -- only JUCE -- so that both of
# those can link it without depending on each other.
add_library(RhinoCore STATIC
    ContentLibrary.cpp
    PitchTracker.cpp
    PsolaShifter.cpp
    AutoTuneEngine.cpp)
target_include_directories(RhinoCore PUBLIC .)
target_link_libraries(RhinoCore
    PUBLIC juce::juce_core
    PRIVATE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(RhinoCore PRIVATE RHINO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
