---
title: Forge themes and Merli
type: component
summary: Every Forge chrome colour is a token in ui/ForgeTheme.h with a value per theme; the theme is a machine setting read once before the first Editor, and Merli, the second theme, redraws the chrome as fur and matte plates over the identical layout.
tags: [forge, ui, theme]
sources: []
updated: 2026-10-22
---

# Forge themes and Merli

Forge has two looks: **Forge**, the gunmetal default, and **Merli**, tortoiseshell fur around dark matte plates with bronze knobs and orange and violet accents. The tokens are commit `425b253` (2026-10-16); Merli's drawing is commit `286e146`. Paths below are under `instruments/rhino-forge/`.

## Tokens

`ui/ForgeTheme.h` names every chrome colour as a token: a reassignable `inline juce::Colour` global in `rhino::forge::ui`, initialised to Forge's value, so a binary that never applies a theme paints as it always did. `Theme` has one member per token and `forgeTheme()` and `merliTheme()` are the palettes. As of 2026-10-17 there are 82 tokens: the sixteen Forge always named, 58 that the hex literals in `ui/` and `src/` became (`fieldFloor`, `displayFloor`, `knob*`, `tab*`, `rail*`, `key*`, `switch*`, `identity*`, `oscRed`/`oscOrange`/`oscGreen` behind `panelColourOf`, ...), and eight only Merli reads (`earPink` went once the ears became art and nothing read it). `tokenBindings()`, built by a macro, is the one list of name, global and member pointer; `applyTheme(ThemeId)` walks it and so do the tests. Content colours stay literals: `fxTypeColour`, the note guide's tones, white glints, black shadows.

A token keeps its name when a theme paints it otherwise: `electricBlue` is the signal accent and is orange in Merli. Renaming hundreds of call sites was rejected.

## A setting of the machine, read once

`Processor::createEditor` calls `Processor::loadThemeOnce()` (`src/ForgeProcessorSettings.cpp`) before it builds the Editor. That reads `%APPDATA%\Rhino Forge\Settings.xml` (`<RhinoForgeSettings theme="merli"/>`, beside the [MIDI map](forge-midi-learn.md)) once per process; a missing file or an unknown name means Forge. Saving reads, changes and writes, so attributes a later build adds survive. `ui::selectTheme` applies a theme and marks it chosen, which pre-empts the file; `Processor::setSettingsFile` redirects the file for tests, as `setMidiMapFile` does.

Why once: components copy tokens as they are built (`setColour`, member initialisers such as `accent = electricBlue`), so a live switch would recolour the painted chrome and miss the controls. The user decided on 2026-10-16 that a switch takes effect on restart, which is also why the cached chassis and plate layers carry no theme in their keys ([Forge's panel repaints whole at 24 Hz](forge-panel-repaint-cost.md)).

The menu is `Editor::showThemeMenu` (`src/ForgeEditorTheme.cpp`, from `ui::themeMenu`: ids one-based, the current one ticked). It opens on a plain click on the top half of the title plate (`ui::identityPlateBounds` less the unit mark, the preset's name on the `PresetPlate`, the status label and LOAD/SAVE: `Editor::onThemePlate`) or on a right-click on bare chassis that no control claims (`src/ForgeEditorInput.cpp`). Choosing the other theme saves it and shows `MERLI ON RESTART` (or `FORGE ...`) in the preset label in `signalViolet`.

## How Merli draws

The layout is the same in both themes: nothing in `ForgePlacement.h`, `ForgeModule.h`, `ForgeModules.h` or `ForgeControlBlock.h` changed, and a test holds every child component's bounds identical. A Forge primitive that Merli redraws starts with `if (isMerli()) { merli::drawX(...); return; }`, leaving the Forge path below it untouched. It is `isMerli()` because `merli` is the namespace.

- `ui/ForgeMerliMaterial.h`: `Asset` and `merli::image()` (through `ImageCache`), `fillTexture`, the procedural paw, rounded plates, screws, wells and matte fills. `drawRoundedPlate` strokes its hairline half a pixel inside the edge (straddling it, the outer half showed as a grey row above the lip at 2x) and takes `topScrews`: SUB's and NOISE's inner plates have screws at the foot only, because a top-left screw crowded the paw.
- `ui/ForgeMerliChassis.h`: `drawBackdrop` (backdrop above y 28, fur, ears, module well, title and cat plates, tabs well, LOAD/SAVE housing, shelf), whose plate geometry is factored out as `presetHousingBounds`, `catPlateBounds` and `catBounds`, and `drawModuleShell` (rounded r7 plates with corner screws, a paw before titles that have no enable LED, handle or bank cards). The fur is filled from `merli::furOrigin()`, (-80, 0): one small symmetric whorl survives in the tile, and from there every copy of it in the header falls under a plate or the tabs well at 1440 and 1180 wide. `furRimLit` lights only the band's top edge and stops short of the ears (from `earWidth - earOverhang` in from each side): a lit line under the ear art's faded base read as a wire across each ear.
- `ui/ForgeMerliControls.h`: `drawKnobFace` (a bronze ring at 0.70-0.81 r that is also the value track, a cap at 0.68 r; `knobCircle`, `onModRing` and the 0.88 mod ring unchanged) and `drawPawLamp`, which throws no halo: at a usable alpha over a dark plate a halo read as a shadow ring.
- Dispatches in `ForgePanels.h`, `ForgeChrome.h` (rails, wear, hatch and grain draw nothing), `ForgeDisplays.h` (a flat display that keeps `crtPath`'s silhouette, because `drawWaveform` clips to it), `ForgeControls.h`, `ForgeArpPlate.h`, `ForgeNoteGuide.h` (black keys `#262829`), `ForgeLookAndFeel.h` and `ForgePerformanceWheel.h`. The wheel's bronze wash (`knobRimLit` at 24%) and the tyre's dark-bronze one (`knobRimDark` at 45%, so it reads as rubber) are drawn through the wheel art's own alpha (`drawImage` with `fillAlphaChannelWithCurrentBrush`, each in a `ScopedSaveState`); a `fillRect` over the art's box first painted a tan rectangle on the deck plate.

### The cat plate names the preset

Since 2026-10-19 Merli's cat plate shows the loaded preset's name, byline and origin to the left of the cat, in `merli::catPlateTextBounds` ([Forge editor (panel)](forge-editor.md#title-plate-and-presets)). Forge shows the same lines on its maker plate. To make room, the cat now draws at most a third of the plate's width (`catBounds`: 0.46 scale, capped at 34% of the plate), toward the plate's right end.

The `PresetPlate` is therefore the one component whose rectangle differs between the themes, placed per theme at the user's request. The test that every component keeps its rectangle under Merli exempts it.

## Rules that bite

- **No static `juce::Image`.** Textures come from `merli::image()` at the point of use, the pattern Forge's own images adopted in commit `f2a972e` ([A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)).
- **Merli's textures only in the cached layers.** What Merli draws per frame is fills and strokes with no clip push; the one image is the wheel's own art, which Forge already draws per frame and Merli re-draws as its wash.
- **A new chrome colour is a token** with a value in every palette, never a literal.
- **`ForgePanels.h` is the only includer of `ForgeMerliChassis.h`**, which forward-declares `drawArpSky`, defined later in `ForgePanels.h`.
- **The spectral well asks `drawWell` for cut 0, and Merli keeps it square**: a rounded floor left the spectrogram's corners hanging.
- **`Graphics::drawImage` draws at the current colour's alpha.** A rim or screw glint set at 55% earlier in the routine left the ears and the FORGE wordmark at about 55% strength (an ear's brightest pixel measured 136 against 249), and the wheel's first wash left the tyre drawn after it at 24%. So set `g.setOpacity(1.0f)` inside the `ScopedSaveState` before any `drawImage` in a routine that has set colours, as `merli::drawEar`, `drawTitlePlate` and `drawScrew` do, and keep a wash's colour inside its own saved state. The cat plate's silhouette and the wheel's washes draw with `fillAlphaChannelWithCurrentBrush` on purpose.
- **JUCE paints a black key's face in `fill.brighter()`** (at least `#494949`), so no token can darken it; Merli overrides `drawBlackNote` (`ForgeNoteGuide.h`) to paint `#262829`.
- **The ears are named for the cat's own**: `earRight` is drawn on the viewer's left.

## Assets and the mockup

`ui/assets/themes/merli/` holds eight PNGs embedded by `juce_add_binary_data`: about 870 KB, roughly 2.6 MB of generated C++. The fur and the ears were rebuilt from the designer's kit, which is not in the repository; the recipe, and the rule to regenerate from the kit rather than from the shipped files, is in [`ui/assets/README.md`](../../instruments/rhino-forge/ui/assets/README.md). In short: the 2048 px fur swatch was a kaleidoscope (mirror axes every 180 x 183 px, dark gutters on many) that tiled into bars and chevrons, so `merli_fur.png` (540 x 549) is two repaired periods, seamless by construction, blended from four offset copies whose weights fall to 3% near their own half-period lines, where the mirror motifs sit, and toned to the mockup's coat; the panel lays it 1:1 with no darkening wash. The ears (160 px art, drawn 76 px wide: `earWidth`, `earTop` -5, `earOverhang` 6) were filled solid under their strands and faded toward the base. Their outside is flood-filled from every border pixel: the first version left an opaque dark patch outside the right ear's outline, in the window's top-right corner. The kit's matte, paw and whiskers are not shipped.

The mockup was drawn over an old Forge layout; the user asked for its look on the current panel, every page. The ears are smaller than the mockup's because the fur band cannot start below y 28 (`furBandTop`). Forge's header plates start at y 10 and 11 (`identityPlateBounds` and the left header polygon); Merli's title and cat plates start at y 36 (`headerPlateTop`, 32 in the first version), a row above the tabs well at 37, which is what lets the left ear show its pink above the title plate.

Contrast, measured 2026-10-17: Merli's `legendText` (`#8A8C8F`) puts the module codes at about 5:1 on their plates. Three smaller labels stay below 4.5:1 in both themes, a known gap, because they are drawn in `mutedText` at partial alpha rather than in `legendText`: the filter's axis numbers, the ENV display's 1 s / 2 s and the matrix row numbers.

`--profile 1440 900 merli` against Forge, three alternating rounds: idle frames 8.36 ms and 8.38 ms; Merli's rebuilds are cheaper.

## Tests and tools

Area `theme` (`tests/ForgeTestsTheme.cpp`, CTest `forge_theme`) checks names, that `applyTheme` sets every token and restores Forge's bytes, the settings round trip through the override only, read-once, the menu, that Merli renders deterministically and keeps every bound but the `PresetPlate`'s, and that Forge is pixel-stable in-process. That last check is what caught a buffered `PresetPlate` ([A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)). The test binary always starts on Forge ([Build and test Forge](build-and-test-forge.md)); `--snapshot` and `--profile` take a theme ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Deferred

A frameless, cat-shaped standalone window (title bar hidden from the standalone's Options, hidden by default under Merli, transparent outside the silhouette), deferred at the user's request. It needs Forge's own standalone app (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` with `StandalonePluginHolder`) and a non-opaque desktop window; a plugin in a host cannot do it, because the host owns the frame.

## Related

- [Forge editor (panel)](forge-editor.md)
- [Colours and typography](colours-and-typography.md)
- [Rhino Forge](forge.md)
- [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)
