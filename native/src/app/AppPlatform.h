#pragma once
#include <juce_core/juce_core.h>

namespace tracktion { inline namespace engine { class Engine; } }
namespace juce { class Component; }

namespace rhino
{

// Where the log goes: %APPDATA%/Rhino/rhino.log.
juce::File rhinoLogFile();

// Windows still offers DirectSound first; Rhino takes Windows Audio when it can.
void avoidLegacyDirectSound(tracktion::engine::Engine& engine);

// Registers .rhinoedit with this executable for the current user (Windows only).
void registerRhinoProjectFileAssociation();

// Keeps one of Rhino's windows above another of its own - a plugin's window or
// the Drum Rack's above the main window - and never above another app's, as
// Live keeps its plugin windows. On Windows that is exactly an owned window,
// so the window is given the other's top-level window as its owner; a null
// owner lets it go. Elsewhere it does nothing yet.
void keepWindowAbove(juce::Component& window, juce::Component* owner);

}
