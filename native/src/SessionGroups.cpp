#include "SessionInternal.h"
#include <algorithm>

// Track groups.
//
// A group is a bus. Its own track carries the group's name, colour, fader,
// mute, solo and device chain, and every member's output is routed into it, so
// a group is a real mixing point rather than a way of drawing the stack. That
// track is an ordinary te::AudioTrack, which is the whole reason this is
// affordable: it keeps its place in getAudioTracks(), so every track-indexed
// call in Session - the mixer, the devices, the automation - reaches a bus
// without a second code path, exactly as they already reach any other track.
//
// What a bus refuses is the point of it: no instrument, no MIDI effect, no
// clips. A bus carries audio that has already been played, so there is nothing
// for an instrument to play and no sequence for a MIDI effect to act on; audio
// effects are the only devices that mean anything on a sum.
//
// The structure is positional, and only positional: a bus is immediately
// followed by the run of tracks that belong to it. Nothing stores a member
// list, so nothing can disagree with what is on screen.
// reconcileTrackGroups enforces that, and it makes the routing follow the
// stack rather than the other way round - which is why carrying a track into a
// band joins it and carrying it out leaves it, with the audio following both.

namespace rhino
{
namespace
{
constexpr int noGroup = 0;

int memberGroupOf(const juce::Array<te::AudioTrack*>& tracks, int track)
{
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return noGroup;
    return static_cast<int>(tracks[track]->state.getProperty(trackGroupMemberID, noGroup));
}

int busGroupOf(const juce::Array<te::AudioTrack*>& tracks, int track)
{
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return noGroup;
    return static_cast<int>(tracks[track]->state.getProperty(trackGroupBusID, noGroup));
}

void setMemberGroup(te::AudioTrack& track, int groupId, juce::UndoManager* undoManager)
{
    if (groupId == noGroup)
        track.state.removeProperty(trackGroupMemberID, undoManager);
    else
        track.state.setProperty(trackGroupMemberID, groupId, undoManager);
}
}

bool Session::isGroupBusTrack(int track) const
{
    return busGroupOf(te::getAudioTracks(*edit), track) != noGroup;
}

int Session::trackGroupBusId(int track) const
{
    return busGroupOf(te::getAudioTracks(*edit), track);
}

int Session::trackGroupId(int track) const
{
    return memberGroupOf(te::getAudioTracks(*edit), track);
}

// Read out of the track order rather than from a stored span. A bus opens a
// group and the run of members directly under it closes it, so what the stack
// shows and what the mixer does cannot drift apart.
std::vector<Session::TrackGroup> Session::trackGroups() const
{
    std::vector<TrackGroup> groups;
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = 0; track < tracks.size(); ++track)
    {
        const auto id = busGroupOf(tracks, track);
        if (id == noGroup)
            continue;
        TrackGroup group;
        group.id = id;
        group.busTrack = track;
        group.name = tracks[track]->getName();
        group.colour = tracks[track]->getColour();
        group.collapsed = static_cast<bool>(tracks[track]->state.getProperty(trackGroupCollapsedID, false));
        group.firstTrack = track + 1;
        while (memberGroupOf(tracks, group.firstTrack + group.trackCount) == id
               && busGroupOf(tracks, group.firstTrack + group.trackCount) == noGroup)
            ++group.trackCount;
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

// Lay the stack out in the order given. Ids rather than indices throughout:
// every move renumbers everything below it.
void Session::arrangeTrackOrder(const std::vector<te::EditItemID>& desired)
{
    const auto indexOf = [this](te::EditItemID id)
    {
        const auto current = te::getAudioTracks(*edit);
        for (int track = 0; track < current.size(); ++track)
            if (current[track]->itemID == id)
                return track;
        return -1;
    };
    for (int position = 0; position < static_cast<int>(desired.size()); ++position)
        if (const auto from = indexOf(desired[static_cast<size_t>(position)]); from >= 0 && from != position)
            moveTrackInEdit(from, position);
}

// The shared half of grouping: gather the members under their bus. The group
// lands where the topmost of them already was, so grouping moves as little as
// it can. The caller has already opened the undo transaction.
juce::Result Session::assignTracksToGroup(std::vector<int> tracks, int groupId)
{
    const auto existing = te::getAudioTracks(*edit);
    auto bus = -1;
    for (int track = 0; track < existing.size(); ++track)
        if (busGroupOf(existing, track) == groupId)
            bus = track;
    if (bus < 0)
        return juce::Result::fail("That group is no longer there.");

    std::sort(tracks.begin(), tracks.end());
    tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
    std::erase_if(tracks, [&existing, bus](int track)
    {
        return !juce::isPositiveAndBelow(track, existing.size()) || track == bus
            || busGroupOf(existing, track) != noGroup;
    });
    if (tracks.empty())
        return juce::Result::fail("Select one or more tracks to group.");

    std::vector<te::EditItemID> members, others;
    for (int track = 0; track < existing.size(); ++track)
    {
        if (track == bus)
            continue;
        (std::binary_search(tracks.begin(), tracks.end(), track) ? members : others)
            .push_back(existing[track]->itemID);
    }
    // Where the group opens: the topmost member's place, counted in the order
    // with the bus and the members themselves already lifted out of it.
    auto insertion = 0;
    for (int track = 0; track < tracks.front(); ++track)
        if (track != bus && !std::binary_search(tracks.begin(), tracks.end(), track))
            ++insertion;
    insertion = std::min(insertion, static_cast<int>(others.size()));
    std::vector<te::EditItemID> desired(others.begin(), others.begin() + insertion);
    desired.push_back(existing[bus]->itemID);
    desired.insert(desired.end(), members.begin(), members.end());
    desired.insert(desired.end(), others.begin() + insertion, others.end());

    auto& undoManager = edit->getUndoManager();
    arrangeTrackOrder(desired);
    const auto moved = te::getAudioTracks(*edit);
    for (const auto id : members)
        for (int track = 0; track < moved.size(); ++track)
            if (moved[track]->itemID == id)
                setMemberGroup(*moved[track], groupId, &undoManager);
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
    const auto existing = te::getAudioTracks(*edit);
    for (const auto track : tracks)
        if (busGroupOf(existing, track) != noGroup)
            return juce::Result::fail("A group cannot go inside another group.");

    // One past the highest id the document has used, so an id freed by an
    // undone grouping is never handed out twice.
    auto nextId = 1;
    auto groupsSoFar = 0;
    for (int track = 0; track < existing.size(); ++track)
        if (const auto id = busGroupOf(existing, track); id != noGroup)
        {
            ++groupsSoFar;
            nextId = std::max(nextId, id + 1);
        }

    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Group tracks");
    auto bus = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (bus == nullptr)
        return juce::Result::fail("The group track could not be created.");
    bus->setName(name.trim().isEmpty() ? "Group " + juce::String(groupsSoFar + 1) : name.trim());
    bus->state.setProperty(trackGroupBusID, nextId, &undoManager);
    ensureSceneSlots();
    ensureTrackMixers();
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
    {
        if (!juce::isPositiveAndBelow(track, trackCount()) || group->covers(track))
            continue;
        if (isGroupBusTrack(track))
            return juce::Result::fail("A group cannot go inside another group.");
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
    setMemberGroup(*tracks[track], noGroup, &undoManager);
    undoManager.beginNewTransaction();
    refreshUtilityPointers();
    reconcileTrackGroups();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// One track at a time, addressed by id: each one slides out of its run before
// it is let go, and that renumbers everything below it.
juce::Result Session::removeTracksFromGroup(std::vector<int> tracks)
{
    const auto existing = te::getAudioTracks(*edit);
    std::vector<te::EditItemID> leaving;
    for (const auto track : tracks)
        if (juce::isPositiveAndBelow(track, existing.size()) && memberGroupOf(existing, track) != noGroup)
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

// Ungrouping takes the bus away and hands its members back to the main output.
// The tracks themselves, and everything on them, survive: what goes is the
// mixing point, which is the only thing the group added.
juce::Result Session::ungroupTracks(int groupId)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    const auto busTrack = group->busTrack;
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Ungroup tracks");
    const auto tracks = te::getAudioTracks(*edit);
    for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
        if (juce::isPositiveAndBelow(track, tracks.size()))
            setMemberGroup(*tracks[track], noGroup, &undoManager);
    if (juce::isPositiveAndBelow(busTrack, tracks.size()))
        edit->deleteTrack(tracks[busTrack]);
    undoManager.beginNewTransaction();
    refreshUtilityPointers();
    reconcileTrackGroups();
    refreshLoop();
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Collapsing is a view setting, like a row's height: written straight to the
// bus track's state, because undo belongs to what the tracks play.
juce::Result Session::setTrackGroupCollapsed(int groupId, bool collapsed)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(group->busTrack, tracks.size()))
        return juce::Result::fail("That group is no longer there.");
    tracks[group->busTrack]->state.setProperty(trackGroupCollapsedID, collapsed, nullptr);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Where every track sends its audio, decided entirely by where it sits. This is
// the one place routing is written, so a track carried into a band starts
// feeding it and one carried out stops, with nothing left over to go stale.
void Session::reconcileTrackGroups()
{
    const auto tracks = te::getAudioTracks(*edit);
    // A track carried into the middle of a group joins it, the way a clip
    // dropped on a lane belongs to that lane. A bus never does: a group inside
    // a group is a routing tree this deliberately does not build.
    for (int track = 1; track + 1 < tracks.size(); ++track)
        if (memberGroupOf(tracks, track) == noGroup && busGroupOf(tracks, track) == noGroup
            && memberGroupOf(tracks, track + 1) != noGroup
            && memberGroupOf(tracks, track + 1) == memberGroupOf(tracks, track - 1))
            setMemberGroup(*tracks[track], memberGroupOf(tracks, track - 1), nullptr);

    // A group is its bus followed by one run of members. Anything else claiming
    // that group was carried away from it, so it comes out.
    auto openGroup = noGroup;
    for (int track = 0; track < tracks.size(); ++track)
    {
        if (const auto bus = busGroupOf(tracks, track); bus != noGroup)
        {
            // A bus is never a member, of its own group or of anyone else's.
            setMemberGroup(*tracks[track], noGroup, nullptr);
            openGroup = bus;
            continue;
        }
        if (memberGroupOf(tracks, track) != openGroup)
        {
            setMemberGroup(*tracks[track], noGroup, nullptr);
            openGroup = noGroup;
        }
    }

    // Now make the audio follow. A member feeds its bus; everything else goes
    // straight to the main output.
    const auto groups = trackGroups();
    for (int track = 0; track < tracks.size(); ++track)
    {
        const auto id = memberGroupOf(tracks, track);
        te::AudioTrack* destination = nullptr;
        if (id != noGroup)
            for (const auto& group : groups)
                if (group.id == id && juce::isPositiveAndBelow(group.busTrack, tracks.size()))
                    destination = tracks[group.busTrack];
        auto& output = tracks[track]->getOutput();
        if (destination != nullptr)
        {
            if (output.getDestinationTrack() != destination)
                output.setOutputToTrack(destination);
        }
        // Asked as "does it already go to the device", not "does it name a
        // track": deleting a bus leaves its members naming a track that is no
        // longer there, which reads as no destination while still being a
        // destination the engine has to resolve on every render.
        else if (!output.usesDefaultAudioOut())
        {
            output.setOutputToDefaultDevice(false);
        }
    }
}

// Documents written before groups were buses carry a node on the edit and a
// membership property on each track, but no bus to send anything to. They are
// rebuilt as real groups on load, so a project saved yesterday opens as one.
void Session::migrateLegacyTrackGroups()
{
    std::vector<juce::ValueTree> legacy;
    for (int i = 0; i < edit->state.getNumChildren(); ++i)
        if (const auto child = edit->state.getChild(i); child.hasType(legacyTrackGroupID))
            legacy.push_back(child);
    if (legacy.empty())
        return;

    for (const auto& node : legacy)
    {
        const auto id = static_cast<int>(node.getProperty(legacyTrackGroupIdID, noGroup));
        const auto tracks = te::getAudioTracks(*edit);
        std::vector<int> members;
        for (int track = 0; track < tracks.size(); ++track)
            if (memberGroupOf(tracks, track) == id && busGroupOf(tracks, track) == noGroup)
                members.push_back(track);
        if (members.empty())
            continue;
        // Cleared first, because grouping reads membership to decide what is
        // already spoken for, and these are about to be given a bus.
        for (const auto track : members)
            setMemberGroup(*tracks[track], noGroup, nullptr);
        const auto name = node.getProperty(legacyTrackGroupNameID, "Group").toString();
        const auto colour = juce::Colour::fromString(node.getProperty(legacyTrackGroupColourID, juce::String()).toString());
        const auto topmost = members.front();
        if (groupTracks(members, name).failed())
            continue;
        if (!colour.isTransparent())
            if (const auto migrated = trackGroup(trackGroupId(topmost)))
                setTrackColour(migrated->busTrack, colour);
    }
    for (const auto& node : legacy)
        edit->state.removeChild(node, nullptr);
}

}
