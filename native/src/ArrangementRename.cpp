#include "ArrangementInternal.h"
#include "Theme.h"

// Renaming a track card or a group band, in place.
//
// One editor serves both, placed over the line the name is already painted on
// so the text does not move when it becomes editable. It is a child of
// laneHeaders, which crops it at the lane viewport exactly as it crops the
// cards' buttons, so an editor on a row scrolled half out of view is clipped
// rather than drawn over the ruler.
//
// Return keeps the new name, Escape abandons it, and clicking away keeps it -
// the same bargain as renaming a file. Nothing here is modal: the transport,
// the browser and every other card stay live while a name is being typed.

namespace rhino
{

void Arrangement::configureNameEditor()
{
    nameEditor.setMultiLine(false);
    nameEditor.setReturnKeyStartsNewLine(false);
    nameEditor.setSelectAllWhenFocused(true);
    nameEditor.setBorder(juce::BorderSize<int>(1));
    nameEditor.setIndents(3, 0);
    nameEditor.setFont(uiFontBold(10.0f));
    nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11161a));
    nameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f4f6));
    nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xffc6d58c));
    nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xffc6d58c));
    nameEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff45535e));
    nameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xfff4f8fa));
    nameEditor.onReturnKey = [this] { endRename(true); };
    nameEditor.onEscapeKey = [this] { endRename(false); };
    nameEditor.onFocusLost = [this] { endRename(true); };
    laneHeaders.addChildComponent(nameEditor);
}

// The name's line on a card. The painter draws into this too, so the editor
// lands exactly where the text it replaces was.
juce::Rectangle<int> Arrangement::trackNameBounds(int track) const
{
    if (!juce::isPositiveAndBelow(track, session.trackCount()))
        return {};
    const auto row = lane(track);
    const auto indent = trackIndent(track);
    return juce::Rectangle<float>(indent + cardControlsWidth + cardDividerWidth,
                                  row.getY() + static_cast<float>(cardControlsTop),
                                  headerWidth - indent - cardControlsWidth - cardDividerWidth, 18.0f)
        .toNearestInt().reduced(6, 0);
}

// The band's name sits between its disclosure and its two buttons, so it is
// measured from both rather than given a width of its own.
juce::Rectangle<int> Arrangement::groupNameBounds(int groupId) const
{
    for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        if (rows[static_cast<size_t>(index)].group == groupId)
        {
            const auto area = rowBounds(index);
            const auto left = static_cast<int>(groupDisclosureBounds(area).getRight()) + 6;
            const auto right = static_cast<int>(groupButtonBounds(area, 0).getX()) - 6;
            return {left, static_cast<int>(area.getY()) + 5, std::max(1, right - left), 15};
        }
    return {};
}

bool Arrangement::isRenaming() const
{
    return renamingTrack >= 0 || renamingGroup > 0;
}

// Positions are inside laneHeaders, which starts at the lanes rather than at
// the top of the panel.
void Arrangement::layoutNameEditor()
{
    if (!isRenaming())
        return;
    const auto area = renamingGroup > 0 ? groupNameBounds(renamingGroup) : trackNameBounds(renamingTrack);
    if (area.isEmpty())
    {
        endRename(false);
        return;
    }
    nameEditor.setBounds(area.translated(0, -static_cast<int>(lanesTop)));
}

void Arrangement::startRename(int track, int groupId, const juce::String& current)
{
    // A rename already running is kept, not abandoned, so starting a second one
    // behaves the way clicking away from the first would.
    endRename(true);
    renamingTrack = track;
    renamingGroup = groupId;
    if (const auto area = renamingGroup > 0 ? groupNameBounds(renamingGroup) : trackNameBounds(renamingTrack);
        area.isEmpty())
    {
        renamingTrack = -1;
        renamingGroup = -1;
        return;
    }
    layoutNameEditor();
    nameEditor.setText(current, juce::dontSendNotification);
    nameEditor.setVisible(true);
    nameEditor.toFront(true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
    repaint();
}

void Arrangement::endRename(bool keep)
{
    if (!isRenaming())
        return;
    // Cleared first: hiding the editor and handing focus back both report a
    // lost focus, which would otherwise come straight back through here.
    const auto track = std::exchange(renamingTrack, -1);
    const auto group = std::exchange(renamingGroup, -1);
    const auto name = nameEditor.getText();
    nameEditor.setVisible(false);
    grabKeyboardFocus();
    if (keep)
    {
        const auto done = group > 0 ? session.setTrackGroupName(group, name)
                                    : session.setTrackName(track, name);
        if (status)
            status(done.failed() ? done.getErrorMessage() : "Renamed to " + name.trim());
    }
    repaint();
}

void Arrangement::renameTrack(int track)
{
    if (session.isMasterTrack(track))
    {
        if (status) status("The main row keeps its name");
        return;
    }
    startRename(track, -1, session.trackName(track));
}

void Arrangement::renameGroup(int groupId)
{
    if (const auto* group = groupById(groupId))
        startRename(-1, groupId, group->name);
}

}
