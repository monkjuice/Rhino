#include "SessionInternal.h"
#include <algorithm>
#include <map>

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
// Groups nest. A bus can itself be inside another group - it then carries its
// own group's id and the id of the group around it - and it feeds that
// group's bus as any member does, so a nested group is a submix feeding a
// submix, up to maxGroupLevels deep.
//
// The structure is positional, and only positional: a bus is immediately
// followed by the run of tracks inside it, groups inside it whole. Nothing
// stores a member list, so nothing can disagree with what is on screen. The
// rules that read and repair the stack are pure (GroupTree.h); this file reads
// the tracks into them and writes the answers back. reconcileTrackGroups makes
// the routing follow the stack rather than the other way round - which is why
// carrying a track into a band joins it and carrying it out leaves it, with
// the audio following both.

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

// What the track's output is written as: for a track, "track N" by its number
// in the stack (TrackOutput::setOutputToTrack at 4536d8a).
juce::String storedOutputName(const te::AudioTrack& track)
{
    return track.state.getChildWithName(te::IDs::OUTPUTDEVICES).getChildWithName(te::IDs::DEVICE)
        [te::IDs::name].toString();
}

// The last track inside the group that is not being carried away, or its bus
// when every one of them is: what a block carried to the group's end lands
// after.
int lastStaying(const Session::TrackGroup& group, const std::vector<int>& moving)
{
    auto after = group.busTrack;
    for (int track = group.firstTrack; track <= group.lastTrack(); ++track)
        if (!std::binary_search(moving.begin(), moving.end(), track))
            after = track;
    return after;
}
}

std::vector<GroupSlot> groupSlotsOf(const juce::Array<te::AudioTrack*>& tracks)
{
    std::vector<GroupSlot> slots;
    slots.reserve(static_cast<size_t>(tracks.size()));
    for (int track = 0; track < tracks.size(); ++track)
        slots.push_back({busGroupOf(tracks, track), memberGroupOf(tracks, track),
                         tracks[track]->state.hasProperty(trackReturnBusID)});
    return slots;
}

juce::Result refuseDeepNesting(const std::vector<GroupSlot>& planned)
{
    if (deepestGroupLevel(planned) <= maxGroupLevels)
        return juce::Result::ok();
    return juce::Result::fail("Groups nest at most " + juce::String(maxGroupLevels) + " levels deep.");
}

bool Session::isGroupBusTrack(int track) const
{
    return busGroupOf(cachedTracks(), track) != noGroup;
}

int Session::trackGroupBusId(int track) const
{
    return busGroupOf(cachedTracks(), track);
}

int Session::trackGroupId(int track) const
{
    return memberGroupOf(cachedTracks(), track);
}

// Read out of the track order rather than from a stored span. A bus opens a
// group and the run of tracks inside it closes it, so what the stack shows and
// what the mixer does cannot drift apart.
std::vector<Session::TrackGroup> Session::trackGroups() const
{
    std::vector<TrackGroup> groups;
    const auto tracks = audioTracks();
    for (const auto& span : readGroupSpans(groupSlotsOf(tracks)))
    {
        auto* bus = tracks[span.bus];
        TrackGroup group;
        group.id = span.id;
        group.busTrack = span.bus;
        group.parent = span.parent;
        group.level = span.level;
        group.name = bus->getName();
        group.colour = bus->getColour();
        group.collapsed = static_cast<bool>(bus->state.getProperty(trackGroupCollapsedID, false));
        group.firstTrack = span.bus + 1;
        group.trackCount = span.count;
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
        const auto current = audioTracks();
        for (int track = 0; track < current.size(); ++track)
            if (current[track]->itemID == id)
                return track;
        return -1;
    };
    for (int position = 0; position < static_cast<int>(desired.size()); ++position)
        if (const auto from = indexOf(desired[static_cast<size_t>(position)]); from >= 0 && from != position)
            moveTrackInEdit(from, position);
}

// The plan names tracks by where they sat when it was made; ids from here on,
// because every move renumbers what is under it.
void Session::applyGroupGathering(const GroupGathering& gathering, te::AudioTrack* newBus)
{
    const auto planned = audioTracks();
    std::vector<te::EditItemID> order;
    std::map<juce::uint64, int> claims;
    for (size_t at = 0; at < gathering.order.size(); ++at)
    {
        const auto index = gathering.order[at];
        auto* track = index < 0 ? newBus : juce::isPositiveAndBelow(index, planned.size()) ? planned[index] : nullptr;
        if (track == nullptr)
            continue;
        order.push_back(track->itemID);
        claims[track->itemID.getRawID()] = gathering.slots[at].claim;
    }
    arrangeTrackOrder(order);
    auto& undoManager = edit->getUndoManager();
    for (auto* track : audioTracks())
        if (const auto found = claims.find(track->itemID.getRawID()); found != claims.end()
            && static_cast<int>(track->state.getProperty(trackGroupMemberID, noGroup)) != found->second)
            setMemberGroup(*track, found->second, &undoManager);
    reconcileTrackGroups();
}

juce::Result Session::groupTracks(std::vector<int> tracks, const juce::String& name)
{
    if (tracks.empty())
        return juce::Result::fail("Select one or more tracks to group.");
    for (const auto track : tracks)
        if (isReturnTrack(track))
            return juce::Result::fail("A return track stays with the returns, outside any group.");
    const auto stack = groupSlotsOf(audioTracks());
    // A bus brings its whole group, which is what lets one group go inside a
    // new one.
    const auto moving = withGroupsWhole(stack, tracks);
    if (moving.empty())
        return juce::Result::fail("Select one or more tracks to group.");

    // One past the highest id the document has used, so an id freed by an
    // undone grouping is never handed out twice.
    auto nextId = 1;
    auto groupsSoFar = 0;
    for (const auto& slot : stack)
        if (slot.bus != noGroup)
        {
            ++groupsSoFar;
            nextId = std::max(nextId, slot.bus + 1);
        }

    // The group opens where the topmost of them already was, so grouping
    // moves as little as it can, and inside whatever group that one was in,
    // so grouping tracks inside a group makes a group inside it.
    const auto topmost = moving.front();
    const auto plan = gatherTracks(stack, moving, topmost - 1, nextId,
                                   {nextId, stack[static_cast<size_t>(topmost)].claim, false});
    if (const auto refused = refuseDeepNesting(plan.slots); refused.failed())
        return refused;

    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Group tracks");
    auto bus = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr, false);
    if (bus == nullptr)
        return juce::Result::fail("The group track could not be created.");
    bus->setName(name.trim().isEmpty() ? "Group " + juce::String(groupsSoFar + 1) : name.trim());
    bus->state.setProperty(trackGroupBusID, nextId, &undoManager);
    ensureSceneSlots();
    ensureTrackMixers();
    applyGroupGathering(plan, bus.get());
    closeUndoStep();
    // Tracks moved and a group's members changed; no kind says that, so
    // every deck is counted behind.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addTrackToGroup(int track, int groupId)
{
    return addTracksToGroup({track}, groupId);
}

// The group keeps its place, so adding to a group inside another never moves
// it out of that one; what joins lands at the end of its run.
juce::Result Session::addTracksToGroup(std::vector<int> tracks, int groupId)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    std::vector<int> joining;
    for (const auto track : tracks)
    {
        if (!juce::isPositiveAndBelow(track, trackCount()))
            continue;
        if (isReturnTrack(track))
            return juce::Result::fail("A return track stays with the returns, outside any group.");
        // The group itself, or one around it, would have to go inside itself.
        if (const auto own = trackGroup(trackGroupBusId(track)); own && own->covers(group->busTrack))
            return juce::Result::fail("A group cannot go inside itself.");
        if (trackGroupId(track) != groupId)
            joining.push_back(track);
    }
    if (joining.empty())
        return juce::Result::fail("Select a track that is not already in that group.");
    const auto stack = groupSlotsOf(audioTracks());
    const auto moving = withGroupsWhole(stack, joining);
    const auto plan = gatherTracks(stack, moving, lastStaying(*group, moving), groupId);
    if (const auto refused = refuseDeepNesting(plan.slots); refused.failed())
        return refused;
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Add to group");
    applyGroupGathering(plan, nullptr);
    closeUndoStep();
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Out of the middle of a run would split the group in two, so the track - a
// bus with its whole group - slides below what stays before it leaves, into
// the group around the one it left. A level up is never too deep.
juce::Result Session::removeTrackFromGroup(int track)
{
    const auto id = trackGroupId(track);
    if (id == noGroup)
        return juce::Result::fail("That track is not in a group.");
    const auto group = trackGroup(id);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    const auto stack = groupSlotsOf(audioTracks());
    const auto moving = withGroupsWhole(stack, {track});
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Remove from group");
    applyGroupGathering(gatherTracks(stack, moving, lastStaying(*group, moving), group->parent), nullptr);
    closeUndoStep();
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// One track at a time, addressed by id: each one slides out of its run before
// it is let go, and that renumbers everything below it. A track inside a bus
// that is leaving its own group goes with that bus rather than on its own.
juce::Result Session::removeTracksFromGroup(std::vector<int> tracks)
{
    const auto existing = audioTracks();
    const auto groups = trackGroups();
    const auto leavesWithBus = [&](int track)
    {
        return std::any_of(groups.begin(), groups.end(), [&](const TrackGroup& group)
        {
            return group.contains(track) && memberGroupOf(existing, group.busTrack) != noGroup
                && std::find(tracks.begin(), tracks.end(), group.busTrack) != tracks.end();
        });
    };
    std::vector<te::EditItemID> leaving;
    for (const auto track : tracks)
        if (juce::isPositiveAndBelow(track, existing.size()) && memberGroupOf(existing, track) != noGroup
            && !leavesWithBus(track))
            leaving.push_back(existing[track]->itemID);
    if (leaving.empty())
        return juce::Result::fail("Those tracks are not in a group.");
    for (const auto id : leaving)
    {
        const auto current = audioTracks();
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

// Lets a group's members go into the group around it, or to the main output,
// while the bus is still there. The order is the point: undo replays a
// transaction backwards, so deleting the bus after this means undo brings the
// bus back first and only then routes the members into it. Routed after the
// deletion, undo pointed them at a bus that did not exist yet and the routing
// was lost. Groups inside it keep their own members. Inside the caller's
// transaction.
void Session::dissolveGroupInEdit(int groupId)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return;
    const auto tracks = audioTracks();
    for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
        if (memberGroupOf(tracks, track) == groupId)
            setMemberGroup(*tracks[track], group->parent, &edit->getUndoManager());
    reconcileTrackGroups();
}

// Ungrouping takes the bus away and hands its members to the group around it,
// or back to the main output. The tracks themselves, and everything on them,
// survive: what goes is the mixing point, which is the only thing the group
// added.
juce::Result Session::ungroupTracks(int groupId)
{
    const auto group = trackGroup(groupId);
    if (!group)
        return juce::Result::fail("That group is no longer there.");
    const auto busTrack = group->busTrack;
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Ungroup tracks");
    dissolveGroupInEdit(groupId);
    const auto tracks = audioTracks();
    if (juce::isPositiveAndBelow(busTrack, tracks.size()))
        edit->deleteTrack(tracks[busTrack]);
    reconcileTrackGroups();
    closeUndoStep();
    refreshLoop();
    markModified(-1, ModelChange::unknown);
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
    const auto tracks = audioTracks();
    if (!juce::isPositiveAndBelow(group->busTrack, tracks.size()))
        return juce::Result::fail("That group is no longer there.");
    tracks[group->busTrack]->state.setProperty(trackGroupCollapsedID, collapsed, nullptr);
    markModified(group->busTrack, ModelChange::none);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Under an open bus a track joins its group, and under anything inside a
// group it joins the innermost one there. Under anything a folded group
// hides, or a folded group's own bus, it lands below the whole of the
// outermost folded group, in the group around that one: inside the fold it
// would be made at no height and never seen. Under a return is the foot of
// the stack, above the returns. A group being copied is no group to land in.
Session::TrackLanding Session::landingUnder(int below, const std::vector<int>& copied) const
{
    TrackLanding landing;
    if (!juce::isPositiveAndBelow(below, firstReturnTrack()))
        return landing;
    landing.after = below;
    const auto groups = trackGroups();
    const auto isCopied = [&copied](const TrackGroup& group)
    {
        return std::find(copied.begin(), copied.end(), group.busTrack) != copied.end();
    };
    // In stack order, so the first folded group covering it is the outermost.
    for (const auto& group : groups)
        if (group.collapsed && group.covers(below) && !isCopied(group))
        {
            landing.after = group.lastTrack();
            landing.joining = group.parent;
            return landing;
        }
    // And the last covering it the innermost: a bus's own group, since what
    // lands under a bus is its first member.
    for (const auto& group : groups)
        if (group.covers(below) && !isCopied(group))
            landing.joining = group.id;
    return landing;
}

// Where every track sends its audio, decided entirely by where it sits. This is
// the one place routing is written, so a track carried into a band starts
// feeding it and one carried out stops, with nothing left over to go stale.
//
// Every write goes through the undo manager, and every caller runs this inside
// the transaction of the move, group or delete that decided it. One Ctrl+Z
// then puts back where the tracks sat, who is in which group and where their
// audio goes together, rather than leaving the membership behind.
void Session::reconcileTrackGroups()
{
    auto* undoManager = &edit->getUndoManager();
    // The returns keep the foot of the stack. Every path that makes or moves
    // a track already leaves them there; this holds the line for one that
    // does not, by carrying any return found above another track down.
    {
        const auto stack = audioTracks();
        std::vector<te::EditItemID> playing, returns;
        for (auto* track : stack)
            (track->state.hasProperty(trackReturnBusID) ? returns : playing).push_back(track->itemID);
        if (!returns.empty() && firstReturnTrack() != static_cast<int>(playing.size()))
        {
            playing.insert(playing.end(), returns.begin(), returns.end());
            arrangeTrackOrder(playing);
        }
    }
    const auto tracks = audioTracks();
    // Who is inside which group, from where each track sits: a claim holds
    // while its group is open there, a track carried between two tracks of a
    // group joins it, the way a clip dropped on a lane belongs to that lane,
    // and a group nested past the limit is lifted out until it fits
    // (reconcileGroupClaims). A return never joins.
    const auto claims = reconcileGroupClaims(groupSlotsOf(tracks));
    for (int track = 0; track < tracks.size(); ++track)
        if (const auto claim = claims[static_cast<size_t>(track)]; memberGroupOf(tracks, track) != claim)
            setMemberGroup(*tracks[track], claim, undoManager);

    // Now make the audio follow. A track feeds the bus of the group it is
    // directly inside, a nested bus included, so a nested group is a submix
    // feeding the one around it; everything else goes straight to the main
    // output. Down the stack, so a bus is pointed where it now goes before
    // anything is pointed at it, and no step on the way makes a loop.
    std::map<int, te::AudioTrack*> buses;
    for (int track = 0; track < tracks.size(); ++track)
        if (const auto bus = busGroupOf(tracks, track); bus != noGroup)
            buses.emplace(bus, tracks[track]);
    for (int track = 0; track < tracks.size(); ++track)
    {
        te::AudioTrack* destination = nullptr;
        if (const auto found = buses.find(claims[static_cast<size_t>(track)]); found != buses.end())
            destination = found->second;
        auto& output = tracks[track]->getOutput();
        if (destination != nullptr)
        {
            // The engine writes the destination as "track N" and resolves
            // that to a track only when the text changes, so a reorder leaves
            // the text naming whatever now sits at N while the track it
            // resolved stays right - and rewriting the text it already has
            // resolves nothing. Both are checked. Text that already reads
            // right over the wrong track, which inserting a group above a
            // nested one makes routine, is cleared first so it is resolved
            // afresh; left alone, deleting the old destination cut the track
            // off.
            const auto named = "track " + juce::String(destination->getAudioTrackNumber());
            const auto stored = storedOutputName(*tracks[track]);
            if (output.getDestinationTrack() != destination || stored != named)
            {
                if (stored == named)
                    output.setOutputToDefaultDevice(false);
                output.setOutputToTrack(destination);
            }
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
    // The audio has new paths, and a sidechain may now be one of them back.
    clearSidechainLoops();
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
        const auto tracks = audioTracks();
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
