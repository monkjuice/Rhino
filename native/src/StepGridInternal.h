#pragma once
#include "StepGrid.h"
#include "SelectionInput.h"
#include "Theme.h"

// Shared internals of the StepGrid implementation, which is defined across
// StepGrid.cpp, StepGridPainter.cpp, StepGridGestures.cpp and
// StepGridEditing.cpp. Internal: nothing outside those files should include it.
//
// The pointer rules come from SelectionInput.h, which the arrangement reads
// too. Only what is peculiar to a grid of notes lives here.

namespace rhino
{

// Ctrl or Command, asked of a keystroke rather than of a press. It is the same
// chord the selection modifier uses, so it is the same answer.
inline bool isShortcutDown(const juce::ModifierKeys& mods)
{
    return isCommandModifier(mods);
}

inline int pitchClassOf(int pitch)
{
    return (pitch % 12 + 12) % 12;
}

// The last argument is the octave number middle C carries: 3, so MIDI 60 reads
// C3 the way Ableton, FL and Logic name it, and the way Forge's own keyboard
// and the Drum Rack's pads do. It was 4, which was consistent inside Rhino and
// wrong against every DAW a pattern gets compared with. Both of
// StepGridPainter's keyboards name their rows through this.
inline juce::String pitchName(int pitch)
{
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 3);
}

}
