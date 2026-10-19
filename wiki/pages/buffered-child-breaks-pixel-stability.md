---
title: A buffered child broke Forge's byte-identical paint
type: gotcha
summary: Buffering the title plate's PresetPlate to an image made two pixels of another component differ between two fresh Forge panels under Direct2D and failed the theme suite's byte-for-byte check, so the plate is drawn unbuffered; run forge_theme after adding any buffered child.
tags: [forge, ui, direct2d, testing]
sources: []
updated: 2026-10-19
---

# A buffered child broke Forge's byte-identical paint

The theme suite (`tests/ForgeTestsTheme.cpp`, CTest `forge_theme`) paints the OSC page of two fresh panels, in Forge and again in Merli, and requires the two pictures to match byte for byte. That match is what lets a hash of `--snapshot` prove that a change changed nothing ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).

On 2026-10-18/19 the new `ui::PresetPlate` (`ui/ForgePresetPlate.h`), which shows the preset's name on the title plate, was first given `setBufferedToImage(true)`. Two pixels of OSC C's table-field arrow then differed between the two panels under Direct2D. The arrow belongs to a different component from the plate. With the plate unbuffered the suite passes, and three short lines of text cost the 24 Hz frame next to nothing ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)).

The cause was not traced, and buffering is not banned:

- Every control caption is buffered (`dressControlLabel`, `src/ForgeEditor.cpp`) and the suite still passes.
- The preset browser is buffered too, but it is hidden in the panels the suite compares.

So when you add a buffered child that a fresh panel shows, run `forge_theme` before assuming the image cache is free. If two fresh panels stop agreeing, try the child unbuffered first.

## Related

- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
- [Forge themes and Merli](forge-themes.md)
- [Forge editor (panel)](forge-editor.md)
