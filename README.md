# Theta

[GitHub repository](https://github.com/monkjuice/THETA)

A desktop DAW for creating electronic music, with arrangement and live clip workflows as the longer-term direction. Development currently prioritizes **Windows**, while keeping the native architecture portable to macOS.

The active application uses **C++20, Tracktion Engine, and JUCE**. It provides an early pattern-composition workflow. See [ARCHITECTURE.md](ARCHITECTURE.md) for the architecture and [native/README.md](native/README.md) for implementation details.

Research deliverables: [open-source DAW and Ableton study](research/DAW-STUDY.md), [pinned source map](research/SOURCE_MAP.md), [internal device architecture](research/DEVICE-SYSTEM.md), [interaction and visual direction](research/UX-SPEC.md), and [implementation roadmap and validation status](research/ROADMAP.md).

## Native App

- A startup loading screen showing engine, audio-device, and workspace initialization.
- One-bar, 16-step note grid covering MIDI notes 48-59.
- Playback through Tracktion's 4OSC synth and Theta's internal Utility gain device.
- Drawing and erasing notes, grouped undo/redo, and tempo control from 40-240 BPM.
- Play, pause, stop, looping, and audio hardware settings.
- Audio import onto a separate track, with successive files appended.
- An arrangement view showing the synth clip and imported audio waveforms, with audio clip selection, moving, non-destructive trimming, and deletion.
- Mute/solo controls for both tracks, timeline seeking, zoom, scrolling, and optional 1/16-note snapping.
- Native project save/open, unsaved-change prompts, and background file writing/parsing.
- A display-synchronized playhead with narrow repaint regions and cached note display data.
- VST3 hosting through Tracktion Engine, with the independent Theta Forge synth discoverable from its development build or a standard VST3 installation and shown with its plugin-owned editor.

This is an early composition workflow, not a complete DAW. Recording, media bundling/relinking, a general plugin browser/scanner, and the live launcher are not implemented yet. WAV export renders the complete arrangement offline at 48 kHz/24-bit, peak-normalised to -1 dBFS to preserve headroom. macOS has not been built or tested.

## Run On Windows

If the Release build already exists, run this from the repository root:

```powershell
& ".\native\build\ThetaNative_artefacts\Release\Theta.exe"
```

To build it, install Python 3.12+, CMake 3.24+, and Visual Studio 2022 with the **Desktop development with C++** workload and Windows SDK. Run from the repository root:

```powershell
python native/scripts/fetch-dependencies.py
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
& ".\native\build\ThetaNative_artefacts\Release\Theta.exe"
```

The fetch script downloads pinned Tracktion and JUCE revisions into `native/.deps`. Dependencies and build outputs are ignored by version control.

In the current workspace, portable CMake tools are also available at `native/.tools/cmake-3.31.6-windows-x86_64/bin`. If CMake is not on your PATH, invoke `cmake.exe` and `ctest.exe` from that directory. These local tools are not included in a fresh checkout.

## Create A Pattern

1. Draw notes in the grid. Drag from an empty cell to add notes; drag from an existing note or right-drag to erase. Each stroke is one undo action. Select a sustained note and press **Ctrl+E** to divide its span into retriggers without changing the visible grid; keep Ctrl held and use the arrow keys or wheel to choose 2-32 equal divisions. Hold **V** and use Up/Down or the wheel to adjust the selected notes' velocity; the footer shows their shared percentage or `MIXED` when they differ.
2. Press **Play**. Adjust BPM to change tempo while keeping the pattern one bar long. **Synth Gain** controls the Utility device after the synth.
3. Use **Add audio** to place audio on the separate track. The first file starts at zero, and later files append to that track.
4. Use **Save** to keep the project and **Open** to return to it. An asterisk beside the project name marks unsaved changes.

Imported audio appears in the arrangement above the note editor, and the timeline fits the imported material automatically. Drag the body of an audio clip to move it; drag its left or right edge to trim it. Trimming preserves the source file, and you can extend the edges back to the available source boundaries. Each completed drag is one undo action. Dragging previews the change; playback adopts it on release.

Use **Snap 1/16** to toggle snapping, or hold Alt during a drag to bypass it. Escape cancels an active drag. Delete/Backspace removes the selected audio clip when the arrangement is focused. **M** and **S** mute and solo each track. Click the timeline ruler to seek; **Fit**, **+**, **-**, the scrollbar, and mouse wheel navigate the timeline. Ctrl+wheel zooms around the pointer. The synth clip is currently displayed at bar one; its notes remain editable in the grid below.

The loop covers both the pattern and imported audio. If audio extends beyond one bar, the synth pattern plays only during its first bar; it does not automatically repeat across the longer arrangement.

| Shortcut | Action |
| --- | --- |
| Space, with the grid focused | Play/pause |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+O | Open |
| Ctrl+E, then Ctrl+arrows/wheel | Divide a selected note into retriggers |
| V+Up/Down or V+wheel | Adjust selected-note velocity |

Command-key handling is included for macOS, but remains unvalidated there.

## Native Project Files

Native projects use `.thetaedit` and preserve notes, tempo, device state, and audio references. Imported audio stays at its original path: keep those files in place. Projects do not yet collect media into a portable folder.

Save writes a detached project snapshot on a worker thread to a temporary file before replacing the destination. Edits made during saving remain marked unsaved. Open validates the project before replacing the current edit; engine reconstruction still runs on the message thread, with editing disabled during that operation.

## Validation

Windows validation includes the Release build and five CTest cases. Tests cover arrangement geometry, Utility DSP, note gestures and undo/redo, tempo and loop duration, a real 48 kHz MIDI-to-synth WAV render, audio import, project state/media-reference round trips, invalid-project rejection, and changes made after a save snapshot. The arrangement workflow exercises pointer drags, visible waveform drawing, cancellation, mute/solo undo, deletion/recovery, reopened clip offsets, and a render that verifies the trimmed source region.

These checks do not establish physical audio-device behavior, end-to-end pointer latency, sustained FPS, large-project capacity, or macOS compatibility. The render is an integration test; the File menu exposes the same offline WAV-rendering path.

Audio scheduling belongs to Tracktion rather than the UI. Control feedback is event-driven, playheads follow display refresh, and a separate 10 Hz timer updates only transport text. Audio thumbnails cache waveform data and scan samples in the background. Clip drags use a local preview and update the engine once on release. Audio-import metadata work and engine reconstruction still need further work to avoid long message-thread stalls. Large mock sessions and benchmark scaffolding are deliberately deferred while development uses small correctness checks.
