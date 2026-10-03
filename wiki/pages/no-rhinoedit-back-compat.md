---
title: No backward compatibility for .rhinoedit
type: decision
summary: Rhino owes older .rhinoedit documents nothing; a project that opens wrong is fixed in the current save and load path, not by a migration.
tags: [rhino, project-files, persistence]
sources: []
updated: 2026-10-03
---

# No backward compatibility for .rhinoedit

The user stated this on 2026-09-20; no repo doc records it. Rhino does not owe documents written by older builds anything. When a project fails to open or plays wrong, fix the path a *current* save and load take. Do not add a migration or a tolerance for old shapes.

## Context

That day a project saved by the same day's build reopened silent, and one made of samples lost the track its samples were on. Calling it a legacy-format problem would have hidden the real defect: reopening re-derived state the document already recorded. `restoreProject` took track 0 to be the pattern track, but in a document whose stack opens with a group, track 0 is a bus. It then made sure that track ran an instrument. Commit `0bf26c7` removed both. Its scenario `GroupBusReload.inc` renders the reopened project, because only a render says it went quiet.

## Decision

- Load what the document says and stop. A load path decides nothing about what a track runs, is called or contains, as a comment in `Session::restoreProject` says.
- When an accommodating branch gets in the way, delete it rather than widen it.
- The format gate stays. `buildStarterEdit` writes `rhinoFormatVersion` 1, and `restoreProject` refuses anything else with "This is not a supported Rhino native project."

## Alternatives considered

The earlier plans assumed versioned migration. `research/ROADMAP.md` lists file migration under Stage 5 hardening, and the quality gates in `INSTRUMENT_PLAN.md` say to preserve old Rhino Wave projects. Both predate 2026-09-20.

## Consequences

- Accommodations that still run on every load are the kind of code this targets; each can go when it next gets in the way:
  - `collapseStackedInstruments`, for tracks that stacked instruments ([One instrument per track](one-instrument-per-track.md));
  - `migrateLegacyTrackGroups`, for groups from before [a group was a bus](group-is-a-bus-track.md).
- `ensureTrackMixers` and `ensureSceneSlots` also run on load, but they are not legacy-only: they equip any track that
  lacks a fader or slots, including lanes the current build makes without a fader
  ([Hazards found while seeding the wiki](known-hazards.md)). Keep them until no path can create such a track.
- Design a new property so its absence means the old default. `rhinoTrackType` is written only for MIDI, so every older document reads as audio and nothing had to be migrated ([Track kinds: audio and MIDI](track-kinds.md)).
- When a project will not open, look for the fix in the save path that wrote it ([Debugging a crash only one project triggers](debugging-a-crashing-project.md)).

## Contrast with Forge

Forge draws the line inside its own files. A `.forgepreset` from another `presetFormatVersion` (3 today, in `src/ForgeProcessorState.cpp`) is refused outright. Within one version, `Processor::migrated` reconciles the parameters. Host state carries no version at all and is always reconciled, so a Rhino project holding older Forge state still opens.

## Related

- [Project files (.rhinoedit)](project-files.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Track groups (bus tracks)](track-groups.md)
- [Forge presets and state](forge-presets-and-state.md)
