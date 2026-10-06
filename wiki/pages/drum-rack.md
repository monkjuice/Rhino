---
title: Drum Rack
type: component
summary: Sixteen pads on C2-D#3, each a sample or one of eight synthesised drums with six controls of its own, blank until a kit (.rdk), a drum preset (.rdp) or a dropped sample fills it.
tags: [rhino, devices, drums, dsp, library]
sources: []
updated: 2026-10-06
---

# Drum Rack

Rhino's drum instrument since 2026-10-06 (branch `feat/drum-rack`). It replaced Rhino Drums (`DrumDevice`, five hard-coded kits). The catalog id is still `Drums`, so `Session::Instrument::Drums`, the drum patterns and `isPatternDrums` name it unchanged. The type is new, `rhino.drumrack.v1`, and nothing reads `rhino.drums.v1`, so a project saved with Rhino Drums opens without its drums ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

| Layer | Files |
| --- | --- |
| DSP and files (`RhinoCore`) | `core/DrumRackEngine.*` (pads, 32 voices), `core/DrumSynth.*` (the synth models), `core/DrumKitFile.*` (`DrumSound`, `DrumKit`, both file formats) |
| Device | `devices/instruments/DrumRackDevice.*`, on the device SDK |
| Model | `SessionDrums.cpp`; `findDrumRack`, `loadDefaultDrumKit`, `drumKitOf` and `fillBlankDrumRack` in `SessionInternal.h` |
| UI | the face in `DeviceEditorPanelDrums.cpp`, pad drops in `DeviceRack.cpp`, the Drums section in `BrowserPanel.cpp`, lane drops in `ArrangementDrops.cpp` |

## A pad

- **16 pads on notes 48-63**: C2 to D#3, with middle C as C3. The note editor's 16 drum rows are the 16 pads ([Note editor (StepGrid)](note-editor.md)). A strike takes a free voice, else the oldest.
- **A sample or a synth.** The synths (`DrumModel`) are small 808-style models: Kick, Snare, Tom, ClosedHat, OpenHat, Clap, Rim and Cowbell. `DrumModelInfo` gives each a file `id` (never renamed), a name, the browser drum kind it files under, and its starting Decay and Tone.
- **Six controls.**
  - Tune: ±24 semitones.
  - Decay: the seconds to fall 60 dB. At its top (10 s) it reads "Full" and a sample plays to its end.
  - Tone: a low-pass from 120 Hz to 20 kHz on a sample, switched off at the top; brightness on a synth.
  - Velocity: how much quieter a soft note is.
  - Level: dB, with the bottom (-48) as silence.
  - Pan: a balance, so the centre is unity on both sides.
- **Choke groups 1-4, mute and solo.** A strike fades out, over 4 ms, the voices of every other pad in its group. While any pad is soloed, only soloed pads sound. A pad ignores note-offs and plays out, as a drum does; all-notes-off and panic fade it.
- Sample voices use 4-point Hermite interpolation, which is bit-exact for an untuned sample at its file's own rate. A pad still refuses a second strike within a millisecond ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)).
- `DrumRackEngine::renderStrike` plays one strike through a voice of its own and touches nothing the audio thread uses. The face's picture and the browser's drum-preset audition both use it, and a test holds it sample-identical to live playback.

How a replaced sample is freed without a lock, and the denormal flushes, are in [Real-time audio rules](real-time-audio-rules.md).

## The device

`DrumRackDevice` is a `NativeInstrument` ([The native device standard](native-device-standard.md)). It declares 96 controls pad by pad, `p1Tune` to `p16Pan`, so a parameter's index is `pad * 6 + control` (`parameterIndex`). Automation lanes store that index ([Track automation](automation.md)), so despite the `Control` enum's "append, never reorder" comment, a seventh control per pad cannot simply be appended: `controlCount` would grow, every pad after the first would move, and saved lanes would point at other controls. Declare new per-pad controls after `p16Pan` and index them apart, or take the break knowingly while no device compatibility is owed ([Stored indices are append-only](append-only-stored-indices.md)).

What a pad holds (`sample` or `synth`, `name`, `choke`, `mute`, `solo`) is content no control can carry. It lives in a `PADS` child of the plugin state, with 16 `PAD` children, written through the edit's undo manager. ValueTree listeners hand every change, an undo included, to `syncPads()`. That reads into the engine only the pads whose content changed, and a `writing` flag holds it back until a multi-property write is done.

- **Samples are read on the message thread**, whenever a pad changes: a drop, a kit, an undo or a document opening. `readSample` reads the whole file up to 10 s, ending in a 5 ms fade. A missing file logs `drum sample missing`. A file that will not decode, such as an unpulled Git LFS pointer, logs `drum sample unreadable`. Either way the pad falls silent and the face says "Sample missing".
- **A pad played from the face** (its play button, or Play in its menu) calls `previewPad`, which sets a bit in `previews` that the audio thread takes at its next render. `DrumRackEngine::prepare` clears the bits, so a pad played while nothing was rendering does not sound late whenever rendering starts.
- **What a pad keeps.** A sample dropped on a pad that held a sample keeps all six controls, so auditioning one snare after another keeps the pad's tuning. A sample on a synth pad or an empty pad starts at the defaults but keeps level, pan, velocity and choke. A switch to a synth keeps the same four and takes the model's Decay and Tone. A kit replaces every pad and clears mute and solo.
- The selected pad (`selPad`) is view state on the device, written without undo.
- `padPicture` renders a strike at 16 kHz, at most 3 s, and caches it by sound, settings and width ([What a change costs the interface](ui-cost-of-a-change.md)).
- `hasNameForMidiNoteNumber` names each filled pad. `Session::patternNoteName` passes that name to the note editor's rows.

## Kits and drum presets

`core/DrumKitFile.*` defines two XML files. A **kit** (`.rdk`, `RHINO_DRUM_KIT`) has up to 16 `PAD index="n"` elements; an index left out is an empty pad. A **drum preset** (`.rdp`, `RHINO_DRUM_SOUND`) is one pad's sound.

- Only format 1 is read. Numbers are written through `core/ExactFloatText.h`, shared with `.rnd`, so they read back exactly.
- A sample is named, never embedded. Library content is `library:<path under the library>`; anything else is a full path (`ContentLibrary::storedPath`, `resolveStoredPath`).
- A setting left out takes its default. Mute and solo are not saved.

Where the factory and user files live: [Content library](content-library.md).

## Through the session

`SessionDrums.cpp` makes each edit one undo step:
- `addDrumKit(file, track)` makes a MIDI track's instrument a rack playing the kit and names the track after the kit. A rack already on the track takes the kit.
- `loadDrumKit`, `saveDrumKit`, `loadDrumPadSample`, `loadDrumPadPreset` and `saveDrumPadPreset` address a rack by track and slot.
- `addDrumSound(file, track)` fills the first empty pad. A drum preset brings a blank rack to a MIDI track; a bare sample is refused unless the track already runs one.
- `previewDrumSound` renders one strike into a `MemoryAudioSource` for the browser's preview ([Browser and library preview](browser.md)).
- A drum pattern on a blank rack loads `Drums/Kits/808 Kit.rdk` (`loadDefaultDrumKit`), the kit the patterns were written for ([Pattern presets](pattern-presets.md)).
- A MIDI clip moved or pasted onto a track whose rack arrives blank brings its source rack's kit ([One instrument per track](one-instrument-per-track.md)).

Drops:
- **On a lane:** a kit goes to `addDrumKit` and a drum preset to `addDrumSound`. A sample from the browser or the desktop, dropped on a MIDI track that runs a rack, goes to the next empty pad.
- **On the rack's face:** a sample or drum preset lands on the pad under the pointer, or on the selected pad when dropped between pads, and that pad lights while the drag is over it. A kit loads into the rack. Several desktop files fill pads one after another ([Device rack and device editors](device-rack.md)).
- Each file dropped is an undo step of its own, unlike `importAudioFilesAt`.

## Decisions

- **Blank by default.** Kits, drum presets and dropped samples fill a rack. Only a drum pattern brings a kit, because it needs one to make a sound.
- **Synth pads, not hosted instruments.** Pads that play without a sample (asked for as "MIDI-based") are Rhino's own synthesised drums. A pad cannot host an instrument or a VST3; that is a possible later step.
- **16 pads**, matching the note editor's 16 rows from C2, so every row is a pad.
- **Kits are their own format, not `.rnd`.** A `.rnd` holds parameter values only ([Device presets (.rnd)](device-presets.md)), and a kit's samples, synth models, names and choke groups are not parameters. So the rack's name-bar menu lists kits instead of `.rnd` presets.

## Tests

- `tests/DrumRackTest.cpp` (`checkDrumRack`, run by `--self-test`):
  - measures what the rack plays, by routes that cannot agree with it by construction ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)): pitch by zero crossings, decay, tone, velocity, level, pan, choke, mute and solo, all eight models finite and dying away, the kick at 52 Hz;
  - covers the sample hand-off, pictures against live playback, file round trips and refusals, the factory content and `drumTypeOf`;
  - drives the session paths: undo, patterns, note names, kit carry and reopening;
  - logs each model's strike cost.
- `--self-test` writes to `rhino.log` how long the 808 Kit's eight samples take to read onto their pads (`setKit`): 2.8-4.0 ms warm over six runs (2026-10-06). `DrumRackTest.cpp` saves a kit and a drum preset under a temporary Drums root, not `userDrums()`, and finds them with `ContentLibrary::drumKitsIn` and `drumPresetsIn` (`user = true`), the preset under the kind of drum its folder names.
- `--device-test` primes a blank rack with three synth pads and an 808 sample (`prime` in `DeviceConformance.cpp`). A blank rack is silent and would fail every check that listens for a note.
- `runDrumRackFaceTest` (`tests/Pattern/DeviceRackTest.cpp`, in the Pattern runner) covers the face:
  - it finds the pads by asking `drumPadAt`;
  - the knobs follow selection, and M mutes with an undo;
  - samples and presets drop onto pads, and a kit drop loads the rack;
  - it writes `RHINO_DRUMS_SNAPSHOT` and logs the face's cost.

## Related

- [Built-in devices](built-in-devices.md)
- [Device catalog](device-catalog.md)
- [The native device standard](native-device-standard.md)
- [Device rack and device editors](device-rack.md)
- [Browser and library preview](browser.md)
- [Content library](content-library.md)
- [Device presets (.rnd)](device-presets.md)
- [Pattern presets](pattern-presets.md)
- [Note editor (StepGrid)](note-editor.md)
- [Real-time audio rules](real-time-audio-rules.md)
- [What a change costs the interface](ui-cost-of-a-change.md)
- [Track automation](automation.md)
