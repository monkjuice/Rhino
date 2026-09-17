# Rhino instruments: delivery plan

This plan separates the finished built-in instrument from the next, independent
plugin. The second project takes inspiration from Serum 2's fast visual sound-
design workflow, not its code, assets, branding, or a feature-for-feature clone.

## Milestone A — finish Rhino Wave

1. Replace analytic oscillator reads with precomputed wavetable frames and
   octave-band table selection. Keep the existing `rhino.wave.v1` state keys
   compatible with saved Rhino projects.
2. Add a per-voice LFO and four deliberate modulation depths: wavetable
   position, cutoff, pitch, and motion. The LFO is free-running; transport-sync
   belongs to the later modulation/clip-automation layer.
3. Replace the one-pole tone stage with a stable resonant state-variable low-
   pass filter, one state per voice/channel. Preserve bounded output and no
   allocations in the audio callback.
4. Add factory sound variants to the browser only when the browser's preset
   model can represent them cleanly. Patch state remains ordinary device state
   and continues to save in `.rhinoedit` files.
5. Extend offline rendering tests to exercise the new modulation and verify
   finite, bounded audio.

## Milestone B — Rhino Forge (independent VST3)

1. Create `instruments/rhino-forge/` as an independently configurable CMake
   project. Its DSP is a JUCE-only static library; it must not include
   Tracktion or Rhino application headers.
2. Build a VST3 and standalone target from the same JUCE `AudioProcessor`.
   Parameter IDs, versioned state, and host automation are defined at this
   boundary.
3. Deliver a usable v0.1 voice engine: two wavetable oscillators, sub/noise,
   ADSR, unison, resonant filter, and output limiter. The initial editor is a
   compact, resizable signal-flow view, not a copied Serum interface.
4. Next increments: modulation matrix (envelopes/LFOs/macros), wavetable
   import/editor, effects rack, MPE, preset browser, then sample/granular and
   spectral sources only when the core is mature.

> Item 4 is superseded by [instruments/rhino-forge/PLAN.md](instruments/rhino-forge/PLAN.md),
> which breaks the synth build-out into milestones and records the decisions
> already settled. Read that file before changing Forge.

## Compatibility and quality gates

- No allocations, locks, filesystem work, or UI work in an audio callback.
- Preserve old Rhino Wave projects; new Wave controls use new state keys with
  musically safe defaults.
- Each milestone builds in Release and passes the relevant CTest coverage.
- A VST3 is considered shipped only after it has been loaded and automated in
  at least one external host. A successful compile alone is not host proof.
