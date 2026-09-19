#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Track groups.
//
// A group is organisational rather than a bus: it names a contiguous run of
// tracks, can be collapsed to one row, and passes mute, solo, colour and
// selection on to its members. Audio still reaches the main output track by
// track, so nothing here touches the signal path.
//
// Membership is one property on the track, and the group's own name, colour
// and collapsed flag are a node on the edit. Both are part of the document, so
// a group needs no save path of its own; and because membership travels with
// the track, reordering a track carries its group with it.
//
// The one invariant everything else relies on: a group is a single run of
// tracks. Reordering and deletion can break that, so reconcileTrackGroups
// repairs the runs rather than trusting them.

namespace rhino
{
namespace
{
constexpr int noGroup = 0;

int groupOf(const juce::Array<te::AudioTrack*>& tracks, int track)
{
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return noGroup;
    return static_cast<int>(tracks[track]->state.getProperty(trackGroupMemberID, noGroup));
}

void setGroupOf(te::AudioTrack& track, int groupId, juce::UndoManager* undoManager)
{
    if (groupId == noGroup)
        track.state.removeProperty(trackGroupMemberID, undoManager);
    else
        track.state.setProperty(trackGroupMemberID, groupId, undoManager);
}
}

int Session::trackGroupId(int track) const
{
    return groupOf(te::getAudioTracks(*edit), track);
}

juce::ValueTree Session::trackGroupState(int groupId) const
{
    if (groupId == noGroup)
        return {};
    for (int i = 0; i < edit->state.getNumChildren(); ++i)
        if (const auto child = edit->state.getChild(i);
            child.hasType(trackGroupID) && static_cast<int>(child.getProperty(trackGroupIdID, noGroup)) == groupId)
            return child;
    return {};
}

// Read from the track order rather than from a stored span: a group that has
// lost its node, which only a hand-edited document can produce, reads as
// ungrouped instead of as a band with nothing behind it.
std::vector<Session::TrackGroup> Session::trackGroups() const
{
    std::vector<TrackGroup> groups;
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = 0; track < tracks.size(); ++track)
    {
        const auto id = groupOf(tracks, track);
        if (id == noGroup)
            continue;
        if (!groups.empty() && groups.back().id == id && groups.back().lastTrack() == track - 1)
        {
            ++groups.back().trackCount;
            continue;
        }
        const auto state = trackGroupState(id);
        if (!state.isValid())
            continue;
        TrackGroup group;
        group.id = id;
        group.name = state.getProperty(trackGroupNameID, "Group").toString();
        group.colour = juce::Colour::fromString(state.getProperty(trackGroupColourID, juce::String()).toString());
        group.collapsed = static_cast<bool>(state.getProperty(trackGroupCollapsedID, false));
        group.firstTrack = track;
        group.trackCount = 1;
        groups.push_back(group);
    }
    return groups;
}

std::optional<Session::TrackGroup> Session::trackGroup(int groupId) const
{
    for (const auto& group : trackGroups())
        if (group.id == groupId)
            return group;
    return {};
}

// The shared half of grouping: carry the tracks together into one run and give
// them the same group. The run starts where the topmost of them already was,
// so grouping moves as little as it can. The caller has already opened the undo
// transaction, so creating a group and filling it undo as one step.
juce::Result Session::assignTracksToGroup(std::vector<int> tracks, int groupId)
{
    const auto existing = te::getAudioTracks(*edit);
    std::sort(tracks.begin(), tracks.end());
    tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
    std::erase_if(tracks, [&existing](int track) { return !juce::isPositiveAndBelow(track, existing.size()); });
    if (tracks.empty())
        return juce::Result::fail("Select one or more tracks to group.");

    // Ids rather than pointers: the moves below reorder the track list, and an
    // index read before them means something else afterwards.
    std::vector<te::EditItemID> members, others;
    for (int track = 0; track < existing.size(); ++track)
        (std::binary_search(tracks.begin(), tracks.end(), track) ? members : others)
            .push_back(existing[track]->itemID);
    // Every track above the topmost member is unselected, so the run starts at
    // that same index once the members are lifted out.
    const auto insertion = std::min(static_cast<size_t>(tracks.front()), others.size());
    std::vector<te::EditItemID> desired(others.begin(), others.begin() + static_cast<long>(insertion));
    desired.insert(desired.end(), members.begin(), members.end());
    desired.insert(desired.end(), others.begin() + static_cast<long>(insertion), others.end());

    auto& undoManager = edit->getUndoManager();
    const auto indexOf = [this](te::EditItemID id)
    {
        const auto current = te::getAudioTracks(*edit);
        for (int track = 0; track < current.size(); ++track)
            if (current[track]->itemID == id)
                return track;
        return -1;
    };
    for (int position = 0; position < static_cast<int>(desired.size()); ++position)
    {
        const auto from = indexOf(desired[static_cast<size_t>(position)]);
        if (from >= 0 && from != position)
            moveTrackInEdit(from, position);
    }
    const auto moved = te::getAudioTracks(*edit);
    for (const auto id : members)
        if (const auto track = indexOf(id); track >= 0)
            setGroupOf(*moved[track], groupId, &undoManager);
    undoManager.beginNewTransaction();
    refreshUtilityPointers();
    reconcileTrackGroups();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::groupTracks(std::vector<int> tracks, const juce::String& name)
{
    if (tracks.empty())
        return juce::Result::fail("Select one or more tracks to group.");

    // One past the highest id the document has ever used, so an id freed by an
    // undone grouping is never handed out twice.
    auto nextId = 1;
    auto groupsSoFar = 0;
    for (int i = 0; i < edit->state.getNumChildren(); ++i)
        if (const auto child = edit->state.getChild(i); child.hasType(trackGroupID))
        {
            ++groupsSoFar;
            nextId = std::max(nextId, static_cast<int>(child.getProperty(trackGroupIdID, noGroup)) + 1);
        }

    juce::ValueTree group(trackGroupID);
    group.setProperty(trackGroupIdID, nextId, nullptr);
    group.setProperty(trackGroupNameID,
                      name.trim().isEmpty() ? "Group " + juce::String(groupsSoFar + 1) : name.trim(), nullptr);
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Group tracks");
    edit->state.addChild(group, -1, &undoManager);
    return assignTracksToGroup(std::move(tracks), nextId);
}

juce::Result Session::addTrackToGroup(int track, int groupId)
{
    return addTracksToGroup({track}, groupId);
}

// The group's own members come along, because the run has to be rebuilt around
// whatever is joining it rather than only around the newcomers.
juce::Result Session::addTracksToGroup(std::vector<int> tracks, int groupId)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    std::vector<int> members;
    auto joining = 0;
    for (const auto track : tracks)
        if (juce::isPositiveAndBelow(track, trackCount()) && !group->contains(track))
        {
            members.push_back(track);
            ++joining;
        }
    if (joining == 0)
        return juce::Result::fail("Select a track that is not already in that group.");
    for (int member = group->firstTrack; member <= group->lastTrack(); ++member)
        members.push_back(member);
    edit->getUndoManager().beginNewTransaction("Add to group");
    return assignTracksToGroup(std::move(members), groupId);
}

juce::Result Session::removeTrackFromGroup(int track)
{
    const auto id = trackGroupId(track);
    if (id == noGroup)
        return juce::Result::fail("That track is not in a group.");
    const auto group = trackGroup(id);
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Remove from group");
    // Out of the middle of a run would split the group in two, so the track
    // slides below the members that stay before it leaves.
    if (group && track < group->lastTrack())
    {
        moveTrackInEdit(track, group->lastTrack());
        track = group->lastTrack();
    }
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("Select a track to remove from its group.");
    setGroupOf(*tracks[track], noGroup, &undoManager);
    undoManager.beginNewTransaction();
    refreshUtilityPointers();
    reconcileTrackGroups();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// One track at a time, addressed by id: each one slides out of its run before
// it is let go, and that renumbers everything below it. The cost is one undo
// step per track, which is the honest shape of the operation anyway.
juce::Result Session::removeTracksFromGroup(std::vector<int> tracks)
{
    const auto existing = te::getAudioTracks(*edit);
    std::vector<te::EditItemID> leaving;
    for (const auto track : tracks)
        if (juce::isPositiveAndBelow(track, existing.size()) && groupOf(existing, track) != noGroup)
            leaving.push_back(existing[track]->itemID);
    if (leaving.empty())
        return juce::Result::fail("Those tracks are not in a group.");
    for (const auto id : leaving)
    {
        const auto current = te::getAudioTracks(*edit);
        for (int track = 0; track < current.size(); ++track)
            if (current[track]->itemID == id)
            {
                if (const auto done = removeTrackFromGroup(track); done.failed())
                    return done;
                break;
            }
    }
    return juce::Result::ok();
}

juce::Result Session::ungroupTracks(int groupId)
{
    const auto state = trackGroupState(groupId);
    if (!state.isValid())
        return juce::Result::fail("That group is no longer there.");
    const auto tracks = te::getAudioTracks(*edit);
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Ungroup tracks");
    for (int track = 0; track < tracks.size(); ++track)
        if (groupOf(tracks, track) == groupId)
            setGroupOf(*tracks[track], noGroup, &undoManager);
    edit->state.removeChild(state, &undoManager);
    undoManager.beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setTrackGroupName(int groupId, const juce::String& name)
{
    auto state = trackGroupState(groupId);
    if (!state.isValid())
        return juce::Result::fail("That group is no longer there.");
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail("A group needs a name.");
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Rename group");
    state.setProperty(trackGroupNameID, trimmed, &undoManager);
    undoManager.beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setTrackGroupColour(int groupId, juce::Colour colour)
{
    auto state = trackGroupState(groupId);
    if (!state.isValid())
        return juce::Result::fail("That group is no longer there.");
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Colour group");
    if (colour.isTransparent())
        state.removeProperty(trackGroupColourID, &undoManager);
    else
        state.setProperty(trackGroupColourID, colour.toString(), &undoManager);
    undoManager.beginNewTransaction();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Collapsing is a view setting, like a row's height: it is written straight to
// the state, because undo belongs to what the tracks play.
juce::Result Session::setTrackGroupCollapsed(int groupId, bool collapsed)
{
    auto state = trackGroupState(groupId);
    if (!state.isValid())
        return juce::Result::fail("That group is no longer there.");
    state.setProperty(trackGroupCollapsedID, collapsed, nullptr);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

bool Session::trackGroupMuted(int groupId) const
{
    const auto group = trackGroup(groupId);
    if (!group)
        return false;
    for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
        if (!trackMixer(track).muted)
            return false;
    return true;
}

bool Session::trackGroupSoloed(int groupId) const
{
    const auto group = trackGroup(groupId);
    if (!group)
        return false;
    for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
        if (!trackMixer(track).soloed)
            return false;
    return true;
}

void Session::setTrackGroupMuted(int groupId, bool muted)
{
    if (const auto group = trackGroup(groupId))
        for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
            setTrackMuted(track, muted);
}

void Session::setTrackGroupSoloed(int groupId, bool soloed)
{
    if (const auto group = trackGroup(groupId))
        for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
            setTrackSoloed(track, soloed);
}

// Called after anything that can reorder or remove tracks. Empty group nodes
// are deliberately left alone: removing one here would not be part of the
// transaction that emptied it, so undoing that transaction would bring the
// members back to a group that no longer exists.
void Session::reconcileTrackGroups()
{
    const auto tracks = te::getAudioTracks(*edit);
    // A track carried into the middle of a group joins it, the way a clip
    // dropped on a lane belongs to that lane.
    for (int track = 1; track + 1 < tracks.size(); ++track)
        if (groupOf(tracks, track) == noGroup && groupOf(tracks, track - 1) != noGroup
            && groupOf(tracks, track - 1) == groupOf(tracks, track + 1))
            setGroupOf(*tracks[track], groupOf(tracks, track - 1), nullptr);

    // A group is one run. A track still claiming a group whose run has already
    // ended was carried out of it, so it comes out.
    std::set<int> closed;
    auto previous = noGroup;
    for (int track = 0; track < tracks.size(); ++track)
    {
        auto id = groupOf(tracks, track);
        if (previous != noGroup && id != previous)
            closed.insert(previous);
        if (id != noGroup && closed.contains(id))
        {
            setGroupOf(*tracks[track], noGroup, nullptr);
            id = noGroup;
        }
        previous = id;
    }
}

}
