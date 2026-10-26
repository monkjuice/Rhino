---
title: Note editor tools panel
type: component
summary: A 168 px column down the note editor's left, modelled on Live 12's Clip View, holding the scale highlight (any root, twelve scales) and Humanize, Reverse and Legato, which act on the selected notes or the whole clip in one undo step by rules the model owns.
tags: [rhino, ui, notes, scale, model]
sources: []
updated: 2026-10-26
---

# Note editor tools panel

Commit `2220948` (2026-10-26, at the user's request) added `NoteToolsPanel` (`native/src/ui/notes/NoteToolsPanel.*`), modelled on Live 12's Clip View Pitch and Time Utilities (manual pp. 217-220, 268-270) and its Scale controls (pp. 49-50, 206-207). Nobody has used it in the running app yet.

## Where it stands

- **A sibling of the grid, not a child.** `MainComponentLayout.cpp` takes `min(NoteToolsPanel::width, pane width / 3)` (168 px) off the left of the lower pane and gives `StepGrid` the rest, so the grid's own geometry gained no x offset. It shows only with the note editor's face ([App shell and control bar](app-shell.md#the-lower-pane)). `MainComponent` owns it after the grid (`NoteToolsPanel noteTools {session, grid}`).
- **It holds no notes.** The commands are the editor's, because the selection is: the panel keeps only the scale it shows and the Humanize amount, both view settings of the run, saved neither in the document nor the settings file.
- **It never takes the keys.** No control wants keyboard focus, and every click hands focus back to the grid, so B, the arrows and the clipboard keys keep working.
- Disabled with no MIDI clip open; the SCALE section also on a drum clip, whose pad rows are never lit. It catches up when shown, like the other hidden faces ([What a change costs the interface](ui-cost-of-a-change.md)).

## SCALE

A root chooser (twelve pitch classes), a scale chooser and a Highlight switch. They replaced the shell's single "Scale: off / C Major ... B Minor" combo (`MainComponent::scaleHighlight`, in the Clip/Devices strip), which offered major and minor only. The chooser offers every `MusicalScale` in `core/dsp/ScaleQuantizer.h` but Chromatic, which would light every row: twelve, the set [Rhino Tune](rhino-tune.md) corrects to and the [Push](ableton-push.md) lays its pads in. Choosing a root or a scale switches Highlight on, since lighting the rows is all the choice does; the switch turns it off and on. `StepGrid::setScaleHighlight(bool shown, int root, MusicalScale)` replaced `setScaleHighlight(int comboId)`, and the painter reads `maskForScale(scaleKind, scaleRoot)`.

## NOTES: Humanize, Reverse, Legato

A Humanize amount bar (0-100 %, default 25 % where Live's is 10 %, double-click returns to 25 %) beside the Humanize button, then Reverse and Legato. Each calls `StepGrid::humanizeNotes(amount)`, `reverseNotes()` or `legatoNotes()` (`StepGridTools.cpp`), sharing `applyNoteTool`: it does nothing while a gesture holds the undo step, ends a chop or held V first, brackets the model call in `beginNoteGesture`/`endNoteGesture` so the tool is one undo step, and reports through `StepGrid::status` ("Reversed 3 notes", "Made every note in the clip legato", or the model's refusal).

The rules are the model's, `model/SessionNoteTools.cpp`. Live's rule picks the notes: those selected, or with none selected every note of the open clip. Only notes whose shown start lies inside the clip (0 to the clip's length in steps, `clipLengthSteps`) are acted on, at `editorNotes`' shown positions, so a note begun before the clip's window moves from step 0 and loses its hidden part, as a chop does ([Note editor](note-editor.md#a-note-that-began-before-the-window)).

- **Reverse** mirrors the notes in time across a span: the selection's own (earliest start to latest end) or the whole clip. Ends past the clip's end are cut there first, so no mirror image lands before the span. A mirror never makes the targets overlap each other, but it can land one on an unselected note of its pitch, and then it refuses ("Select them too.").
- **Legato** takes each note to the next distinct start among the targets (a chord's notes share theirs), the last to the clip's end (Live says the loop end; Rhino's loop is the song's), and never past the next note on its own pitch, selected or not. It can shorten as well as lengthen.
- **Humanize** moves each start by a uniform random drift up to amount x `Session::humanizeReachSteps` (0.25) of a step either way: Live's "a quarter of a grid division", the division being the editor's step (1/16, 1/32 or 1/64). Per pitch in time order, a note is held between the previous note's new end and the next note's start, inside the clip, and clamped only when it already stands inside that range, so it never moves further than its drift and notes that overlapped before stay put. Lengths are kept, and 0 % refuses. `humanizeNotes(states, amount, seed)` takes a seed so a drift can be repeated; the grid passes `juce::Random::getSystemRandom().nextInt64()`.
- All three write through the private `placeNotes`, which panics the track's MIDI before the first change while playing and restarts playback after, as `moveNotes` does, then marks `ModelChange::notes` and announces once.

## Tests

`tests/Arrangement/scenarios/NoteTools.inc` ("note tools", after `NoteMenu`, on a document of its own) checks the layout, the scale lighting by painted pixels on a software image (D Dorian lights C and D and not C#, D major the reverse), the switch, the twelve scales, each tool's exact positions, kept selection and single undo step, Legato's same-pitch stop, Humanize's bounds over 24 seeds, its determinism by seed and its 0 % refusal, and Reverse's refusal. `RHINO_NOTE_TOOLS_SNAPSHOT=<png>` writes the panel beside the editor ([Seeing the UI without taking the screen](headless-ui-snapshots.md)). `PartialRepaint.inc` now lights the scale with `setScaleHighlight(true, 0, MusicalScale::Major)`.

## Not done

No keyboard shortcuts and no right-click menu items for the three tools; the panel cannot be collapsed.

## Related

- [Note editor (StepGrid)](note-editor.md)
- [App shell and control bar](app-shell.md)
- [Session, the model](session-model.md)
- [Rhino Tune](rhino-tune.md)
