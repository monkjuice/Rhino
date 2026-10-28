---
title: Naming a Release stack with a linker map
type: guide
summary: Name the functions behind a captured stack or a crash's fault offset in the Release RhinoDAW, which has no PDB, by relinking with /MAP (a link-only change), recording return addresses as offsets from the exe's base with CaptureStackBackTrace, and naming each one after the nearest preceding symbol in RhinoDAW.map; on 2026-10-28 this named the Tracktion code behind the undo history's unnamed steps and a test crash in an edit's teardown.
tags: [rhino, debugging, windows, tooling, tracktion]
sources: []
updated: 2026-10-28
---

# Naming a Release stack with a linker map

The Release `RhinoDAW.exe` is built without a PDB, so a stack captured inside it is a list of bare addresses ([Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)). A linker map names those addresses without a debugger, a PDB or a recompile. On 2026-10-28, in the work for commit `8d2d883`, this method found which Tracktion code wrote steps nobody had asked for into the undo history ([Tracktion re-sorts the edit through the undo manager a message later](tracktion-resorts-through-undo-manager.md)).

## 1. Relink with a map

Configure with `-DCMAKE_EXE_LINKER_FLAGS_RELEASE="/INCREMENTAL:NO /MAP"`, then build. Only the link changes, so nothing recompiles, and the linker writes `RhinoDAW.map` beside the exe. The map describes only the binary from that link, so capture the stacks with that binary. Afterwards, configure again with the flag's previous value. The build tree reconfigures only when told to ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).

## 2. Capture the stack where it happens

In a temporary test scenario, hook the event you want to explain. For the undo history, that was a `ValueTree::Listener` attached to the edit's state while the scenario pumped the message queue by hand, with its `valueTreeChildOrderChanged` and `valueTreeChildAdded` capturing a stack. Call `CaptureStackBackTrace`. Record each frame as an RVA, its offset from the exe's base: the address minus `GetModuleHandle(nullptr)`. Print the RVAs and name them outside the process.

## 3. Look the addresses up

The map lists each public symbol with its `Rva+Base`: its address at the preferred load address, which is `0x140000000` for this 64-bit exe. A small Python script did the lookup:

1. Read the symbols.
2. Subtract `0x140000000` to get each symbol's RVA, and sort them.
3. Name each captured RVA after the nearest symbol at or before it.

The names are MSVC-decorated, but readable enough as they are.

A crash needs no capture at all. The `Application Error` event's fault offset in `RhinoDAW.exe` is already an RVA ([Debugging a crash only one project triggers](debugging-a-crashing-project.md)), so look it up the same way, in the map from the link that built the crashing binary.

Treat a name as a lead, not proof. An inlined function has no entry of its own, so its address falls under whichever symbol comes before it. Confirm a name against the source at the pinned commit before acting on it ([Directories not to read](off-limits-directories.md)).

## What it named

On 2026-10-28 the method named three engine functions: `te::TrackList::handleAsyncUpdate`, `te::ClipOwner::ClipList::sortClips`, and `te::MidiClip::getFollowActions`, which the playback graph reaches through `createNodeForLauncherClips`. The same day, a CTest run crashed with 0xC0000005 in `RhinoDAW.exe`, and the map named the fault offset as `te::ExternalControllerManager::detachFromEdit`. That was an edit being torn down while a scenario pumped messages posted for documents already destroyed ([Writing Rhino tests](writing-rhino-tests.md#rules), rule 25). The same steps apply to any Release-only behaviour that a scenario can catch, or that leaves a fault offset.

## Related

- [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)
- [Tracktion re-sorts the edit through the undo manager a message later](tracktion-resorts-through-undo-manager.md)
- [Undo and the history](undo-history.md)
- [Debugging a crash only one project triggers](debugging-a-crashing-project.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Writing Rhino tests](writing-rhino-tests.md)
