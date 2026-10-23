#include "SessionInternal.h"
#include <algorithm>
#include <map>

// Whole tracks on the clipboard: what a track card's cut, copy, paste,
// duplicate and delete act on, as a region is what the lanes' act on.
//
// A copy is the track's own state, copied when it is taken. A paste gives that
// copy new ids throughout - the track, its clips, its cells, its devices - the
// way the engine's own Edit::copyTrack does, so the copy and the original are
// two tracks rather than one track reached twice. Rhino's own names for things
// are renewed with them: a device's key, which its automation lanes name it
// by, and a group's id, which its members name their bus by.

namespace rhino
{
namespace
{
constexpr int noGroup = 0;

// A bus is its group, so it brings its members along; everything else is the
// track it is. In stack order, each once, the main row and anything out of
// range left out.
std::vector<int> wholeGroupsOf(const Session& session, const std::vector<int>& chosen)
{
    std::vector<int> covered;
    for (const auto track : chosen)
    {
        if (!juce::isPositiveAndBelow(track, session.trackCount()))
            continue;
        covered.push_back(track);
        if (const auto group = session.trackGroup(session.trackGroupBusId(track)))
            for (int member = group->firstTrack; member <= group->lastTrack(); ++member)
                covered.push_back(member);
    }
    std::sort(covered.begin(), covered.end());
    covered.erase(std::unique(covered.begin(), covered.end()), covered.end());
    return covered;
}

bool namesMainRow(const Session& session, const std::vector<int>& chosen)
{
    return std::any_of(chosen.begin(), chosen.end(), [&session](int track) { return session.isMasterTrack(track); });
}

void renewKeysIn(juce::ValueTree node, std::map<juce::String, juce::String>& renewed)
{
    if (const auto key = node.getProperty(deviceKeyID).toString(); key.isNotEmpty())
    {
        const auto fresh = juce::Uuid().toString();
        renewed[key] = fresh;
        node.setProperty(deviceKeyID, fresh, nullptr);
    }
    for (auto child : node)
        renewKeysIn(child, renewed);
}

// A lane names its device by key, and the key is on the device, so a copied
// track carries both. Left alone, the copy's devices would answer to the same
// keys as the original's, and a knob taken over by hand on one track would be
// taken over on the other.
void renewDeviceKeys(juce::ValueTree track)
{
    std::map<juce::String, juce::String> renewed;
    renewKeysIn(track, renewed);
    for (auto child : track)
        if (child.hasType(trackAutomationID))
            if (const auto found = renewed.find(child.getProperty(automationDeviceID).toString());
                found != renewed.end())
                child.setProperty(automationDeviceID, found->second, nullptr);
}
}

Session::TrackClipboard Session::copyTracks(const std::vector<int>& chosen) const
{
    TrackClipboard copied;
    const auto tracks = audioTracks();
    for (const auto track : wholeGroupsOf(*this, chosen))
    {
        // A hosted plugin keeps its settings to itself until asked, so the
        // state is brought up to date before it is copied.
        tracks[track]->flushStateToValueTree();
        copied.tracks.push_back(tracks[track]->state.createCopy());
    }
    return copied;
}

juce::Result Session::pasteTracks(const TrackClipboard& clipboard, int below, std::vector<int>* pasted)
{
    return insertTrackCopies(clipboard, below, pasted, clipboard.tracks.size() == 1 ? "Paste track" : "Paste tracks");
}

juce::Result Session::duplicateTracks(const std::vector<int>& chosen, std::vector<int>* duplicated)
{
    const auto covered = wholeGroupsOf(*this, chosen);
    if (covered.empty())
        return juce::Result::fail(namesMainRow(*this, chosen) ? "The main row cannot be duplicated."
                                                              : "Select a track to duplicate.");
    return insertTrackCopies(copyTracks(covered), covered.back(), duplicated,
                             covered.size() == 1 ? "Duplicate track" : "Duplicate tracks");
}

juce::Result Session::insertTrackCopies(const TrackClipboard& clipboard, int below, std::vector<int>* inserted,
                                        const juce::String& action)
{
    if (clipboard.isEmpty())
        return juce::Result::fail("Copy one or more tracks first.");
    const auto carriesGroup = std::any_of(clipboard.tracks.begin(), clipboard.tracks.end(), [](const juce::ValueTree& state)
    {
        return static_cast<int>(state.getProperty(trackGroupBusID, noGroup)) != noGroup;
    });

    // addTrack's rule for where a new track goes, with one more case: a copied
    // group cannot go inside another, so it lands below the whole of one.
    auto after = juce::isPositiveAndBelow(below, trackCount()) ? below : -1;
    auto joining = noGroup;
    const auto groupId = trackGroupBusId(after) != noGroup ? trackGroupBusId(after) : trackGroupId(after);
    if (const auto group = trackGroup(groupId))
    {
        if (group->collapsed || carriesGroup)
            after = std::max(group->busTrack, group->lastTrack());
        else
            joining = group->id;
    }

    // One past the highest id in the document, as grouping takes, and one more
    // for each bus pasted.
    auto nextGroupId = 1;
    for (const auto& group : trackGroups())
        nextGroupId = std::max(nextGroupId, group.id + 1);

    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction(action);
    std::map<int, int> copiedGroups;
    std::vector<te::EditItemID> made;
    te::Track* preceding = nullptr;
    if (const auto tracks = audioTracks(); juce::isPositiveAndBelow(after, tracks.size()))
        preceding = tracks[after];
    for (const auto& source : clipboard.tracks)
    {
        auto state = source.createCopy();
        te::EditItemID::remapIDs(state, nullptr, *edit);
        renewDeviceKeys(state);
        // A copy that recorded the moment it landed would be a surprise, and
        // two tracks armed on one input record the same take twice.
        state.removeProperty(trackArmedID, nullptr);
        // A pasted bus is a new group, and the members copied with it follow
        // it there; any other track is in the group it lands in, or none.
        if (const auto bus = static_cast<int>(state.getProperty(trackGroupBusID, noGroup)); bus != noGroup)
        {
            copiedGroups[bus] = nextGroupId;
            state.setProperty(trackGroupBusID, nextGroupId++, nullptr);
            state.removeProperty(trackGroupMemberID, nullptr);
        }
        else if (const auto found = copiedGroups.find(static_cast<int>(state.getProperty(trackGroupMemberID, noGroup)));
                 found != copiedGroups.end())
            state.setProperty(trackGroupMemberID, found->second, nullptr);
        else if (joining != noGroup)
            state.setProperty(trackGroupMemberID, joining, nullptr);
        else
            state.removeProperty(trackGroupMemberID, nullptr);

        const auto at = preceding != nullptr ? te::TrackInsertPoint(nullptr, preceding)
                                             : te::TrackInsertPoint::getEndOfTracks(*edit);
        auto track = edit->insertTrack(at, state, nullptr);
        if (track == nullptr)
        {
            // The engine's limit on tracks: whatever landed before it goes too.
            undoManager.undoCurrentTransactionOnly();
            repairPatternClip();
            sendSynchronousChangeMessage();
            return juce::Result::fail("The document cannot take any more tracks.");
        }
        made.push_back(track->itemID);
        preceding = track.get();
    }
    // The copies carry their originals' routing, which names a bus by its
    // place in the stack; reconciling points each one where it now sits.
    reconcileTrackGroups();
    ensureSceneSlots();
    ensureTrackMixers();
    undoManager.beginNewTransaction();
    if (inserted != nullptr)
    {
        inserted->clear();
        const auto tracks = audioTracks();
        for (int track = 0; track < tracks.size(); ++track)
            if (std::find(made.begin(), made.end(), tracks[track]->itemID) != made.end())
                inserted->push_back(track);
    }
    // Every lane under the copies moved down, and one may have joined a group.
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::removeTracks(const std::vector<int>& chosen)
{
    const auto doomed = wholeGroupsOf(*this, chosen);
    if (doomed.empty())
        return juce::Result::fail(namesMainRow(*this, chosen) ? "The main row cannot be removed."
                                                              : "Select a track to remove.");
    if (static_cast<int>(doomed.size()) >= trackCount())
        return juce::Result::fail("Keep at least one track.");
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction(doomed.size() == 1 ? "Remove track" : "Remove tracks");
    // By id: each deletion renumbers everything below it.
    std::vector<te::EditItemID> ids;
    {
        const auto tracks = audioTracks();
        for (const auto track : doomed)
            ids.push_back(tracks[track]->itemID);
    }
    // removeAudioTrack's order, for its reasons: a sidechain naming a track
    // that goes loses its source, and a bus lets its members go while it is
    // still there to be routed away from, so undo brings it back first.
    for (const auto id : ids)
        clearSidechainSourcesNaming(id);
    for (const auto track : doomed)
        if (const auto groupId = trackGroupBusId(track); groupId != noGroup)
            dissolveGroupInEdit(groupId);
    for (const auto id : ids)
        for (auto* track : audioTracks())
            if (track->itemID == id)
            {
                edit->deleteTrack(track);
                break;
            }
    repairPatternClip();
    reconcileTrackGroups();
    refreshLoop();
    undoManager.beginNewTransaction();
    markModified(-1, ModelChange::unknown);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
