---
title: Rhino Arp
type: component
summary: A built-in MIDI arpeggiator with beat or millisecond scheduling and a dedicated five-area rack face.
tags: [devices, midi, arpeggiator, timing, ui]
sources: []
updated: 2026-10-05
---

# Rhino Arp

Rhino Arp is a standalone built-in `te::Plugin` in `RhinoDevices`, registered under the stable type `rhino.arp.v1`. It remains a rack MIDI effect before the instrument: it owns no synth voices or audio path, and adding or editing it must not involve `Session` or an instrument implementation.

Its 15 automatable controls cover style (Up, Down, UpDown, DownUp, Chord), rate mode and value, gate/overlap, Hold, pattern Offset, groove (Straight, Swing 8, Swing 16), retrigger (Off, Note, Beat plus interval), finite/infinite repeats, and scale-aware Distance/Steps with Chromatic, Major, or Minor plus Root. The intentional preset-like defaults are UpDown, beat mode at 1/16, 125 ms stored for free mode, 84% gate, Distance -7 scale degrees, Steps 2, Swing 16, Hold on, Beat retrigger every 1/2 note, infinite repeats, and G# minor.

## Timing contract

Beat mode schedules in edit beats, not block-relative seconds. Millisecond mode keeps the next absolute wall-clock step time, clamps the free rate to 10-2000 ms, and converts that time back into edit beats for event output; gate duration follows the same conversion. This makes free-rate spacing independent of song tempo while preserving the processor's beat-domain event machinery.

Apply incoming note changes at their actual in-block MIDI timestamp, then convert scheduled events to seconds only when writing output. This prevents generated notes from appearing before the input event that triggered them. Reuse pre-sized MIDI output scratch arrays and fixed-size pending note-off storage on the realtime path; do not introduce callback allocation.

Gate may extend to 200%, so note-offs can overlap following steps. Scale Distance is measured in scale degrees, not semitones; for example, -7 degrees in C minor moves down an octave.

## Editor and tests

The dedicated 820 px rack face lives in `DeviceEditorPanelArp.cpp` and puts all 15 automatable parameters under the hand as 15 controls: seven sliders (rate or free rate, Offset, Interval, Repeats, Steps, Distance, Gate), five dropdowns (Style, Groove, Retrigger, Root, Scale), Hold and the two Beat/ms rate-mode buttons. Its five areas read left to right Pattern, Rate, Sequence (the moving-note display), Timing, Harmony: `arpLayout` gives them 160, 92, 210 and 150 px with gaps of 6, 3, 3 and 6, and Harmony takes the rest (182 px at 820). The display paints itself inset 5 px, so its 3 px gaps make an 8 px gutter. Its notes and connecting line are orange, `palette::arpSequence`, a colour no control wears, so the picture of the output stands apart from what shapes it; `palette::midiEffect` (cyan) colours the controls only (commit `d28a687`).

Rate owns a knob and compact Beat/ms mode buttons; Retrigger Interval and Steps are rotary knobs, and Steps, Distance and Gate share Harmony's lower row in equal thirds. While Steps is 0, Distance is disabled, drawn in `palette::disabled` with its caption and value dimmed, and its tooltip adds "Does nothing while Steps is 0.": Distance is the size of each transposition step, so with no steps it moves nothing. Repeats and Offset are bars, both `LinearBarVertical`, which keeps Rhino's vertical drag. Repeats fills bottom-up and snaps to the pointer. Offset fills left to right through the component property `barFillsAcross` (`"rhinoBarFillsAcross"`, declared in `Theme.h`) and drags relatively (`setSliderSnapsToMousePosition(false)`), since a press's height means nothing on a bar that fills across. `Theme::drawLinearSlider` computes that fill's edge from `valueToProportionOfLength`, because JUCE passes a vertical bar's `sliderPos` as a y.

Arp-specific control construction and styling stay in this translation unit rather than enlarging the shared `DeviceEditorPanel.cpp`, with one exception: the sliders' style, snapping, `barFillsAcross` and Distance's disabled state are set in the shared `styleControls`, which runs on every `setTarget`, so Distance follows a Steps drag through the rack's `refreshTouchedDevice`. List the file explicitly in `native/CMakeLists.txt`; MIDI-effect chrome uses the named theme colour rather than a literal. Known blemish, pre-existing and unfixed: Repeats' "All" is drawn twice, by the `parameterValues` readout laid over the bar and by `drawLinearSlider`'s own in-bar text, so it renders bolder than the rest of the face.

Coverage belongs at three levels: direct processor timing/pitch behavior, parameter/default registration, and dedicated-face bounds plus off-screen snapshot coverage. Millisecond mode has an exact 80 ms spacing regression; timing tests must also assert that no generated event precedes an in-block note-on. `native/src/tests/Pattern/DeviceRackTest.cpp` pins the face: Steps is rotary; Offset is a non-snapping `LinearBarVertical` with `barFillsAcross` and Repeats lacks the property; Pattern and Rate sit left of the display and Timing and Harmony right of it; Steps turned to 0 through the slider, so the rack's own refresh runs, disables Distance, and turning it back re-enables it.

The reference face is `docs/images/rhino-arp.png`, written when `RHINO_ARP_SNAPSHOT` names a path under the `native_pattern_workflow` CTest case. The test sets a local `Theme` on the panel (`setLookAndFeel`) for the snapshot; before `d28a687` it painted in stock `LookAndFeel_V4`, so the PNG showed JUCE's knob and a bar with no number rather than the app's face ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Device rack and device editors](device-rack.md)
- [Colours and typography](colours-and-typography.md)
- [Device chain order](device-chain-order.md)
