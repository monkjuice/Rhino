---
title: Tracktion Engine with a native JUCE UI
type: decision
summary: Rhino runs on Tracktion Engine with a hand-built JUCE interface, chosen in September 2026 over a custom engine, Qt Quick, WebView or Tauri.
tags: [rhino, architecture, tracktion, juce]
sources: []
updated: 2026-10-03
---

# Tracktion Engine with a native JUCE UI

Rhino's audio model is Tracktion Engine and its whole interface is JUCE components, in C++20 (`CMAKE_CXX_STANDARD 20` in `native/CMakeLists.txt`). The reasoning is in `research/DAW-STUDY.md` (*Recommendation*) and `research/ROADMAP.md`, both from the study of 2026-09-08.

## Context

The first commit held an Electron web prototype beside a small native evaluation. Before writing an engine, the study read source from Ardour, LMMS, Zrythm and Tracktion Engine (snapshots in `research/sources/`). Tracktion came out as the strongest reuse candidate: an `Edit` that groups tracks, clips, devices, tempo, automation and undo, and a plugin manager that registers built-in types, which matched the wish for internal devices as the extension mechanism. The web prototype was deleted the same day (commit `3b1fa06`).

## Decision

- Engine: Tracktion Engine, pinned, rather than "a second competing session model merely to claim independence" (`DAW-STUDY.md`).
- Interface: native JUCE, evaluated first because it integrates directly with the engine and with plugin windows.
- Windows first; macOS kept portable in the architecture but not validated.

## Alternatives considered

- **A custom engine on JUCE**: the fallback if Tracktion failed measured requirements. It has not been needed.
- **Qt Quick/QML**: the principal interface alternative, on Zrythm's precedent. Never prototyped.
- **A JUCE shell with an HTML WebView**: fast visual iteration, but bridge, memory and focus costs. Kept as an option, not taken.
- **Rust/Tauri**: rejected as a second language boundary with no established benefit.

The code has settled the frontend choice: there is no Qt or WebView code, and the app builds with `JUCE_WEB_BROWSER=0` (checked 2026-10-03).

## Consequences

- Tracktion owns edits, playback graphs, streaming, parameters and device state, and the UI never schedules audio. `Session` is a facade over one `te::Edit`, see [Session, the model](session-model.md).
- Rhino's devices are `te::Plugin` types registered by `DeviceCatalog::registerBuiltInTypes` ([Device catalog](device-catalog.md)). Forge arrives through Tracktion's external-plugin wrapper ([Forge is an independent VST3](forge-is-an-independent-vst3.md)).
- Where the engine falls short, Rhino owns that piece instead of patching Tracktion. `RhinoEngineBehaviour` is the only thing Rhino tells the engine about itself, and the count-in is Rhino's own because Tracktion's cannot count in at bar one ([Recording and the count-in](recording.md)).
- `native/scripts/fetch-dependencies.py` pins both revisions by hash: Tracktion `4536d8a`, the revision the study read, and its JUCE `37c894f`. They are fetched into `native/.deps/`, which nobody reads. Tracktion's licence is separate from JUCE's; for a personal project it was a detail to record, not a blocker, and the upstream licence files stay in `.deps`.
- The frame-pacing and pointer-response gates in `research/UX-SPEC.md` are requirements that have never been measured. At the user's direction there is no benchmark harness and no synthetic large session yet.

## Related

- [Overview](../overview.md)
- [Session, the model](session-model.md)
- [Rhino's build targets](rhino-build-targets.md)
- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Directories not to read](off-limits-directories.md)
- [Dependency direction](dependency-direction.md)
