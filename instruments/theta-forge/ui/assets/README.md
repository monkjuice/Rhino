# Forge brand assets

Cut from the supplied FORGE artwork, which places the emblem and the wordmark
in one image with the emblem's top arm overhanging the F. They were separated
by connected-component labelling, with the glow shared between the emblem and
the F divided along its middle, so neither piece carries a ghost of the other.

| File | Used by |
| --- | --- |
| `forge_logo.png` | The panel wordmark. Embedded via `juce_add_binary_data`, drawn by `drawWordmark` in `ForgeVisuals.h`. Stored at the size it is drawn, so a repaint blits rather than rescaling. |
| `forge_app_icon_512.png`, `forge_app_icon_256.png` | `ICON_BIG` / `ICON_SMALL` on `juce_add_plugin`, which is where the standalone executable's icon comes from. |
| `forge_logo_full.png`, `forge_logo_lockup.png` | Masters, not compiled in. Full-resolution wordmark and the emblem-plus-wordmark lockup, for packaging and store art. |
| `forge_app_icon_128/64/32.png` | Not compiled in. JUCE builds its own icon ladder from the two above; these are here for installers and web use. |

Regenerating `forge_logo.png` at a different size: resample from
`forge_logo_full.png`, never from `forge_logo.png`.
