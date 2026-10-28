---
title: Rhino Arp
type: component
summary: A built-in MIDI arpeggiator with beat or millisecond scheduling and a dedicated five-area rack face.
tags: [devices, midi, arpeggiator, timing, ui]
sources: []
updated: 2026-10-28
---

# Rhino Arp

Rhino Arp is a standalone built-in `te::Plugin` in `RhinoDevices`, registered under the stable type `rhino.arp.v1`. It remains a rack MIDI effect before the instrument: it owns no synth voices or audio path, and adding or editing it must not involve `Session` or an instrument implementation.

Its 15 automatable controls cover style (Up, Down, UpDown, DownUp, Chord), rate mode and value, gate/overlap, Hold, pattern Offset, groove (Straight, Swing 8, Swing 16), retrigger (Off, Note, Beat plus interval), finite/infinite repeats, and scale-aware Distance/Steps plus Root. Factory defaults are Up, beat mode at 1/16, 84% gate, Steps 0, Distance +7 scale degrees, Straight, Hold off, Retrigger off, infinite repeats, and C Chromatic; free rate stores 125 ms and retrigger interval stores 1/2 note. Explicit saved settings retain their values. For a plain uninterrupted chord in an older instance, set Steps to 0 and Retrigger to Off.

The scale choices reuse `MusicalScale` names and masks from `native/src/core/dsp/ScaleQuantizer.h`: Chromatic, Major, Minor, Harm Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pent, Minor Pent, Blues, and Whole Tone. Preserve the original persisted indices (Chromatic 0, Major 1, Minor 2); additions follow them.

## Timing contract

Beat mode schedules in edit beats, not block-relative seconds. Millisecond mode keeps the next absolute wall-clock step time, clamps the free rate to 10-2000 ms, and converts that time back into edit beats for event output; gate duration follows the same conversion. This makes free-rate spacing independent of song tempo while preserving the processor's beat-domain event machinery.

Every beat/second conversion reads `edit.tempoSequence.getInternalSequence()` through `tempoMap(edit)`, never `edit.tempoSequence.toBeats`/`toTime`. Those calls can apply a pending tempo change under the audio callback lock, and on a graph worker that deadlocked RhinoDAW during a tempo drag until `b72e67d` ([Tracktion's tempo sequence can take the audio callback lock](tracktion-tempo-sequence-takes-callback-lock.md)).

Apply incoming note changes at their actual in-block MIDI timestamp, then convert scheduled events to seconds only when writing output. This prevents generated notes from appearing before the input event that triggered them. Reuse pre-sized MIDI output scratch arrays and fixed-size pending note-off storage on the realtime path; do not introduce callback allocation.

Gate may extend to 200%, so note-offs can overlap following steps. Each MIDI channel/pitch pair owns one output gate; release its previous gate before retriggering that pair. Simultaneous inputs mapped to the same output deduplicate, and each gate tracks its contributing input pitches in a fixed bitset: releasing one contributor must not cut another. Prune inactive contributors and retain the initial source ID for the matching note-off. Pending releases stay in the scheduler until their timestamp is reached, rather than being emitted ahead of intervening input changes. This also replaces the old 512-entry pending-off queue, which could overflow during dense retriggers. Input chord identity likewise includes channel as well as pitch.

With Hold off, releasing the final input note ends generated gates at the incoming event timestamp; turning Hold off also releases latched output. Stopped transport has an independent timeline (`timelineValid` and `lastBlockEndBeat`) that advances through silent blocks and ordinary reset/panic; initialization and the playing-to-stopped transition reanchor it. Do not derive elapsed time from pattern activity (`clockValid`): this previously stranded note-offs and made the second phrase's Beat phase depend on audio buffer size. Fresh live input still arpeggiates while transport is stopped.

Note and Beat retrigger explicitly restart finite repeats; Repeats is a cycle count between retriggers, not a global lifetime limit. Hold intentionally permits continuation after key release, as in the [Ableton arpeggiator](https://www.ableton.com/en/live-manual/12/live-midi-effect-reference/#arpeggiator). Note retrigger restarts on a new input note. Beat retrigger resets the pattern at the next existing rate tick at or after its interval boundary, preserving the rate cadence and the separate groove phase. It never inserts an off-grid attack; missed boundaries advance directly beyond the current tick. Inserting an attack at an exact boundary formerly produced near-duplicate roots and shortened intervals when input arrived just before it.

CC120/123 clear the chord on their MIDI channel. Global panic, reset, transport stop, and seek clear remembered notes. `reset` and `midiPanic` request an audio-thread reset atomically and send a downstream global AllNotesOff; consume the atomic request even when the incoming block already carries a global panic, or a delayed reset can kill fresh input in the next block.

Scale first corrects each input to the nearest allowed pitch, choosing the lower note on ties, then applies Distance in scale degrees using the shared `ScaleQuantizer.h` helpers. Correction applies even with Steps 0: C–E–G becomes C–E♭–G in C minor, or B–E–F♯ in E major. This replaces preservation of chromatic passing-note offsets and follows the documented [Ableton scale correction rule](https://help.ableton.com/hc/en-us/articles/11425083250972-Keys-and-Scales-in-Live-12-FAQ). Root selects the scale's tonal center; Chromatic preserves input pitches and ignores Root. Degree transposition uses each scale's actual size, so -7 degrees in C minor is one octave; clamp final pitches to valid in-scale MIDI endpoints.

Parameter registration remains in `native/src/devices/midi/RhinoArpDevice.cpp`; scheduling and note ownership live in the same class's `RhinoArpDeviceProcessing.cpp` translation unit.

## Editor and tests

The dedicated 820 px rack face lives in `DeviceEditorPanelArp.cpp` and puts all 15 automatable parameters under the hand as 15 controls: seven sliders (rate or free rate, Offset, Interval, Repeats, Steps, Distance, Gate), five dropdowns (Style, Groove, Retrigger, Root, Scale), Hold and the two Beat/ms rate-mode buttons. Its five areas read left to right Pattern, Rate, Sequence (the moving-note display), Timing, Harmony: `arpLayout` gives them 160, 92, 204 and 150 px, every gap `cardGap` (6), and Harmony takes the rest (182 px at 820). Since commit `a925614` (2026-10-28) the Sequence display is a card like the other four: it fills its area (reduced 0.5, radius 3) and its title is drawn in `cardTitleArea` in the cards' `midiEffect` ink at 7.5 bold, so all five tops and bottoms line up. Until then it painted itself inset 5 px, with 3 px gaps, so its top and bottom stood 5 px inside its neighbours'. The graph is inset 12 px at the sides and 22 px top and bottom. Its notes and connecting line are orange, `palette::arpSequence`, a colour no control wears, so the picture of the output stands apart from what shapes it; `palette::midiEffect` (cyan) colours the controls only (commit `d28a687`).

Rate owns a knob and compact Beat/ms mode buttons; Retrigger Interval and Steps are rotary knobs, and Steps, Distance and Gate share Harmony's lower row in equal thirds. On Harmony's upper row Root is 54 px and Scale takes the rest: split evenly, "Chromatic" was cut to "Chrom...". While Steps is 0, Distance is disabled, drawn in `palette::disabled` with its caption and value dimmed, and its tooltip adds "Does nothing while Steps is 0.": Distance is the size of each transposition step, so with no steps it moves nothing. Repeats and Offset are bars, both `LinearBarVertical`, which keeps Rhino's vertical drag. Repeats fills bottom-up and snaps to the pointer. Offset fills left to right through the component property `barFillsAcross` (`"rhinoBarFillsAcross"`, declared in `Theme.h`) and drags relatively (`setSliderSnapsToMousePosition(false)`), since a press's height means nothing on a bar that fills across. `Theme::drawLinearSlider` computes that fill's edge from `valueToProportionOfLength`, because JUCE passes a vertical bar's `sliderPos` as a y.

Arp-specific control construction and styling stay in this translation unit rather than enlarging the shared `DeviceEditorPanel.cpp`, with one exception: the sliders' style, snapping, `barFillsAcross` and Distance's disabled state are set in the shared `styleControls`, which runs on every `setTarget`, so Distance follows a Steps drag through the rack's `refreshTouchedDevice`. List the file explicitly in `native/CMakeLists.txt`; MIDI-effect chrome uses the named theme colour rather than a literal. Known blemish, pre-existing and unfixed: Repeats' "All" is drawn twice, by the `parameterValues` readout laid over the bar and by `drawLinearSlider`'s own in-bar text, so it renders bolder than the rest of the face.

Coverage belongs at three levels: direct processor timing/pitch behavior, parameter/default registration, and dedicated-face bounds plus off-screen snapshot coverage. `native/src/tests/ArpTest.cpp`, using private `ArpTestHarness.h`, checks emitted MIDI through `native_device_conformance`: note lifetime, retriggers, shared output ownership, panic/reset/stop/seek, live scale/root changes, all 13 scales, degree movement, and MIDI endpoints. Timing cases cover playing and stopped transport at 64, 127, 480, 512, and 24000 samples, including staggered chords near a Beat boundary and a second phrase after silence. At an 80 ms free rate with a 500 ms Beat interval, reset waits for the 560 ms tick; swing retains its phase. Keep this suite in the existing 120-second conformance runner; adding it to the 30-second self-test exhausted that runner's budget during later Drum Rack checks.

`native/src/tests/Pattern/DeviceRackTest.cpp` pins the face: Steps is rotary; Offset is a non-snapping `LinearBarVertical` with `barFillsAcross` and Repeats lacks the property; Pattern and Rate sit left of the display and Timing and Harmony right of it; Steps turned to 0 through the slider, so the rack's own refresh runs, disables Distance, and turning it back re-enables it.

The reference face is `docs/images/rhino-arp.png`, written when `RHINO_ARP_SNAPSHOT` names a path under the `native_pattern_workflow` CTest case. The test sets a local `Theme` on the panel (`setLookAndFeel`) for the snapshot; before `d28a687` it painted in stock `LookAndFeel_V4`, so the PNG showed JUCE's knob and a bar with no number rather than the app's face ([Seeing the UI without taking the screen](headless-ui-snapshots.md)).

## Related

- [Device rack and device editors](device-rack.md)
- [Colours and typography](colours-and-typography.md)
- [Device chain order](device-chain-order.md)
- [Tracktion's tempo sequence can take the audio callback lock](tracktion-tempo-sequence-takes-callback-lock.md)
