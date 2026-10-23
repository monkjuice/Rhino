#include "Playhead.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace rhino
{
void movePlayhead(juce::Component& owner, float& current, float next, juce::Rectangle<int> area)
{
    if (current == next) return;
    const auto previous = std::exchange(current, next);
    for (const auto x : {previous, next})
    {
        const auto damage = playheadDamage(-1, x, area);
        if (!damage.isEmpty()) owner.repaint(damage);
    }
    deliverVBlankDamage(owner);
}

void deliverVBlankDamage(juce::Component& owner)
{
   #if JUCE_WINDOWS
    if (auto* peer = owner.getPeer())
    {
        const auto engines = peer->getAvailableRenderingEngines();
        if (engines[peer->getCurrentRenderingEngine()] == "Direct2D")
        {
            // In pinned JUCE 37c894f, HWNDComponentPeer::onVBlank invokes us
            // BEFORE D2DRenderContext::onVBlank paints its deferred damage.
            // repaint() only calls InvalidateRect; WM_PAINT normally transfers
            // that damage later. Without this handoff, the current frame can
            // erase the old line but clip out its new position.
            // UpdateWindow delivers WM_PAINT now. For this Direct2D backend it
            // only collects damage; GPU drawing still happens at vblank.
            // performAnyPendingRepaintsNow() is a no-op in this JUCE backend.
            UpdateWindow(static_cast<HWND>(peer->getNativeHandle()));
        }
    }
   #endif
}
}
