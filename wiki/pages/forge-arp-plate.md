---
title: Forge ARP plate, slots and waterfall
type: component
summary: The painted plate between Forge's keys and note guide - a lamp, four slot buttons that swap the five pattern panes' 25 parameters through ARPSLOT nodes on the state tree, a door to the overlay, and a well where every note the voices play falls, and a held one hangs and brightens.
tags: [forge, arpeggiator, ui, state, real-time]
sources: []
updated: 2026-10-13
---

# Forge ARP plate, slots and waterfall

The plate on the keyboard shelf that fronts the [Forge arpeggiator](forge-arpeggiator.md). It is painted on the chassis, not built of components, so its geometry (`ui/ForgePlacement.h`), its hit test (`Editor::mouseDown`, `src/ForgeEditorInput.cpp`) and its tooltips (`Editor::getTooltip`, `src/ForgeEditorSpectral.cpp`) all read the same rectangles. Written on 2026-10-13 for commit `8f28dc7` on branch `feat/forge-arp-slots-and-waterfall`.

## Where it stands

`keyboardBounds` starts at the shelf's left edge against the wheels, `arpPlateBounds` follows the keys and `noteGuidePlateBounds` follows the plate. Until 2026-10-13 the plate stood at the keys' left end, where Serum puts its switch; it moved, at the same width, so the lamp, the slot buttons and the chord helper are found at one end. Inside the housing is a chamfered face (`arpPlateFace`, the plate reduced 7 x 9, cut `arpPlateFaceCut` = 5) holding, left to right: the lamp and the name, a 72 px column that is the door (`arpPlateNameBounds`); four buttons two by two (`arpSlotGridBounds`, `arpSlotButtonBounds`, `arpSlotAt`); and the well, whatever the face has left (`arpWaterfallBounds`), which gives up width first. The lamp switches the arp on, a button chooses a slot, anything else toggles the overlay. The plate is taken before anything a tab owns, since it is reachable from every tab.

The lamp says the arp runs; the lit outline, drawn to the face's own chamfer (`chamferedPath`, the well's cut) at fill 0.05 and stroke 0.32, only says the door is open. The two are kept distinct because an arp running with its panes put away and an arp on screen but switched off are both ordinary.

## Two halves

The metal, the well, the engraved rule after the name column and the sky (`drawArpSky`, a faint violet haze and a fixed-seed scatter of stars) are `drawArpPlateHousing` in `ui/ForgePanels.h`, inside the cached chassis layer. Everything that moves - lamp, outline, buttons, sparks - is `drawArpPlateState` in `ui/ForgeArpPlate.h`, included from `ui/ForgeVisuals.h`, and is kept to fills of small shapes; the only stroked path is the open outline. Drawn whole in the live pass the plate cost about 3 ms a frame ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)).

## Slots

Four configurations, `arpSlotCount` in `core/ForgeArp.h`, one of them the live parameters. A slot holds the 25 controls of PATTERN, TRANSPOSE, PLAYBACK, RETRIGGER and VELOCITY, listed once in `arpSlotParameterIds()`; `arpEnable`, `arpLaunchQuant` and `arpSlot` itself (the ARP pane) are shared, so choosing another slot while playing changes the pattern and nothing else. `arpSlot` is an `AudioParameterChoice` "1".."4" (`src/ForgeParameters.cpp`), a parameter rather than a view setting so a host lane or a MIDI-learned pad can switch patterns while playing. The arp reads the live parameters and knows nothing about slots.

`Processor::applyArpSlot()` (`src/ForgeProcessorState.cpp`, "The arp's slots") compares the parameter with `liveArpSlot` and, on a change, writes the live values into the slot being left (`storeArpSlot`) and reads the chosen slot's into the parameters through `setValueNotifyingHost` (`recallArpSlot`). It runs on the Processor's own 60 Hz timer after `drainMidiControl()` (`src/ForgeProcessorMidi.cpp`), because parameters are written on the message thread only, so a slot chosen with no editor open still takes effect. `Editor::chooseArpSlot` (`src/ForgeEditorPages.cpp`) calls it straight after a press so the knobs follow at once, and `Editor::paint` lights the button `arpSlot` names, not the one the Processor has reached. The slots are `ARPSLOT` child nodes of the state tree and ride in presets and host state with no extra step; the live slot's node may be stale in a saved file, harmlessly - see [Forge presets and state](forge-presets-and-state.md).

`chassisParameters()` in `ui/ForgeModules.h` names `arpSlot`, so the layout test's rule that every parameter appears on the panel finds a control painted on the chassis; it needs a tooltip in `ui/ForgeTooltips.h` like any other.

## The waterfall

Every note handed to `Core::noteOn`, from a key or from the arp, is also pushed into `NoteFlashQueue` (`core/ForgeNoteFlash.h`): `NoteFlash {note, velocity}` through a 64-deep single-producer, single-consumer ring like `MidiControlQueue`, dropping when full and never blocking ([Real-time audio rules](real-time-audio-rules.md)). `Editor::gatherNoteSparks` (`src/ForgeEditorInput.cpp`, every 24 Hz tick) pops them through `Processor::popNoteFlash` into `NoteSpark`s: position across the well from the note's place in the keyboard's range (33..96), size from velocity, a random pace of 0.85-1.15 and warmth, a random `seed`, born now. Sparks past the floor are dropped and the list is capped at `maxSparks` = 48; at the cap `addNoteSpark` drops the oldest *released* spark, never a held one. A spark is drawn from its age alone (`sparkProgress`: a `sparkFallMs` = 1500 ms fall with a mild ease, lit over the first tenth, put out over the last third), never stepped per frame. The editor drains the queue in its constructor, since it holds the last sixty-odd notes of a session with no editor open.

### A held note hangs and charges

Added 2026-10-13, uncommitted on `main` when written; the constants in `ui/ForgeArpPlate.h` may still be tuned after visual review. A note still held does not fall away: it parks at `sparkRestProgress` = 0.6 (before the fade at 0.7), its trail stretches to the top of the well as a column, and over `sparkChargeMs` = 1800 ms parked it turns whiter, its halo and column brighter, until "ultra": a white burst (`sparkUltraFlashMs`), then column and body swell up to `sparkUltraSwellPx` = 3 in a ripple running down into the light, and `sparkUltraMotes` = 10 stardust motes twinkle and drift around it. Released, the hold dims over `sparkLetGoMs` = 320 and the spark falls the remaining 0.4 at the standard pace, because the fall clock stops while parked (`sparkParkedMs`, subtracted in `sparkProgress`). `sparkHold` returns the whole state from the spark and the clock.

- **Held state crosses as a set, not as note-offs.** `HeldNotes` (`core/ForgeNoteFlash.h`) is 128 bits in two lock-free `std::atomic<std::uint64_t>`, read through `Processor::noteHeld`. Note-offs in `NoteFlashQueue` were rejected: it drops when full, and a dropped release would leave a light in ultra forever. A set cannot fall behind.
- **Ordering:** the audio thread sets a note's bit before pushing its flash, and `gatherNoteSparks` pops the queue first and calls `releaseNoteSparks` with `noteHeld` after, so a spark just popped is never seen as already released.
- **"Held" means held by a voice.** Every `Core` note-on and note-off - keys, the arp's steps, `arp.flush` when the arp is switched off - goes through `Processor::playNote`/`stopNote`, which keep the set; all-notes-off (CC 123) and `prepareToPlay` clear it. With the arp on, a note is held for its gate, not for as long as the key is down.
- Every spark is born held (`releasedMs` < 0). Striking a note again releases its older held spark: only the newest strike of a note can hang.
- **A note released before it reaches rest (about 0.9 s) draws pixel for pixel as before**, because every hold effect is driven by parked time, which is zero for it. A first draft attached the column from the press, changed every short note's trail, and was reworked for that reason.
- Still cheap on the frame path: fills of small shapes, no strokes. Motes are placed by `sparkNoise(seed, a, b)`, an integer hash of the spark's seed and the clock, so nothing is stored or stepped per frame. The column is cut into ~3 px whole-pixel slices; slices overlapping by half a pixel band into visible lines.

## Tests

- `forge_layout` (`tests/ForgeTestsLayout.cpp`) sweeps every allowed size in 20 px steps and requires keys, plate and guide in that order without overlap; lamp, name, grid and well inside the face and clear of each other; and the well at least 60 px wide (76 at the minimum window, about 134 at 1440 wide, about 284 at 2000). It also holds `arpSlotParameterIds()` to the panes both ways and keeps the ARP pane's controls out of it.
- `slotSuite` and `flashSuite` in `tests/ForgeTestsArp.cpp`: switching, returning, defaults for an unvisited slot, the shared lamp and LAUNCH QUANT, round trips through a `.forgepreset` and through `getStateInformation`; both routes into the flash queue, and `noteHeld` for keys (across blocks, note-off, all-notes-off, re-prepare) and for the arp (held for its gate only).
- `sparkSuite` (area `arp`, same file): the hold's timing (an early release unchanged, parking, monotonic charge, ultra only after it, the resumed fall and let-go), the retrigger and cap rules, `sparkNoise`, and painted checks on a 120x60 software image. The stardust check samples after the ultra flash, because the flash alone lights that corner.
- `--snapshot ... ARP` shows the plate open with notes falling; `ARPHELD` adds four notes held into ultra beside two short ones falling ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Forge arpeggiator](forge-arpeggiator.md)
- [Forge editor (panel)](forge-editor.md)
- [Forge presets and state](forge-presets-and-state.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
- [Real-time audio rules](real-time-audio-rules.md)
