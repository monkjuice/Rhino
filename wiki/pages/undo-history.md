---
title: Undo and the history
type: component
summary: One Ctrl+Z takes back one named step whole and leaves it to redo - Session::undo and redo fold unnamed steps into the named one before them, so every step a person makes is named; every command closes with closeUndoStep, which first does the re-sorts Tracktion would write later; a control set with no drag opens a named loose step, a note command its own NoteStep; an open gesture holds Tracktion's undo timer off; and an undo moves the engine's parameters to the values it restored.
tags: [rhino, model, undo, tracktion]
sources: []
updated: 2026-10-28
---

# Undo and the history

The history is the Tracktion edit's `juce::UndoManager`. `Session` (`native/src/model/Session.cpp`, `Session.h`) decides what goes into it and what Ctrl+Z takes out. The rules below date from commit `8d2d883` (2026-10-28), after the user reported: "Ctrl+Z is the buggiest operation, sometimes I need to press it multiple times to undo an action, some actions don't even undo." Each section says what was wrong before.

## Every step a person makes is named

- **`Session::undo`** undoes any unnamed steps on top, then one named step. **`redo`** redoes one step, then any unnamed steps after it. An unnamed step counts as bookkeeping that belongs to the named step before it, such as an engine re-sort or a knob handed back from its lane.
- **`canUndo()`** is true only while a named step exists. It answers at once when the newest step has a name, and reads every step's name only otherwise.
- **So a model command that writes without naming its step is folded into the previous action** under one Ctrl+Z. The `BrowserDrops` scenario showed this when its unbracketed `setNote` joined the drop before it.
- Both do nothing while a bounce or an export runs ([A long command runs in slices behind a modal screen](long-commands-run-in-slices.md)).

## How a command writes

- **A command opens a named step and closes it with `Session::closeUndoStep()`**, never with a bare `beginNewTransaction()`. The only bare call left in `model/` is the one inside `closeUndoStep`, which has 86 callers. `closeUndoStep` first runs `sortAsTheEngineWill`, which makes the engine's later re-sorts and follow actions part of this step ([Tracktion re-sorts the edit through the undo manager a message later](tracktion-resorts-through-undo-manager.md)). Then it forgets the loose control and closes the step.
- **A refusal after the step opened takes the step back** with `undoCurrentTransactionOnly`. This includes `addDevice`, whose late refusals used to leave a ghost "Add ..." step open, and both late refusals in `finishBounce`, which used to leave a half-done bounce open. It also includes `addClipDevice`, which now opens its step before it makes the plugin: making the plugin writes the plugin's first settings through the undo manager, and those writes were an unnamed step of their own. Other commands do the same ([Session, the model](session-model.md#what-every-command-does)).
- **A note command opens its own named step unless a note gesture is open.** `Session::NoteStep` is an RAII guard at the top of `addNote`, `removeNotes`, `setNoteVelocities`, `setNote`, the resizes, `ensurePatternLengthSteps`, the fills, the moves, `redistributeNotes`, `chopNotes`, `reverseNotes`, `legatoNotes` and `humanizeNotes`. It counts as a gesture while it runs, so inside a gesture, or inside another note command, it does nothing.
- **A knob drag is a named step.** `beginDeviceParameterGesture` opens "Change <parameter name>" when no other parameter gesture is open. Before, it opened none, so a drag wrote into whatever step was open: a wheel notch or double-click just before it went under the same Ctrl+Z, and a drag on its own had no name. The fader, pan, main and send gestures name theirs as before ([Mixer](mixer.md)).
- **A control set with no drag opens a named loose step** through `openLooseControlStep`. This covers `setDeviceParameter`, `setTrackVolumeDb`, `setTrackPan`, `setMasterVolumeDb`, `setMasterPan` and `setTrackSendDb` called outside a gesture: a double-click reset, a wheel notch or a Push encoder. These used to leave unnamed steps open. A run on the same control, such as several wheel notches, stays one step until something closes it: the next command, an undo, or Tracktion's timer (below).
- **View settings write with no undo manager.** `setEditorStepCount`, the note editor's resolution, did not until 2026-10-28, so it left an unnamed step open.

## A gesture holds Tracktion's undo timer off

Tracktion's `Edit::UndoTransactionTimer` calls `beginNewTransaction()` 350 ms after any change through the undo manager, unless a mouse button is down anywhere or an `Edit::UndoTransactionInhibitor` is held. Tracktion relies on that timer to end its steps. For Rhino it split every gesture held without a mouse button at any pause: V held over the velocity lane, Ctrl held over a chop, a finger on a Push encoder, tap tempo. `Session::updateUndoTimerHold` now holds an inhibitor while any gesture is open (note, note velocity, audio clip, tempo, device parameter or mixer), and runs wherever a gesture's depth changes. `unwatchEdit` releases the inhibitor, because `te::Edit::~Edit` asserts that none is held.

## After an undo or a redo

`refreshAfterUndoRedo` does three things:

- **It moves the engine's parameters to the values the undo restored** (`followRestoredParameters`). It calls `updateFromAttachedValue` on every parameter that is not on a curve: in each track's and clip's plugin list, in the master list, and on the master fader. Tracktion's `AutomatableParameter::valueTreePropertyChanged` deliberately does not push a restored value into a parameter attached to a `CachedValue` (`tracktion_AutomatableParameter.cpp` at `4536d8a`). So until 2026-10-28, undoing a fader, pan, send, the main level, Rhino EQ or the engine's own devices (Compressor, Delay, Reverb, 4OSC) restored the stored value, but the knob and the sound stayed where they were. Rhino's SDK devices were not affected, because `NativeDevice` follows its own state.
- **It takes back the default an undo makes the engine write.** When the undo removed a stored value altogether (a knob never moved off its default), the engine writes the default back through the undo manager as it follows. `followRestoredParameters` takes that one transaction back at once with `undoCurrentTransactionOnly`, which keeps the redo steps.
- **It makes no starter clip** (`repairPatternClip(false)`), and neither does a command that takes itself back. The note editor then has no clip until one is chosen, and `hasPatternClip()` is false.

**Nothing may write through the undo manager after an undo.** JUCE's `UndoManager::perform` moves the redo steps aside as soon as anything new is performed. So such a write throws redo away and leaves an unnamed step on top. Two writes did this:

- `repairPatternClip` made a hidden starter clip whenever an undo had taken away the note editor's clip on a first MIDI track with no other MIDI clip. Every further Ctrl+Z undid that clip and the repair made it again, so undo was stuck for good.
- The automation mirror handed a knob back to its base value with `setParameter`, which writes the knob's attached `CachedValue` through the undo manager ([Track automation](automation.md#how-it-plays)).

## History size

`watchEditTree` keeps at least 500 steps, and more while the history stays under 50 million units (`setMaxNumberOfStoredUnits(50 * 1000 * 1000, 500)`). Tracktion's default (`te::Edit::getDefaultNumUndoLevels`) is 30 steps, which a session of clip edits used up within minutes.

## In the interface

- `FloatingDeviceWindow`, a device's own window in `ui/rack/DeviceRack.cpp`, forwards Ctrl+Z and Ctrl+Y to `DeviceRack::shortcut`. Before, the two keys did nothing while it had focus. It forwards only those two, so a letter typed into a plugin's field never reaches the shell.
- A double-click on a Rhino EQ band handle now ends the two gestures its second press began ([Rhino EQ](rhino-eq.md#the-face)).
- `~AudioClipPanel` ends an open warp or envelope gesture, as `editWillChange` already did.

## Tests

`tests/Arrangement/scenarios/UndoSteps.inc` runs as the scenario "undo steps: one Ctrl+Z, one thing done, all of it, left to redo". It pumps the Windows message queue with a `settle` lambda, because the runners dispatch no message loop and most of these bugs arrived through it. That is why it is the first thing `runArrangementTest` does, before the shared `Session session;` exists, and works only on documents of its own.

At first it ran after `GroupUndo.inc`. There, its pump delivered messages that earlier scenarios had posted for documents already destroyed. One full CTest run crashed with 0xC0000005 in `RhinoDAW.exe`, at a fault offset in `te::ExternalControllerManager::detachFromEdit` (an edit being torn down), while two other runs passed. The rule this taught is in [Writing Rhino tests](writing-rhino-tests.md#rules) (rule 25), and the map that named the offset is [Naming a Release stack with a linker map](naming-a-release-stack-with-a-linker-map.md). It requires:

- a new document has nothing to undo;
- once the loop has run, each of these is one step: a created clip, an import, a split, a duplicated track, a cell's clip and a clip device;
- the note editor's resolution is no step;
- one Ctrl+Z takes a split back whole and leaves it to redo;
- Ctrl+Z moves the engine's Compressor and a fader back, and redo moves them again;
- a loose knob set before a drag on another knob stays when the drag is undone;
- a fader held through a 600 ms pause is one step (Windows only);
- undoing a lane on a knob the curve had moved leaves the lane to redo;
- forty renames all undo;
- the stuck undo: an empty MIDI track moved to the top, a clip made on it and opened, then undone, leaves redo, and the next Ctrl+Z takes the move back.

## How the causes were found

A temporary probe scenario, removed before the commit, ran about 100 `Session` commands against a real document. For each command it:

1. counted the undo steps and checked whether a step was left open;
2. undid once;
3. diffed the whole edit `ValueTree` and every plugin parameter's current value against the state before the command;
4. redid.

It pumped the message queue by hand, as `settle` does. Without that, the re-sorts, the timer and the follow actions never ran, and the runners saw none of the bugs. To find the engine code behind a write nobody had made, it captured stacks in a `ValueTree::Listener` and named them through a linker map ([Naming a Release stack with a linker map](naming-a-release-stack-with-a-linker-map.md)). The same audit is worth running again before changing how steps open or close.

## Open issues

- **An external plugin's parameters are not undoable at all** (Rhino Forge, Rhino Duck, any VST3). A rack knob or the plugin's own window changes the processor only, and nothing is written to the `ValueTree`. Fixing it needs a custom `juce::UndoableAction` per gesture ([Mapped parameters](mapped-parameters.md)).
- **Undo leaves an automation lane overridden.** Undoing a knob move that overrode a lane restores the value but leaves the lane overridden, because `AutomationRuntime::overridden` is session state, not document state. Live re-enables the automation.
- **Ctrl+Z during a mouse drag undoes the drag's open step**, and the drag then goes on writing. Only the note editor's velocity gestures swallow the key ([Note editor velocity lane](note-velocity-lane.md#holding-the-bracket-safely)).
- **The hand-back's fallback still records a step.** It applies to an engine device whose stored property name differs from its parameter id, such as the Reverb's "room size" against `roomSize`. After an undo, that step discards redo ([Track automation](automation.md#how-it-plays)).
- **A gesture's end can name a different control than its start.** A device knob's gesture end works its parameter out again from the face's current index, so a Drum Rack auto-select or an EQ band change during a drag ends a different parameter. A send knob's end runs even when its begin was skipped. Both affect only announcements and counters, not the history.

## Related

- [Session, the model](session-model.md)
- [Tracktion re-sorts the edit through the undo manager a message later](tracktion-resorts-through-undo-manager.md)
- [Naming a Release stack with a linker map](naming-a-release-stack-with-a-linker-map.md)
- [Mixer](mixer.md)
- [Return tracks and sends](return-tracks.md)
- [Track automation](automation.md)
- [Note editor (StepGrid)](note-editor.md)
- [Note editor velocity lane](note-velocity-lane.md)
- [Device rack and device editors](device-rack.md)
- [Rhino EQ](rhino-eq.md)
- [Writing Rhino tests](writing-rhino-tests.md)
