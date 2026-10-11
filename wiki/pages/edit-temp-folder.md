---
title: An edit's project id names its temp folder
type: gotcha
summary: Tracktion keeps an edit's cached renders in a folder named for its project id and purges it whenever an edit is built, so each DJ deck's copies of the document take an id of the deck's own, and the booth deletes the folders.
tags: [rhino, tracktion, dj, rendering, files]
sources: []
updated: 2026-10-11
---

# An edit's project id names its temp folder

Read in the pinned engine (`4536d8a`: `Edit::getTempDirectory`, `TemporaryFileManager`, `AudioClipBase::getPlaybackFile`), not measured.

## What the engine keeps there

An edit keeps its cached renders in `edit_<project id>` under the engine's temp directory, which is `%APPDATA%\<engine name>\Temporary` (`Theda Native` for the app, `Rhino Native Tests` for the runners), not `%TEMP%`:

- `proxy_<hash>.wav`: a compressed source decoded to WAV, for every file that is not WAV or AIFF (`AudioFileInfo::needsCachedProxy`).
- `clip_0_<clip id>_<hash>.wav`: an unwarped clip pitched or stretched (`usesTimeStretchedProxy`). A warped clip has proxies off ([Time warp](time-warp.md)).
- The render manager's files: a reversed clip reversed.

A clip asks for its render while a render's graph is built, and starts it 600 ms later from a timer on the message thread; an offline render waits until every wave node's file opens (`WaveNode::isReadyToProcess`), so a missing render holds the render, and a missing source holds it for good ([Offline renders that never return](renders-that-never-return.md)).

**Building an edit purges its folder** (`purgeOrphanFreezeAndProxyFiles`): the `clip_` and reversed renders its own clips do not use go. `proxy_` files are never purged. `Edit::~Edit` deletes only the proxies its timeline clips are still making, cancelling them (`cancelAllProxyGeneratorJobs`, below); a finished render stays.

## Which id a deck's copy takes

A DJ deck's bounce renders a copy of the document holding only the deck's tracks ([A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)), so the id it is built with decides whose renders it purges and finds:

- **The live edit's id** (until 2026-10-11): building the copy purged the song's renders of every clip on every other track.
- **A new id per bounce** (the first fix of 2026-10-11): every bounce made its renders again in an empty folder - for a five-minute MP3, the 600 ms timer and a whole decode, holding the booth's only worker - and left the folder behind, about the track's size each time.
- **One id per deck slot** (since 2026-10-11): `DjBooth::copyIds`, made with the booth and written into the copy by `deckCopyState`. A deck's next bounce finds its last one's renders, the purge reaches only that deck's, and the song's are never touched. A deck's first bounce still makes its own.

## Two copies of one deck

A copy cancelled for a new source can still be alive when its deck's next copy is built, and the two share the folder. Destroying a copy cancels the proxy renders its clips began (`Edit::cancelAllProxyGeneratorJobs`), and a clip begins its proxy once and afterwards only looks for the file, so the newer copy would wait out its sixty seconds. `DjBooth::releaseFinishedBounces` therefore keeps a copy whose render did not finish while a newer copy of its deck still renders; a render that finished waited for every file it reads, so its copy goes at once.

## Cleanup

`copyFolders` records each folder as a copy is built; `removeCopyFolders` deletes them after the copies, in `djReset` (a new or reopened document) and `~DjBooth`, letting each file go from the engine's cache first (`AudioFileManager::releaseFile`). A crash leaves up to six; the live edit's own folder is the engine's business.

`DjBounces.inc` leaves stand-in renders in a deck's folder and in the song's, and checks that the next bounce keeps the deck's folder and its render, purges nothing of the song's, and that a reopened document and the booth's end each remove the deck's folder.

## Related

- [A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)
- [DJ view and the booth](dj-view.md)
- [Offline renders that never return](renders-that-never-return.md)
- [Time warp](time-warp.md)
