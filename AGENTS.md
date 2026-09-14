# Theta project memory

Theta is a native desktop DAW: C++20, Tracktion Engine, JUCE. The application lives entirely in `native/src`. See [the root README](README.md) for what it does, [ARCHITECTURE.md](ARCHITECTURE.md) for direction, and [native/README.md](native/README.md) for implementation contracts.

> **Read [HANDOVER.md](HANDOVER.md) first if you last worked on this before commit `105cd78`.** `Session.cpp`, `StepGrid.cpp` and `Arrangement.cpp` were split into focused files in September 2026. No header, signature or call site changed, but the file you remember editing has probably moved. HANDOVER.md maps every move and lists the issues currently being inherited.

## Working preferences

- Build a polished native desktop DAW. Prioritize Windows while preserving macOS portability.
- Treat UI frame pacing and immediate pointer response as core requirements.
- Preserve existing native work. Avoid large mock projects or benchmark scaffolding unless requested.
- The product is Theta and the repository is https://github.com/monkjuice/THETA.git. The local workspace may still be named `theda`.

## Do not read these directories

Two large trees in this workspace are not Theta's code. Reading or searching them wastes context and returns misleading results.

- **`native/.deps/`** — pinned JUCE and Tracktion checkouts, 400,000+ lines, fetched by a script and ignored by git. Never grep or glob here. To understand engine behaviour, read `native/README.md` first, then the curated snapshots below.
- **`research/sources/`** — read-only snapshots of Ardour, LMMS, Zrythm and Tracktion source, about 30,000 lines, kept as study material. It is tracked by git and therefore **is** searched by default, so exclude it deliberately. Reach it only through the index in [research/SOURCE_MAP.md](research/SOURCE_MAP.md), and only when prior art is explicitly wanted.

Neither tree is compiled or imported. Nothing in either is ever the answer to "where is this implemented".

## Where things live

Application code, `native/src`:

| Area | Files |
| --- | --- |
| Session model and engine ownership | `Session.h` declares everything; `Session.cpp` holds construction, project load/save and undo. Implementation is split across `Session*.cpp` by responsibility — notes, devices, automation, clips, presets, tracks, transport. |
| Note grid UI | `StepGrid.*` — the 16-step pattern editor |
| Arrangement UI | `Arrangement.*`, `ArrangementGeometry.cpp`, `ClipGeometry.h` |
| Session view (clip launcher) UI, paused, see [SESSION-VIEW.md](SESSION-VIEW.md) | `SessionView.h`, `SessionView.cpp`, `SessionViewPainter.cpp`, `SessionViewGestures.cpp` |
| Scenes, clip slots and launching | `SessionSlots.cpp` |
| Mixer: track volume, pan, mute, solo, main output | `SessionMixer.cpp` |
| Device rack and editors | `DeviceRack.*` |
| Browser | `BrowserPanel.*` |
| App shell and lifecycle | `Main.cpp` |
| Project files | `ProjectFiles.*` |
| Playhead rendering | `Playhead.*` |
| Built-in devices | `UtilityDevice`, `DrumDevice`, `ThetaArpDevice`, `ThetaBloomDevice`, `ThetaSpaceDevice`, `ThetaWaveDevice` |
| Theme | `Theme.h` |

The UI depends on `Session`; `Session` knows nothing about the UI. Keep that direction.

A track has exactly one instrument, matching Live and Logic. Dropping an instrument replaces the one already there and removes it; the track's clips are untouched, so the pattern survives the swap. Never cache an instrument pointer across a switch.

The session view and the arrangement are two presentations of one project, not two documents. Tracks, devices, the mixer and the transport are shared because both views read the same `Session`; never let a view cache a copy of that state. Selection, focus, scroll and zoom are per-view and should stay that way. Clips are the one thing that genuinely differs: slot clips belong to scenes, timeline clips belong to the arrangement, exactly as in Live. The session view is currently switched off in the shell: `sessionViewEnabled` in `Main.cpp` gates the control-bar switch and the Tab shortcut, while the model and tests keep running. Read [SESSION-VIEW.md](SESSION-VIEW.md) before touching any of it. Because the two sets of clips are separate, the only way between them is to copy: `copySlotClipToArrangement` and `copyClipToSlot` in `SessionSlots.cpp`, reached from the right-click menu in either view. Neither view should ever try to display the other's clips.

Every source file is listed explicitly in `native/CMakeLists.txt` — nothing is globbed. A new `.cpp` needs a line there or it silently will not compile.

## Keeping files small

Split a `.cpp` when it passes roughly 600 lines or gains a second responsibility. Prefer the mechanism already used here: **define one class across several translation units**, as `SessionTransport.cpp` does for `Session` and `ArrangementGeometry.cpp` does for `Arrangement`. That needs no header change, no change at any call site, and preserves `friend` declarations used by tests — only a new line in `CMakeLists.txt`.

When those units need to share helpers that were previously in an anonymous namespace, put them in a `*Internal.h` header next to the class — `SessionInternal.h`, `StepGridInternal.h`, `ArrangementInternal.h`. Those headers are private to the class's own translation units; nothing else should include them. Helpers shared by *different* classes get a normal header instead, as `BrowserIds.h` does for the arrangement and the device rack.

Extract a genuinely new type only when it buys testability. `ClipGeometry.h` is the model: a pure header with no JUCE or engine dependency, unit-tested in 40 lines without a `Session`.

Two classes are still defined inline inside a single `.cpp` and are the next things worth separating: `ControlWindow` in `Main.cpp` and `FloatingDeviceWindow` in `DeviceRack.cpp`. Splitting either means converting inline method bodies to declaration plus definition, which is a real restructuring rather than a file move — do it deliberately, not as a side effect of another change.

## Tests

Five CTest cases, all the same binary with different flags. Build and run:

```powershell
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

Workflow scenarios live in `native/src/tests/*/scenarios/*.inc`. They are bare statement blocks included inside a runner function, not translation units — they share one `Session`, run in order, and may depend on state from an earlier scenario. The first failure aborts the whole runner, so you get one message rather than a list. Add a scenario by including it in the runner; `.inc` files are deliberately absent from CMake.

Prefer a unit test over a scenario whenever the code under test needs no `Session`, render, or pointer sequence.

## Commit and push workflow

- The user explicitly wants regular pushes and good Git practices. At meaningful completed milestones, make focused commits and push to the current branch's upstream. Routine commits and pushes are authorized without repeated confirmation.
- Before committing, inspect status and the diff, run relevant existing checks, and ensure the commit contains only intended changes. Preserve unrelated user edits and never include secrets, build output, caches, or downloaded dependencies.
- Use clear commit messages describing the result. Avoid accumulating a large amount of completed work locally; push verified milestones before handing them back to the user.
- Confirm the destination remote and branch before pushing. If the remote has moved, inspect and reconcile safely; never force-push or rewrite shared history without explicit authorization.
- Match validation to the change: native behavior changes need relevant native checks; documentation-only edits need a diff review, not a full build. Do not add tests that merely mirror low-impact changes.
- Report what was committed and pushed, the relevant validation, and any blockers. Never describe a local commit as pushed until the push succeeds.
