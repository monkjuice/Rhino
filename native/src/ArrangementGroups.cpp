#include "ArrangementInternal.h"
#include "Theme.h"
#include <algorithm>

// Track groups in the arrangement.
//
// A group is a bus, and a bus is a track, so there is no band component here
// and no row type of its own: the group's row is an ordinary card with an
// ordinary fader, mute, solo and name, drawn from the same code as every other
// card. What this file adds is only what makes the grouping legible - the
// disclosure that folds the members away, the step in the left edge that says
// which cards are inside, and the multi-card selection that grouping acts on.
//
// Collapsing lays the members out at zero height rather than dropping their
// rows, so lane, bounds, cardAt and the header controls all answer with an
// empty rectangle and draw nothing without a hidden case in each of them.

namespace rhino
{
namespace
{
const juce::Colour defaultGroupColour {0xff5a6874};

juce::Colour bandColour(const Session::TrackGroup& group)
{
    return group.colour.isTransparent() ? defaultGroupColour : group.colour;
}
}

const Session::TrackGroup* Arrangement::groupById(int groupId) const
{
    for (const auto& group : groups)
        if (group.id == groupId)
            return &group;
    return nullptr;
}

const Session::TrackGroup* Arrangement::groupForBus(int track) const
{
    for (const auto& group : groups)
        if (group.busTrack == track)
            return &group;
    return nullptr;
}

const Session::TrackGroup* Arrangement::groupContaining(int track) const
{
    for (const auto& group : groups)
        if (group.contains(track))
            return &group;
    return nullptr;
}

bool Arrangement::isTrackHidden(int track) const
{
    const auto* group = groupContaining(track);
    return group != nullptr && group->collapsed;
}

// A card inside a group starts further right. Its bus does not: the step in the
// left edge is what says which side of the group a card is on.
float Arrangement::trackIndent(int track) const
{
    return groupContaining(track) != nullptr ? groupIndent : 0.0f;
}

bool Arrangement::isTrackArmed(int track) const
{
    return track >= 0 && track < static_cast<int>(armedTracks.size()) && armedTracks[static_cast<size_t>(track)];
}

bool Arrangement::isTrackSelected(int track) const
{
    return std::find(selectedTracks.begin(), selectedTracks.end(), track) != selectedTracks.end();
}

// Shift-click gathers the run between the anchor and the card under the
// pointer, which is the selection Ctrl+G reads.
void Arrangement::selectTrackRange(int from, int to)
{
    const auto last = std::max(0, session.trackCount() - 1);
    from = juce::jlimit(0, last, from);
    to = juce::jlimit(0, last, to);
    selectedTracks.clear();
    for (int track = std::min(from, to); track <= std::max(from, to); ++track)
        selectedTracks.push_back(track);
    if (selectedTrack != to)
    {
        selectedTrack = to;
        if (trackSelected) trackSelected(to);
    }
}

void Arrangement::toggleTrackSelection(int track)
{
    if (!juce::isPositiveAndBelow(track, session.trackCount()))
        return;
    const auto found = std::find(selectedTracks.begin(), selectedTracks.end(), track);
    if (found != selectedTracks.end())
    {
        // One card always stays selected: the rest of the app works on it.
        if (selectedTracks.size() <= 1)
            return;
        selectedTracks.erase(found);
    }
    else
        selectedTracks.push_back(track);
    trackSelectionAnchor = track;
    const auto working = isTrackSelected(track) ? track : selectedTracks.back();
    if (selectedTrack != working)
    {
        selectedTrack = working;
        if (trackSelected) trackSelected(working);
    }
}

// The arrow lives in the gutter a member's indent opens, so the disclosure and
// the step it controls are the same column.
juce::Rectangle<float> Arrangement::busDisclosureBounds(juce::Rectangle<float> row) const
{
    return {groupSpineLeft - 1.0f, row.getY() + cardControlsTop + 2.0f, groupIndent, 13.0f};
}

bool Arrangement::beginGroupGesture(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown() || event.position.x >= headerWidth)
        return false;
    const auto track = cardAt(event.position);
    if (track < 0)
        return false;
    const auto* group = groupForBus(track);
    if (group == nullptr || !busDisclosureBounds(lane(track)).contains(event.position))
        return false;
    // Read before touching the session: a change message rebuilds the row stack
    // and with it the group this points at.
    const auto groupId = group->id;
    const auto name = group->name;
    const auto collapsed = group->collapsed;
    const auto done = session.setTrackGroupCollapsed(groupId, !collapsed);
    if (status)
        status(done.failed() ? done.getErrorMessage() : (collapsed ? "Expanded " : "Collapsed ") + name);
    return true;
}

// What tells a group apart on the cards: an arrow on the bus, and the colour of
// that bus filling the step every member is pushed right by.
void Arrangement::paintGroupGutter(juce::Graphics& g, int track, juce::Rectangle<float> row)
{
    if (const auto* member = groupContaining(track))
    {
        g.setColour(bandColour(*member).withAlpha(0.9f));
        g.fillRect(groupSpineLeft, row.getY(), groupIndent - groupSpineLeft, row.getHeight());
        return;
    }
    const auto* bus = groupForBus(track);
    if (bus == nullptr)
        return;
    // Right for a folded group, down for an open one: the arrow points at what
    // clicking it reveals.
    const auto area = busDisclosureBounds(row);
    juce::Path arrow;
    if (bus->collapsed)
        arrow.addTriangle(area.getX() + 4.0f, area.getY() + 1.0f,
                          area.getX() + 4.0f, area.getBottom() - 1.0f,
                          area.getRight() - 4.0f, area.getCentreY());
    else
        arrow.addTriangle(area.getX() + 2.0f, area.getY() + 3.0f,
                          area.getRight() - 2.0f, area.getY() + 3.0f,
                          area.getCentreX(), area.getBottom() - 2.0f);
    g.setColour(bandColour(*bus).contrasting(0.7f));
    g.fillPath(arrow);
}

void Arrangement::toggleGroupCollapsed(int groupId)
{
    const auto* group = groupById(groupId);
    if (group == nullptr)
        return;
    const auto collapsed = group->collapsed;
    if (const auto done = session.setTrackGroupCollapsed(groupId, !collapsed); done.failed() && status)
        status(done.getErrorMessage());
}

void Arrangement::groupSelectedTracks()
{
    std::vector<int> tracks;
    for (const auto gathered : selectedTracks)
        if (juce::isPositiveAndBelow(gathered, session.trackCount()))
            tracks.push_back(gathered);
    if (tracks.empty())
    {
        if (status) status("Select one or more track cards to group");
        return;
    }
    // The bus lands where the topmost selected card already was, so that is the
    // row the group opens on.
    const auto first = *std::min_element(tracks.begin(), tracks.end());
    const auto count = tracks.size();
    const auto result = session.groupTracks(std::move(tracks), {});
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    selectTrack(first);
    focusTrack();
    repaint();
    if (status)
        status("Grouped " + juce::String(count) + " track" + (count == 1 ? "" : "s")
               + " into a bus - drop audio effects on it");
}

void Arrangement::ungroupSelection()
{
    auto groupId = session.trackGroupBusId(selectedTrack);
    if (groupId <= 0)
        groupId = session.trackGroupId(selectedTrack);
    if (groupId <= 0)
    {
        if (status) status("Select a group, or a track inside one, to ungroup");
        return;
    }
    const auto* group = groupById(groupId);
    const auto name = group != nullptr ? group->name : juce::String("the group");
    const auto result = session.ungroupTracks(groupId);
    if (status) status(result.failed() ? result.getErrorMessage() : "Ungrouped " + name);
    repaint();
}

}
