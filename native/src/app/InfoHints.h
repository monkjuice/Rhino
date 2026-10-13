#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
// When a reading about the control under the pointer replaces what the Info
// View is showing.
//
// The panel has two writers: the hint that follows the pointer, and the status
// line that reports what just happened. They share one panel, so the rule for
// which of them wins is the whole of the behaviour and is worth stating in one
// place rather than leaving inside a paint-adjacent timer.
//
// Two decisions, neither of them obvious from the call site:
//
//   A control that says nothing never writes. Resting on a lane, a clip or a
//   gap between two buttons is not a request for silence, so the panel keeps
//   the last thing it was told and crossing the room between two controls does
//   not blank it.
//
//   A reading is keyed on the control, not on the words. Keyed on the words, a
//   status message would be wiped a thirtieth of a second after it arrived by a
//   pointer that had not moved; keyed on the control alone, a button that
//   relabels itself under the pointer - play becoming pause - would never be
//   re-read.
inline bool infoHintReplaces(const juce::Component* showing, const juce::String& shown,
                             const juce::Component* under, const juce::String& text)
{
    if (text.isEmpty()) return false;
    return under != showing || text != shown;
}
}
