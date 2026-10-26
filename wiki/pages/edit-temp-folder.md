---
title: An edit's project id names its temp folder
type: gotcha
summary: Tracktion keeps an edit's cached renders in a folder named for its project id and purges it whenever an edit is built, so a second edit built from a document's state must not take the document's id; learnt from the DJ deck copies, removed on 2026-10-12.
tags: [rhino, tracktion, dj, rendering, files]
sources: []
updated: 2026-10-26
---

# An edit's project id names its temp folder

Read in the pinned engine (`4536d8a`: `Edit::getTempDirectory`, `TemporaryFileManager`, `AudioClipBase::getPlaybackFile`), not measured.

## What the engine keeps there

An edit keeps its cached renders in `edit_<project id>` under the engine's temp directory, which is `%APPDATA%\<engine name>\Temporary` (`Theda Native` for the app, `Rhino Native Tests` for the runners), not `%TEMP%`:

- `proxy_<hash>.wav`: a compressed source decoded to WAV, for every file that is not WAV or AIFF (`AudioFileInfo::needsCachedProxy`).
- `clip_0_<clip id>_<hash>.wav`: a clip pitched or stretched (`usesTimeStretchedProxy`) that still allows proxies. Since commit `5c9c1e1` Rhino turns them off for every warped or pitched clip, because this copy plays late and cuts its end ([Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)).
- The render manager's files: a reversed clip reversed.

A clip asks for its render while a render's graph is built, and starts it 600 ms later from a timer on the message thread; an offline render waits until every wave node's file opens (`WaveNode::isReadyToProcess`), so a missing render holds the render, and a missing source holds it for good ([Offline renders that never return](renders-that-never-return.md)).

**Building an edit purges its folder** (`purgeOrphanFreezeAndProxyFiles`): the `clip_` and reversed renders its own clips do not use go. `proxy_` files are never purged. `Edit::~Edit` deletes only the proxies its timeline clips are still making, cancelling them (`cancelAllProxyGeneratorJobs`, below); a finished render stays.

**So an edit built from a document's state alongside the document must not take the document's id.** With it, building the copy purges the document's renders of every clip the copy does not hold; with a fresh id per build, every build makes its renders again in an empty folder and leaves the folder behind. Nothing in Rhino builds such a second edit since 2026-10-12: the one other edit built from a document's state is the one `restoreProject` swaps in for the document (`loadDocumentEdit`, checked 2026-10-12).

## History: the DJ decks' copies (until 2026-10-12)

A DJ deck's bounce rendered a copy of the document holding only the deck's tracks ([superseded](dj-bounce-on-a-copy.md)); decks now play live through the track and copy nothing ([DJ decks play live through the track's chain](dj-decks-play-live-through-the-track.md)). The id the copy was built with decided whose renders it purged and found:

- **The live edit's id** (until 2026-10-11): building the copy purged the song's renders of every clip on every other track.
- **A new id per bounce** (the first fix of 2026-10-11): every bounce made its renders again in an empty folder - for a five-minute MP3, the 600 ms timer and a whole decode, holding the booth's only worker - and left the folder behind, about the track's size each time.
- **One id per deck slot** (2026-10-11 to 10-12): `DjBooth::copyIds`, written into the copy by `deckCopyState`. A deck's next bounce found its last one's renders, the purge reached only that deck's, and the song's were never touched.

Two copies of one deck shared the folder, and destroying a copy cancels the proxy renders its clips began (`Edit::cancelAllProxyGeneratorJobs`) while a clip begins its proxy once and afterwards only looks for the file, so `releaseFinishedBounces` kept a copy whose render had not finished while a newer copy of its deck still rendered. `copyFolders` and `removeCopyFolders` deleted the folders in `djReset` and `~DjBooth`, letting each file go from the engine's cache first (`AudioFileManager::releaseFile`). All of it went with the bounce.

## Related

- [Offline renders that never return](renders-that-never-return.md)
- [Time warp](time-warp.md)
- [A deck's bounce renders on a worker against a copy of the document](dj-bounce-on-a-copy.md)
- [DJ view and the booth](dj-view.md)
