---
title: Tracktion reads a delayed plugin's automation early
type: gotcha
summary: PluginNode reads a plugin's automation one latency early (automationAdjustmentTime = -latency), as if a parameter acted on the plugin's output rather than its input, so a Pitch envelope on a 0.32 s shifter changed the sound 320 ms late; Rhino writes Clip Pitch's curve 0.32 s early, which is why that latency is a constant.
tags: [rhino, tracktion, latency, automation, audio-clips]
sources: []
updated: 2026-10-26
---

# Tracktion reads a delayed plugin's automation early

Measured on 2026-10-26 against the pinned engine (`4536d8a`), while building [clip envelopes](clip-envelopes.md) (commit `a1c9607`).

## Symptom

A step in a clip's Pitch envelope changed the sound 320 ms after the point where it was drawn. 320 ms is the latency of Clip Pitch.

## Cause

`PluginNode` reads a plugin's automation curves at `automationAdjustmentTime`, which is minus the plugin's latency. It reads the value for the moment when the output will be heard, so the plugin applies that value to input that is one latency older. That is the right timing for a parameter that acts on what comes out. For a parameter that shapes what goes in, such as a pitch shift, the change reaches the audio one latency late.

## Workaround

`mirrorClipEnvelopes` (`SessionClipEnvelopes.cpp`) writes the Pitch curve `ClipPitchDevice::latencySeconds` early. A point that would fall before 0 is clamped there, with the line's value at 0 (`clipEnvelopeValueBefore`). The mirror writes the curve before the device is prepared, so it needs the latency in advance. That is why Clip Pitch pads its delay to a fixed 0.32 s at every rate and does not report whatever the stretcher happens to need ([Tracktion's pitch shifter under-reports its latency](tracktion-pitch-shifter-under-reports-latency.md)). Clip Envelope has no latency and needs no shift.

## Elsewhere

The rule applies to every plugin that reports latency, not only to clip devices. A track lane on a latent device whose parameter acts on its input would be read early in the same way. Rhino Tune reports real latency ([Rhino Tune](rhino-tune.md)). This has not been measured for track lanes, and nothing corrects for it there.

## Related

- [Clip envelopes](clip-envelopes.md)
- [Track automation](automation.md)
- [Tracktion fades a clip after its plugins, ignoring their delay](tracktion-clip-fades-ignore-clip-plugin-delay.md)
