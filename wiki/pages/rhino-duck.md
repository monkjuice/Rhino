---
title: Rhino Duck
type: component
summary: An independent VST3 and standalone sidechain ducker under effects/rhino-duck/, built the way Forge is: sixteen curves read by a Sync, MIDI or Audio clock, Mix for depth, Hold, Release, Attack, Depth and Shift kept per curve, a band split, and a scope, thumbnails and gain-reduction meter drawn from what the audio runs.
tags: [duck, vst3, effects, dsp, juce]
sources: []
updated: 2026-10-26
---

# Rhino Duck

A sidechain ducking effect in the shape of Cableguys' Kickstart 2, whose manual (in the Downloads folder,
`Nicky-Romero-Kickstart-2-Manual.pdf`, 15 pages) was the brief: a ducking curve applied straight to the sound on its
track, so a bassline drops out of a kick's way with no compressor and no routing. It is Rhino's second independent
plugin project, `effects/rhino-duck/`, a JUCE `AudioProcessor` that builds a VST3 and a standalone app and owns no
Tracktion or Rhino type ([Forge is an independent VST3](forge-is-an-independent-vst3.md) gives the reasons, and they
hold here). The code, the look and the curves are Rhino's own; only the workflow follows the manual.

## Source layout

| Directory | Holds |
| --- | --- |
| `core/DuckCurves.h` | The sixteen curves as pure arithmetic, no JUCE |
| `core/DuckEngine.h` | The engine: clock, triggers, delay, crossover, scope, no JUCE |
| `ui/` | `DuckTheme.h` (palette and the three faces), `DuckLayout.h` (geometry as a pure function), `DuckControls.h`, `DuckArt.h` (plates, grain, the duck), `DuckMeter.h`, the subset fonts |
| `src/` | `DuckProcessor.*` and `DuckParameters.cpp` (the host boundary), `DuckScopeView.*`, `DuckEditor.*` and `DuckEditorPaint.cpp` |
| `tests/` | One file per area, `curves`, `engine`, `processor`, `layout`, and `--snapshot` |

Build and test exactly as Forge: `cmake -S effects/rhino-duck -B effects/rhino-duck/build -G "Visual Studio 17 2022" -A x64`,
then `cmake --build ... --config Release --parallel 2 -- /p:BuildInParallel=false`, and `ctest --test-dir effects/rhino-duck/build -C Release`
([Build and test Forge](build-and-test-forge.md) has the traps; `CMAKE_SUPPRESS_REGENERATION` and `VST3_AUTO_MANIFEST FALSE`
are set here too). The build tree is git-ignored. Every Duck source compiles against `src/DuckPch.h`.

## The curves

`curveGain(index, phase, adjust)` in `core/DuckCurves.h` is the one function: a gain 0 to 1 over a cycle of phase 0 to 1.
A curve is a start level and up to six points, each the phase a segment ends at, the level reached and how it travels
there (linear, smoothstep, ease-out, ease-in); after the last point the level holds, and the wrap from the last level
back to the first is the cut on the beat. The sixteen are five families, in the order the grid shows: eight sidechain
curves (Classic, Hook, Tight, Late, Ramp, Soft, Snap, Long), two sub-bass curves with slower rises that fall again
before the next beat (Sub Slow, Sub Deep), two kick trimmers that start open and close (Trim, Trim Slow), two reverse
curves open at both ends with a dip between (Suction, Suction Wide), and two rhythm curves closed at both ends (Swell,
Double).

Five adjustments, `CurveAdjust { hold, shift, release, attack, depth }`, bend a curve and are remembered **per
curve**, as the manual's Kickstart does (Hold and Release replaced Slope in 7a4c161, 2026-10-26):

- **Hold** moves the points flagged `mobile`, the sloped part, by an absolute phase offset of its start; **Release**
  scales the part's length from that start. Both stay within the room the fixed points leave (`mobileRoom`,
  `slopedPart`); `holdRoom`/`releaseRoom` are each knob's travel given the other, and `withHold`/`withRelease` clamp
  each against the other, so turning one never moves the other (evaluation clamps too, Release winning). A part
  designed to start at phase 0 may stay there, which is why Swell and Double gained a mobile point at phase 0, level 0:
  they now have a part to hold and release, and sound identical at the defaults (tested).
- **Attack** (phase, at most 0.5) eases, smoothstep, from the level the cycle ends at into the curve over its first
  stretch; a curve that ends where it starts is unchanged by it.
- **Depth** keeps that much of the dip: `1 - depth * (1 - level)`.
- **Shift** moves the whole curve round the cycle, wrapping. The four arrows step it by 1/16 and 1/256 of the length.

None is a parameter ([Decisions](#decisions)). The processor keeps them as atomics (`Processor::Memory`) and saves
them in `CURVE` children of a `CURVES` node (`hold`, `shift`, `release`, `attackMs`, `depth`; no reading of the
day-old `slope` attribute). **Attack is stored in milliseconds** (0-100 ms) and converted to phase at
`cycleSeconds()`, Length in beats x 60 / the host's last bpm, which `processBlock` stores before reading the settings:
a tempo change leaves Attack's ms alone while Hold and Release, being phase, scale with tempo and Length like the rest
of the curve. `readCurves` takes raw values within sane bounds; `slopedPart` applies the room whenever they are read.

## The engine

`Engine::process` in `core/DuckEngine.h` works a sample at a time and allocates nothing after `prepare`
([Real-time audio rules](real-time-audio-rules.md)):

- **The clock.** Sync reads the host's ppq at the block's start and advances it per sample, so the duck lands on the
  beat wherever playback starts; with the host stopped it runs free at the host's tempo from the phase it was left at.
  MIDI restarts the cycle on a note (filtered by the menu's channel and key), Audio on an onset in the sidechain: a
  0.2 ms follower overtaking a 150 ms one by 6 dB above -40 dBFS, once per 50 ms, re-armed once it falls back. Loop
  repeats the cycle; off, it runs once to the End marker and freezes there. Before any trigger the signal passes unducked.
- **Latency.** Audio detection looks 10 ms ahead; anti-click smoothing (on by default) leans the gain towards the new
  cycle's start over the 6 ms before a restart. Sync has none, MIDI 0 or 6 ms, Audio 10 or 16 ms. The engine delays the
  signal by that much itself (`latencySamples`), schedules each restart as an absolute frame, and the processor tells the
  host from the message thread through an `AsyncUpdater`, never from the audio thread.
- **The gain** is `1 - mix * (1 - curve)`, slewed so its fastest swing from 1 to 0 takes 1 ms: the cut on the beat is
  sharp but never a single-sample step.
- **Band split** is a fourth-order Linkwitz-Riley crossover (two cascaded Butterworth sections each way), run whether or
  not the band is on so switching it meets warm filters; the band and solo switches crossfade over 10 ms, and the
  cutoff glides over about 20 ms, recomputed per block. The first block after a reset lands on its cutoff at once, so a
  static setting renders the same in blocks of any size.
- **The scope** is 256 columns of lowest and highest sample, output, sidechain and dry (the delayed input before the
  gain), six atomics per column, indexed by phase and written by the audio thread; the panel reads them whole. The
  sidechain shown is delayed by the latency, so it lines up with the output.
- **The meter's reading.** `takeDeepestGain()` exchanges the deepest gain applied since the last take (Mix included)
  with `noGainReading` (2.0: no block ran). The atomic is `mutable` so the panel's `const Engine&` can take it; the
  audio thread does a load-min-store, so a take landing between the two costs at most one block's figure. Only the
  meter takes it; `cycleCount()` tells a picture that a cycle started.

## The panel

Since 7a4c161 (2026-10-26) the panel is 1000 x 620 in the industrial Rhino and Forge look: a rail with the brand, a
rubber-duck gain-reduction meter and Mix; the chips, the scope and the sixteen curve cells; a strip with Attack, Hold,
Release and Depth for the chosen curve beside the band split; and a preset bar that steps through the curves.
[Rhino Duck's panel](rhino-duck-panel.md) has the layout, the meter, the scope's layers and the snapshot flags. The
menu holds anti-click smoothing, the MIDI channel and note, the curve resets and the window size.

## Hosting

Since 2026-10-26 Rhino's device catalog lists Duck as `RhinoDuck`, an external audio effect under Dynamics, so the
browser's Audio FX tab offers it and a drop or the rack's `+` menu puts the VST3 on a track through Tracktion's
external-plugin wrapper. The entry carries what discovery needs (`pluginName`, `bundle`, `developmentArtefacts`) and
the nine parameters its rack shows first (Mix, Curve, Length, Trigger, Loop, End, Band split, Band cutoff, Band solo;
MAP adds more from its own window), and `SessionExternalPlugins.cpp` finds every external entry the way it found
Forge: a VST3 already in the engine's known-plugin list, else the development build under the repository, else the
bundle in the platform's VST3 folders ([Hosting Forge in Rhino](forge-hosting.md), [Device catalog](device-catalog.md)).
The scenario in `native/src/tests/Pattern/scenarios/DeviceParameters.inc` adds it to an audio lane and a MIDI lane
where a Duck build is found, and requires it to be found when the development Release bundle exists.
Duck's Audio trigger reads its stereo Sidechain bus. Tracktion enables that bus, so the hosted Duck has four inputs and its face carries the rack's Sidechain strip ([Sidechains](sidechains.md)).
Until 2026-10-26 nothing could choose a source, and the host clears those channels when none is routed, so in Audio mode Duck passed the sound unducked.
Sync mode needs no sidechain. A MIDI trigger from another track's notes cannot be routed yet. Outside Rhino it is an ordinary VST3 from `effects/rhino-duck/build/RhinoDuck_artefacts/Release/VST3/Rhino Duck.vst3`.
As with Forge, a VST3 counts as shipped only after it has been loaded in a real host, and a running host locks the
bundle against relinking ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).

## Tests

`RhinoDuckTests`, one CTest case per area (`duck_curves`, `duck_engine`, `duck_processor`, `duck_layout`), measure
rendered audio rather than read the DSP ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)): the
output follows the curve at the host's phase and from a mid-beat start; Mix 0 passes the signal untouched; blocks of
64, 1000 and 4096 render identically in Sync and with the Audio trigger; the crossover sums flat within 0.3 dB and a
200 Hz split leaves 2 kHz within 0.5 dB while taking 60 Hz down 30 dB; a kick on the sidechain lands the duck 10 ms
later on its own onset and a second kick restarts an open curve; a note cuts on its frame, and with smoothing fades
over 288 samples; a one-shot freezes at End; the largest sample-to-sample step is under 0.004 with smoothing and
about 0.02 without; Depth 50% ducks half way, Attack lets the beat's first moments through, a shorter Release opens
sooner and a longer Hold cuts longer; the dry trace is recorded and the deepest gain includes Mix; untouched curves
play as designed and Hold and Release keep their rooms; the state round-trips all five adjustments per curve, and
Attack keeps its ms across Length and host tempo; latency follows the mode; the MIDI filters hold. The panel's tests
and `--snapshot` are on [Rhino Duck's panel](rhino-duck-panel.md#tests-and-snapshots). For 7a4c161, dropping Depth
from `curveGain` and moving the waterline with the duck each failed their area before being reverted.

Not yet verified for 7a4c161 (as of 2026-10-26): the new VST3 loaded in a real host, and Rhino's own suite, which
was not run because RhinoDAW was open.

## Decisions

- **An independent VST3, not a native device.** Asked for on 2026-10-25; the reasons are Forge's. It joined the
  catalog as an external entry the next day, which is what made the catalog carry an external plugin's facts.
- **A new top-level `effects/` folder**, beside `instruments/`, because an effect under `instruments/` would mislead.
- **Curve adjustments are state, not parameters.** The manual treats them as the curve buttons' own memory. Per-curve
  parameters would flood every host's automation list, and four shared Attack/Hold/Release/Depth parameters would
  make a tile forget its edits when another is chosen. So when the handoff's strip arrived (7a4c161) the user kept all
  five per curve: none is automatable or on Rhino's rack, whose `rackParameters` stay nine.
- **The band keeps its single Linkwitz-Riley cutoff** (20 Hz-5.12 kHz); the mockup's low-high range was not built.
  WHOLE SIGNAL is a switch shown as the inverse of the existing `band` parameter, so no parameter changed.
- **Lookahead is the engine's own delay line**, not a request to the host, so a restart lands on the sample it was heard
  with in every host alike.

## Related

- [Rhino Duck's panel](rhino-duck-panel.md)
- [Rhino Forge](forge.md)
- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Build and test Forge](build-and-test-forge.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Displays draw from the DSP](displays-draw-from-the-dsp.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [Sidechains](sidechains.md)
- [Rhino Vocoder](rhino-vocoder.md)
