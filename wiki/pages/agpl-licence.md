---
title: Rhino is licensed under the AGPL v3 or later
type: decision
summary: Since commit 294207c (2026-11-19) the repository - the DAW, Forge and Duck - is under the GNU AGPL v3 or later, because the pinned JUCE (AGPLv3 or commercial) and Tracktion (GPLv3+ or commercial) already bind any distributed build to it; third-party work is listed in THIRD_PARTY_NOTICES.md, a commercial licence beside the AGPL stays possible only if outside contributions come under a CLA (none exists yet), and the VinylDrums pack's redistribution terms and the installers' source offer are still open.
tags: [both, licence, repository]
sources: []
updated: 2026-11-19
---

# Rhino is licensed under the AGPL v3 or later

## Decision

Commit `294207c` (2026-11-19) replaced the proprietary `LICENSE` of `ce97dd7` ([superseded](proprietary-licence.md)) so that the project can be open-sourced. Three root files carry it:

- `LICENSE` is the FSF's `agpl-3.0.txt` verbatim (SHA-256 `0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0`). Add nothing to it: an unmodified text is what lets GitHub detect the licence.
- `README.md`, section *Licence*, holds the copyright line, which names the author personally (no longer "Rhino Forge"), the "version 3 or (at your option) any later version" grant, and its scope: the DAW in `native/`, [Rhino Forge](forge.md) in `instruments/rhino-forge/` and [Rhino Duck](rhino-duck.md) in `effects/rhino-duck/`, neither of which has a licence file of its own.
- `THIRD_PARTY_NOTICES.md` lists everything that keeps its own licence (below).

## Why the AGPL

- The pinned JUCE (`37c894f`) is AGPLv3 or the commercial JUCE 8 licence (`native/.deps/juce/LICENSE.md`), and Tracktion Engine (`4536d8a`) is GPLv3 or later or a commercial licence ([Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)). Without the commercial licences any distributed build is bound by the AGPL anyway, so the licence now says what the binary already is. This answers the open question the proprietary licence left.
- The user wrote all 749 commits up to `294207c`, so they can still sell commercial licences beside the AGPL later, once they hold JUCE's and Tracktion's commercial licences.
- That stays possible only while every outside contribution comes under a contributor licence agreement granting those rights. **No CLA exists yet**: put one in place before merging anyone else's code. For the same reason, code copied from another project keeps its own licence and cannot be relicensed; [Serum 2's preset format](serum-2-preset-format.md) takes only facts from the GPLv3 serum2vital, and that should stay so.

## Third-party notices

`THIRD_PARTY_NOTICES.md` lists the fetched dependencies (JUCE, which bundles the VST3 SDK under MIT among others in its `LICENSE.md`; Tracktion; SoundTouch, LGPL 2.1, and Signalsmith Stretch, MIT, which reach the binary through Tracktion for [time warp](time-warp.md); zstd `f8745da`, BSD 3-Clause dual with GPLv2, Forge only), the OFL fonts, the TR808 (CC0) and VinylDrums (royalty-free *Giant Vinyl Drums Pack*) sample packs, which the AGPL does not cover, the Cumbia and HandClap packs, which their author made for Rhino and licensed under the AGPL v3 or later on 2026-11-19 (each says so in its `SOURCE.md`), and the research snapshots ([Directories not to read](off-limits-directories.md)). A new dependency, font or sample pack gets a line there and its licence or `SOURCE.md` beside it ([Content library](content-library.md)).

## Still open (as of 2026-11-19)

- VinylDrums is published "royalty free"; whether that permits redistributing the samples themselves is unchecked.
- The `research/sources/lmms` snapshot has no `COPYING` file of its own.
- The AGPL requires every distributed binary to come with its source or an offer of it. The release installers, targeted for December 2026-January 2027 ([Documentation ownership](documentation-ownership.md)), must do so.

Not legal advice: this records the licence files as of `294207c`.

## Related

- [Rhino is proprietary, all rights reserved](proprietary-licence.md) (superseded)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Content is files, never compiled in](content-is-files.md)
- [Git workflow](git-workflow.md)
