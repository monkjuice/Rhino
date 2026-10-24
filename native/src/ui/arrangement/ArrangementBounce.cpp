#include "ArrangementInternal.h"

// Bounce to New Track: Ctrl+B, and the clip and card menus. The model decides
// what is rendered, where it lands and what is muted (SessionBounce.cpp); this
// decides what the selection meant and says what happened, because a bounce
// lands below the lane it came from and the muting is easy to miss.

namespace rhino
{

// What the last click selected decides what is bounced, as it decides what
// Delete removes: the cards, or the selection - a span of the timeline
// whatever made it, bounced as a span cut at its edges. For a clicked clip
// that span is the clip, whole. Only a primary clip with no span around it
// is bounced as a clip.
void Arrangement::bounceSelected()
{
    cancelDrag();
    syncIfStale();
    if (focus == Focus::track)
    {
        bounceTracks(selectedTracks);
        return;
    }
    if (timeSelection.active && timeSelection.isRange())
    {
        bounceRegion(timeSelection);
        return;
    }
    auto chosen = clipsToActOn();
    if (chosen.empty())
    {
        if (status) status("Select clips, a track or a group to bounce");
        return;
    }
    bounceClips(std::move(chosen));
}

void Arrangement::bounceClips(std::vector<te::EditItemID> ids)
{
    cancelDrag();
    if (const auto started = session.startBounceClips(ids); started.failed())
    {
        if (status) status(started.getErrorMessage());
        return;
    }
    bouncingTracks = false;
    bouncingRegion = false;
    runBounce();
}

void Arrangement::bounceTracks(std::vector<int> tracks)
{
    cancelDrag();
    if (const auto started = session.startBounceTracks(tracks); started.failed())
    {
        if (status) status(started.getErrorMessage());
        return;
    }
    bouncingTracks = true;
    bouncingRegion = false;
    runBounce();
}

void Arrangement::bounceRegion(TimeSelection region)
{
    cancelDrag();
    if (const auto started = session.startBounceRegion(region.start, region.end, region.firstTrack, region.lastTrack);
        started.failed())
    {
        if (status) status(started.getErrorMessage());
        return;
    }
    bouncingTracks = false;
    bouncingRegion = true;
    runBounce();
}

void Arrangement::runBounce()
{
    if (bounceStarted)
    {
        bounceStarted();
        return;
    }
    while (session.continueBounce(1000.0)) {}
    bounceFinished();
}

// The selection moves onto what the bounce made: the new cards, which are the
// audio now, or the new clips, as a merge's does - the clips it was pointing
// at are muted, and bouncing them again would be refused.
void Arrangement::bounceFinished()
{
    Session::BounceResult result;
    const auto outcome = session.bounceOutcome(&result);
    if (outcome.failed())
    {
        if (status) status(outcome.getErrorMessage());
        return;
    }
    // The selection is read off the list, which the bounce's own
    // announcement left stale if the timeline is hidden.
    syncIfStale();
    if (bouncingTracks)
    {
        selectTrack(result.tracks.front());
        for (size_t i = 1; i < result.tracks.size(); ++i)
            toggleTrackSelection(result.tracks[i]);
        setSelection({});
        focus = Focus::track;
        if (status)
        {
            if (result.sourceCount == 1)
                status("Bounced to " + session.trackName(result.tracks.front())
                       + (result.group ? ". The group's bus is muted." : ". The original track is muted."));
            else
                status("Bounced " + juce::String(result.sourceCount) + " tracks to new tracks. The originals are muted.");
        }
    }
    else
    {
        setSelection(result.clips);
        if (status && bouncingRegion)
        {
            status("Bounced the selection to "
                   + (result.tracks.size() == 1 ? session.trackName(result.tracks.front())
                                                : juce::String(static_cast<int>(result.tracks.size())) + " new tracks")
                   + ". The originals are muted inside it.");
        }
        else if (status)
        {
            const auto one = result.sourceCount == 1;
            status("Bounced " + (one ? juce::String("1 clip") : juce::String(result.sourceCount) + " clips") + " to "
                   + (result.tracks.size() == 1 ? session.trackName(result.tracks.front())
                                                : juce::String(static_cast<int>(result.tracks.size())) + " new tracks")
                   + (one ? ". The original is muted." : ". The originals are muted."));
        }
    }
    repaint();
}

}
