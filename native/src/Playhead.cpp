#include "Playhead.h"
#include <tracktion_engine/tracktion_engine.h>

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
double playheadTime(const tracktion::engine::TransportControl& transport)
{
    // TransportControl::getPosition is copied from the audio playhead by a
    // 50 Hz message-thread timer. Read the audio graph's latency-adjusted
    // position on every display refresh instead; no UI clock can drift from it.
    if (transport.isPlaying() && !transport.isUserDragging())
        if (auto* context = transport.getCurrentPlaybackContext(); context && context->isPlaybackGraphAllocated())
        {
            // Show an immediate seek while the audio thread adopts it. A
            // scheduled future jump must not move the display ahead of audio.
            if (const auto pending = context->getPendingPositionChange(); pending && *pending == transport.getPosition())
                return pending->inSeconds();
            if (context->isPlaying())
                return context->getAudibleTimelineTime().inSeconds();
        }
    return transport.getPosition().inSeconds();
}

void movePlayhead(juce::Component& owner, float& current, float next, juce::Rectangle<int> area)
{
    if (current == next) return;
    const auto previous = std::exchange(current, next);
    for (const auto x : {previous, next})
    {
        const auto damage = playheadDamage(-1, x, area);
        if (!damage.isEmpty()) owner.repaint(damage);
    }

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
