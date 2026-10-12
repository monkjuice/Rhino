---
title: Tracktion's hosted device is silent until its start is dispatched
type: gotcha
summary: Driving Tracktion's real playback graph headless through its hosted audio device renders nothing, the transport not even moving, until te::DeviceManager::dispatchPendingUpdates runs the preparation the device's start posted to a message loop the test runners never dispatch.
tags: [rhino, tracktion, testing, audio]
sources: []
updated: 2026-10-12
---

# Tracktion's hosted device is silent until its start is dispatched

The test runners open no audio device, so a scenario that needs the engine's own playback graph, not a render and not a chain played by hand, drives it through Tracktion's hosted audio device: the device a plugin build of the engine plays through, fed one block at a time by the caller. `DjLiveChain.inc`'s scenario "dj live: the song's own graph plays a deck's track while the song stands still" is the first and the pattern (2026-10-12).

## What went wrong

The first version heard total silence: the song's transport did not even advance while it played. Read at the pinned engine commit `4536d8a` (`tracktion_DeviceManager.cpp`): `DeviceManager::audioDeviceAboutToStart` sets `isSuspended` and posts `prepareToStart` to the message thread, and until that runs `audioDeviceIOCallbackWithContext` clears its outputs and returns without rendering any context. The runners run on the message thread and never dispatch it, so it never ran. Two more early outs sit on the same path: `audioDeviceIOCallbackInternal` skips the contexts while the CPU usage is over the engine's limit, and `addContext` waits up to 200 × 1 ms for the stream time to move.

## The recipe

1. Release the machine's device: a Rhino `Session` opens it as it is made (`Session::releaseAudioDevice`). Tracktion's own engine-in-a-plugin pattern avoids that by building its `Engine` with `autoInitialiseDeviceManager()` false.
2. `engine.getDeviceManager().getHostedAudioDeviceInterface()`, `initialise` with 0 inputs, 2 outputs, 48 kHz and 512, then `prepareToPlay`.
3. `engine.getDeviceManager().dispatchPendingUpdates()`, which runs the posted preparation.
4. `edit->getTransport().ensureContextAllocated()`, `edit->dispatchPendingUpdatesSynchronously()`, and `dispatchPendingUpdates()` once more.
5. Each block, `hosted.processBlock(buffer, midi)`: the graph renders into the buffer as the device would hear it.

Work in a document of its own, so that only its engine leaves the machine's device, and release it at the block's end ([Writing Rhino tests](writing-rhino-tests.md)).

## Related

- [Writing Rhino tests](writing-rhino-tests.md)
- [Reproducing a live timing bug offline](reproducing-live-timing-offline.md)
- [DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)
- [Directories not to read](off-limits-directories.md), for how an engine body is read at the pinned commit
