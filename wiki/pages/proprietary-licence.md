---
title: Rhino is proprietary, all rights reserved
type: decision
summary: Since commit ce97dd7 (2026-10-27) the root LICENSE makes the repository - the DAW, Forge and Duck - proprietary, all rights reserved by "Rhino Forge", with binaries only under an end-user licence agreement; third-party files keep their own licences, and whether Tracktion's and JUCE's licences allow closed-source binaries is an open question.
tags: [both, licence, repository]
sources: []
updated: 2026-10-27
---

# Rhino is proprietary, all rights reserved

## Decision

Commit `ce97dd7` (2026-10-27) added `LICENSE` at the repository root; before it the repository had no licence at all. The text is the user's: copyright 2026 **Rhino Forge** (the holder the user chose, not "Rhino"), all rights reserved, proprietary and confidential. Copying, modifying, distributing, sublicensing, publishing, reverse engineering or deriving works from the source, assets, algorithms, designs, UI, audio processing, presets or documentation needs written permission; compiled binaries may be used only under an end-user licence agreement; an "AS IS" clause disclaims warranty. The pasted text repeated the copyright line at its end, and the duplicate was dropped.

It covers everything Rhino wrote in the tree: the DAW in `native/`, [Rhino Forge](forge.md) in `instruments/rhino-forge/` and [Rhino Duck](rhino-duck.md) in `effects/rhino-duck/`, neither of which has a licence file of its own. The README does not mention the licence (as of `ce97dd7`).

## What it does not cover

Third-party files keep their own licences, each with its licence file beside it:

- Fonts under the SIL Open Font License 1.1, which permits bundling: Inter in `native/assets/fonts/` (`OFL.txt`); Rajdhani, Chakra Petch and IBM Plex Mono in `instruments/rhino-forge/ui/assets/fonts/` and `effects/rhino-duck/ui/assets/fonts/` (`OFL-*.txt`).
- Samples under `library/`, with `SOURCE.md` and licence files beside them, such as `library/Samples/TR808/LICENSE-CC0.txt` ([Content is files](content-is-files.md)).
- `research/sources/`, tracked snapshots of Ardour, LMMS, Zrythm and Tracktion under their upstream licences ([Directories not to read](off-limits-directories.md)).
- JUCE, Tracktion and zstd in `native/.deps/`, which git does not track.

Code copied from a GPL project would bring its licence with it. [Serum 2's preset format](serum-2-preset-format.md) already takes only facts, not code, from the GPLv3 serum2vital; keep to that.

## Open question: the engine's licences

Not checked against current terms, and not legal advice. Tracktion Engine is offered under GPLv3 or a commercial licence, and JUCE under AGPL/GPL or a commercial licence with its own small-business tiers. Distributing closed-source binaries under an end-user licence would generally need the commercial licence of each; the wiki records no such licence. The [architecture decision](tracktion-and-juce.md) and `research/DAW-STUDY.md` called the engine's licence a detail for a personal project, which no longer holds. The other libraries compiled into the binaries need the same check: SoundTouch and Signalsmith, Tracktion's two stretchers enabled in `native/CMakeLists.txt` ([Time warp](time-warp.md)), and zstd in Forge.

## Related

- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Git workflow](git-workflow.md)
- [Documentation ownership](documentation-ownership.md)
