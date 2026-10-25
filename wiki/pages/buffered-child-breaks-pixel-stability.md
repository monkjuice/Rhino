---
title: A buffered child broke Forge's byte-identical paint
type: gotcha
summary: Buffering the title plate's PresetPlate to an image made two pixels of another component differ between two fresh Forge panels under Direct2D and failed the theme suite's byte-for-byte check, so the plate is drawn unbuffered; run forge_theme after adding any buffered child. Since 2026-10-25 the committed tree fails that check by two pixels a shade apart with no buffered child to blame, on whichever curve or glow the layout puts them, because Direct2D draws the same path into a second bitmap a shade differently; chromeCacheSuite paints on a software image for that reason.
tags: [forge, ui, direct2d, testing]
sources: []
updated: 2026-10-25
---

# A buffered child broke Forge's byte-identical paint

The theme suite (`tests/ForgeTestsTheme.cpp`, CTest `forge_theme`) paints the OSC page of two fresh panels, in Forge and again in Merli, and requires the two pictures to match byte for byte. That match is what lets a hash of `--snapshot` prove that a change changed nothing ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).

On 2026-10-18/19 the new `ui::PresetPlate` (`ui/ForgePresetPlate.h`), which shows the preset's name on the title plate, was first given `setBufferedToImage(true)`. Two pixels of OSC C's table-field arrow then differed between the two panels under Direct2D. The arrow belongs to a different component from the plate. With the plate unbuffered the suite passes, and three short lines of text cost the 24 Hz frame next to nothing ([Forge's tick repaints its live regions at 24 Hz](forge-panel-repaint-cost.md)).

The cause was not traced, and buffering is not banned:

- Every control caption is buffered (`dressControlLabel`, `src/ForgeEditor.cpp`) and the suite still passes.
- The preset browser is buffered too, but it is hidden in the panels the suite compares.

So when you add a buffered child that a fresh panel shows, run `forge_theme` before assuming the image cache is free. If two fresh panels stop agreeing, try the child unbuffered first.

## The two pixels are there without any buffered child, 2026-10-25

Found by session rhino-62 while running the whole suite for the preset-browser work ([Forge preset browser and bank](forge-preset-browser.md)): `forgeStableSuite` fails on the committed tree, `fa07c22`, built as a control in a scratch worktree with the same JUCE, identically and deterministically. "two Forge panels differ in 2 pixels", at (1156,174) and (1157,177) at 1440x900, one shade apart (`ff1b4331` against `ff1a412f`), on the glow at the top-right corner of OSC C's picture tube beside its table field, the same neighbourhood as the arrow above. The browser work changes nothing about it: the same two pixels differ with it, and nothing in it buffers a child or touches a fresh panel's paint. `merliRenderSuite`'s two Merli panels still match, and 21 of 22 areas pass; `forge_theme` fails on that one check alone.

So the difference is independent of any buffered child, and "try the child unbuffered" is the first thing to try rather than the explanation. Check against a control build of the committed tree, not against this page ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).

## The pixels move with the layout, and a software canvas removes them, 2026-10-25

When the keyboard grew from 80 to 88 px ([Forge note guide](forge-note-guide.md)), the two pixels left OSC C's tube and landed on the LFO curve, at (833,533) and (1084,644) at 1440x900, again one shade apart (`ff1d1934` against `ff1c1832`). The committed tree with only `keyboardHeight` changed to 88 failed by 4 pixels, so the count and the place follow the geometry, not the change. The same two pixels also broke `chromeCacheSuite`'s check that "an oscillator switched to spectral draws what one opened spectral draws" (`forge_displays`), which tolerates one: the LFO curve is drawn live, from identical state, onto two `juce::Image`s, and on Windows those are Direct2D bitmaps. Painted on `juce::SoftwareImageType()` the two agree to the pixel, and with `chromeKey` broken on purpose (the spectral marker removed) the check still fails, so `chromeCacheSuite` now paints on software ([Profile paint on a software image](profile-paint-on-a-software-image.md)).

So the instability is Direct2D drawing the same path into a second bitmap, not the panel: the exact mechanism inside JUCE's renderer is still untraced. `forgeStableSuite` keeps painting through `createComponentSnapshot`, the way `--snapshot` paints, because that is what its byte match vouches for, and it still fails by two pixels; which two depends on the layout. A failure there by a couple of pixels a shade apart on a curve or a glow is this; anything page-wide or more than a shade is yours.

## Related

- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
- [Profile paint on a software image](profile-paint-on-a-software-image.md)
- [Forge themes and Merli](forge-themes.md)
- [Forge editor (panel)](forge-editor.md)
