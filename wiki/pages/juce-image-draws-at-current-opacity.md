---
title: A JUCE image is drawn at the last colour's opacity
type: gotcha
summary: juce::Graphics composites drawImage and its variants at the context's current opacity, which the last setColour set, so a kept layer pasted after a translucent colour comes out faded; Forge's FX picture cache greyed out every picture from its second frame on until commit 44e19bb set the opacity to 1 before the blit.
tags: [forge, juce, ui, caching]
sources: []
updated: 2026-10-22
---

# A JUCE image is drawn at the last colour's opacity

`juce::Graphics::drawImage`, `drawImageAt` and `drawImageTransformed` composite the image at the context's current opacity. `setColour` sets that opacity from the colour's alpha, so an image drawn after a translucent fill or stroke is drawn translucent as well. Nothing warns you.

## How it showed

`Editor::paintFxWell` (`src/ForgeEditorFxView.cpp`, commit `e11aef3`) keeps each FX rack picture as an image and pastes it back on later frames ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)). The last colour set before the paste was a panel frame's translucent one, so from its second frame on every kept picture was pasted faded. The user reported that the compressor's meter and the reverb and compressor pictures looked greyed out. The real cause was the paste, not the art.

Snapshots never showed it. A snapshot paints once, and the first frame at a size draws the picture straight through without using the cache ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## The fix and the rule

- Commit `44e19bb` calls `g.setOpacity(1.0f)` right before the blit.
- Any `drawImage*` that does not open a paint sets its own opacity first. The ones already safe in Forge:
  - The chrome layer is blitted first in `Editor::paint` (`src/ForgeEditorPaint.cpp`), before any colour is set.
  - The spectrogram's blit sets its own alpha.
  - The Merli chassis's ears and logo set 1.0 (`ui/ForgeMerliChassis.h`). Its cat silhouette is filled with the current colour on purpose (`fillAlphaChannelWithCurrentBrush`).
- To test a cache, paint twice and compare the kept frame with a directly drawn one at the picture's own pixels. The `forge_fx` cache test now does this: each channel must be within 24, and no more than 2% of the pixels may be off. Without the fix it failed, with 150 of 2610 pixels faded.

## Related

- [Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [JUCE's rasteriser is not clip-invariant](juce-rasteriser-not-clip-invariant.md)
