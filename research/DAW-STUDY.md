# Rhino: lessons from existing DAWs

Research date: 2026-09-08. Scope: focused source-code and primary-documentation analysis, not a runtime benchmark or a full audit of these applications.

## Recommendation

Rhino should have a native C++ audio engine, an extensible internal device system, and an interface designed around composing complete songs. Evaluate Tracktion Engine before constructing our own scheduler, recording pipeline, automation system, and routing graph. Compare native JUCE and Qt Quick for the production interface using the same representative editing workload.

This recommendation addresses the clarified requirements: Windows and macOS from the start; personal use; electronic composition plus recording; large projects; instruments and effects implemented as replaceable modules. Third-party VST hosting is an optional future adapter, not the definition of modularity.

## Evidence and limits

Four repositories were indexed at exact commits, and 38 selected source/reference files were downloaded. The review concentrated on engine boundaries, processing stages, device contracts, parameter state, media access, and UI integration. [SOURCE_MAP.md](SOURCE_MAP.md) links every file to its immutable upstream revision; [sources/manifest.json](sources/manifest.json) records hashes.

These are development-branch snapshots, not a claim about every released version. No upstream application was built, run, or benchmarked in this research pass. Public manuals and engineering documentation describe Ableton and Bitwig behavior; their private implementation details remain unknown. Recommendations below are engineering judgments derived from the evidence, not claims that we can duplicate those products' internals.

## Open-source comparison

| Project | What the evidence shows | What Rhino should learn | Suitability as our starting point |
| --- | --- | --- | --- |
| Ardour | Separate audio backend, session processing, processor abstraction, region/source model, disk readers, and dependency-based graph processing | Native device access, non-destructive media editing, background disk work, explicit latency and routing | Strong implementation reference; extracting its whole application engine would require integration work |
| LMMS | Distinct instrument/effect interfaces, automatable models, staged note/instrument/effect/mix processing | Musical modules need shared parameter and lifecycle contracts | Strong electronic-composition reference; its scheduling choices should not be adopted without measuring our workload |
| Zrythm | Current source combines C++23, Qt/QML, JUCE, DSP graph scheduling, and separate UI adapters | Declarative native UI can coexist with C++ audio; distinguish base, automated, and modulated parameter values | Useful modern UI/engine reference; current migration status makes feature completeness an explicit evaluation question |
| Tracktion Engine | An embeddable sequenced-audio engine with session objects, device registration, graph nodes, automation, media facilities, and launcher integration | Reuse engine infrastructure while owning Rhino's interaction design | Leading engine candidate; validate a pinned release against our use cases before committing |

The following sections connect these conclusions to actual files.

### Ardour: a region is an edit, not another audio file

`Region` distinguishes the position on the timeline, the offset inside the source, and the region's length. That separation is fundamental: splitting or moving a clip should change references and bounds, without copying all its audio. [Source A3](SOURCE_MAP.md#a3)

Its `Processor` abstraction includes buffer processing, channel configuration, activation, automation, and latency. The `Graph` exposes route processing and a queue of nodes ready for execution. `DiskReader` uses playback buffers, reports underruns, and requests background refill work. [Sources A2, A4, A6](SOURCE_MAP.md#a2)

**For Rhino:** keep immutable media separate from editable clip instances; make routing and latency explicit; budget streaming buffers; report disk underruns separately from DSP overload. A “track” must not be a UI row that also owns all decoded samples.

Ardour's older transport-design page describes UI requests being split between real-time work and background transport/disk work. Its JACK-specific wording should not be mistaken for a complete description of current backend support; the current `AudioEngine` header is the relevant source for that boundary. [Transport design](https://ardour.org/transport_threading.html), [Source A1](SOURCE_MAP.md#a1).

### LMMS: modular sounds need a coherent host contract

LMMS separates instruments from effects, with note/MIDI handling and release behavior on the instrument side, and buffer processing/bypass behavior on the effect side. `AutomatableModel` also carries parameter ranges, controller relationships, and persistence. [Sources L2–L4](SOURCE_MAP.md#l2)

The engine's rendering path has distinct note-setup, instrument, effect, and mixing stages. The inspected `renderNextPeriod` acquires `m_changeMutex`: existing C++ software is not automatically free of lock-related real-time tradeoffs. This observation is not a claim that LMMS always glitches; it identifies a design choice requiring context and measurement. [Source L5](SOURCE_MAP.md#l5)

**For Rhino:** define devices and automatable parameters centrally. Avoid adding a special track type and new engine conditionals for every synth. Do not confuse use of C++ with proof of predictable execution.

### Zrythm: modern interfaces do not require a browser

The current README identifies C++23, Qt/QML, and JUCE. It also explicitly lists capabilities not yet ported from v1. That qualification matters more than a broad feature list. [Source Z10](SOURCE_MAP.md#z10)

The DSP source has a graph scheduler and processor abstraction. The playhead has a QML-facing wrapper with a timer, while the parameter model distinguishes user edits from value synchronization and exposes base/automated/modulated values. Its timeline uses QML list/delegate structures; waveform rendering has a dedicated native renderer. [Sources Z2, Z4–Z6, Z11–Z12](SOURCE_MAP.md#z2)

An `engine-process` directory exists, but the inspected application file contains disabled IPC code and unfinished setup. We must not cite the directory name as proof of a finished process-isolated engine. [Source Z9](SOURCE_MAP.md#z9)

**For Rhino:** include Qt Quick in the UI shortlist, publish model changes through deliberate adapters, and preserve the distinction between a knob's base position and its effective modulated value. Its code is a reference, not a reason to inherit its entire application or assume completed migration features.

### Tracktion Engine: the strongest reuse candidate

The `Edit` model groups tracks, clips, devices, tempo, automation, racks, transport, and undo. The plugin interface provides processing context, preparation, channel topology, tails, and automation integration. A separate manager registers built-in plugin types, which closely matches the user's intended extensibility. [Sources T1–T2, T9](SOURCE_MAP.md#t1)

At the processing level, nodes expose input dependencies, readiness, sample-range context, channel count, and latency. Preparation is distinct from processing, and the processing contract forbids resizing its provided buffers. A dedicated arranger/launcher switching node demonstrates that these workflows have an engine-level relationship. [Sources T3–T5](SOURCE_MAP.md#t3)

Its audio-file cache offers readers with timeout parameters. That is a useful facility, but safe use still requires inspecting the selected backend and ensuring the callback never waits on disk. [Source T10](SOURCE_MAP.md#t10)

**For Rhino:** adapt our device and editor model around a proven engine where practical. Do not build a second competing session model merely to claim independence. Establish thin application services that expose editing operations and stable IDs; use the engine's existing undo and automation mechanisms when they meet requirements.

The repository documents separate engine/JUCE licensing. For this personal project it is a dependency-selection detail to record, rather than an assumption that commercial licensing is required now. No upstream code has been incorporated into the app. [Repository licensing](https://github.com/Tracktion/tracktion_engine#license).

## What makes Ableton work as an instrument

### One song, two ways to work

Ableton's Session View supports launching clips and scenes; those actions can be recorded into Arrangement View. The views therefore participate in one musical project rather than creating independent songs. [Session View manual](https://www.ableton.com/en/manual/session-view/).

**Rhino design implication:** keep a single set of tracks, devices, and media. Give each track explicit arrangement-versus-launcher playback ownership. Display queued, playing, stopped, and overridden states. Recording a performance should produce editable arrangement events. Switching views must preserve selection and playback.

### Sound design is compositional

Ableton racks combine serial/parallel device chains, playable zones, and macro mappings. Bitwig documents a unified modulation system that can expose the effective parameter value while allowing control of the underlying setting. [Ableton racks](https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/), [Bitwig modulation](https://www.bitwig.com/userguide/latest/the_unified_modulation_system/).

**Rhino design implication:** a “sound” is a preset for a device or rack, not another class of track. Support stable parameter identities and consistent mapping behavior first. Introduce simple serial chains, then parallel racks and macros. A modulator should be a reusable module rather than an LFO hard-coded into every control.

### Audio has musical time as well as source time

Ableton's warp controls let users change audio timing, including tempo synchronization and independent pitch behavior depending on mode. [Warping manual](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/).

**Rhino design implication:** store clip placement in musical time separately from sample offsets and source tempo. Distinguish “original speed,” “follow tempo,” and repitch behavior in the model and UI. Avoid promising every stretch algorithm initially; evaluate a maintained implementation using drums, vocals, sustained tones, and full mixes.

### Performance is constrained by the longest dependent path

Ableton documents parallel processing of independent signal-path segments and serial processing where data dependencies require it. Its CPU meter measures proximity to the audio deadline rather than the operating system's overall CPU percentage. It also describes a responsiveness-versus-throughput tradeoff. [Ableton multicore explanation](https://help.ableton.com/hc/en-us/articles/209067649-Multi-core-performance-in-Ableton-Live-FAQ).

**Rhino design implication:** benchmark routing shapes, not just track totals. Test many independent tracks, deep instrument/effect chains, groups, and sidechains. Keep low-latency playing/recording responsive; use freeze and offline render for heavy arrangements. Show audio load and disk pressure meaningfully.

### Audio quality needs executable evidence

Ableton publishes cancellation-based checks for operations intended to preserve audio, and distinguishes those from processing that deliberately changes it. [Audio Fact Sheet](https://www.ableton.com/en/live-manual/12/audio-fact-sheet/).

**Rhino design implication:** write reference tests for unity-gain playback, splitting, routing, bypass, render equivalence, and latency alignment. Use impulses, sine waves, sweeps, noise, and musical fixtures. A pleasing demo is not proof of correctness; language choice does not establish sound quality.

## Interface architecture: a measured shortlist

| Candidate | Why evaluate it | What must be proved |
| --- | --- | --- |
| JUCE native interface | Direct fit with the leading engine candidate and native audio tooling | Dense timeline rendering, typography, accessibility, trackpad zoom, and interaction polish |
| Qt Quick/QML + C++ rendering | Declarative layout and a native scene graph with custom geometry; relevant Zrythm precedent | Qt/JUCE event-loop integration, thread ownership, deployment footprint, platform input behavior, and renderer scalability |
| WebView + C++ engine | Can reuse the current interface code and established web design tooling | Visible-region rendering, memory use, bridge behavior, native windows, and consistent Windows/macOS interaction |

Qt documents explicit render-thread rules for custom scene-graph work. JUCE documents WebView integration and platform differences. Neither framework choice alone demonstrates a responsive large-project editor. [Qt scene graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html), [JUCE WebView integration](https://juce.com/blog/juce-8-feature-overview-webview-uis/).

**Working preference:** evaluate JUCE native first for engine integration, and Qt Quick as the strongest alternative for a highly customized visual workspace. Preserve WebView reuse as an option. Select by the same reference scene and measurements; do not declare a winner from screenshots or sunk implementation time.

## Concrete next experiment

Build one narrow native engine proof before migrating screens: streamed audio plus a MIDI instrument, one internal effect, automation of that effect, recording, undoable edits, project restoration, and offline rendering. Run the same project on Windows and macOS. It must keep playing while the UI is busy or absent.

Then compare a representative timeline/device-panel slice across the leading UI candidates. Use the interaction requirements in [UX-SPEC.md](UX-SPEC.md), the modular contract in [DEVICE-SYSTEM.md](DEVICE-SYSTEM.md), and the acceptance gates in [ROADMAP.md](ROADMAP.md).

The native engine evaluation now carries the active implementation work.
