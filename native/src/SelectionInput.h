#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// One selection rule, read by both editors.
//
// The arrangement and the note editor answer the pointer the same way, because
// a clip and a note are the same kind of thing to select. Plain press on an
// item takes that item and drops everything else; plain press on empty space
// sweeps out a new selection and, if it never travels, clears the old one;
// Ctrl adds one item or takes it back out; Shift extends. That is what every
// file manager and every drawing program does, and neither panel is entitled
// to its own dialect of it.
//
// These predicates are the shared vocabulary rather than a shared gesture:
// what a marquee catches differs between a note grid and a timeline, so each
// panel still decides that for itself.
namespace rhino
{

// Ctrl on Windows, Command on macOS. The same chord means "the command form of
// this key" everywhere in the app, so keyboard shortcuts ask it too.
inline bool isCommandModifier(const juce::ModifierKeys& mods)
{
    return mods.isCommandDown() || mods.isCtrlDown();
}

// Add this item to the selection, or take it back out, leaving the rest alone.
inline bool isToggleSelectionModifier(const juce::ModifierKeys& mods)
{
    return isCommandModifier(mods);
}

// Extend the selection. Neither editor has a meaningful "everything between
// these two" - a note grid is two-dimensional and a timeline is a rectangle -
// so extending degrades to adding, which is the useful half of it.
inline bool isExtendSelectionModifier(const juce::ModifierKeys& mods)
{
    return mods.isShiftDown();
}

// Either of the two: the press is about gathering rather than about replacing,
// so it must not carry, draw or erase anything.
inline bool isMultiSelectModifier(const juce::ModifierKeys& mods)
{
    return isToggleSelectionModifier(mods) || isExtendSelectionModifier(mods);
}

// How far inside an edge a drag starts pulling the view after it, and how fast
// it may pull once the pointer is well past. Both panels scroll on the same
// ramp so a drag that runs off the right feels the same in either.
inline constexpr float autoScrollEdge = 30.0f;

// 0 inside the band, growing to autoScrollSpeedLimit once the pointer is that
// many bands past the edge. The ramp is what makes a small overshoot creep and
// a pointer parked outside the panel travel.
inline constexpr float autoScrollSpeedLimit = 3.0f;

// Signed, in multiples of the panel's own scroll step: negative pulls the view
// towards the start, positive towards the end, zero leaves it alone.
inline float autoScrollPush(float position, float lowEdge, float highEdge)
{
    if (highEdge - lowEdge < autoScrollEdge * 2.0f)
        return 0.0f;
    if (position < lowEdge + autoScrollEdge)
        return -std::clamp((lowEdge + autoScrollEdge - position) / autoScrollEdge, 0.0f, autoScrollSpeedLimit);
    if (position > highEdge - autoScrollEdge)
        return std::clamp((position - (highEdge - autoScrollEdge)) / autoScrollEdge, 0.0f, autoScrollSpeedLimit);
    return 0.0f;
}

}
