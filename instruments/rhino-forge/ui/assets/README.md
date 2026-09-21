# Forge brand assets

The wordmark comes from a supplied FORGE artwork, trimmed to its own alpha
bounding box so the drawn rectangle maps to the lettering and nothing else —
`drawWordmark` places the image with `yMid`, so padding inside the PNG would
read as the logo sitting off-centre on its plate.

`forge_logo_lockup.png` is older artwork, from a version that placed an emblem
and the wordmark in one image with the emblem's top arm overhanging the F.
They were separated by connected-component labelling, with the glow shared
between the emblem and the F divided along its middle, so neither piece carries
a ghost of the other. It no longer matches the panel wordmark.

| File | Used by |
| --- | --- |
| `forge_logo.png` | The panel wordmark. Embedded via `juce_add_binary_data`, drawn by `drawWordmark` in `ForgeVisuals.h`. Stored at the size it is drawn, so a repaint blits rather than rescaling. |
| `forge_app_icon_512.png`, `forge_app_icon_256.png` | `ICON_BIG` / `ICON_SMALL` on `juce_add_plugin`, which is where the standalone executable's icon comes from. |
| `forge_logo_full.png`, `forge_logo_lockup.png` | Masters, not compiled in. Full-resolution wordmark and the emblem-plus-wordmark lockup, for packaging and store art. |
| `forge_app_icon_128/64/32.png` | Not compiled in. JUCE builds its own icon ladder from the two above; these are here for installers and web use. |

Regenerating `forge_logo.png` at a different size: resample from
`forge_logo_full.png`, never from `forge_logo.png`. Keep it trimmed, and keep
the aspect ratio — the panel fits the image into a 43-tall rectangle without
stretching, so a changed aspect changes how wide the wordmark draws.
