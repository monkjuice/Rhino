---
title: Rhino is proprietary, all rights reserved
type: decision
summary: Superseded on 2026-11-19 by the AGPL v3 or later (commit 294207c) - from ce97dd7 (2026-10-27) the root LICENSE made the repository proprietary, all rights reserved by "Rhino Forge", with binaries only under an end-user licence agreement; kept for history.
tags: [both, licence, repository, superseded]
sources: []
updated: 2026-11-19
---

# Rhino is proprietary, all rights reserved

> **Superseded on 2026-11-19.** Commit `294207c` relicensed the repository under the GNU AGPL, version 3 or later: see [Rhino is licensed under the AGPL v3 or later](agpl-licence.md), which also took over this page's list of third-party licences, now kept in `THIRD_PARTY_NOTICES.md`. The open question this page left, whether JUCE's and Tracktion's licences allow closed-source binaries, was settled by that change: without their commercial licences they do not, and any distributed build is bound by the AGPL. Everything below describes the repository as it was.

## Decision (2026-10-27)

Commit `ce97dd7` added `LICENSE` at the repository root; before it the repository had no licence at all. The text was the user's: copyright 2026 **Rhino Forge**, all rights reserved, proprietary and confidential. Copying, modifying, distributing, sublicensing, publishing, reverse engineering or deriving works from the source, assets, algorithms, designs, UI, audio processing, presets or documentation needed written permission; compiled binaries could be used only under an end-user licence agreement; an "AS IS" clause disclaimed warranty. It covered the DAW in `native/`, Rhino Forge and Rhino Duck. The README did not mention it.

## Related

- [Rhino is licensed under the AGPL v3 or later](agpl-licence.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
