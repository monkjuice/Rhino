---
title: Forge presets and state
type: component
summary: Presets and host state carry one tree; a preset of another format version is refused, and within a version everything is reconciled.
tags: [forge, presets, state, compatibility]
sources: []
updated: 2026-10-03
---

# Forge presets and state

Persistence is `src/ForgeProcessorState.cpp`, with LFO tables in `src/ForgeProcessorLfo.cpp`. A `.forgepreset` and the host's plugin state hold the same `RhinoForgeState` tree; the preset wraps it in `<RhinoForgePreset formatVersion="3" name="...">` and is written through a temporary file.

## What the tree holds

- A `<PARAM id value>` per parameter, as plain values.
- Properties: each oscillator's plate colour (`panelColour_<module>`) and the macro names (`macroName_<n>`), one property each so one changing shifts nothing else.
- Child nodes, only when they differ from what every copy of Forge already has: `TABLE` (a drawn or loaded wavetable, deflated then base64), `SAMPLE` (a spectral sample as 16-bit FLAC then base64; the spectrogram is re-analysed on load) and `ForgeLfoTable` (a custom LFO shape or grid). A patch never points at a file on disk.

Not in it: view settings (tab, the bank on show, envelope zoom, FX scroll) and MIDI bindings, which describe the desk rather than the sound ([Forge MIDI learn](forge-midi-learn.md)). The standalone keeps its patch as `filterState` in `%APPDATA%\Rhino Forge\Rhino Forge.settings`.

## A format version is a hard boundary

`Processor::loadPreset` refuses any `formatVersion` but `presetFormatVersion` (3) with "This preset was saved by a different version of Forge." Format 1 described the synth M1 stripped back; format 3 came with the third oscillator, whose reordered destination list gave format-2 slot indices new meanings ([Stored indices are append-only](append-only-stored-indices.md)). An additive change does not bump it: wavetable nodes arrived inside format 2 (`HANDOVER-M9B.md`), at the accepted cost that an older build loads such a preset and ignores the table.

## Within a version, reconcile

`Processor::migrated` drops parameters Forge no longer has and adds missing ones at their defaults; without that, a preset would leave newer controls holding the previous patch's values. It also repairs history: the LFO and ENV bank renames and source shifts, and the +3 dB on sub and noise levels saved before the mixer. Each repair keys off a mark that migrating clears (an old parameter name, a missing `subPan`), so a second run changes nothing.

## Host state is never refused

`getStateInformation` writes the tree without a version and `setStateInformation` always runs `migrated`, so a Rhino project holding old Forge state still opens. The flip side: a project saved before format 3 reopens with its modulation destinations past B PITCH read in the new numbering (checked against commit `a5103e9`). Rhino's own documents take the opposite line ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Stale and missing

- `PLAN.md` (M1, "Decisions locked in") and `HANDOVER-M9B.md` still say format 2.
- The only bundled preset, `presets/aLiLBitDark.forgepreset`, is format 1, referenced by nothing and refused. The M8 factory set was never authored.
- A hand-written preset for `--snapshot` needs `formatVersion="3"`, or the tool exits 1 without a PNG ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)
- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge spectral oscillator](forge-spectral.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Where Forge's docs disagree with its code](forge-doc-drift.md)
