---
title: JUCE's isBold() is false for SemiBold
type: gotcha
summary: juce::Font::isBold() matches only the whole word Bold, so a LookAndFeel keyed on it serves the Regular cut for SemiBold text.
tags: [rhino, juce, typography]
sources: []
updated: 2026-10-03
---

# JUCE's isBold() is false for SemiBold

## Symptom

Nothing looks broken. Every label meant to be SemiBold is drawn in the Regular cut, which is still legible, so the mistake can go unnoticed for months. In Rhino the visible trace was the track card's name. It had been drawn twice over to make it heavier, and that double strike made it the one soft-looking string in the interface.

## Cause

`juce::Font::isBold()` checks the style name with `containsWholeWordIgnoreCase("Bold")`. "SemiBold" is a single word, so the answer is false. The same is true for "Medium", "Black" and "ExtraBold". Rhino asks for its heavier cut with `FontOptions().withStyle("SemiBold")` (`uiFontBold` in `native/src/Theme.h`). `Theme::getTypefaceForFont` used to branch on `isBold()`, so it served the Regular face for every one of those fonts. This was fixed in commit `0a6282d` (2026-09-30).

## What to do

- **Read the style string.** When a LookAndFeel picks a cut, check `font.getTypefaceStyle().containsIgnoreCase("bold")`, as `Theme::wantsHeavierCut` now does. Do not use `isBold()`.
- **Assert it.** A visual check will not catch this mistake. The arrangement workflow's `scenarios/InterfaceFonts.inc` requires `getTypefaceForFont(uiFontBold(em)) != getTypefaceForFont(uiFont(em))` at six sizes from 8 to 17 px per em. It also compares the track card's name pixel for pixel with the same string drawn once, so a returning double strike fails the suite.
- **Find the weight in `getStyle()`.** For a face loaded from embedded data, `Typeface::getName()` returns only the family ("Inter"), and the weight is in `getStyle()`. That is why the scenario checks `getName() + " " + getStyle()`.

Forge is not affected. `ui/ForgeType.h` hands out one of four embedded typefaces according to the job the text does (`Face::header`, `label`, `emphasis` or `reading`), and never asks a font how heavy it is.

## Related

- [Colours and typography](colours-and-typography.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [App shell and control bar](app-shell.md)
