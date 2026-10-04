---
title: Forge's engine splits into headers only
type: decision
summary: Forge's engine splits into headers so renderSample stays inlined without LTCG, while the panel splits into .cpp files freely.
tags: [forge, build, performance, file-layout]
sources: []
updated: 2026-10-03
---

# Forge's engine splits into headers only

## Context

On 2026-09-20, Forge's largest files were split into about forty focused ones. The engine split is commit 9c68bda and the panel split is commit 6909dbf. Before the split:

| File | Lines |
| --- | --- |
| `core/ForgeCore.h` | 2,042 |
| `src/ForgeProcessor.cpp` | 1,179 |
| `src/ForgeEditor.cpp` | 2,634 |
| `ui/ForgeVisuals.h` | 2,649 |

The open question was where a split could be made without costing anything.

## Decision

**The engine (`core/`) splits into headers, never into translation units.** `Processor::processBlock` in `src/ForgeProcessor.cpp` calls `Core::renderSample` once per sample, and the compiler inlines it there. Nothing in Forge's `CMakeLists.txt` turns on link-time code generation. Moving engine code behind a `.cpp` boundary would therefore turn inlined arithmetic on the audio path into real calls.

**The panel is the opposite case and splits freely.** `Editor` is one class defined across many `src/ForgeEditor*.cpp` files, which share `src/ForgeEditorInternal.h` ([Keeping files small](keeping-files-small.md)). A frame spends 8 to 34 ms in Direct2D, so call overhead does not matter. Three alternating `--profile` rounds showed no change after the split.

**Umbrella headers list their parts.** `core/ForgeCore.h`, `ui/ForgeLayout.h` and `ui/ForgeVisuals.h` each name the headers they include at the top, so every old include kept working. Include the narrowest header that answers your question. For example, a test that draws nothing should include neither `ForgeLayout.h` nor `ForgeVisuals.h`.

**`src/ForgePch.h` holds JUCE and the standard library, and no Forge header.** Forge headers change many times an hour. If one were in the precompiled header, every edit would rebuild the header and every translation unit with it. The precompiled header roughly halves the cost of each translation unit: twenty of them now build faster than the original three did.

## Shown, not argued

Two binary-identical checks against a worktree build of the previous commit proved the split changed nothing ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)):

- `RhinoForgeTests --render` wrote the same 491,520 bytes.
- `--snapshot` produced identical PNGs for all five tabs.

## Consequences

- **Header edits are expensive.** Editing a widely included `core/` or `ui/` header recompiles every translation unit that includes it ([Build and test Forge](build-and-test-forge.md)).
- **There is no engine library.** A single `forge_sources` list is compiled into both the plugin and the test binary.

## Related

- [Forge engine (Core)](forge-engine.md)
- [Forge editor (panel)](forge-editor.md)
- [Keeping files small](keeping-files-small.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
