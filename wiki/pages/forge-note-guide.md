---
title: Forge note guide
type: component
summary: A view-only chord and scale helper - a door on the keyboard shelf that names what the guide is set to and opens a NOTE GUIDE overlay on ENV and LFO, as the ARP plate does, where a root is picked on an octave of keys, a chord and a scale from button grids, and a readout spells their notes and degrees; the piano colours them, and every C carries its octave.
tags: [forge, ui, keyboard, layout]
sources: []
updated: 2026-10-25
---

# Forge note guide

Lights a chosen root, chord and scale across Forge's on-screen keys. It is UI only: it never changes MIDI or the
voices, and its choices are four properties on the state tree (`noteGuideRoot`, `noteGuideChord`, `noteGuideMinor`,
`noteGuideScale`), written by `Editor::saveNoteGuide` (`src/ForgeEditorNoteGuide.cpp`), so they ride in host state
and presets without being parameters. Redesigned on 2026-10-25 at the user's request: until then it was four fields
(ROOT, CHORD, MAJ/MIN, SCALE) crammed on the plate at the shelf's right-hand end, each opening a popup menu.

## The door

`ui::NoteGuideDoor` (`ui/ForgeNoteGuidePanel.h`) is a button over the well of the plate at the right-hand end of the
keyboard strip (`ui::noteGuideFace`, the plate reduced by 6, chamfer `noteGuideFaceCut`). It reads `GUIDE` with
`SETTINGS` or `CLOSE` stamped beside it, then the chord symbol in the chord colour (or the root alone in the root
colour, or `OFF`) and the scale under it in the scale colour (`NO SCALE` without one, `CHORDS & SCALES` while the guide is off). A click runs `Editor::toggleGuideOpen`. Open, its face
takes the ARP plate's faint lit outline in electric blue; under the pointer, a fainter one. `Editor::applyPage` sets
`setOpen(guideOpen)`, so every toggle path leaves the door telling the truth.

The plate is `noteGuidePlateWidth` = `jlimit(168, 210, shelf / 6)` wide, against about 250 when it held the fields.
The shelf is now laid out right to left: the guide's plate from the strip's right end, the ARP plate (still
`arpPlateWidth`) left of it, and the keys take the rest (`ui/ForgePlacement.h`). So the keys grew with the change,
774 px wide at 1440 against 714, and `keyboardHeight` went from 80 to 88, paid for by the two module rows. Eight is
the most it can take: at 1180 x 820 the signal row must still hold the FX rack's three 92 px strips whole
(`fxVisibleSlotCount` >= 3 in `rackCaptionsFitSuite`, `forge_layout`), which had 4 px to spare at 80 and failed at 92.

## The overlay

A module declared with no rows, `noteGuide` ("NOTE GUIDE", row 1, column 3, 18 wide, `only(Page::guide)`), on exactly
the cells the arp's panes take. `Page::guide` is the second page that is never a tab: `moduleShown` shows it while
`guideOpen` and hides whatever `ui::coveredBy(module, Page::guide)` finds under it, the same rule the arp uses. The
arp, the guide and the expanded FX rack close one another (`toggleArpOpen`, `toggleGuideOpen`, `toggleFxExpanded`).
The editor seats `ui::NoteGuidePanel` in the module's `controlArea`, as the TABLE tab's panel is seated in WAVETABLE,
and `applyEnableStates` shows it with the module.

The panel paints itself and hit-tests one pure layout, `ui::noteGuideLayout(area)`: three columns over a readout.

- **ROOT**: an octave of keys (`guideOctaveKey`, black keys straddling the white edges), drawn and coloured as the
  piano is, with an `R` on the root. A press on a key chooses that root.
- **CHORD**: the four chords the quality switch changes, the `MAJ | MIN` switch under them, then the seven fixed ones.
- **SCALE**: the twelve scales, three across.
- Each title carries its column's `OFF` at its right-hand end and a dot of its colour, which is the legend.
- **Readout** (a display well): for the chord, its symbol, its name in words and each note with its degree from the
  root (`R b3 b5 bb7`); for the scale, the same in scale degrees (`1 2 b3 4 5 b6 b7`), each note in the colour it
  wears on the keys.

A press goes through `applyNoteGuideHit`, which refuses a chord or scale with no root and the switch for a chord
without a quality (both drawn greyed), as the old fields did. ROOT's `OFF` clears only the root: the chord and scale
are kept for the next one (`guidePressSuite`). The panel then calls `onChange`, which saves and
repaints the door, the panel and the keys. Nothing on it moves on its own, so it is not in `liveRegions()`.

## Spelling

`ui/ForgeNoteGuide.h` holds the theory: the masks, `guideTone`, `guideChordSymbol` (`Cmaj7`, `Am`, `F#7#5`,
`Cm(maj7)`), `guideChordDescription`, `guideNotes` and `guideDegreeName`. Degrees are spelt by what the set is, not by
distance alone: a diminished seventh's top is `bb7`, an augmented fifth `#5`, and a scale's sixth semitone `#4` where
the fourth is missing (Lydian) but `b5` where it is kept (Blues, Locrian). Notes are named with sharps only, matching
the key labels. The three tone colours (`guideToneColour`) are literals, the same in both themes, because they name
notes rather than chrome.

## Key labels

`NoteGuideKeyboard::drawGuide` labels every C with its octave, guide or no guide, counted from the keyboard's
`getOctaveForMiddleC()` (3, so MIDI 60 is C3 and the keys run A0 to C6); with a root chosen the other keys are named
too (`guideKeyLabel`). White-key labels are 10.5 and black 9, up from 9 and 8. ASCII spellings only (`C#`, `->`):
Unicode accidentals have rendered as broken glyphs in this font path.

## Tests

`forge_guide` (`tests/ForgeTestsGuide.cpp`, added with the redesign): key labels, chord symbols and degree spellings;
the panel's layout at every allowed size (inside its module, nothing overlapping, every caption measured whole at
`guideCellTextSize`, every key answering a press on its own face) and the door's longest lines at the narrowest
window; presses against the state; and the editor: the door opens the panel over ENV and LFO, the ARP plate closes
it and it closes the arp, and a press is saved to the state tree. The masks themselves stay in `noteGuideSuite`
(`forge_layout`). `--snapshot out.png 1440 900 GUIDE` opens it on A minor over A natural minor
([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

The taller keys moved the two pixels Direct2D draws unstably onto the LFO curve, where they broke a `forge_displays`
cache check; that check now paints on a software image, and `forge_theme`'s byte match still fails by two pixels, as
the committed tree's did ([A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)).

## Related

- [Forge editor (panel)](forge-editor.md)
- [Forge ARP plate, slots and waterfall](forge-arp-plate.md)
- [Forge arpeggiator](forge-arpeggiator.md)
- [One knob diameter for the whole Forge panel](forge-knob-diameter-is-panel-wide.md)
- [A buffered child broke Forge's byte-identical paint](buffered-child-breaks-pixel-stability.md)
