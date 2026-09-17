#pragma once
#include "StepGrid.h"

// Shared internals of the StepGrid implementation, which is defined across
// StepGrid.cpp, StepGridPainter.cpp, StepGridGestures.cpp and
// StepGridEditing.cpp. Internal: nothing outside those files should include it.

namespace rhino
{

inline bool isShortcutDown(const juce::ModifierKeys& mods)
{
    return mods.isCommandDown() || mods.isCtrlDown();
}

inline int pitchClassOf(int pitch)
{
    return (pitch % 12 + 12) % 12;
}

inline juce::String drumLaneName(int pitch)
{
    if (pitch == 48) return "Kick";
    if (pitch == 50) return "Low Tom";
    if (pitch == 52) return "Mid Tom";
    if (pitch == 53) return "Snare";
    if (pitch == 54) return "High Tom";
    if (pitch == 56) return "Clap";
    if (pitch == 58) return "Closed Hat";
    if (pitch == 59) return "Open Hat";
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
}

}
