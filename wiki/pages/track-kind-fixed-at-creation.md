---
title: A track's kind is fixed when it is made
type: decision
summary: A track declares audio or MIDI at creation, nothing dropped on it changes that, and every mismatched drop is refused in the model.
tags: [rhino, tracks, model, decision]
sources: []
updated: 2026-10-05
---

# A track's kind is fixed when it is made

## Context

The rule arrived in three steps.

1. **Inferred from the chain** (until 2026-09-20). A track was MIDI if it ran an instrument, so a new track held no clip until an instrument landed on it.
2. **Declared but overridable** (2026-09-20, commit `ccfbea6`). `addTrack` wrote `rhinoTrackType`, so a MIDI track could hold clips before it had an instrument. But `trackType` still answered `midi` for any track carrying an instrument, and `switchTrackInstrument` wrote the property. Dropping a synth on an audio lane turned it into a MIDI lane on contact: the lane someone made to hold a file quietly became one that could not, and no gesture put it back. Audio onto a MIDI lane was refused with a message; the other direction converted silently.
3. **Fixed** (2026-10-01, commit `001a690`): this decision.

## Decision

A track says what it is for when it is created, and the answer is final. `Session::trackType` reads `rhinoTrackType` and nothing else. Every mismatched drop is refused with a message **in the model**, so every UI path inherits the refusal:

- instruments, MIDI effects, drum kits, patterns and new clips refuse an audio track (`addInstrumentDevice`, `addMidiEffectDevice`, `addDrumKit`, `preparePresetTrack`, `createClip`);
- audio refuses a MIDI track (`importAudioAt`);
- a moved or pasted clip refuses a lane of the other kind (`editClip`, `pasteClipRegion`), and a paste is checked before its destination is cleared, so it fails whole rather than half done.

Audio effects go on either kind, and so does a channel-strip facility (an `infrastructure` catalog entry such as Utility), because it joins the chain rather than becoming what the track plays.

## Alternatives rejected

- **Infer the kind from the chain** (step 1): "what is this track" became a question only the chain could answer, and every lane a guess about what it would accept next.
- **A declaration an instrument can confirm** (step 2): it kept a silent, one-way conversion.

## Consequences

- The UI has to ask: the `+` button offers MIDI or audio, and Ctrl+T repeats the last choice.
- A MIDI track holds clips before it runs anything, and taking its instrument off leaves a MIDI track.
- Leftover: `createClip`'s refusal on an audio track still suggests "drop an instrument here", which is now refused as well (checked 2026-10-05). The paused session view's slot paths ignored the kind until commit `23de20c`; they now refuse a mismatch through `clipLaneRefusal` ([Session view (paused)](session-view.md)).

## Related

- [Track kinds: audio and MIDI](track-kinds.md)
- [One instrument per track](one-instrument-per-track.md)
- [Dependency direction](dependency-direction.md)
- [Session, the model](session-model.md)
