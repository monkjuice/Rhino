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
- One instrument per track, as in Live and Logic. Dropping an instrument on a track replaces the one already there and renames the track after it. The track's clips are untouched, so the pattern stays and the sound changes. Audio samples and files are refused on a track that runs an instrument.
- Clips are created deliberately: double-click an empty part of a lane to add a one-bar clip starting where you clicked, or press **Ctrl+A** to add one to the focused track at the playhead.
- Drawing and erasing notes, grouped undo/redo, and tempo control from 40-240 BPM.
- Play, pause, stop, looping, and audio hardware settings.
- Audio import onto a separate track, with successive files appended. The browser also includes generated Whistle and Siren WAV samples; these are audio clips, not MIDI presets.
- An arrangement view showing the synth clip and imported audio waveforms, with audio clip selection, moving, non-destructive trimming, and deletion.
- A session view clip launcher, **built but currently switched off** while its remaining behaviour is decided (see [SESSION-VIEW.md](SESSION-VIEW.md)): tracks as columns, scenes as rows, and clip slots you fill from the browser, a right-click menu, or an audio file drop. Clips launch on the chosen quantisation, a track plays one clip at a time, and scene rows launch together. Launching a clip makes that track ignore its timeline clips; **Back to Arrangement** appears while that is true and hands the tracks back. Session clips and arrangement clips are separate, as they are in Live, so each view has a menu item that copies a clip across: right-click a slot for **Copy to arrangement at playhead**, or right-click an arrangement clip for **Copy to session slot**.
- A main track pinned below the arrangement's tracks, one line tall, carrying its name, level and pan. It takes audio effects only; instruments, patterns, clips and audio are refused. Select it to edit its effect chain in the Device View.
- Track cards you can size, order, colour and rename. A card is two columns: mute, solo and the two faders on the left, the track name on its colour to the right. Drag a card's bottom edge to make its row taller or shorter, and the rows below move along; a row stops shrinking once it is down to its name, mute and solo. Drag the body of a card to carry that track to another place in the stack. Right-click a card for its colour palette and **Rename**; the colour tints the card and leaves the clips their own colours.
- One mixer shared by both views: volume, pan, mute and solo per track, plus the main level on the main track. The session view shows it as a vertical strip under each track, the arrangement as a horizontal strip in each track header, and editing either moves the same track.
- Timeline seeking, zoom, scrolling, and optional 1/16-note snapping.
- Native project save/open, unsaved-change prompts, and background file writing/parsing.
- A display-synchronized playhead with narrow repaint regions and cached note display data.
- VST3 hosting through Tracktion Engine, with the independent Rhino Forge synth discoverable from its development build or a standard VST3 installation and shown with its plugin-owned editor.
- The interface is drawn with Inter, bundled under the SIL Open Font License (`native/assets/fonts/OFL.txt`), rather than whichever font the desktop happens to use.

This is an early composition workflow, not a complete DAW. Recording, media bundling/relinking, and a general plugin browser/scanner are not implemented yet. The session view is paused rather than finished: the note editor cannot open a session clip, and follow actions, level meters, sends and returns, and recording into slots are missing. [SESSION-VIEW.md](SESSION-VIEW.md) records where it stopped and how to resume. WAV export renders the complete arrangement offline at 48 kHz/24-bit, peak-normalised to -1 dBFS to preserve headroom. macOS has not been built or tested.

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

Imported audio appears in the arrangement above the note editor, and the timeline fits the imported material automatically. Drag the body of an audio clip to move it; drag its left or right edge to trim it. Trimming preserves the source file, and you can extend the edges back to the available source boundaries. Each completed drag is one undo action. Dragging previews the change; playback adopts it on release.

The browser is a library rail over a folder tree, as Live's is. The rail picks a section - **Instruments** change a track, **Patterns** fill a clip, **Samples** are audio, and **Audio FX** and **MIDI FX** are devices - and the tree below shows that section's folders. Searching looks across every section and groups the hits by where they came from. A library row leaves the browser by being dragged onto the track or clip it is meant for: there is no click-to-add, so browsing and auditioning can never land a device on whatever track happened to be selected. Use **Snap 1/16** to toggle snapping, or hold Alt during a drag to bypass it. Escape cancels an active drag. Delete/Backspace removes the selected audio clip when the arrangement is focused. **M** and **S** mute and solo each track. Resting the pointer on a header control says what it does in the Info View. Click the timeline ruler to seek; **Fit**, **+**, **-**, the scrollbar, and mouse wheel navigate the timeline. Ctrl+wheel zooms around the pointer. The synth clip is currently displayed at bar one; its notes remain editable in the grid below.

Tracks can be grouped. Shift-click or Ctrl-click track cards to gather them, then **Ctrl+G** makes a named band over them; the tracks are carried together so a group is always one run of the stack. A card inside a group starts a step further right, in the group's colour, so it is clear at a glance which tracks the band covers. The band has a disclosure arrow that folds its tracks away - a collapsed group keeps showing its members' clips flattened onto its own row - plus its own **M** and **S**, which reach every track under it. Right-click the band to rename, colour or ungroup it, or right-click a card to add it to a group or take it out of one - a card menu acts on every selected card, not only the one under the pointer. **F2** renames whichever is selected, and renaming happens on the card itself: Return keeps the new name, Escape abandons it, and nothing else is blocked while you type. Dragging a card into the middle of a band joins it, and dragging it out leaves it, so where a track sits is what decides what it is in. Groups are organisational: audio still reaches the main output track by track rather than through a group bus.

The loop covers both the pattern and imported audio. If audio extends beyond one bar, the synth pattern plays only during its first bar; it does not automatically repeat across the longer arrangement.

| Shortcut | Action |
| --- | --- |
| Space, with the grid focused | Play/pause |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+O | Open |
| Ctrl+T | Add a track |
| F2 | Rename the selected track card or group |
| Ctrl+G | Group the selected track cards |
| Ctrl+Shift+G | Ungroup the selected group |
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
