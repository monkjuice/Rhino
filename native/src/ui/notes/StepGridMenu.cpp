#include "StepGridInternal.h"
#include <algorithm>
#include <cmath>

// The right button. A right-click takes what it landed on the way a press
// would - a note that is not selected becomes the selection, one inside the
// selection keeps it - and opens a menu of what can be done with that: the
// notes' commands over a note or its marker in the velocity lane, the grid's
// over empty cells or empty lane. Nothing is edited until an item is chosen.
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
    auto onePitch = !states.empty();
    for (const auto& state : states)
        if (const auto* note = laneNoteFor(state))
        {
            onePitch = onePitch && (pitch < 0 || note->pitch == pitch);
            pitch = note->pitch;
        }
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
    juce::PopupMenu divide;
    for (const auto parts : {2, 3, 4, 6, 8})
        divide.addItem(divideCommand + parts, juce::String(parts) + " Parts");
    // One pitch at a time, as Ctrl+E divides.
    menu.addSubMenu("Divide       Ctrl+E", divide, onePitch);
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
    if (command > divideCommand && command <= divideCommand + 32)
    {
        divideSelection(command - divideCommand);
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
        case loopCommand:         loopEditedClip(); break;
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
