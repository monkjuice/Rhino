#pragma once
#include "StepGrid.h"
#include "SelectionInput.h"

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
// does. It was 4, which was consistent inside Rhino and wrong against every
// DAW a pattern gets compared with. StepGridPainter names its rows the same
// way; the two have to move together.
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
    return juce::MidiMessage::getMidiNoteName(pitch, true, true, 3);
}

}
