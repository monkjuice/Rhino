---
title: A JUCE parameter cannot print a value outside its range
type: gotcha
summary: NormalisableRange::convertTo0to1 and convertFrom0to1 both clamp, and a parameter's getText formats the denormalised value, so text for a value past either end always prints that end; format an out-of-range value yourself or name the end.
tags: [forge, juce, parameters]
sources: []
updated: 2026-10-19
---

# A JUCE parameter cannot print a value outside its range

`juce::NormalisableRange::convertTo0to1` clamps its result to 0..1, and `convertFrom0to1` clamps again. `RangedAudioParameter::getText(normalised, length)` hands the parameter's string function the denormalised value, so the text for any value past either end of the range comes out as that end. No assertion fires.

Found on 2026-10-19 in [Forge preset import](forge-preset-import.md). `PatchWriter::set` (`instruments/rhino-forge/src/ForgePresetImport.cpp`) printed the original value with `parameter->getText(range.convertTo0to1(plain))`, so a Serum preset warned "LFO 3 RATE: 20.00 Hz is past Forge's range, set to 20.00 Hz". The warning now names the end the value is held at instead of the value. The import test holds this with a Vital release twice Forge's 8 s maximum.

A parameter's own text is only for values it can hold. To show a value outside the range, format it yourself, in units that match what the knob displays.

## Related

- [Forge preset import (Serum 2 and Vital)](forge-preset-import.md)
- [JUCE's isBold() is false for SemiBold](juce-isbold-misses-semibold.md)
- [A juce::Path holding only a start point is empty](juce-path-isempty-ignores-a-lone-point.md)
