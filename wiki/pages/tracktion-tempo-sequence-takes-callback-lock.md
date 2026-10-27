---
title: Tracktion's tempo sequence can take the audio callback lock
type: gotcha
summary: edit.tempoSequence.toBeats and toTime apply a pending tempo change on the calling thread, under the audio device's callback lock, so Rhino Arp calling them from a graph worker during a tempo drag deadlocked RhinoDAW on 2026-10-26; code on the audio thread or a worker reads getInternalSequence() instead (b72e67d).
tags: [rhino, tracktion, real-time, audio-thread, tempo, deadlock]
sources: []
updated: 2026-10-26
---

# Tracktion's tempo sequence can take the audio callback lock

Found on 2026-10-26 by reading the pinned engine (`4536d8a`), proved by a test, fixed in commit `b72e67d`.

## Symptom

Lowering the tempo froze RhinoDAW in a project with a Forge track behind [Rhino Arp](rhino-arp.md), its Root automated. It was a hang, not a crash: Windows logged `AppHangTransient` and no `Application Error`. One thread, most likely the WASAPI audio thread, ran at 100% CPU. The message thread and Tracktion's worker-pool threads sat in Wait. The hang is intermittent. The user closed the app before its stacks were read ([Reading a hung RhinoDAW's stacks](reading-a-hung-process.md)).

## Cause

- `TempoSequence::toBeats` and `toTime` call `updateTempoDataIfNeeded()`. If the tempo list's `AsyncUpdater` is pending, that runs `handleUpdateNowIfNeeded()` → `updateTempoData()` on the calling thread. `updateTempoData()` takes `deviceManager.getAudioCallbackLock()` to swap in the new internal `tempo::Sequence`.
- Writing the `bpm` property posts that update (`TempoAndTimeSigListBase::sendChange`). `TempoSetting::setBpm` writes it and then applies the change before it returns, since the internal sequence already holds the new tempo when it does, which cancels the pending update (`updateTempoData` starts with `tempos->cancelPendingUpdate()`). The body of `setBpm` was not read; the likeliest route is `EditTimecodeRemapperSnapshot::remapEdit`, which calls `updateTempoData`. So an update is pending only in a short window inside each tempo change, once per step of a tempo drag.
- The arp converted through those calls every block. On a graph worker inside the window, it blocked on the callback lock. The audio thread holds that lock for the whole callback while it spin-waits for its workers, and the message thread's own `updateTempoData` then blocked on the same lock. On the audio thread itself the lock is re-entrant and nothing hangs, which is why the hang is intermittent.

## Rule

**On the audio thread or a graph worker, convert through `edit.tempoSequence.getInternalSequence()`, never through `edit.tempoSequence.toBeats`, `toTime` or any other `TempoSequence` member that calls `updateTempoDataIfNeeded()`.** Tracktion documents `getInternalSequence` as "only safe to call from the message thread or during audio callbacks". It never applies a pending update and never locks, and it avoids the allocation the old path could make on the audio thread.

## Fix

`native/src/devices/midi/RhinoArpDeviceProcessing.cpp` makes all four of its conversions through a file-local `tempoMap(edit)` that returns the internal sequence. On 2026-10-26 the arp was the only caller of the old calls in `devices/`. A grep found none in `core/` or in the MIDI-thread code (`SessionKeyboards.cpp`, `SessionControllers.cpp`, the Push).

## The test

"a pending tempo change never blocks the arp on a worker thread", in `native/src/tests/ArpTest.cpp` (`native_device_conformance`):

- It opens `setBpm`'s window by writing `tempo.state`'s `IDs::bpm` directly, with no undo manager, as `setBpm` does before its remap. Then it asserts that the internal sequence still shows the old tempo. A first version called `setBpm` itself, which applies the change at once, and passed vacuously. The precondition assert exists to stop that from happening again.
- It holds the callback lock as the audio thread would, renders the arp on a `std::thread`, and requires it to finish within 2 s. It failed on the unfixed arp and passes with the fix.

## Related

- [Rhino Arp](rhino-arp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Reading a hung RhinoDAW's stacks without a debugger](reading-a-hung-process.md)
- [Transport, tempo and loop](transport.md)
