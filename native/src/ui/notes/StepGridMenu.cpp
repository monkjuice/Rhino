#include "StepGridInternal.h"
#include <algorithm>
#include <cmath>

// The right button. A right-click takes what it landed on the way a press
// would - a note that is not selected becomes the selection, one inside the
// selection keeps it - and opens a menu of what can be done with that: the
// notes' commands over a note or its marker in the velocity lane, the grid's
// over empty cells or empty lane, the loop's over the ruler. Nothing is edited
// until an item is chosen.
// It replaced an eraser, which cost a note to every right-click meant as a
// question. Each item is a command the editor already has, labelled with the
// key it already answers to, so the menu is also where those keys are learnt.

namespace rhino
{

void StepGrid::openContextMenu(juce::Point<float> point)
{
    if (!session.hasPatternClip())
        return;
    menuStep = menuPitch = -1;
    juce::ValueTree note;
    if (isOverLaneBlock(point))
    {
        // The lane's edge and its scale are not about any note.
        if (isOverLaneDivider(point) || point.x < labelWidth)
            return;
        if (const auto index = velocityMarkerAt(point); index >= 0)
            note = laneNotes[static_cast<size_t>(index)].state;
    }
    else if (const auto cellIndex = cellHit(point); cellIndex >= 0)
    {
        if (const auto noteIndex = hit(point); noteIndex >= 0)
            note = visibleNotes[static_cast<size_t>(noteIndex)].state;
        else
        {
            menuStep = cellIndex % Session::steps;
            menuPitch = pitchForIndex(cellIndex);
        }
    }
    else
        return;
    grabKeyboardFocus();
    if (note.isValid())
    {
        if (!isSelected(note))
            setSelectedStates({note});
        presentContextMenu(noteMenu());
        return;
    }
    // Empty space keeps the selection, as a right-click on a desktop leaves
    // the selected icons alone, and puts the insert line where it landed, so
    // a paste chosen from the menu goes where the menu was opened.
    setStepInsertPoint(std::floor(stepAtX(point.x)));
    presentContextMenu(gridMenu());
}

juce::PopupMenu StepGrid::noteMenu() const
{
    const auto states = selectedStates();
    auto pitch = -1;
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
            pitch = note->pitch;
    juce::PopupMenu menu;
    menu.addSectionHeader(states.size() == 1 && pitch >= 0 ? rowName(pitch).toUpperCase()
                                                           : juce::String(static_cast<int>(states.size())) + " NOTES");
    menu.addItem(cutCommand, "Cut       Ctrl+X");
    menu.addItem(copyCommand, "Copy       Ctrl+C");
    menu.addItem(pasteCommand, "Paste       Ctrl+V", !noteClipboard.notes.empty());
    menu.addItem(duplicateCommand, "Duplicate       Ctrl+D");
    menu.addItem(deleteCommand, "Delete       Del");
    menu.addSeparator();
    juce::PopupMenu transpose;
    transpose.addItem(octaveUpCommand, "Up an Octave       Shift+Up");
    transpose.addItem(semitoneUpCommand, "Up a Semitone       Up");
    transpose.addItem(semitoneDownCommand, "Down a Semitone       Down");
    transpose.addItem(octaveDownCommand, "Down an Octave       Shift+Down");
    menu.addSubMenu("Transpose", transpose);
    // Each note where it stands, on any pitch: on the grid, as Ctrl+E chops,
    // or into so many equal parts.
    juce::PopupMenu chop;
    chop.addItem(chopCommand, "On Grid       Ctrl+E");
    for (const auto parts : {2, 3, 4, 6, 8})
        chop.addItem(chopCommand + parts, juce::String(parts) + " Parts");
    menu.addSubMenu("Chop", chop, !states.empty());
    menu.addItem(fillCommand, "Fill to Clip End       F");
    menu.addSeparator();
    menu.addItem(selectAllCommand, "Select All       Ctrl+A");
    menu.addSeparator();
    menu.addItem(drawModeCommand, "Draw Mode       B", true, drawMode);
    menu.addItem(velocityLaneCommand, "Velocity Lane", true, velocityLaneShown);
    return menu;
}

juce::PopupMenu StepGrid::gridMenu() const
{
    juce::PopupMenu menu;
    if (menuPitch >= 0)
    {
        menu.addSectionHeader(rowName(menuPitch).toUpperCase() + ", STEP " + juce::String(menuStep + 1));
        menu.addItem(addNoteCommand, "Add Note       Double-click");
    }
    menu.addItem(pasteCommand, "Paste       Ctrl+V", !noteClipboard.notes.empty());
    menu.addSeparator();
    menu.addItem(selectAllCommand, "Select All       Ctrl+A", !laneNotes.empty());
    menu.addSeparator();
    menu.addItem(drawModeCommand, "Draw Mode       B", true, drawMode);
    menu.addItem(velocityLaneCommand, "Velocity Lane", true, velocityLaneShown);
    menu.addItem(loopCommand, "Loop Clip       Ctrl+L");
    return menu;
}

// The ruler's: the loop's options rather than the loop's removal, which a
// right-click there used to do on the spot.
juce::PopupMenu StepGrid::rulerMenu() const
{
    const auto looping = session.hasManualLoopRange();
    juce::PopupMenu menu;
    menu.addSectionHeader("LOOP");
    menu.addItem(loopCommand, "Loop Clip       Ctrl+L");
    menu.addItem(doubleLoopCommand, "Double Loop Length", looping);
    menu.addItem(clearLoopCommand, "Clear Loop", looping);
    return menu;
}

void StepGrid::presentContextMenu(juce::PopupMenu menu)
{
    std::function<void(int)> chosen = [safe = juce::Component::SafePointer<StepGrid>(this)](int command)
    {
        if (safe != nullptr && command != 0)
            safe->performMenuCommand(command);
    };
    if (presentMenu)
    {
        presentMenu(std::move(menu), std::move(chosen));
        return;
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), std::move(chosen));
}

// An item is chosen after the press that opened the menu is over, so nothing
// here can land inside a gesture's undo step.
void StepGrid::performMenuCommand(int command)
{
    if (command >= chopCommand && command <= chopCommand + 32)
    {
        chopSelection(command - chopCommand);
        return;
    }
    switch (command)
    {
        case cutCommand:          cutSelection(); break;
        case copyCommand:         copySelection(); break;
        case pasteCommand:        pasteSelection(); break;
        case duplicateCommand:    duplicateSelection(); break;
        case deleteCommand:       deleteSelection(); break;
        case fillCommand:         fillSelectionToClipEnd(); break;
        case selectAllCommand:    selectAllNotes(); break;
        case drawModeCommand:     toggleDrawMode(); break;
        case velocityLaneCommand: setVelocityLaneShown(!velocityLaneShown); break;
        case loopCommand:         loopEditedClip(); repaint(); break;
        case clearLoopCommand:    session.clearManualLoopRange(); repaint(); break;
        case doubleLoopCommand:
        {
            // From where it starts, twice as long, past the clip if need be:
            // the loop is the song's, and runs on into the next clip.
            const auto range = session.loopRange();
            const auto start = range.getStart().inSeconds();
            if (const auto result = session.setLoopRange(start, start + 2.0 * range.getLength().inSeconds());
                result.failed())
                report(result.getErrorMessage());
            repaint();
            break;
        }
        case octaveUpCommand:     transposeSelection(12); break;
        case semitoneUpCommand:   transposeSelection(1); break;
        case semitoneDownCommand: transposeSelection(-1); break;
        case octaveDownCommand:   transposeSelection(-12); break;
        case addNoteCommand:
            // What a double-click on the cell does.
            if (menuPitch >= 0)
            {
                clearSelection();
                session.beginNoteGesture("Draw note");
                session.setNote(menuStep, menuPitch, true);
                session.endNoteGesture();
            }
            break;
        default: break;
    }
}

}
