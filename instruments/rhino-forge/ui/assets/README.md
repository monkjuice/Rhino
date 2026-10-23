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

## Merli theme (`themes/merli/`)

The second look, from a designer's kit of textures and a mockup drawn over an
older Forge layout. Every file here but `merli_plate.png` is embedded and fetched through
`juce::ImageCache` at the point of use (`merli::image` in
`ui/ForgeMerliMaterial.h`); none is held in a static, and none is drawn on the
frame path, only into the cached chassis and plate layers.

| File | Used by |
| --- | --- |
| `merli_fur.png` | The fur band, side margins, foot and shelf gaps, tiled 1:1 by `merli::drawFur`. |
| `merli_ear_left.png`, `merli_ear_right.png` | The two ears, drawn at 76 px wide. Named for the cat's own ears, so `merli_ear_right.png` is the one on the viewer's left. |
| `merli_panel.png` | The well the modules stand in, under a 60% floor colour. |
| `merli_plate.png` | Not compiled in. It grained the title plate behind the wordmark until the plate was removed and the wordmark set straight on the fur (`merli::drawWordmarkOnFur`); kept for a plate that wants grain again. |
| `merli_title_plate.png` | Source of the cat silhouette on the plate right of the tabs: read out as a mask by brightness at each rebuild, never blitted whole. |
| `merli_screw_small.png`, `merli_screw_large.png` | The plates' corner screws and the preset housing's screw. |

The designer's kit (`forge_kitty_assets.zip`: 2048 px fur, 1024 px ears and
textures) is not in the repository; ask for it before regenerating anything,
and regenerate from it, never from these files. Three of the files are the
kit's as delivered or merely smaller; the fur and the ears were rebuilt.

- **`merli_panel.png`, `merli_plate.png`, `merli_title_plate.png` and the
  screws** are the kit's `08_panel_bg`, `10_metal_plate`, `16_title_plate_blank`,
  `11_screw_large` and `12_screw_small`; the first two are scaled down (to 512
  and 256 px) with Lanczos and saved as RGB.
- **The fur** (`01_fur_base`) is a kaleidoscope: one cell mirrored about
  columns 179.5 + 180k and rows 182.5 + 183k, with dark gutters 4 to 10 px wide
  along many of those axes. Tiled as delivered it drew dark bars and a repeating
  chevron. The shipped tile is made in five steps:
  1. Repair each gutter by reflecting the fur beside it inward from both sides,
     a line counting as gutter while its mean is under 0.7 of its neighbours'.
  2. Crop exactly two periods each way, rows 183 to 914 and columns 180 to 899
     (720 x 732), from axis to axis, so the crop wraps onto itself.
  3. Scale to 0.75 (540 x 549), the size of the mockup's tufts at 1:1.
  4. Blend four copies, rolled by (0, 0), (97, 151), (211, 67) mirrored, and
     (31, 223), through softmax weights of periodic low-pass noise (FFT, cut-off
     wavelength 16 px, sharpness 3.4). Each copy's weight falls to 3% within a
     few pixels of its own half-period lines, where the mirror motifs sit.
  5. Tone: `v * (0.6 + 0.4 * sqrt(v))` per channel, then 10% toward grey.

  One small whorl survives; `merli::furOrigin` places the tile so that it
  falls under a plate in the header.
- **The ears** (`05_ear_right` is the cat's right ear, on the viewer's left)
  are hair strands about a third opaque, which showed the backdrop through the
  ear. Each is cropped to its strands with a 32 px margin, given a hull (alpha
  over 24, closed by three 9 px dilations and erosions, its outside flood-filled
  from every border pixel, eroded 5 px and blurred 3 px), filled with #18100B
  under the strands, faded out between 42% and 62% of its height, trimmed back to
  a 12 px margin and saved 160 px wide.

The kit's matte, paw and whisker images are not shipped: the paw is drawn
procedurally (`merli::drawPaw`), because it tints by state and is on the frame
path, and nothing in the current panel uses the other two.
