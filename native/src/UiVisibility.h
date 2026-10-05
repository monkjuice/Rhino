#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
// True when a component sits inside a window but it, or a panel above it, is
// switched off - the shell hides the session view, and whichever lower pane is
// not showing. Such a panel has no reason to rebuild itself for every change in
// the session, and rebuilding the hidden ones was part of what every knob and
// fader drag paid per pixel. It marks itself stale instead and catches up when
// it is shown.
//
// A component standing on its own, as the tests and the profiler build them,
// is never hidden by this, so they go on seeing every change as it happens.
inline bool isHiddenInShell(const juce::Component& component)
{
    for (auto* each = &component; each != nullptr; each = each->getParentComponent())
        if (each->getParentComponent() != nullptr && !each->isVisible())
            return true;
    return false;
}
}
