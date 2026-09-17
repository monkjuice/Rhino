# Rhino interaction and visual direction

Status: proposed design requirements informed by the [DAW study](DAW-STUDY.md). These are not implemented product guarantees.

## Design premise

The interface should support a continuous path from a sound to a finished song. Keep the useful design findings: a quiet charcoal palette, clear track colors, and an integrated clip editor. Avoid tiny typography, fixed panel sizes, hidden track overflow, whole-panel redraws, and controls that change editing state implicitly.

Confirmed user priorities: UI performance, sustained frame rate, consistent frame pacing, and immediate pointer response are primary selection criteria alongside visual and interaction quality. Evaluate these during editing and audio playback, not just idle animation.

“Beautiful” means readable musical structure, confident editing, and coherent device behavior in addition to visual polish. The user should be able to tell what is selected, what is sounding, what will happen next, and what an action will change.

## Workspace model

Use three views over one project. Their initial names are descriptive working labels:

| View | Primary task | What remains consistent |
| --- | --- | --- |
| Compose | Build patterns, audition variations, launch clips and scenes | Track identity, device chain, mixer state, transport, browser |
| Arrange | Shape sections, edit audio/MIDI, record and draw automation | Same tracks, sounds, media, selection, and command vocabulary |
| Mix | Balance tracks, inspect routing, sends, effects, and levels | Same device instances and parameter state |

Ableton documents performance flowing from its launcher into its arrangement; Bitwig documents context-aware browsing and unified modulation. Rhino should learn those relationships rather than merely reproducing a toolbar layout. [Ableton Session View](https://www.ableton.com/en/manual/session-view/), [Bitwig browsers](https://www.bitwig.com/userguide/latest/browsers/), [Bitwig modulation](https://www.bitwig.com/userguide/latest/the_unified_modulation_system/).

Proposed layout:

```text
┌ Project / undo ─────── Transport / record / tempo ───── Views / audio status ┐
├ Browser (resizable) ┬ Song overview / section markers ─────────────────────┤
│ Search / favorites │                                                      │
│ Sounds / devices   │  Main work area: launcher, timeline, or mixer          │
│ Samples / projects │                                                      │
│ Preview controls   │  Clear selection, playback, and recording states      │
├────────────────────┼──────────────────────────────────────────────────────┤
│ Context inspector  │  Clip editor / device chain (resizable, detachable)   │
├────────────────────┴──────────────────────────────────────────────────────┤
│ Contextual help / exact values                             Tasks / alerts │
└───────────────────────────────────────────────────────────────────────────┘
```

The overview and panel splitters are functional navigation controls. Remember panel sizes and view state per workspace; do not store window geometry inside the DSP model.

## Visual rules

- Use a restrained neutral background with visibly separated surfaces. Track colors identify musical material; selection uses an additional outline; recording and faults have dedicated states. Never encode state by color alone.
- Target 12–14 logical-pixel text for normal controls, with scaling available. Small labels must remain legible at common Windows scaling and macOS Retina settings.
- Align numbers, grids, and waveforms consistently. Use tabular numerals for time and levels. Show meaningful units: dB, Hz, milliseconds, beats, and percentages where appropriate.
- Prefer flat, compact controls with clear focus and hover states. Use space to group related operations; avoid giving every control a large card.
- Device panels share a common header, bypass, preset, macro, and parameter treatment. Give individual synths distinct visualizations without inventing a new control language for each one.
- Animation communicates transport, pending launches, modulation, or transitions. Do not animate every panel or let decoration compete with waveforms and notes.
- Empty areas explain the next useful action briefly. Remove promotional copy from space needed for editing.

These are Rhino design choices. Their final appearance should be tested with realistic sessions and both dark/light viewing conditions, not only a sparse demo screenshot.

## Editing behavior

### Timeline

Single-click selects; drag moves; edge handles trim; a named command splits at the edit cursor. Multi-selection, marquee, duplicate, and repeat operate consistently. Provide an explicit draw tool rather than overloading every blank click with creation.

Keep edit cursor, playhead, selected range, and loop region distinct. Zoom around the pointer or current selection; provide fit selection, fit song, and zoom history. If auto-follow is on, manual editing/scrolling should temporarily suspend it rather than fighting the user. This draws on the documented navigation and follow behavior in [Ableton Arrangement View](https://www.ableton.com/en/live-manual/12/arrangement-view/).

Clip edits are non-destructive. Trimming a MIDI clip hides out-of-range notes without deleting them. Trimming audio changes source offset and visible duration; restoring a boundary reveals the original material. A destructive crop/consolidate operation is separately named and undoable.

Snap adapts to zoom but shows its current resolution. Offer straight/triplet values and a temporary modifier override. Display the actual target position during a drag. Dragging to an incompatible destination gives a clear indicator before release.

### Piano roll and drums

The piano roll needs marquee selection, move, resize, duplicate, velocity editing, audition, fold-to-used-notes, and a clear scale overlay. Quantization should be an explicit editable operation with strength; avoid silently changing timing. Vertical velocity bars and readable pitch/time feedback matter more than ornamental keyboard shading.

The drum editor is a focused view of the same note data. Drum lanes use pad/sample names, with per-lane audition, mute, velocity, and note length where the instrument needs them. Switching editors must not rewrite the pattern.

### Devices and sound browsing

Search should place focus immediately, support keyboard audition, and insert at an explicit target. Distinguish devices from presets and samples. Audition is temporary; commit and cancel are clear. Keep favorites and recent sounds close at hand.

Dragging a device between devices should preview the insertion location. Device chains reveal left-to-right signal flow. Each parameter supports drag adjustment, fine adjustment, direct numeric entry, reset, automation access, and macro mapping through a consistent interaction.

A macro or modulation mapping must show its source, target, range, and effective value. Show both the original knob position and movement caused by modulation, instead of letting the display appear to fight the user's hand. The behavioral policy is defined in [DEVICE-SYSTEM.md](DEVICE-SYSTEM.md).

### Recording

Arming a track makes the destination unmistakable. Input selection, input meter, monitoring mode, count-in, and recording latency information belong in the recording workflow. Transport record should not silently create an unknown destination or discard the chosen loop behavior.

Display capture immediately, then finalize peaks in the background. Retain raw capture if analysis or thumbnail generation fails. Treat takes and overdubs as explicit states. Test mouse, keyboard, and hardware-MIDI operation.

### Saving and recovery

The application owns one authoritative session. Saving should complete atomically and show completion accurately. Distinguish normal saved state, unsaved edits, recovery availability, and missing media.

Show the file path and collection status when relevant. Moving a project between Windows and macOS should not depend on absolute source paths. Autosave/recovery must not interrupt playback or silently overwrite the only good manual save.

## Interaction walkthroughs to validate

1. **From blank to groove:** add a drum rack, place kick/snare/hat notes, add bass, audition a preset, vary the pattern, and switch views without losing selection.
2. **From loop to song:** launch several variations, capture them to the arrangement, duplicate a section, add a transition, and resume arrangement playback with clear ownership.
3. **Sound design:** put a delay after a synth, map a macro to cutoff and delay mix, record a gesture, and restore the original preset with undo.
4. **Recording:** select an input, arm a track, record alongside the backing track, trim the take, and reopen the saved project with its media intact.
5. **Large project:** find a named track, collapse a group, zoom to a section, edit a note, and change a device while audio remains stable.

Record completion time, mistakes, unexpected state changes, and recovery steps. “Feels good” needs observation of real actions as well as frame-time measurements.

## Rendering and accessibility gates

Render the visible time/track range plus a small margin. Use cached waveform levels appropriate to zoom. Do not instantiate one heavyweight UI object for every sample, automation point, or offscreen note. Keep playhead/meters on lightweight updates instead of rebuilding the editor.

### Frame pacing and pointer response

The following are proposed acceptance targets on reference hardware, not measured results. Test at 60 Hz and 120 Hz where supported; adapt rendering to the display rather than imposing a fixed 60 FPS ceiling. One refresh interval is approximately 16.7 ms at 60 Hz and 8.3 ms at 120 Hz.

| Measure | Proposed gate | How to evaluate |
| --- | --- | --- |
| Sustained presentation | Match display refresh during continuous dragging, scrolling, and zooming; fewer than 1% missed presentation opportunities in a 10-minute reference trace | Record presented frames and missed intervals, with the display mode and trace duration |
| Frame pacing | Record median, p95, p99, and longest frame; no application-caused interaction freeze over 50 ms in the reference trace | Profile CPU update/render submission, GPU work, and presentation separately; average FPS alone is insufficient |
| Pointer feedback | p95 OS-event-to-presented visual response within two refresh intervals: about 33.3 ms at 60 Hz or 16.7 ms at 120 Hz | Measure the moved clip, note, selection box, or parameter indicator; smooth movement of the OS cursor alone does not count |
| Editing correctness | No lost press/release events, accumulated drag lag, incorrect final positions, or late selection jumps | Replay pointer gestures; verify capture, snap, final committed state, cancellation, and undo |
| Audio coexistence | UI load introduces no audio deadline misses on a workload with established audio headroom | Compare the same session with and without scripted editing; retain underrun counts and callback traces |

Input-handler timing or frame submission is only a partial latency measurement. Label those endpoints explicitly. If reliable presentation timestamps are unavailable, report the partial measurement and use an external high-speed-camera test for end-to-end response. OS-event-to-presentation excludes physical mouse polling and panel response; do not label it input-to-photon latency. Measure parameter-to-audible-change latency separately from visual pointer feedback.

Use the same dense session and gesture traces for every frontend candidate: move and resize clips, drag piano-roll notes, marquee-select, adjust knobs/faders, scrub, resize panels, and zoom/scroll through cached waveforms. Run with playback, recording, background waveform generation, and saving. Include high-DPI scaling and display transitions on Windows and macOS. Record resolution, refresh rate, pointer polling rate, audio buffer size, hardware, and power mode so comparisons are meaningful.

Implementation constraints: draw drag previews promptly without waiting for disk work or an audio-thread round trip; reconcile previews with authoritative model acknowledgements. Reuse cached geometry and update affected regions. Coalesce redundant pointer motion for rendering when appropriate, while preserving gesture boundaries, final positions, and correctly timestamped automation events. Do not add easing or a trailing animation to direct manipulation. Keep expensive analysis off the UI thread and avoid unnecessary continuous redraw when idle. GPU acceleration is a candidate technique, not proof that these gates pass.

Keep keyboard access for all essential commands, visible focus, readable tooltips and labels, adjustable scaling, and adequate contrast as acceptance requirements alongside these performance gates.

Validate on Windows and macOS from the first native proof: trackpad scrolling and pinch behavior, modifier-key conventions, high-DPI transitions, native dialogs, text entry, and audio-device changes. A Windows-only screenshot cannot establish cross-platform usability.
