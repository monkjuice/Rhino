---
title: Forge presets and state
type: component
summary: Presets and host state carry one tree, which also holds the patch's name, author, style, notes and conversion warnings; every arriving patch goes through applySavedState; a preset of another format version is refused, and within a version everything is reconciled.
tags: [forge, presets, state, compatibility]
sources: []
updated: 2026-10-25
---

# Forge presets and state

Persistence is in `src/ForgeProcessorState.cpp`, with the LFO tables in `src/ForgeProcessorLfo.cpp`. A `.forgepreset` and the host's plugin state hold the same `RhinoForgeState` tree. The preset wraps the tree in `<RhinoForgePreset formatVersion="3" name="..." author="..." style="...">` and is written through a temporary file (`writePresetFile`). The wrapper repeats the name, author and style so a list can read them without parsing the tree ([Forge preset browser and bank](forge-preset-browser.md)).

## What the tree holds

- A `<PARAM id value>` per parameter, as plain values.
- Properties: each oscillator's plate colour (`panelColour_<module>`, clamped by `core/ForgePanelColours.h`, the one header for the count and the default that both the state and the panel read since 2026-10-25) and the macro names (`macroName_<n>`), one property each so one changing shifts nothing else.
- Since 2026-10-19, the patch's `PresetInfo` as the properties `presetName`, `presetAuthor`, `presetStyle`, `presetNotes`, `presetSource` and `presetWarnings` (one line per warning). Each is removed when empty, as macro names are. Because the info rides on the tree, a project reopens showing the patch's name. `presetSource` and `presetWarnings` are set only by a conversion ([Forge preset import](forge-preset-import.md)).
- Child nodes, only when they differ from what every copy of Forge already has: `TABLE` (a drawn or loaded wavetable, deflated then base64), `SAMPLE` (a spectral sample as 16-bit FLAC then base64; the spectrogram is re-analysed on load), `ForgeLfoTable` (a custom LFO shape or grid) and, since 2026-10-13, `ForgeLfoPath` (a drawn PATH route as `Point x y` children, the default diamond never written; [Forge LFOs](forge-lfos.md)). A patch never points at a file on disk.
- `ARPSLOT` nodes (since 2026-10-13), one per arp slot that has been left at least once: property `slot` (0-3) and one property per id in `arpSlotParameterIds()` holding the plain value ([Forge ARP plate, slots and waterfall](forge-arp-plate.md)). They are read from the parameter state itself, so they ride through `copyState` with no append or apply step, and `migrated` keeps them because they carry no `id`. **A slot's node is written only when the slot is left**, so the live slot's node is stale in a saved file whenever a knob moved since; harmless, because the only read is a switch *to* a slot, preceded by the write of the slot being left. A slot never visited has no node and opens on the defaults, as does a control a node predates. `liveArpSlot` is set from `arpSlot` in `applySavedState`.

Not in it: view settings (tab, the bank on show, envelope zoom, FX scroll), MIDI bindings and favourite presets. They describe the desk rather than the sound ([Forge MIDI learn](forge-midi-learn.md)). The standalone keeps its patch as `filterState` in `%APPDATA%\Rhino Forge\Rhino Forge.settings`.

## One way a patch arrives

`Processor::applySavedState` applies the tables, samples and LFO tables, then `replaceState(migrated(...))`, then sets the arp slot. Every route that brings in a patch uses it:

- `loadPreset`.
- `setStateInformation`, which clears `presetFile()` because host state does not know the file.
- `initialisePatch`, the INIT patch, which applies an empty tree.
- The importers: `loadPreset` sends a `.SerumPreset` or `.vital` to them and applies the tree they build. `convertPreset` writes such a tree to a file without touching the patch that is playing.

On a `.forgepreset` load, the file's name is the preset's name, so renaming the file renames the patch. The wrapper's `author`, `style` and `notes` outrank the tree's, because the wrapper is what the browser listed.

A load is also what one click in the browser's list costs. On 2026-10-25 `--profile-browser` measured a factory preset at 98 ms median ([Forge preset browser and bank](forge-preset-browser.md#what-it-costs)), and a temporary timing of `applySavedState` split it into three nearly equal parts, each fixed the same day (uncommitted at the time of writing):

- `applyTables` reset every oscillator's built-in table on every load, so the band-limited copies the voice already read were rebuilt (about 37 ms). It now leaves an oscillator alone when `tables.edit(osc).isUntouched()`. A test in `tests/ForgeTestsPresets.cpp` requires a load with no `TABLE` node not to bump `tableStore().revision(0)` for an oscillator already on the built-in table.
- `migrated` called `parameterEntry` once per parameter, each call a walk of the whole tree (about 38 ms). It now gathers the ids present in one pass into a `std::set`.
- `replaceState` (9-35 ms) was mostly the editor laying the rack out once per FX slot whose type moved. `applySavedState` now holds `applyingState` with a `juce::ScopedValueSetter` for the burst of parameter writes, read as `Processor::isApplyingState()`; the editor's per-control reactions stand down while it is set and `Editor::showPresetResult` lays out once afterwards ([Forge editor (panel)](forge-editor.md#rules-that-bite)). `setStateInformation` is not followed by `showPresetResult`; the tick's `fxTypesShown` comparison catches host state.

`presetRevision()` moves on every load, save or info change, so the panel notices on its 24 Hz tick by comparing one number. `presetFileMoved` keeps the loaded file in step when the browser renames or moves it. `wavetableStateNode`, `lfoTableStateNode` and `readWavetableFrames` are free functions, so the importers build nodes exactly as a save does. None of this changed the format version: it is all additive.

## A format version is a hard boundary

`Processor::loadPreset` refuses any `formatVersion` but `presetFormatVersion` (3) with "This preset was saved by a different version of Forge." Format 1 described the synth stripped back; format 3 came with the third oscillator, whose reordered destination list gave format-2 slot indices new meanings ([Stored indices are append-only](append-only-stored-indices.md)). An additive change does not bump it: wavetable nodes arrived inside format 2, at the accepted cost that an older build loads such a preset and ignores the table, and the `arpSlot` parameter and `ARPSLOT` nodes arrived inside format 3. So did FILTER 2 (2026-10-22). An older preset or host state opens it at its defaults, with every route off and `filterSerial` on MAIN, so nothing reaches it and the patch sounds as it did ([Forge filter](forge-filter.md)).

## Within a version, reconcile

`Processor::migrated` drops parameters Forge no longer has and adds missing ones at their defaults; without that, a preset would leave newer controls holding the previous patch's values. It also repairs history: the LFO and ENV bank renames and source shifts, the +3 dB on sub and noise levels saved before the mixer, and a `lfo{n}Shape` of 4, sample and hold when it was a shape, which becomes Type = S&H and Shape = 0. Each repair keys off a mark that migrating clears (an old parameter name, a missing `subPan`, a shape past the list), so a second run changes nothing. The `subPan` mark is coarse: a preset that simply never wrote `subPan`, as the 18 original factory presets do not, is raised too ([A Forge preset with no subPan loads its sub 3 dB hotter](forge-presets-without-subpan-load-hotter.md)).

## Host state is never refused

`getStateInformation` writes the tree without a version and `setStateInformation` always runs `migrated`, so a Rhino project holding old Forge state still opens. The flip side: a project saved before format 3 reopens with its modulation destinations past B PITCH read in the new numbering (checked against commit `a5103e9`). Rhino's own documents take the opposite line ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## The factory set

`presets/Factory/` holds 29 presets, all generated by `presets/make-presets.py`: the 18 originals of 2026-10-19 at its root (basses, leads, keys, pads, percussion, a sequence that uses the arp, SFX and an Init Saw template) and, since 2026-10-23, eleven dubstep growls, leads and subs in `Factory/Drum n Bass/`, which the bank shows as a folder. Edit the script, not the files. The script's vocabulary, the design rules behind the Drum n Bass set and how a preset is checked are on [Writing Forge factory presets](writing-forge-factory-presets.md).

Each preset holds only the `PARAM`s that differ from Forge's defaults. The script starts every preset from a `BASE` that switches off what a fresh Forge demos: oscillators B and C, the sub, the noise, and matrix slot 1's stale default. It writes the name, author and style on both the wrapper and the tree. On first use the set is copied into the player's bank ([Forge preset browser and bank](forge-preset-browser.md)). Only the Drum n Bass presets write `subPan` and `noisePan`, so only they load their sub at the level the script states ([A Forge preset with no subPan loads its sub 3 dB hotter](forge-presets-without-subpan-load-hotter.md)).

## Stale and missing

- `presets/aLiLBitDark.forgepreset` is still format 1. Nothing references it, the current build refuses it, and it is not part of the factory set.
- A hand-written preset for `--snapshot` needs `formatVersion="3"`, or the tool exits 1 without a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Forge preset browser and bank](forge-preset-browser.md)
- [Writing Forge factory presets](writing-forge-factory-presets.md)
- [A Forge preset with no subPan loads its sub 3 dB hotter](forge-presets-without-subpan-load-hotter.md)
- [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)
- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge LFOs](forge-lfos.md)
- [Forge ARP plate, slots and waterfall](forge-arp-plate.md)
- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Hosting Forge in Rhino](forge-hosting.md)
