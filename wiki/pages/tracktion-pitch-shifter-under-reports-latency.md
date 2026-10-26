---
title: Tracktion's pitch shifter under-reports its latency
type: gotcha
summary: te::PitchShiftPlugin reports only the 150 ms its output queue is primed with and not its stretcher's own 150 ms, and delay compensation corrects only what is reported, so every hit through it came exactly 150 ms late, on a clip or a track; Rhino's Clip Pitch reports both, padded to a fixed 0.32 s.
tags: [rhino, tracktion, latency, audio-clips, measurement]
sources: []
updated: 2026-10-26
---

# Tracktion's pitch shifter under-reports its latency

Measured on 2026-10-26 by rendering against the pinned engine (`4536d8a`), while building [clip envelopes](clip-envelopes.md) (commit `a1c9607`).

## Symptom

Every hit through `te::PitchShiftPlugin` came exactly 150 ms late at 48 kHz. This happened on a clip's plugin list and on a track alike.

## Cause

The plugin reports as its latency only the frames its output FIFO is primed with (`TimeStretcher::getMaxFramesNeeded`, 7200 samples, 150 ms at 48 kHz). It leaves out the stretcher's own delay, `TimeStretcher::getLatencySamples`, which is another 7200 samples for `signalsmithDefault`. Delay compensation corrects only what the graph reports, and so does the offline render, which drops that many samples (`NodeRenderContext`'s `numLatencySamplesToDrop`). The unreported half stays in the audio.

## Workaround

Rhino offers no device built on the engine's shifter. `ClipPitchDevice` (`native/src/devices/infra/ClipPitchDevice.*`) runs the same stretcher, primes its own output queue with silence, and reports the queue plus the stretcher, padded to exactly `ClipPitchDevice::latencySeconds` (0.32 s) at any sample rate. Its hits land within 3 ms (`ClipEnvelopes.inc`). The padding to a fixed figure is there for [the automation trap](tracktion-reads-delayed-plugin-automation-early.md).

**A device that wraps `te::TimeStretcher` must report `getLatencySamples()` on top of whatever it buffers.**

## Related

- [Clip envelopes](clip-envelopes.md)
- [Tracktion reads a delayed plugin's automation early](tracktion-reads-delayed-plugin-automation-early.md)
- [Tracktion's time-stretched proxy plays late and cuts its end](tracktion-stretched-proxy-plays-late.md)
- [Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)
