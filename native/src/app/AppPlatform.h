#pragma once
#include <juce_core/juce_core.h>

namespace tracktion { inline namespace engine { class Engine; } }

namespace rhino
{

// Where the log goes: %APPDATA%/Rhino/rhino.log.
juce::File rhinoLogFile();

// Windows still offers DirectSound first; Rhino takes Windows Audio when it can.
void avoidLegacyDirectSound(tracktion::engine::Engine& engine);

// Registers .rhinoedit with this executable for the current user (Windows only).
void registerRhinoProjectFileAssociation();

}
