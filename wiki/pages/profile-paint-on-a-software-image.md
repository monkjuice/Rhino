---
title: Profile paint on a software image
type: gotcha
summary: On Windows a plain juce::Image is a Direct2D bitmap whose context costs about 3 ms a paint whatever is drawn, so time painters on juce::SoftwareImageType() and repaint what the app really invalidates.
tags: [both, juce, ui, performance, windows]
sources: []
updated: 2026-10-05
---

# Profile paint on a software image

## Symptom

A paint profile says a small repaint is expensive, and an optimisation barely moves it. Rhino's first `--profile-ui` (commit `2a4830f`) put the arrangement's 4 px playhead strip at 4.2 ms. Painted on a software image, over the area the app really invalidates, the same painter measured 1.3-1.5 ms before culling and 0.4 ms after (commit `c9d7d5d`).

## Cause

`juce::Image(juce::Image::ARGB, w, h, true)` takes the platform's native image type, which on Windows is a Direct2D bitmap. Opening a `juce::Graphics` on one cost about 3 ms at the arrangement's size (1400 × 620), whatever was drawn. No on-screen repaint pays that cost, and it buried the painters' own cost, which was the thing being compared.

## What to do

- Paint into `juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType())`, as `canvasFor` in `native/src/tests/UiProfile.cpp` does.
- Repaint the rectangle the app invalidates. `UiProfile.cpp` uses `playheadDamage` over the area `updatePlayhead` passes, which leaves out the ruler, header and footer, rather than a hand-picked full-height column.
- When a paint costs more than its painter can explain, time sections inside `paint()` with temporary probes before optimising anything.

## Forge

`RhinoForgeTests --profile` (`tests/ForgeTestTools.cpp`) still paints into a plain `juce::Image`, although its comment calls the result software-rendered (checked 2026-10-05). On the evidence above, its frame times include the same overhead. A comparison between two builds cancels it out. Absolute figures do not, and nor do conclusions drawn from them, such as "clipping a repaint bought only about 10%" ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)). Re-measure those on a software image before relying on them.

## Related

- [What a change costs the interface](ui-cost-of-a-change.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
- [Playhead rendering](playhead.md)
