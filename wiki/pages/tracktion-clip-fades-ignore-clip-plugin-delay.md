---
title: Tracktion fades a clip after its plugins, ignoring their delay
type: gotcha
summary: A clip's FadeInOutNode runs after the clip's own plugins and allows for none of their latency, so with Clip Pitch on a clip the fade-out fell 320 ms early and silenced the clip's last 320 ms; while a clip has a Pitch envelope, Rhino holds its fades as rhinoFadeIn/rhinoFadeOut and Clip Envelope plays them ahead of the shifter, so every fade reader goes through clipFadeInSeconds/clipFadeOutSeconds.
tags: [rhino, tracktion, latency, audio-clips, measurement]
sources: []
updated: 2026-10-29
---

# Tracktion fades a clip after its plugins, ignoring their delay

Measured on 2026-10-26 against the pinned engine (`4536d8a`), while building [clip envelopes](clip-envelopes.md) (commit `a1c9607`).

## Symptom

On a clip with Clip Pitch and a fade-out, the fade fell 320 ms early, and the clip's last 320 ms were silenced. In the render, the hit before the fade played at 0.59 of its level and the hit inside the fade at 0.000.

## Cause

`createNodeForAudioClip` (`EditNodeBuilder`) chains the wave node, then the clip's plugins, then a `FadeInOutNode`, which it creates only when a fade is longer than 0. The fade node works on the clip's edit times and makes no allowance for the delay its plugins add, so on a latent clip plugin's output it lands early by that plugin's latency.

## Workaround

All of this is in `SessionClipEnvelopes.cpp`, declared in `SessionInternal.h`.

- **Rhino holds the fades.** While a clip has a Pitch envelope, its fades are stored on the clip as `rhinoFadeIn` and `rhinoFadeOut`, in seconds. The engine's own fades are set to zero. Clip Envelope, which runs ahead of Clip Pitch, plays the fades on its `fade` parameter, as straight lines like the engine's default fade shape. So a Pitch envelope always brings Clip Envelope onto the clip, even with no Gain or Pan envelope.
- **The handover.** `syncClipEnvelopeDevices` moves the fades to Rhino and back to the engine (`placeFades`), inside the same transaction.
- **Every reader and writer of a clip's fades uses `clipFadeInSeconds`, `clipFadeOutSeconds` and `setClipFades`.** This covers `audioClipMix`, `setAudioClipFadeIn` and `setAudioClipFadeOut`, and the region copy through `audioClipMix`. Calling `getFadeIn()` directly returns 0 on such a clip.
- **Fitting.** Held fades are fitted as `AudioClipBase::getFadeIn` fits the engine's own: neither may be longer than the clip, and if they overlap both are shortened in proportion.
- **Cuts.** Splitting a clip clears the held fade on each side of the cut (`clearHeldFadesAtCut`, in `splitClipAt`, `splitClips` and the bounce's region cuts).
- **Merge.** Ctrl+J renders with plugins off, so `lendHeldFadesToEngine` puts the held fades on the engine for that render, outside the undo history, and takes them back afterwards.

Only Clip Pitch is handled. A latent effect in a clip's own plugin list, with a fade, has not been measured, but would presumably lose its end in the same way. Since commit `dd27e92` (2026-10-29) a browser drop on a clip puts the effect on the track instead, so such an effect now comes only from a document saved before then ([Device chain order](device-chain-order.md#clip-effects)).

## Related

- [Clip envelopes](clip-envelopes.md)
- [Audio clip editor](audio-clip-editor.md)
- [Tracktion reads a delayed plugin's automation early](tracktion-reads-delayed-plugin-automation-early.md)
- [Tracktion's pitch shifter under-reports its latency](tracktion-pitch-shifter-under-reports-latency.md)
