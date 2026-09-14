# Session view: paused, and how to resume it

The session view is a working clip launcher that is **switched off in the shell**.
Its model, its UI and its tests are all built and green; nothing in the running
application reaches it. This document says what exists, what does not, and what
you need to know before turning it back on.

Paused September 2026, after the launcher, the shared mixer and clip copying
were finished and manually exercised.

## Turning it back on

One line, in `native/src/Main.cpp`:

```cpp
static constexpr bool sessionViewEnabled = false;   // set to true
```

That restores the **Session** / **Arrange** buttons in the control bar, the
**Tab** shortcut, and the **Back to Arrangement** button. Put the `Tab` line back
in the keyboard-shortcut list in `showHelpMenu` at the same time, and revisit the
README, which currently describes the feature as present but disabled.

Nothing else is stubbed out. `Session`'s scene and slot API runs regardless, so
every project already carries eight scenes and a clip slot per track whether or
not the view is reachable.

## Why it was paused

Two views over one project raises more questions than it first appears, and the
answers matter more than the code. The launcher works; what is unfinished is the
set of behaviours around it. Rather than guess at them, development stopped at a
point where everything present is correct.

## What exists

| Area | Files |
| --- | --- |
| Scenes, clip slots, launching, clip copying between views | `native/src/SessionSlots.cpp` |
| Mixer: volume, pan, mute, solo, main output | `native/src/SessionMixer.cpp` |
| The view itself | `native/src/SessionView.h`, `SessionView.cpp`, `SessionViewPainter.cpp`, `SessionViewGestures.cpp` |
| Tests | `native/src/tests/Arrangement/scenarios/SessionView.inc`, `SharedMixer.inc`, `ClipRoundTrip.inc` |

Working today:

- Tracks are columns, scenes are rows, every cell is a clip slot.
- Slots are filled from the browser, a right-click menu, or an audio file drop.
- Clips launch on a chosen quantisation, with playing and queued states drawn.
- A track plays one clip at a time; a scene row launches together and stops
  tracks whose slot in that row is empty.
- Per-track stop buttons, a stop-all button, and add-scene / add-track.
- A mixer strip that edits the same track state the arrangement edits.
- Copying a clip either way between the views, via right-click in either view.
- **Back to Arrangement**, which hands every overridden track back at once.

## The model, in one paragraph

Launching is Tracktion's, not Theta's. `ClipSlot` holds at most one clip,
`SceneList` keeps the slot lists aligned across tracks, and each clip exposes a
`LaunchHandle` whose `play`/`stop` the message thread queues and the audio thread
acts on. The engine raises a track's `playSlotClips` flag itself once a slot is
actually playing, and clearing that flag stops the track's slot clips. Launch
positions come from the playback context's sync point; before that context exists
the first launch is immediate. `SceneWatcher` notifies the UI only when a slot's
play or queue state genuinely changes, which is what the view repaints from.

Two rules are Theta's rather than the engine's, and both live in `SessionSlots.cpp`:
a track plays at most one slot clip, so launching stops that track's others at the
same quantised beat; and launching a scene stops tracks whose slot in that row is
empty.

## What is shared between the two views, and what is not

This caused the most confusion during development, so it is worth stating plainly.
It matches Live, which was checked against the manual rather than assumed.

**Shared, because both views read the same `Session`:** tracks and their order,
track names, devices and FX, the mixer (volume, pan, mute, solo), the main output,
tempo, time signature and transport. Creating a track in either view creates one
track, which then appears in both with the same name, type and devices. Deleting
or renaming behaves the same way.

**Per-view, and should stay that way:** selection, focus, scroll and zoom.
Selecting a track in the session view does not move the arrangement's selection.
The Device View follows whichever view is currently showing.

**Genuinely separate:** the clips. Slot clips belong to scenes, timeline clips
belong to the arrangement, and neither view can or should display the other's.
The Live manual is explicit: "The Session clips and the Arrangement clips in one
track are mutually exclusive: Only one can play at a time." Adding a pattern to a
slot therefore does not put anything in the arrangement, which is correct and not
a bug. The only routes across are `copySlotClipToArrangement` and `copyClipToSlot`,
and both copy rather than move.

## Known gaps, most important first

### 1. One instrument per track (resolved)

Kept here because it shaped the design. **This is now implemented**; no action
is needed.

Live gives a track exactly one instrument, with MIDI effects before it and audio
effects after it, and racks nest chains rather than adding a second instrument to
the track's own chain. Logic is the same: a software instrument track has one
instrument slot, and Track Stacks group several tracks rather than stacking
instruments on one. That rule is what makes a clip launcher coherent, because the
instrument belongs to the track, a slot clip is only note data, and clips on a
track are therefore interchangeable.

Theta used to keep every instrument on a track at once and toggle `setEnabled`
so one was audible. `switchTrackInstrument` now replaces instead: the new
instrument goes in at the old one's index, keeping MIDI effects before it and
audio effects after it, and the previous instrument is removed along with its
patch. Clips are untouched, so the pattern stays and the instrument under it
changes. `collapseStackedInstruments` migrates projects saved under the old
model on load, keeping whichever instrument was enabled.

The consequence for slots is that a preset still carries an instrument with it,
so dropping a preset into a slot sets the whole track's instrument. That is
inherent to bundling notes with a sound and is worth revisiting if slot clips
ever need to be freely mixed on one track, but it is no longer tangled up with a
stack of dormant plugins.

### 2. The note and drum editor cannot open a slot clip

Confirmed by inspection, and the clearest reason the two views feel unequal:
double-clicking a session clip gets you nothing, while an arrangement clip loads
into the editor below.

Two separate things block it.

- `Session::findClip` (`SessionClips.cpp`) walks `te::getAudioTracks(*edit)` and
  calls `Track::findClipForID`, which searches a track's own clip list and its
  sub-tracks. A slot clip is owned by its `ClipSlot`, which is a `ClipOwner` but
  not a track clip list, so `findClip` never finds it. `selectPatternClip` fails
  for exactly this reason, and so would `editClip`, `deleteClip` and anything
  else keyed on a clip ID.
- `SessionView` never tries to select a clip at all. A click launches; there is
  no double-click or modifier path that asks for editing.

Tracktion already provides the missing half of the lookup:
`findClipSlotForID (const Edit&, EditItemID)` in `tracktion_EditUtilities.h`.

The obvious fix is to let `findClip` fall back to searching clip slots. Be
careful: that instantly widens what every existing caller can reach. The
arrangement passes clip IDs to `editClip`, `splitClip`, `duplicateClip` and
`deleteClip`, and those assume a clip on a timeline with a position, a track and
a place in the arrangement's undo story. A slot clip has none of that in the same
sense. Decide deliberately whether to widen `findClip` and audit its callers, or
to add a separate slot-aware lookup and route only the editor through it. The
second is smaller and safer; the first is tidier if the callers turn out to cope.

Also note `Session::ensureEditablePatternClip`, which runs after undo, redo and
project load: it uses `findClip(patternClipID)` and silently falls back to the
first MIDI clip on track 0 when that returns null. If a slot clip ever becomes
the edited pattern without `findClip` seeing slots, the editor will quietly jump
to a different clip after any undo.

### 3. Per-track Back to Arrangement

Live shows a small handback button **on each track** whose session clips are
overriding the timeline, in addition to the global one, with the tooltip:
"When you play a clip on this track in the Session View, this button will appear
to indicate that the current state differs from the state which is stored in the
Arrangement."

Theta only has the global version. The model is already per-track — the flag is
`AudioTrack::playSlotClips`, and `Session::anyTrackPlayingSlots` just folds it
across tracks. A per-track button needs a per-track getter, a per-track clear,
and a spot in the arrangement's track header. That header is already crowded at
small lane heights, which is why `Arrangement::showTrackMixer` hides the fader
below 56px; the same treatment likely applies.

### 4. No drag between views, and no Arrangement Record

Live offers three routes across: copy and paste, dragging over the view
selectors, and recording a session performance into the arrangement. Theta has
the first only, as menu items. Arrangement Record is the big one and the reason
people build arrangements from session jams: it logs launched clips onto the
timeline at the positions they played. That needs a recording mode, a write path
from `LaunchHandle` state into timeline clips, and a decision about what happens
to existing arrangement material underneath.

### 5. Smaller missing pieces

- **Level meters** on the mixer. Live shows peak and RMS per track. This needs a
  level-measuring plugin per track and a bounded-rate UI read; do not poll the
  audio thread from paint.
- **Sends and returns.** There are no return tracks at all, so the Sends and
  Returns sections of the mixer do not exist.
- **Track rename and colour.** Neither view can rename a track or recolour one,
  so tracks are stuck with `Audio 1`, `Audio 2` and so on.
- **Follow actions.** The engine supports them (`tracktion_FollowActions.h`) and
  the node builder already reads them; nothing in Theta sets them.
- **Recording into slots.** No arm, no input monitoring per slot.
- **Scene rename.** `Scene::name` exists and `sceneName` falls back to
  "Scene N"; nothing writes it.
- **Clip length and quantisation per clip.** Every inserted preset slot clip is
  one bar. Live lets each clip carry its own launch quantisation and loop length.

## Design decisions worth keeping

- **`UtilityDevice` is not the track fader.** It is a device in a chain, like
  Live's Utility. The fader is a `VolumeAndPanPlugin` at the end of the track's
  plugin list, added on load to tracks from older documents. The two bottom-bar
  gain sliders that used to drive Utility devices were replaced by the main
  output level, because they read as a second, contradictory mixer.
- **Copies, not references, between views.** Wave clips re-reference the same
  source file rather than duplicating media; MIDI sequences are cloned. The loop
  range flips with the destination, since a slot clip repeats until stopped and
  an arrangement clip plays its span once.
- **The view paints its cells** rather than building a component per slot, so the
  grid costs one repaint per launch-state change. The mixer strip underneath is
  real components, because sliders and buttons want to be.

## Testing

The three scenario files run inside the arrangement workflow runner and cover the
slot model, the shared mixer in both directions, and the clip round trip
including save and reopen. They are bare statement blocks sharing one `Session`,
so they depend on the state earlier scenarios leave behind.

```powershell
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

**Expect `native_arrangement_workflow` to fail intermittently.** This is
pre-existing and unrelated: measured at 6 of 12 runs before the session view
existed and 5 of 12 with it. Every failing run stops between
`scenario("gestures: clip drag and trim")` and `scenario("persistence: track
state")` in `GesturesAndPersistence.inc`, which is the clip copy/paste and
selection block — **not** at teardown, despite what HANDOVER.md originally said.
Re-run before assuming you broke something, and see HANDOVER.md for the
correction.
