# Rhino: staged production plan

Status: proposed sequence after source research. No duration or capacity estimate is presented as a measured result.

## Confirmed scope

- Prioritize Windows implementation and validation now; keep the architecture and build portable to macOS for later validation.
- Personal use.
- Electronic music composition first, alongside audio recording.
- Internal devices as the extension mechanism for sounds, effects, and later modulators.
- Large sessions and a polished, coherent interface.
- UI performance, consistent FPS/frame pacing, and immediate pointer response are explicit priorities.
- Preserve useful design findings while moving implementation work into the native app.

Still unspecified: reference Windows/Mac hardware and audio interfaces, typical/max project size, which genres and sound palette to prioritize, and whether arrangement editing or live launching is the first primary workflow. Until clarified, use both in the model and prioritize a complete arrangement workflow for the first native proof. No macOS runtime validation can be claimed from the current Windows workspace.

## Stage 0 — Research and design record

Deliverables: [DAW-STUDY.md](DAW-STUDY.md), [SOURCE_MAP.md](SOURCE_MAP.md), [DEVICE-SYSTEM.md](DEVICE-SYSTEM.md), [UX-SPEC.md](UX-SPEC.md), and this plan. Preserve source snapshots.

Exit: the engine recommendation, interface shortlist, and unknowns are explicit. This stage is complete as a source/documentation study; runtime comparisons are still pending.

## Stage 1 — Engine evaluation on both platforms

Pin a suitable Tracktion/JUCE release and dependencies. Establish reproducible CMake builds on Windows and macOS, with a small headless test target. Evaluate the ability to add our own device without changing the sequencer. Use a minimal native control window only for audio-device selection and transport.

Prove streamed audio, MIDI playback, a synth plus utility effect, automation, recording, save/reopen, and offline render. Verify the chosen engine's callback behavior, graph rebuilds, cache configuration, and latency model rather than relying solely on documentation.

Exit: identical session fixtures work on both platforms; audio continues when the UI is busy; no missing-media/state surprises; measured callback and memory data recorded. Record an ADR adopting the engine or explaining a concrete failure. A custom engine remains a fallback, not an assumed prerequisite.

## Stage 2 — Interface selection and visual proof

Compare JUCE native and Qt Quick using the same timeline/device-panel scene and the engine from Stage 1. A full rewrite in multiple frameworks is unnecessary: implement a focused vertical slice.

Include typography, splitter resizing, trackpad zoom, marquee selection, note dragging, device parameters, and UI scaling. Verify event-loop ownership and native windows. Use both sparse and dense sessions. Compare interaction latency, memory, frame-time distribution, build/deployment complexity, and accessibility. Run the same 60 Hz / 120 Hz presentation and pointer-response traces defined in [UX-SPEC.md](UX-SPEC.md#frame-pacing-and-pointer-response), during audio playback and background work. Report p95/p99 and worst stalls as well as average FPS; a smoothly moving system cursor does not demonstrate responsive application controls.

Exit: choose one production frontend with measurements and screenshots. A candidate must satisfy the agreed frame-pacing, pointer-response, and audio-coexistence gates before visual preference decides between passing candidates. Fix the interaction model before multiplying screens and controls.

## Stage 3 — One complete song workflow

Implement project/media management, arrangement editing, MIDI input/recording, the drum/piano editors, device chains, basic mixer/routing, undo, recovery, and export.

A small set of good devices is enough: sampler/drum rack, polyphonic synth, utility, filter/EQ, delay. Reuse the demo as a workflow fixture, while acknowledging that the native sound implementation may differ.

Exit: create, record, arrange, save, reopen, and export a complete song on both platforms without using a developer console or editing project files manually.

## Stage 4 — Composition and sound-design depth

Add launcher/scenes and performance capture, better quantization and groove, tempo-following audio, automation gestures, macros, parallel racks, sends/groups, and freeze/bounce. Extend the common device contract rather than adding ad hoc behavior.

Exit: switching between composing and arranging is predictable, audio timing is correct, and device state survives editing and recovery. Evaluate time-stretching quality with musical material rather than just sine-wave timing.

## Stage 5 — Scale, reliability, and finish

Harden long recording, streaming, stress under editing, device hot-plug, sleep/wake, missing media, undo growth, file migration, and failure recovery. Improve shortcuts, discoverability, rendering, and visual consistency based on actual use. Third-party plugin hosting enters here only if wanted, with its own scan/isolation/editor/compatibility plan.

Exit: pass the agreed regression suite and complete representative songs. The goal is dependable musical work, not a count of implemented toolbar buttons.

## Measurement matrix

These are proposed future targets and fixtures, not claims of supported capacity. Per the user's direction, do not generate large mock sessions or build a benchmark harness now. Implement with these constraints in mind and use small correctness checks during the initial native work.

| Dimension | Fixture | Evidence to retain |
| --- | --- | --- |
| Deadline stability | 48 kHz at 64/128/256-frame buffers; independent and deep serial routing | Callback distribution/maxima, overruns, device/driver/configuration; buffer duration is not round-trip latency |
| Streaming | 32/64/128 stereo tracks, 5–30 minute media, cold and warm caches | Working set, I/O rate, read-ahead fill, underruns, seek recovery |
| DSP load | Fixed voices, specified device chains, automation, groups and sidechains | Audio load by path and device, voice limits, thermal behavior |
| UI scale | 10,000 clips and 100,000 notes; rapid zoom/scroll/edit while playing | Frame-time distribution, input feedback, memory, stale selection/state failures |
| Frame pacing and pointer response | Identical 10-minute gestures at 60/120 Hz; high DPI; playback, recording, saving, and background analysis | Presented FPS, missed refreshes, p95/p99 and longest frames, event-to-present latency, final gesture correctness, audio underruns; see UX specification for proposed gates |
| Recording | 30-minute capture and repeated start/stop with backing tracks | Missing samples, timestamp alignment, measured latency correction, recovery files |
| Correctness | Impulses, tones, noise, reference MIDI and deterministic devices | Null/difference tests, event positions, latency alignment, render equivalence |
| Persistence | Media relocation, missing devices, older schema, interrupted save | Recovery behavior, state round trips, preserved source files |
| Platform behavior | Windows and macOS, high DPI, audio-device changes, sleep/wake | Build logs, screenshots, interaction results, audio tests on physical hardware |

Do not promise “unlimited tracks.” Report workload and hardware. A deep serial chain can miss deadlines even when average CPU usage is low, as [Ableton documents](https://help.ableton.com/hc/en-us/articles/209067649-Multi-core-performance-in-Ableton-Live-FAQ).

## Prototype status

The web source has been removed from the active tree. A small native engine evaluation now lives in `native/`; no production migration or packaged release is complete.

## Long-Term Instrument Plan: Rhino Wave

Rhino Wave is the long-running path toward a built-in wavetable instrument in the spirit of Ableton Wavetable, adapted to Rhino's simpler native workflow. The goal is not to clone every feature at once; it is to grow a musical instrument in stable layers.

### Phase 1: Playable Wavetable Core

- Add `Rhino Wave` as a native synth device and browser instrument.
- Implement a small polyphonic voice engine with MIDI note on/off, velocity, amp envelope, output gain, and panic/reset behavior.
- Start with a morphing oscillator that blends basic table shapes: sine, triangle, saw, square, and bright folded/harmonic shapes.
- Expose compact rack macros: Position, Shape, Sub, Cutoff, Resonance, Attack, Decay, Sustain, Release, Unison, Detune, Width, Output.
- Keep CPU and allocation behavior real-time safe inside `applyToBuffer`.

### Phase 2: Musical Presets

- Add browser sounds that use Rhino Wave for pads, plucks, basses, sirens, bells, and soft chords.
- Make presets set the track instrument plus the MIDI clip content, like current 4OSC presets.
- Keep track-device knobs global, Ableton-style. Per-clip variation should come later through clip envelopes/automation, not duplicated hidden device state.

### Phase 3: Visual Device Panel

- Add a dedicated Rhino Wave editor window with waveform display, wavetable position feedback, oscillator controls, filter controls, and envelope curves.
- Keep the inline Device Rack view macro-focused and compact.
- Add preset names and visual state that stay readable at small rack sizes.

### Phase 4: Real Wavetable Content

- Replace the basic analytic shapes with a proper wavetable bank.
- Add table interpolation, anti-aliasing strategy, mip levels or band-limited generation, and importable internal tables.
- Add oscillator extras: bend/fold, sync-like motion, phase offset, noise/sub oscillator, stereo spread, and better unison models.

### Phase 5: Modulation And Clip Envelopes

- Add LFOs, modulation envelopes, velocity/key tracking, and a modulation matrix.
- Add clip envelopes that target device parameters, matching the Ableton-style model: clips automate track/device controls during playback instead of owning separate hidden device copies.
- Let clip envelopes be drawn as zones/curves in the arrangement and editor.

### Phase 6: Production Polish

- Add patch browser, saveable presets, categorized factory sounds, migration/versioning for older projects, parameter automation tests, render null checks, and stress tests.
- Add UI affordances for modulation depth, hover readouts, MIDI learn later if wanted, and better accessibility naming.
