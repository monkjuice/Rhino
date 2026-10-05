---
title: Forge engine (Core)
type: component
summary: The header-only voice engine that allocates sixteen voices, modulates per voice per sample and scales the output by 0.28.
tags: [forge, engine, voices, real-time]
sources: []
updated: 2026-10-05
---

# Forge engine (Core)

`rhino::forge::Core` in `core/ForgeCore.h` is the sound engine. It covers the voices with their envelopes and LFOs, the oscillators, the filter, the busses, the three FX racks, and the readings the panel draws. It has no AudioProcessor, UI, state tree or filesystem access.

## How the Processor drives it

- **Ownership:** `Processor` holds `Core core;` by value.
- **Preparation:** `prepareToPlay` calls `Core::initialise`, which builds everything that allocates: the tables, the noise calibration, the delay lines and the spectral heap block.
- **Per block:** `Processor::patch()` fills a `Patch` snapshot by copying values across pointers. The constructor pairs every parameter a block reads with the field it fills (`patchBindings`, `modulationBindings`, and `lfoRateParameters` for the LFO rates), so a block spells no parameter id. Building those ids as `juce::String`s cost about 2,100 heap allocations a block until commit `c9a37a6`; a block now allocates nothing, which `realtimeSuite` (area `engine`) checks.
- **Per sample:** `renderSample` runs once per sample and must not allocate, lock or touch files. That rule is a hard gate on every milestone ([Real-time audio rules](real-time-audio-rules.md)).
- **The block promise:** `Core::beginBlock(patch)` promises that every `renderSample` of the block gets that same, unchanged patch, and `endBlock` ends the promise. With any matrix slot live, each voice used to copy the whole `Patch`, LFO tables included, every sample before applying its modulation. Under the promise the scratch is copied once a block, and each voice puts back only the fields the live slots write (commit `0d97e98`). A caller that makes no promise, such as a test driving `Core` directly, still gets the whole copy.
- **Arpeggiator:** it stands in front of Core and calls `noteOn` and `noteOff`; Core never calls it ([Forge arpeggiator](forge-arpeggiator.md)).

## Voices

- **Count:** 16 voices (`std::array<Voice, 16>`). POLY (1–16, default 8) sets how many are used.
- **Allocation (M9d):** the order of preference is:
  1. a silent voice, still rotating so notes do not share start phases;
  2. the quietest voice already releasing;
  3. the quietest held voice.

  A stolen voice that is still audible is retuned, not rebuilt. Its phases, filter state and envelope level carry over, so only the pitch jumps.
- **Release (M9e):** when ENV 1 reaches zero, the voice fades out over 15 ms (`voiceTailSeconds`) instead of being cut, because the filter is still ringing. The stated reason for 15 ms is "half a cycle of the lowest cutoff", but with the CUTOFF floor at 30 Hz, half a cycle is 16.7 ms.
- **All notes off (CC123)** lets every voice go into its release, as lifted keys do, through the same `releaseVoice`, and leaves the racks ringing. It used to reset the whole `Core`, zeroing about 20 MB of delay and reverb lines at 48 kHz inside one sample: a click, lost tails and a risk to a small buffer's deadline, on every stop and loop a host sends it (commit `2cc79de`).
- **Mono legato** joins only notes that overlap. A voice already in its release has no key down, so the next note starts again; joined, it carried on the release and faded out under a held key (commit `32147e1`).
- **Per-voice modulators:** all four envelopes, and every LFO in TRIG or ENV mode, run per voice. LFOs in OFF mode share one free-running cycle.

Modulation is applied per voice and per sample, in each destination's normalised 0..1 space. Offsets are summed per destination and then applied once, so two half-depth slots equal one full one ([Forge modulation: matrix, envelopes, LFOs and macros](forge-modulation.md)).

The panel's readings come from the loudest voice by ENV 1: envelope stage, LFO phase, modulation offsets and spectral scan position. Core keeps them as plain members, and the Processor copies them into atomics once per block. Once per block is plenty for a panel that redraws at 24 Hz.

## The master stage: measure pieces alone

After the racks, the sum is scaled by `jlimit(0, 1.25, output) * 0.28`, then soft-clipped, linear below 0.8. At the default OUTPUT of 0.75 the gain is 0.21, about −13.5 dB. A full-scale source therefore comes out of `Core` about 13.5 dB down, and its 0.707 centre pan takes another 3 dB. When a source "sounds quiet", measure that piece on its own ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)).

Tests build `Core` on the stack, so the test binary links an 8 MB stack ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).

## Related

- [Rhino Forge](forge.md)
- [Forge's engine splits into headers only](forge-engine-headers-only.md)
- [Forge oscillators and wavetables](forge-oscillators.md)
- [Forge mixer and effects racks](forge-mixer-and-fx.md)
- [Real-time audio rules](real-time-audio-rules.md)
