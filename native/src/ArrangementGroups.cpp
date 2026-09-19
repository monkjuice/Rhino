#include "ArrangementInternal.h"
#include "Theme.h"
#include <algorithm>

// Track groups in the arrangement: the header band, the spine down its members'
// cards, the pointer gestures on both, and the multi-card selection grouping
// acts on.
//
// A group is one row in the stack, drawn above the first of its members. It
// owns no clips of its own; collapsed, it summarises its members' clips so a
// folded group still shows where its music is.
//
// Every pointer on the group's row is painted rather than a child component:
// groups come and go with the edit, and a repainted band has no lifetime to
// keep in step with the session.

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

const Session::TrackGroup* Arrangement::groupStartingAt(int track) const
{
    for (const auto& group : groups)
        if (group.firstTrack == track)
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

// A card inside a group starts further right, and the band above it does not.
float Arrangement::trackIndent(int track) const
{
    return groupContaining(track) != nullptr ? groupIndent : 0.0f;
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
    selectedGroup = -1;
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
    selectedGroup = -1;
    const auto working = isTrackSelected(track) ? track : selectedTracks.back();
    if (selectedTrack != working)
    {
        selectedTrack = working;
        if (trackSelected) trackSelected(working);
    }
}

// Selecting a band selects everything under it, so a command aimed at the group
// and a command aimed at its cards reach the same tracks.
void Arrangement::selectGroup(int groupId)
{
    const auto* group = groupById(groupId);
    if (group == nullptr)
        return;
    const auto first = group->firstTrack;
    const auto last = group->lastTrack();
    selectedTracks.clear();
    for (int track = first; track <= last; ++track)
        selectedTracks.push_back(track);
    trackSelectionAnchor = first;
    if (selectedTrack != first)
    {
        selectedTrack = first;
        if (trackSelected) trackSelected(first);
    }
    setSelection({});
    selectedGroup = groupId;
    focus = Focus::group;
}

int Arrangement::groupRowAt(juce::Point<float> point) const
{
    if (point.y < lanesTop || point.y >= masterLane().getY())
        return -1;
    const auto row = rowAt(point.y);
    return row >= 0 && rows[static_cast<size_t>(row)].group >= 0 ? row : -1;
}

juce::Rectangle<float> Arrangement::groupDisclosureBounds(juce::Rectangle<float> row) const
{
    return {6.0f, row.getY() + (row.getHeight() - 14.0f) * 0.5f, 16.0f, 14.0f};
}

// Index 0 is mute, 1 is solo, both right-aligned in the header so the name
// keeps every pixel between the disclosure and them.
juce::Rectangle<float> Arrangement::groupButtonBounds(juce::Rectangle<float> row, int index) const
{
    constexpr auto width = 26.0f, gap = 3.0f;
    const auto right = headerWidth - 8.0f - static_cast<float>(1 - index) * (width + gap);
    return {right - width, row.getY() + (row.getHeight() - 16.0f) * 0.5f, width, 16.0f};
}

bool Arrangement::beginGroupGesture(const juce::MouseEvent& event)
{
    const auto row = groupRowAt(event.position);
    if (row < 0)
        return false;
    const auto groupId = rows[static_cast<size_t>(row)].group;
    const auto* group = groupById(groupId);
    if (group == nullptr)
        return false;
    // Read what is needed before touching the session: a session call rebuilds
    // the row stack, and with it the group this points at.
    const auto name = group->name;
    const auto collapsed = group->collapsed;
    const auto bounds = rowBounds(row);

    if (event.mods.isRightButtonDown())
    {
        selectGroup(groupId);
        repaint();
        showGroupMenu(groupId);
        return true;
    }
    if (!event.mods.isLeftButtonDown())
        return true;
    if (event.position.x < headerWidth)
    {
        if (groupDisclosureBounds(bounds).contains(event.position))
        {
            const auto done = session.setTrackGroupCollapsed(groupId, !collapsed);
            if (status)
                status(done.failed() ? done.getErrorMessage()
                                     : (collapsed ? "Expanded " : "Collapsed ") + name);
            return true;
        }
        if (groupButtonBounds(bounds, 0).contains(event.position))
        {
            session.setTrackGroupMuted(groupId, !session.trackGroupMuted(groupId));
            return true;
        }
        if (groupButtonBounds(bounds, 1).contains(event.position))
        {
            session.setTrackGroupSoloed(groupId, !session.trackGroupSoloed(groupId));
            return true;
        }
    }
    selectGroup(groupId);
    repaint();
    if (event.getNumberOfClicks() == 2 && event.position.x < headerWidth)
        renameGroup(groupId);
    return true;
}

void Arrangement::paintGroupRow(juce::Graphics& g, int row)
{
    const auto* group = groupById(rows[static_cast<size_t>(row)].group);
    const auto area = rowBounds(row);
    if (group == nullptr || area.isEmpty())
        return;
    const auto colour = bandColour(*group);
    const auto bandSelected = selectedGroup == group->id;
    const auto full = area.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f);
    g.setColour(juce::Colour(0xff161b20));
    g.fillRect(full);

    const auto header = area.withX(0.0f).withWidth(headerWidth);
    g.setColour(colour.withAlpha(bandSelected && focus == Focus::group ? 1.0f : 0.78f));
    g.fillRect(header);
    if (bandSelected)
    {
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(header.withWidth(3.0f));
    }

    // Right for a folded group, down for an open one: the arrow points at what
    // clicking it reveals.
    const auto disclosure = groupDisclosureBounds(area);
    juce::Path arrow;
    if (group->collapsed)
        arrow.addTriangle(disclosure.getX() + 4.0f, disclosure.getY(),
                          disclosure.getX() + 4.0f, disclosure.getBottom(),
                          disclosure.getRight() - 3.0f, disclosure.getCentreY());
    else
        arrow.addTriangle(disclosure.getX() + 1.0f, disclosure.getY() + 3.0f,
                          disclosure.getRight() - 1.0f, disclosure.getY() + 3.0f,
                          disclosure.getCentreX(), disclosure.getBottom() - 2.0f);
    g.setColour(colour.contrasting(0.85f));
    g.fillPath(arrow);

    g.setFont(uiFontBold(10.0f));
    const auto nameLeft = static_cast<int>(disclosure.getRight()) + 6;
    const auto nameRight = static_cast<int>(groupButtonBounds(area, 0).getX()) - 6;
    {
        juce::Graphics::ScopedSaveState scope(g);
        const auto nameArea = juce::Rectangle<int>(nameLeft, static_cast<int>(area.getY()) + 6,
                                                   std::max(1, nameRight - nameLeft), 14);
        g.reduceClipRegion(nameArea);
        drawSnappedText(g, group->name + "  (" + juce::String(group->trackCount) + ")", nameArea);
    }

    // Read from the cached band rather than through the session's own group
    // lookups: this runs every frame, and those rebuild the whole band list.
    const auto everyMember = [this, group](bool Session::TrackMixer::*flag)
    {
        for (int track = group->firstTrack; track <= group->lastTrack(); ++track)
            if (!(session.trackMixer(track).*flag))
                return false;
        return true;
    };
    const auto muted = everyMember(&Session::TrackMixer::muted);
    const auto soloed = everyMember(&Session::TrackMixer::soloed);
    for (int index = 0; index < 2; ++index)
    {
        const auto box = groupButtonBounds(area, index);
        const auto on = index == 0 ? muted : soloed;
        g.setColour(on ? juce::Colour(index == 0 ? 0xff97634c : 0xff657440) : juce::Colour(0x38101418));
        g.fillRect(box);
        g.setColour(juce::Colour(on ? 0xfff0f4f6 : 0xcc11161a));
        g.setFont(uiFontBold(9.0f));
        g.drawText(index == 0 ? "M" : "S", box.toNearestInt(), juce::Justification::centred, false);
    }
    g.setFont(uiFont(10.0f));

    // A folded group keeps showing where its music is: its members' clips,
    // flattened onto the one row the group still has.
    if (group->collapsed)
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(headerWidth), static_cast<int>(area.getY()),
                                                std::max(1, getWidth() - static_cast<int>(headerWidth) - 14),
                                                std::max(1, static_cast<int>(area.getHeight()))));
        for (const auto& clip : clips)
        {
            if (!group->contains(clip.track))
                continue;
            const auto position = displayedPosition(clip);
            const juce::Rectangle<float> box {xFor(position.start), area.getY() + 5.0f,
                                              std::max(2.0f, xFor(position.end) - xFor(position.start)),
                                              area.getHeight() - 10.0f};
            const auto visible = box.getIntersection(area);
            if (visible.isEmpty())
                continue;
            g.setColour((clip.colour.isTransparent() ? juce::Colour(0xff284b59) : clip.colour).withAlpha(0.75f));
            g.fillRect(visible);
        }
    }

    g.setColour(juce::Colour(0xff11161a));
    g.drawHorizontalLine(static_cast<int>(area.getBottom()) - 1, 0.0f, full.getRight());
}

// The gap the indent opens on a member's card, filled with the group's colour.
// It runs the full height of every member, so a group reads as one block with a
// step in its left edge rather than as cards that happen to share a colour.
void Arrangement::paintGroupSpine(juce::Graphics& g, int track, juce::Rectangle<float> row)
{
    const auto* group = groupContaining(track);
    if (group == nullptr)
        return;
    g.setColour(bandColour(*group).withAlpha(0.9f));
    g.fillRect(groupSpineLeft, row.getY(), groupIndent - groupSpineLeft, row.getHeight());
}

void Arrangement::showGroupMenu(int groupId)
{
    const auto* group = groupById(groupId);
    if (group == nullptr)
        return;
    const auto name = group->name;
    const auto colour = group->colour;
    const auto collapsed = group->collapsed;
    const auto anchorRow = trackRowIndex.empty() ? juce::Rectangle<float>() : lane(group->firstTrack);

    juce::PopupMenu menu;
    menu.addSectionHeader(name);
    menu.addCustomItem(1, std::make_unique<TrackSwatches>(colour,
        [safe = juce::Component::SafePointer<Arrangement>(this), groupId](juce::Colour chosen)
        {
            if (safe != nullptr) safe->session.setTrackGroupColour(groupId, chosen);
        }), nullptr);
    menu.addItem(2, "No colour", !colour.isTransparent());
    menu.addSeparator();
    menu.addItem(3, "Rename...");
    menu.addItem(4, collapsed ? "Expand" : "Collapse");
    menu.addItem(5, "Ungroup tracks");
    const auto anchor = localPointToGlobal(anchorRow.getTopLeft().toInt());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea({anchor.x, anchor.y, 1, 1}),
        [safe = juce::Component::SafePointer<Arrangement>(this), groupId, collapsed](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            if (choice == 2) safe->session.setTrackGroupColour(groupId, {});
            else if (choice == 3) safe->renameGroup(groupId);
            else if (choice == 4) safe->session.setTrackGroupCollapsed(groupId, !collapsed);
            else if (choice == 5)
            {
                safe->selectedGroup = groupId;
                safe->ungroupSelection();
            }
        });
}

void Arrangement::renameGroup(int groupId)
{
    const auto* group = groupById(groupId);
    if (group == nullptr)
        return;
    const auto current = group->name;
    auto* window = new juce::AlertWindow("Rename group", "New name for " + current + ":",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", current, {});
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<Arrangement>(this), groupId, window](int result)
        {
            const auto name = window->getTextEditorContents("name");
            delete window;
            if (result != 1 || safe == nullptr) return;
            const auto done = safe->session.setTrackGroupName(groupId, name);
            if (safe->status)
                safe->status(done.failed() ? done.getErrorMessage() : "Renamed the group to " + name.trim());
        }), false);
}

void Arrangement::groupSelectedTracks()
{
    std::vector<int> tracks;
    for (const auto track : selectedTracks)
        if (juce::isPositiveAndBelow(track, session.trackCount()))
            tracks.push_back(track);
    if (tracks.empty())
    {
        if (status) status("Select one or more track cards to group");
        return;
    }
    // The group forms where the topmost selected card already was, so this is
    // the row the new band lands on.
    const auto first = *std::min_element(tracks.begin(), tracks.end());
    const auto count = tracks.size();
    const auto result = session.groupTracks(std::move(tracks), {});
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    selectGroup(session.trackGroupId(first));
    repaint();
    if (status)
        status("Grouped " + juce::String(count) + " track" + (count == 1 ? "" : "s")
               + " - Ctrl+Shift+G ungroups");
}

void Arrangement::ungroupSelection()
{
    const auto groupId = selectedGroup > 0 ? selectedGroup : session.trackGroupId(selectedTrack);
    if (groupId <= 0)
    {
        if (status) status("Select a group to ungroup");
        return;
    }
    const auto* group = groupById(groupId);
    const auto name = group != nullptr ? group->name : juce::String("the group");
    const auto result = session.ungroupTracks(groupId);
    selectedGroup = -1;
    if (focus == Focus::group)
        focus = Focus::track;
    if (status) status(result.failed() ? result.getErrorMessage() : "Ungrouped " + name);
    repaint();
}

}
