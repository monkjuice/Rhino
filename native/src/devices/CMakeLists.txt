# Rhino's own plugins, built apart from the application.
#
# Nothing here may include Session.h or any UI header: a device talks to the
# engine and to RhinoCore and to nothing else.  That is what lets this target
# compile on its own, and what keeps editing one device from rebuilding the
# app -- Session.h used to include all six device headers, so touching any of
# them rebuilt nearly every translation unit in the program.
#
# Sources are listed explicitly, as they are in the parent CMakeLists: a new
# device needs a line here, a line in DeviceCatalog.cpp, and nothing else.
add_library(RhinoDevices STATIC
    DeviceCatalog.cpp
    instruments/DrumDevice.cpp
    instruments/RhinoWaveDevice.cpp
    audio/UtilityDevice.cpp
    audio/RhinoSpaceDevice.cpp
    audio/RhinoBloomDevice.cpp
    audio/AutoTuneDevice.cpp
    midi/RhinoArpDevice.cpp)
# Only the devices/ root is public, so an include names the kind it wants:
# #include "instruments/DrumDevice.h".
target_include_directories(RhinoDevices PUBLIC .)
target_link_libraries(RhinoDevices
    PUBLIC
        RhinoCore
        tracktion::tracktion_engine tracktion::tracktion_graph tracktion::tracktion_core
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(RhinoDevices PRIVATE /bigobj)
endif()
