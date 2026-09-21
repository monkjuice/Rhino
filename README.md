# Rhino

[GitHub repository](https://github.com/monkjuice/Rhino)

A desktop DAW for creating electronic music, with arrangement and live clip workflows as the longer-term direction. Development currently prioritizes **Windows**, while keeping the native architecture portable to macOS.

The active application uses **C++20, Tracktion Engine, and JUCE**. It provides an early pattern-composition workflow. See [ARCHITECTURE.md](ARCHITECTURE.md) for the architecture and [native/README.md](native/README.md) for implementation details.

Research deliverables: [open-source DAW and Ableton study](research/DAW-STUDY.md), [pinned source map](research/SOURCE_MAP.md), [internal device architecture](research/DEVICE-SYSTEM.md), [interaction and visual direction](research/UX-SPEC.md), and [implementation roadmap and validation status](research/ROADMAP.md).

## Native App

- A startup loading screen showing engine, audio-device, and workspace initialization.
- One-bar, 16-step note grid covering MIDI notes 48-59.
- Playback through Tracktion's 4OSC synth and Rhino's internal Utility gain device.
- Five drum kits under Instruments / Drum Rack, one matching each drum pattern. A kit is the drum instrument with its pads retuned, shortened and rebalanced, and the Clap kit puts the clap on the backbeat rather than the snare.
- One instrument per track, as in Live and Logic. Dropping an instrument on a track replaces the one already there and renames the track after it. The track's clips are untouched, so the pattern stays and the sound changes. Dropping one also makes the track a MIDI track for good. Audio samples and files are refused on a MIDI track.
- **A new track is audio or MIDI, and you say which.** The `+` button above the track cards asks: a **MIDI track** holds clips and records what you play, an **audio track** takes files, samples and the audio input. **Ctrl+T** skips the question and adds whichever kind you picked last, MIDI to begin with. A MIDI track holds clips straight away, before anything is dropped on it - the notes wait for an instrument - and an audio track refuses them, which is the same rule under another name.
- Clips are created deliberately: double-click an empty part of a lane to add a one-bar clip starting where you clicked, or press **Ctrl+A** to add one to the focused track at the playhead.
- Drawing and erasing notes, grouped undo/redo, and tempo control from 40-240 BPM.
- Play, pause, stop, looping, and audio hardware settings.
- **Recording** into any track, audio or MIDI. Click the record dot on a track card to arm it, then press the record button or **F9**. What a track records follows from what the track is: one running an instrument records the MIDI input, one without records the audio input chosen in **Edit > Audio settings**. Neither needs a clip to record into - the take becomes a new clip covering the span it was recorded over, replacing what was underneath it, which is the same rule every other clip here follows. A MIDI take is drawn **while it is played**: the engine writes no clip until the transport stops, so the notes are read from its live feed and drawn where the clip will be, in the colours and the layout the clip will use. Recorded audio is written beside the project as `<track> Take <n>.wav`, or into `%APPDATA%/Rhino/Recordings` while the project is still untitled. An armed MIDI track always plays what you send it, because otherwise you could not hear the instrument you were recording.
- A **computer MIDI keyboard**, because most of the time there is no controller plugged in and a MIDI track you cannot play is a MIDI track you cannot record. **M** switches it on: **A** to **;** play seventeen semitones with the home row as the white keys and the row above it as the black ones, **Z**/**X** move the octave and **C**/**V** the velocity. The keys, the octave numbering and its limits are **the same as Rhino Forge's own on-screen keyboard**, so a part played into Forge's window and a part recorded through Rhino come out in the same key. The notes go into the MIDI input rather than into a track, so arming decides which track hears them, monitoring whether they are audible, and recording captures them - there is no separate path for them anywhere. While it is on it takes the letter keys, so the plain-letter shortcuts are unavailable until **M** switches it off again.
- **Hearing yourself** on an audio track is the **Monitor** setting, Live's three: **Off**, **Auto** (audible while a track is armed) and **In** (audible always). Right-click a track card for it, or use **Edit > Monitor the audio input**. Unlike Live it starts at **Off**, and it belongs to the audio input rather than to one card: every audio track here records the one input, so the setting reads the same on every card because it is the same setting. Turn it up only on headphones or an interface - a laptop has a microphone at one end and speakers at the other, and Auto or In puts one into the other for as long as a track stays armed.
- A **count-in** of one, two, three or four bars, under the arrow beside the metronome. The click counts while the playhead stands still, and the transport starts recording on the beat after the last one counted. It counts whether or not the metronome itself is switched on - counting you in is the whole point of it - but it does follow the metronome's level and its emphasis setting. It is a project setting, saved with the project, and it applies when recording starts from a standstill - punching in while the transport is already rolling has nothing to count into.
- Audio import onto a separate track, with successive files appended. The browser also includes generated Whistle and Siren WAV samples; these are audio clips, not MIDI presets.
- An arrangement view showing the synth clip and imported audio waveforms, with audio clip selection, moving, non-destructive trimming, and deletion.
- A session view clip launcher, **built but currently switched off** while its remaining behaviour is decided (see [SESSION-VIEW.md](SESSION-VIEW.md)): tracks as columns, scenes as rows, and clip slots you fill from the browser, a right-click menu, or an audio file drop. Clips launch on the chosen quantisation, a track plays one clip at a time, and scene rows launch together. Launching a clip makes that track ignore its timeline clips; **Back to Arrangement** appears while that is true and hands the tracks back. Session clips and arrangement clips are separate, as they are in Live, so each view has a menu item that copies a clip across: right-click a slot for **Copy to arrangement at playhead**, or right-click an arrangement clip for **Copy to session slot**.
- An **audio clip editor**, opened by double-clicking an audio clip in the arrangement: gain, pan, pitch, fade in, fade out, reverse and mute, beside the waveform of the part of the source file that clip plays. Everything on it belongs to that one clip, so raising a sample that came in quiet does not move the rest of the lane; the track fader in the card beside it is still the way to move the whole track.
- **Rhino Tune**, a pitch corrector for vocals, under Audio FX / Rhino. Drop it on the track the voice is on. It hears the note being sung, decides which note it should have been, and moves it there; the **Input** column shows what it heard and how far off, the meter above the keyboard shows how far it is pulling in cents, and a dot on the keyboard marks the note it is pulling towards. Set the key with **Root** and **Scale**, or click individual keys to build your own - a key switched off is a note the voice will never be pushed onto, and the chooser then reads **Custom**. **Strength** is how far it insists, **Retune** how long it takes to get there: at zero the pitch snaps and you get the hard sound, at 40 ms and up it slides the way a singer does. **Flex** lets a note escape as it moves away from its target, so a scoop into a note stays a scoop instead of being flattened into a staircase, and **Human** stretches the retune on a held note, which leaves the middle of a long note where the singer put it. A slow retune keeps natural vibrato either way, because the correction follows the drift under a vibrato rather than the vibrato itself.
- Rhino Tune will also move a voice without correcting it: **Pitch** and **Fine** transpose, **Formant** moves the resonances that make a voice sound large or small without moving the note at all, and **F. Follow** decides how much those resonances travel with a transposition - at zero a voice pitched up keeps its own body, at 100% it sounds like the tape was sped up. **Vibrato** adds one, with **Natural** making it wander the way a real one does. **Dry/Wet** at 50% gives a doubler rather than a correction. Set the **High / Mid / Bass** range to the voice: the lamp beside each one lights when the incoming voice is in that band, so a wrong setting says so. The range decides the delay the device needs, shown in its title bar - 34 ms for High up to 120 ms for Bass, taken out of the mix automatically. **LIVE** cuts that delay for monitoring at the cost of some roughness on note onsets. It works on one voice at a time: a chord or a group of singers is the wrong thing to put on it.
- **Rhino Vocoder**, under Audio FX / Rhino, in the one mode that matters: your voice, played by another track's synth. The workflow is Live's. Make a MIDI track and put a synth on it. Make an audio track, drop the vocoder on it, and set **Audio From** on the vocoder's face to the synth track. Arm the audio track so it hears the microphone, and mute the synth track so you hear the vocoder rather than both - the carrier keeps running while it is muted, which is what makes that step safe. Play the synth and talk, and the synth speaks. The two lamps in the title bar say whether the **VOICE** and the **CARRIER** are actually arriving, so a step missed announces itself, and the bars in the middle are the filter bank, one per band.
- The vocoder's controls read left to right. **Bands** is how finely the voice is sliced - fewer is fatter and more robotic, more is more intelligible. **Formant** moves the carrier's bank against the voice's, so the voice keeps its rhythm and changes its size. **BW** widens or narrows every band, **Low** and **High** are where the bank starts and stops, and **Attack** and **Release** are how quickly it follows the voice: a slow release smears words together, a fast one is crisp and can chatter. **Gate** shuts the bank below a level, so room tone does not play the synth. **Unvoiced** mixes noise in while you are on an `s` or a `t` - no carrier can play those, because they have no pitch. **Enhance** flattens the carrier so a synth with a strong fundamental and little else still speaks; if the result sounds muffled, this is the knob. **Depth** is how far the voice is allowed to shape the carrier, **Level** trims the output and **Dry/Wet** blends your untouched voice back in. Set **Audio From** to **None** and the voice passes straight through, so a vocoder with nothing chosen never goes mysteriously silent.
- Two independent panels below the arrangement, each answering to one thing, and both starting out of the way. The **clip editor** answers to the selected clip: a MIDI or drum clip opens the **note editor**, a double-clicked audio clip the **audio clip editor**, and selecting no clip closes it. The **Device View** answers to the track: click a track card, a group band or the main row and that track's device chain comes up, whether it runs an instrument or only effects. Both can be up at once, stacked, with a drag handle between them; the **Clip** and **Devices** toggles under the arrangement open or close either by hand, and an empty strip is given back to the arrangement.
- A main track pinned below the arrangement's tracks, one line tall, carrying its name, level and pan. It takes audio effects only; instruments, patterns, clips and audio are refused. Click it to edit its effect chain in the Device View.
- Track cards you can size, order, colour and rename. A card is two columns: mute, solo, the record dot and the two faders on the left, the track name on its colour to the right. A MIDI track carries a **MIDI From** chooser under its faders, Live's: **All Ins** is every keyboard plus the typing keyboard and is what a new track gets, **Computer Keyboard** is the typing keyboard alone, each keyboard plugged in is offered by name, and **None** leaves the track deaf. It belongs to the track, so two tracks can be played from two keyboards, and it is saved with the project. A short row hides the line rather than squashing it; right-click the card for the same chooser. Drag a card's bottom edge to make its row taller or shorter, and the rows below move along; a row stops shrinking once it is down to its name and its three buttons. Drag the body of a card to carry that track to another place in the stack. Right-click a card for its colour palette and **Rename**; the colour tints the card and leaves the clips their own colours.
- One mixer shared by both views: volume, pan, mute and solo per track, plus the main level on the main track. The session view shows it as a vertical strip under each track, the arrangement as a horizontal strip in each track header, and editing either moves the same track.
- Timeline seeking, zoom, scrolling, and optional 1/16-note snapping.
- Native project save/open, unsaved-change prompts, and background file writing/parsing.
- A display-synchronized playhead with narrow repaint regions and cached note display data.
- VST3 hosting through Tracktion Engine, with the independent Rhino Forge synth discoverable from its development build or a standard VST3 installation and shown with its plugin-owned editor.
- The interface is drawn with Inter, bundled under the SIL Open Font License (`native/assets/fonts/OFL.txt`), rather than whichever font the desktop happens to use.

This is an early composition workflow, not a complete DAW. Media bundling/relinking and a general plugin browser/scanner are not implemented yet, and recording covers only the basics: one take at a time into the armed tracks, with no punch range, no loop takes and no comping. The session view is paused rather than finished: the note editor cannot open a session clip, and follow actions, level meters, sends and returns, and recording into slots are missing. [SESSION-VIEW.md](SESSION-VIEW.md) records where it stopped and how to resume. WAV export renders the complete arrangement offline at 48 kHz/24-bit, peak-normalised to -1 dBFS to preserve headroom. macOS has not been built or tested.

## Run On Windows

If the Release build already exists, run this from the repository root:

```powershell
& ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe"
```

To build it, install Python 3.12+, CMake 3.24+, and Visual Studio 2022 with the **Desktop development with C++** workload and Windows SDK. Run from the repository root:

```powershell
python native/scripts/fetch-dependencies.py
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
& ".\native\build\RhinoNative_artefacts\Release\RhinoDAW.exe"
```

The fetch script downloads pinned Tracktion and JUCE revisions into `native/.deps`. Dependencies and build outputs are ignored by version control.

In the current workspace, portable CMake tools are also available at `native/.tools/cmake-3.31.6-windows-x86_64/bin`. If CMake is not on your PATH, invoke `cmake.exe` and `ctest.exe` from that directory. These local tools are not included in a fresh checkout.

## Create A Pattern

1. Draw notes in the grid. Drag from an empty cell to add notes; drag from an existing note or right-drag to erase. Each stroke is one undo action. Select a sustained note and press **Ctrl+E** to divide its span into retriggers without changing the visible grid; keep Ctrl held and use the arrow keys or wheel to choose 2-32 equal divisions. Hold **V** and use Up/Down or the wheel to adjust the selected notes' velocity; the footer shows their shared percentage or `MIXED` when they differ. **Ctrl+A** selects every note in the clip, including notes above or below the visible pitch window, and **Up/Down** transposes the selection by a semitone, **Shift+Up/Down** by an octave.
2. Press **Play**. Adjust BPM to change tempo while keeping the pattern one bar long. **Synth Gain** controls the Utility device after the synth.
3. Use **Add audio** to place audio on the separate track. The first file starts at zero, and later files append to that track.
4. Use **Save** to keep the project and **Open** to return to it. An asterisk beside the project name marks unsaved changes.

To record instead of drawing, click the dot on a track card to arm it and press the record button, or **F9**. A track with an instrument on it records what you play on the MIDI input; a track without one records the audio input from **Edit > Audio settings**. Set a count-in first under the arrow beside the metronome - one to four bars - and the click counts those bars out with the playhead standing still before the transport starts. The take lands as a new clip covering what it was recorded over. Press the record button again, **F9**, Space or Stop to end it.

No MIDI keyboard to hand? Press **M** and play the typing keyboard: **A** to **;** are seventeen semitones, laid out exactly as Forge's own keyboard lays them out, with **Z**/**X** for the octave and **C**/**V** for the velocity. It plays into the MIDI input, so the armed track's instrument sounds and records exactly as it would from a controller. **M** switches it off and gives the letter keys back to the shortcuts.

Imported audio appears in the arrangement above the note editor, and the timeline fits the imported material automatically. Drag the body of an audio clip to move it; drag its left or right edge to trim it. Trimming preserves the source file, and you can extend the edges back to the available source boundaries. Each completed drag is one undo action. Dragging previews the change; playback adopts it on release.

The browser is a library rail over a folder tree, as Live's is. The rail picks a section - **Instruments** change a track, **Patterns** fill a clip, **Samples** are audio, and **Audio FX** and **MIDI FX** are devices - and the tree below shows that section's folders. Searching looks across every section and groups the hits by where they came from. A library row leaves the browser by being dragged onto the track or clip it is meant for: there is no click-to-add, so browsing and auditioning can never land a device on whatever track happened to be selected. Clicking a sample plays it once so you can hear it before deciding where it goes; the audition is mixed alongside whatever the transport is doing and touches nothing in the project. Turn it off with **Edit > Preview library sounds**, which is remembered between launches. Use **Snap 1/16** to toggle snapping, or hold Alt during a drag to bypass it. Escape cancels an active drag. Delete/Backspace removes the selected audio clip when the arrangement is focused. **M** and **S** mute and solo each track. Resting the pointer on a header control says what it does in the Info View. Click the timeline ruler to seek; **Fit**, **+**, **-**, the scrollbar, and mouse wheel navigate the timeline. Ctrl+wheel zooms around the pointer. The synth clip is currently displayed at bar one; its notes remain editable in the grid below.

Two clips can never overlap on one track, and the clip you just moved or dropped is the one that wins the ground it landed on. What was there is cut at the new clip's edges and the part underneath it goes, so dropping a one-bar clip into the middle of a four-bar one leaves three clips: the part before it, the clip you dropped, and the part after. Trimming a clip over its neighbour eats the neighbour the same way.

Selecting in the arrangement is a **span of the timeline**, not a set of clips: drag across empty lane space and the drag marks out a rectangle - a run of time across the run of tracks it crossed. A click that does not drag leaves an insert point on that lane, and clicking a clip selects the clip and sets the span to the clip. One rule then covers the four commands, and it is Live's. **Ctrl+C** copies whatever is inside the span, cutting the clips that cross its edges rather than taking them whole. **Ctrl+V** puts that at the insert point, replacing what it lands on. **Ctrl+X** copies and then empties the span, and **Delete** empties it without copying - a clip that runs into the span is trimmed, a clip that spans it is left with a hole. **Ctrl+D** duplicates: the copy lands flush against the end of the selection and the selection moves onto it, so holding Ctrl+D turns one bar into four without touching the pointer. It replaces what is already sitting there instead of sliding past it.

The note editor works the same way, in steps instead of seconds. A span dragged out with **S** held, or the notes that are selected, is what the four commands act on; **Ctrl+A** makes it the whole clip, so Ctrl+A then Ctrl+D turns a one-bar pattern into two bars and lengthens the clip to fit. A paste lands on the step the insert point is on and replaces the notes it covers, rather than searching for the first empty space.

Tracks can be grouped into a **bus**. Shift-click or Ctrl-click track cards to gather them, then **Ctrl+G** makes a group: a track of its own that opens where the topmost selected card was, with the members carried under it. Every member's audio is routed into that track, so the group is a real mixing point - it has its own fader, pan, mute, solo and device chain, and dropping an audio effect on it processes everything underneath at once. It takes audio effects and nothing else: a group carries audio that has already been played, so there is nothing for an instrument to play, no sequence for a MIDI effect to act on, and nowhere to put a clip.

A card inside a group starts a step further right, in the group's colour, so it is clear at a glance which tracks the bus covers, and the arrow on the group's card folds them away. Because the group is a track, everything you already know works on it: **F2** renames it, the colour palette colours it, Delete removes it and hands its members back to the main output. Right-click a card to add it to a group or take it out of one - a card menu acts on every selected card, not only the one under the pointer - and **Ctrl+Shift+G** ungroups. Dragging a card into the middle of a group joins it and dragging it out leaves it, with the audio following in both directions, because where a track sits is the only thing that decides what it feeds. Groups do not nest.

Double-click an audio clip to open it in the **audio clip editor** under the arrangement. Its five knobs and two switches are the clip's own: **Gain** and **Pan** move that clip in the mix, **Pitch** transposes it by up to two octaves, **Fade In** and **Fade Out** shape its edges, and **Reverse** and **Mute** act on it alone. The waveform beside them is the span of the source file the clip plays, drawn with the fades shaded over it and mirrored when the clip is reversed. A whole knob drag is one undo step. Once the editor is open it follows the audio clip selection, so clicking from one clip to the next compares them; selecting a MIDI clip hands the panel back to the note editor, and selecting no clip at all - a track card, a region, empty space - closes it. The Device View is unaffected either way: the two panels are independent, so opening a clip never closes the devices beneath it.

The loop covers both the pattern and imported audio. If audio extends beyond one bar, the synth pattern plays only during its first bar; it does not automatically repeat across the longer arrangement.

| Shortcut | Action |
| --- | --- |
| Space, with the grid focused | Play/pause |
| F9 | Record into the armed tracks, or stop the recording |
| M | Play MIDI from the typing keyboard, and switch back to the shortcuts |
| A to ;, while it is on | Play notes; Z/X move the octave, C/V the velocity |
| Click the dot on a track card | Arm that track to record into |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+O | Open |
| Ctrl+T | Add a track of the kind last picked from the + menu, MIDI to begin with |
| F2 | Rename the selected track card or group |
| Ctrl+G | Group the selected track cards |
| Ctrl+Shift+G | Ungroup the selected group |
| Click a MIDI or drum clip | Open its notes in the note editor below |
| Click a track card | Open that track's chain in the Device View below |
| Double-click a clip | Open it below: audio clips in the audio clip editor, MIDI clips in the note editor |
| Drag an empty lane | Select a span of the timeline across the tracks the drag crossed |
| Ctrl+C / Ctrl+X | Copy or cut the selection |
| Ctrl+V | Paste it at the insert point |
| Ctrl+D | Duplicate it directly after itself |
| Delete / Backspace | Empty the selection |
| Ctrl+E, then Ctrl+arrows/wheel | Divide a selected note into retriggers |
| V+Up/Down or V+wheel | Adjust selected-note velocity |
| Up/Down | Transpose the selection a semitone |
| Shift+Up/Down | Transpose the selection an octave |
| F12 | Full screen, also under View in the title bar |

Command-key handling is included for macOS, but remains unvalidated there.

## Native Project Files

Native projects use `.rhinoedit` and preserve notes, tempo, device state, and audio references. Imported audio stays at its original path: keep those files in place. Projects do not yet collect media into a portable folder.

Save writes a detached project snapshot on a worker thread to a temporary file before replacing the destination. Edits made during saving remain marked unsaved. Open validates the project before replacing the current edit; engine reconstruction still runs on the message thread, with editing disabled during that operation.

## Validation

Windows validation includes the Release build and five CTest cases. Tests cover arrangement geometry, Utility DSP, note gestures and undo/redo, tempo and loop duration, a real 48 kHz MIDI-to-synth WAV render, audio import, project state/media-reference round trips, invalid-project rejection, and changes made after a save snapshot. The arrangement workflow exercises pointer drags, visible waveform drawing, cancellation, mute/solo undo, deletion/recovery, reopened clip offsets, and a render that verifies the trimmed source region.

These checks do not establish physical audio-device behavior, end-to-end pointer latency, sustained FPS, large-project capacity, or macOS compatibility. The render is an integration test; the File menu exposes the same offline WAV-rendering path.

Audio scheduling belongs to Tracktion rather than the UI. Control feedback is event-driven, playheads follow display refresh, and a separate 10 Hz timer updates only transport text. Audio thumbnails cache waveform data and scan samples in the background. Clip drags use a local preview and update the engine once on release. Audio-import metadata work and engine reconstruction still need further work to avoid long message-thread stalls. Large mock sessions and benchmark scaffolding are deliberately deferred while development uses small correctness checks.
