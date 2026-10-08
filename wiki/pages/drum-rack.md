---
title: Drum Rack
type: component
summary: A pad on every MIDI note, addressed by note, each a sample or one of eight synthesised drums with six automatable controls; the face shows a bank of sixteen beside a map of all 128 and opens maximised in a window of its own, and kits (.rdk), drum presets (.rdp) or dropped samples fill it.
tags: [rhino, devices, drums, dsp, library]
sources: []
updated: 2026-10-07
---

# Drum Rack

Rhino's drum instrument since 2026-10-06 (branch `feat/drum-rack`). It replaced Rhino Drums (`DrumDevice`, five hard-coded kits). The catalog id is still `Drums`, so `Session::Instrument::Drums`, the drum patterns and `isPatternDrums` name it unchanged. The type is new, `rhino.drumrack.v1`, and nothing reads `rhino.drums.v1`, so a project saved with Rhino Drums opens without its drums ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)). On 2026-10-07 (commit `755b4cc`) it grew from sixteen pads to one per note, with a map of the notes and a sample editor ([Drum Rack sample editor](drum-rack-sample-editor.md)).

| Layer | Files |
| --- | --- |
| DSP and files (`RhinoCore`) | `core/DrumRackEngine.*` (pads, 32 voices, playback), `core/DrumSynth.*` (the synth models), `core/DrumSlicer.*` (where Slice cuts), `core/DrumKitFile.*` (`DrumSound`, `DrumKit`, both file formats, `padNoteName`) |
| Device | `devices/instruments/DrumRackDevice.cpp` (pads, their state, playing them) and `DrumRackDeviceEditing.cpp` (what the face asks for: view state, previews, counts, pictures, slices), sharing `DrumRackDeviceInternal.h` |
| Model | `SessionDrums.cpp`; `findDrumRack`, `loadDefaultDrumKit`, `drumKitOf` and `fillBlankDrumRack` in `SessionInternal.h`; `patternDrumLowestNote` in `SessionPresets.cpp` |
| UI | the face, six `DeviceEditorPanelDrum*.cpp` units ([Device rack and device editors](device-rack.md)); the face in a window of its own, `DrumRackWindow.*` (below); pad drops in `DeviceRack.cpp` and `DeviceEditorPanel::dropDrumSounds`, the Drums section in `BrowserPanel.cpp`, lane drops in `ArrangementDrops.cpp` |

## A pad on every note

- **128 pads** (`DrumRackEngine::padCount`), one per MIDI note, as in Live. A pad is addressed by its note everywhere: engine, device, kit files and every `Session` call that takes a pad. Messages name it with `padNoteName` (`core/DrumKitFile.h`), where middle C is C3, so 48 is C2.
- **A bank of sixteen on the face**: four rows of four, lowest note bottom left, from `firstShownNote()`, a multiple of 4, default 48. It and the selected pad are view state on the device (`firstNote`, `selPad`, both default 48), written without undo. The note editor's sixteen drum rows follow the bank ([Note editor (StepGrid)](note-editor.md)).
- **Kits still sit on C2 up**, where the drum patterns are written, not at Live's C1. A controller sending notes 36-51 therefore plays below the default bank, and the map shows the keys arriving there.
- **A sample or a synth.** The synths (`DrumModel`) are small 808-style models: Kick, Snare, Tom, ClosedHat, OpenHat, Clap, Rim and Cowbell. `DrumModelInfo` gives each a file `id` (never renamed), a name, the browser drum kind it files under, and its starting Decay and Tone.
- **Six controls.** Tune (±24 semitones); Decay (seconds to fall 60 dB; at its 10 s top it reads "Full" and a sample plays to its end); Tone (a low-pass from 120 Hz to 20 kHz on a sample, off at the top; brightness on a synth); Velocity (how much quieter a soft note is); Level (dB, the -48 bottom is silence); Pan (a balance, unity on both sides at centre).
- **Choke groups 1-4, mute and solo.** A strike fades out, over 4 ms, the voices of every other pad in its group. While any pad is soloed, only soloed pads sound; a solo count (an atomic) spares each voice a scan of 128 pads.
- **Note-offs.** A synth pad and a one-shot sample play out, as a drum does. A classic sample pad follows the key: since `755b4cc`, `DrumRackDevice::noteOff` reaches `engine.noteOff`. All-notes-off and panic fade everything.
- Sample voices use 4-point Hermite interpolation, bit-exact for an untuned sample at its own rate. A pad refuses a second strike within a millisecond ([Reproducing a live timing bug offline](reproducing-live-timing-offline.md)). A strike takes a free voice, else the oldest.
- `DrumRackEngine::renderStrike` plays one strike through a voice of its own, touching nothing the audio thread uses, with the pad's playback and a hold length. The face's synth picture and the browser's drum-preset audition use it; a test holds it sample-identical to live playback.

How a replaced sample is freed without a lock, and the denormal flushes, are in [Real-time audio rules](real-time-audio-rules.md).

## The device

`DrumRackDevice` is a `NativeInstrument` ([The native device standard](native-device-standard.md)). It declares six controls per note, 768 in all, pad by pad: ids `n<note><Control>` (`n48Tune`), names `<note name> <Control>` (`C2 Tune`), because an automation lane is read away from the face. A parameter's index is `note * 6 + control` (`parameterIndex`), and lanes store that index ([Track automation](automation.md)). So a seventh per-pad control still cannot simply be appended: every pad after the first would move and saved lanes would point at other controls. Declare new ones after `n127Pan` and index them apart, or take the break knowingly while no device compatibility is owed ([Stored indices are append-only](append-only-stored-indices.md)).

**768 controls cost.** `Session::deviceParameters(track, slot)` reads and formats every control, 1.75 ms median for a rack, and the face and automation lanes did that on every frame of a knob drag. Hence the range reads in `SessionDevices.cpp` ([What a change costs the interface](ui-cost-of-a-change.md)). The face reads only the selected pad's six (16.7 µs) into a `parameters` vector kept 768 long, so each entry sits at its own index. On the audio thread `readSettings` reads controls only for pads that hold something. Making a rack takes 2.4-3.0 ms. Reading the 808 Kit onto its pads went from 2.8-4.0 ms warm to 5.3-6.3 ms (51.6 ms cold). That was measured, its cause was not; the likely one is listener fan-out, since every property change on the device state notifies all 768 `CachedValue`s and `NativeDevice::valueTreePropertyChanged` loops over every slot.

**The fallback editor reads six too.** Edit opens `FloatingDeviceWindow`'s fallback editor (`DeviceRack.cpp`) for a device with no editor of its own. It used to make a label, value, knob and automation button per control, 3,072 components for a rack, to show the first six (C-2's), and to re-read all 768 on its 30 Hz timer. Since 2026-10-07 it reads only the six it shows, and on a Drum Rack those are the selected pad's, following the face (`firstShownParameter`). Since commit `94d4d9f` Edit opens a Drum Rack in its own window instead (below), so that branch is no longer reached from Edit.

What a pad holds (`sample` or `synth`, `name`, `choke`, `mute`, `solo`, and a sample's playback) is content no control can carry. It lives in a `PADS` child of the plugin state, written through the edit's undo manager, with a `PAD` child only for a note that holds a sound, keyed by its `note` property. Clearing a pad removes its child, mute and solo with it. ValueTree listeners hand every change, an undo included, to `syncPads()`. That indexes the children by note once per sync and reads into the engine only the pads whose content changed; a `writing` flag holds it back until a multi-property write is done.

- **Samples are read on the message thread**, whenever a pad changes: a drop, a kit, an undo or a document opening. `readSample` reads the whole file up to 10 s, ending in a 5 ms fade. A missing file logs `drum sample missing`; one that will not decode, such as an unpulled Git LFS pointer, logs `drum sample unreadable`. Either way the pad falls silent and the face says "Sample missing".
- **For the face**, the engine counts every note-on per note, even for an empty pad (`notesReceived`, how the map flashes a key), reports the newest voice's place in its file (`playhead`, -1 when silent), and takes pad strikes from the face as a 128-bit mask (`previews`) at its next render. `prepare` clears the mask, so a pad played while nothing rendered does not sound late.
- **What a pad keeps.** A sample dropped on a pad that held a sample keeps all six controls and its playback, so auditioning one snare after another keeps the pad's tuning. A sample on a synth or empty pad starts at the defaults but keeps level, pan, velocity and choke. A switch to a synth keeps the same four and takes the model's Decay and Tone. A kit replaces every pad and clears mute and solo.
- `padPicture` and `samplePicture` keep the selected pad's pictures by sound, settings and width, two widths of each, the older redrawn first (`keptPicture`): the rack's face and the Drum Rack window draw one pad at two widths, and with one kept they took turns redrawing it every frame a voice played. A reference returned lasts until a third width is asked for. A synth's strike renders at 16 kHz.
- `hasNameForMidiNoteNumber` names each filled pad; `Session::patternNoteName` passes the name to the note editor's rows.

## Kits and drum presets

`core/DrumKitFile.*` defines two XML files, numbers written through `core/ExactFloatText.h` (shared with `.rnd`) so they read back exactly.

- A **kit** (`.rdk`, `RHINO_DRUM_KIT`) is format 2 (`DrumFiles::kitFormat`): a `PAD note="n"` (0-127) per pad that holds a sound. Format 1, sixteen pads indexed from C2, is refused; the eight factory kits were converted with note = 48 + old index. A pad on no valid note, or a note filled twice, is refused.
- A **drum preset** (`.rdp`, `RHINO_DRUM_SOUND`) is one pad's sound, still format 1 (`soundFormat`), now with optional playback attributes ([Drum Rack sample editor](drum-rack-sample-editor.md)). Kit pads carry the same attributes.
- A sample is named, never embedded: `library:<path under the library>` for library content, else a full path (`ContentLibrary::storedPath`, `resolveStoredPath`). A setting left out takes its default. Mute and solo are not saved.

Where the factory and user files live: [Content library](content-library.md).

## Through the session

`SessionDrums.cpp` makes each edit one undo step:
- `addDrumKit(file, track)` makes a MIDI track's instrument a rack playing the kit and names the track after it. A rack already on the track takes the kit.
- `loadDrumKit`, `saveDrumKit`, `loadDrumPadSample`, `loadDrumPadPreset` and `saveDrumPadPreset` address a rack by track and slot, and a pad by note.
- `addDrumSound(file, track)` fills the first empty pad from the bank shown, coming round from the bottom (`firstEmptyPad(from)`), so a sound lands where it can be seen. A drum preset brings a blank rack to a MIDI track; a bare sample is refused unless the track already runs one.
- `showDrumBank(track, slot, firstNote)` is view state, never an undo step, but announced, so the note editor follows. `patternDrumLowestNote()` is the open clip's rack's first shown note.
- `spreadDrumSlices` puts a sample's slices on pads of their own ([Drum Rack sample editor](drum-rack-sample-editor.md)).
- `moveDrumPad(track, slot, from, to)` moves a pad's sound to another note (below).
- `previewDrumSound` renders one strike into a `MemoryAudioSource` for the browser ([Browser and library preview](browser.md)).
- A drum pattern on a blank rack loads `Drums/Kits/808 Kit.rdk` (`loadDefaultDrumKit`), the kit the patterns were written for ([Pattern presets](pattern-presets.md)). A MIDI clip moved or pasted onto a track whose rack arrives blank brings its source rack's kit ([One instrument per track](one-instrument-per-track.md)).

Drops: on a lane, a kit goes to `addDrumKit` and a drum preset to `addDrumSound`; a sample dropped on a MIDI track that runs a rack goes to the next empty pad. On the face, a sample or drum preset lands on the pad under the pointer, or on the selected pad between pads; a kit loads the rack; several desktop files fill pads upward from the one dropped on, stopping at note 127. Each file is an undo step of its own, unlike `importAudioFilesAt`.

## Moving a pad

Since commit `6af25c2` (2026-10-07), a filled pad dragged onto an empty note moves there and leaves its own pad empty; dragged onto a filled pad, the two trade places. The target may be a pad in the bank or any note on the map. Everything the pad holds goes with it: sample or synth, name, choke group, mute, solo, playback and the six controls as set, written to the target note's parameters. A vacated pad's controls return to their defaults.

- `DrumRackDevice::movePad(from, to)` writes both pads in one `writing` batch, then calls `syncPads`. Both pads' samples are read from disk again, because `syncPads` caches by note, not by sound.
- `Session::moveDrumPad` is one undo step, named "Move Kick to D#2" or "Swap Kick and Snare", and selects the target pad. It refuses an empty source and a note outside 0-127. A pad dropped on itself succeeds and changes nothing.
- The gesture on the face is described in [Device rack and device editors](device-rack.md).

## Auto Select

Since commit `60442e5` (2026-10-07), a pad played becomes the selected pad, so the face's knobs and sample side follow what is playing. The user asked for it; it is Live's Auto Select (Live 12 manual §24.4.1, p. 476, and the chain list, p. 478; where the manual is: [Development environment and reference material](development-environment.md)).

- **The setting** is view state on the device, like `selPad` and `firstNote`: `DrumRackDevice::autoSelect()` and `setAutoSelect(bool)`, property `autoSelect`, default on, written without undo (`DrumRackDeviceEditing.cpp`). The face's AUTO switch, under the map, toggles it ([Device rack and device editors](device-rack.md)).
- **The face selects, not the device or the session.** In `tickDrums` (24 Hz) the lowest note whose `notesReceived` count moved since the last frame, on a pad that holds a sound (`filledPads`), becomes the selected pad, then `readDrumParameters`, `styleDrumControls` and partial repaints. An empty note still flashes on the map and selects nothing. The timer idles while the rack is hidden, and so does Auto Select.
- **Never under the hand.** Nothing is selected while a mouse button is down on the panel or any of its knobs (`isMouseButtonDown(true)`), so a knob being turned never changes pads beneath it.

## Its own window

Since commit `94d4d9f` (2026-10-07) a rack opens maximised in a window of its own, the pads above and the sample editor across the whole width below, as the user asked. The button at the right end of a Drum Rack's name bar opens it, and so does Edit; `KEY <note>` stands left of the button.

- **The same face, stacked.** `DrumRackWindow.cpp` hosts a second `DeviceEditorPanel` on the rack. `layoutFor` stacks a face at least `DeviceEditorPanel::drumStackedHeight` (400 px) tall, which only the window's is: map and pads across the top, 45% of the height up to 400 px, centred, pads at most twice as wide as tall; then the selected pad's side, its picture as wide as the window, cells 74 px tall and up to 84 px wide with 48 px knobs. Map cells follow the map's size (`mapCell`, `mapRowAt`), so every gesture works at either size. The rack's own face stayed pixel-identical below its name bar.
- **It follows its rack, not a slot.** `DrumRackView` keeps the rack's `EditItemID` ([Hold ids, not pointers](ids-not-pointers.md)), and the rack's change listener calls `follow()` on every change, hidden rack or not, which finds the rack again by id. A device added in front of it, a track moved or an undo leaves it on the same rack. When the rack leaves the edit the window closes, and `editWillChange` closes it with the document.
- **One at a time.** `DeviceRack::openDrumWindow` brings the rack's open window to the front, or replaces it with another rack's.
- **It works like the rack.** It takes browser samples, drum presets and kits, and sound files from the desktop, on its pads (`dropDrumSounds`, shared with the rack). It reads automated knobs back at 30 Hz while the transport rolls. It hands keys to the shell's shortcuts (`DeviceRack::shortcut`) and the typing keyboard (`listenForKeys`), so Space, undo and the computer MIDI keyboard work there. Every key goes to the main window's `keyPressed`, so F12 in the drum window toggles the main window's full screen, not its own (as of `94d4d9f`).
- **Two faces on one rack.** Each face's tick catches up when the other has picked another pad (`drumParametersFrom`), so a knob never turns a pad other than the one it shows. The picture cache keeps two widths (above).
- **Unverified:** a browser drag into the window relies on JUCE's `TreeView` starting drags that may leave their window. No test drives a real cross-window drag (as of 2026-10-07).

## Decisions

- **Blank by default.** Kits, drum presets and dropped samples fill a rack. Only a drum pattern brings a kit, because it needs one to make a sound.
- **Synth pads, not hosted instruments.** Pads that play without a sample (asked for as "MIDI-based") are Rhino's own synthesised drums. A pad cannot host an instrument or a VST3; that is a possible later step.
- **A pad on every note** (2026-10-07). The user chose 128 pads, as in Live, over sixteen pads that could be moved to other notes. A pad is now its note in every file and call, and the face pages through banks. The price is 768 controls. Until then the rack had 16 pads, matching the note editor's 16 rows from C2.
- **Kits are their own format, not `.rnd`.** A `.rnd` holds parameter values only ([Device presets (.rnd)](device-presets.md)), and a kit's samples, synth models, names, choke groups and playback are not parameters. So the rack's name-bar menu lists kits instead of `.rnd` presets.
- **A sample's playback is content, not controls**, so it is not automatable ([Drum Rack sample editor](drum-rack-sample-editor.md)).
- **Moving a pad takes the sound, not the music** (2026-10-07). Clip notes and automation lanes stay on their notes, as they do when a pad is moved in Live. A moved sound is therefore played by the target note's clips and shaped by that note's lanes. Lanes store `note * 6 + control` and are named by note (`C2 Decay`), which keeps this readable. Moving the lanes with the sound was considered and not done.
- **Auto Select follows clips as well as live input** (2026-10-07), as Live's does, so a playing clip keeps moving the selection; switch it off to keep one pad in the editor while a clip plays. It never moves the bank shown: a pad selected outside the sixteen is lit only on the map.

## Tests

- `tests/DrumRackTest.cpp` (run by `--self-test`):
  - measures what the rack plays by routes that cannot agree with it by construction ([Measure sound, don't read the DSP](measure-sound-dont-read-dsp.md)): pitch by zero crossings, decay, tone, velocity, level, pan, choke, mute and solo, all eight models finite and dying away, the kick at 52 Hz. `checkEveryNote` and `checkPlayback` render constant-level files, so a gain reads straight off the samples;
  - `checkSlicing` cuts a synthetic beat: a quiet tone, four loud hits and one soft one;
  - covers the sample hand-off, pictures against live playback, file round trips and refusals (playback attributes included), the factory content and `drumTypeOf`;
  - `checkSampleEditorThroughSession` drives undo steps, the spread, banks, the note editor's rows, the parameter range reads against the full list, and the timing log line;
  - `checkMovingPads` covers a move carrying everything, its undo, a swap, a move to note 127, the refusals and a self-drop.
- `--self-test` logs how long making a rack takes and how long the 808 Kit's eight samples take to read onto their pads (`setKit`). It saves a kit and a drum preset under a temporary Drums root, not `userDrums()`, and finds them with `ContentLibrary::drumKitsIn` and `drumPresetsIn` (`user = true`).
- `--device-test` primes a blank rack (`prime` in `DeviceConformance.cpp`) on the notes its chord strikes: synths on 48, 53 and 58, the 808 kick sample on 60. A blank rack is silent and would fail every check that listens for a note.
- `runDrumRackFaceTest` (`tests/Pattern/DeviceRackTest.cpp`) covers the face: the map (click, wheel, drag), the key flash, the knobs following selection, mute with an undo, modes, a marker drag and a knob drag (one undo each), the spread button, drops of samples, presets and kits, and pad drags: onto an empty pad (the target lit mid-drag), onto a filled pad, onto the map's top-left note (124), and a 3 px wobble that moves nothing. Auto Select: a nudged `drumNotesSeen` on a filled pad selects it and the knobs follow, the earlier nudge on empty C1 selected nothing, and switched off nothing follows. With `RHINO_NATIVE_INPUT_TEST=1` it also clicks Loop and a pad's M through a desktop peer and requires each to toggle once ([A JUCE component listening to itself hears its own clicks twice](juce-self-listener-hears-clicks-twice.md)). It writes `RHINO_DRUMS_SNAPSHOT` and logs the face's cost ([Writing Rhino tests](writing-rhino-tests.md)).
- `runDrumRackWindowTest` (`tests/Pattern/DrumRackWindowTest.cpp`) covers the window, which a rack off the screen makes without showing: the name-bar button and Edit, keys handed on, the stacked layout (pads above the picture, both bigger than the rack's), the map's bigger cells, a lane followed, a pad picked in one face reaching the other, the four kinds of drop, the two kept picture widths, following an arpeggiator added in front and a track moved, and closing on a deleted rack and a new document. It writes `RHINO_DRUM_WINDOW_SNAPSHOT` at 1280 x 800.

## Related

- [Drum Rack sample editor](drum-rack-sample-editor.md)
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
- [Hold ids, not pointers](ids-not-pointers.md)
