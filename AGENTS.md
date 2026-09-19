# Rhino project memory

Rhino is a native desktop DAW: C++20, Tracktion Engine, JUCE. The application lives entirely in `native/src`. See [the root README](README.md) for what it does, [ARCHITECTURE.md](ARCHITECTURE.md) for direction, and [native/README.md](native/README.md) for implementation contracts.

> **Read [HANDOVER.md](HANDOVER.md) first if you last worked on this before commit `105cd78`.** `Session.cpp`, `StepGrid.cpp` and `Arrangement.cpp` were split into focused files in September 2026. No header, signature or call site changed, but the file you remember editing has probably moved. HANDOVER.md maps every move and lists the issues currently being inherited.

## Working preferences

- Build a polished native desktop DAW. Prioritize Windows while preserving macOS portability.
- Treat UI frame pacing and immediate pointer response as core requirements.
- Preserve existing native work. Avoid large mock projects or benchmark scaffolding unless requested.
- The product is Rhino and the repository is https://github.com/monkjuice/Rhino.git.

## Do not read these directories

Two large trees in this workspace are not Rhino's code. Reading or searching them wastes context and returns misleading results.

- **`native/.deps/`** — pinned JUCE and Tracktion checkouts, 400,000+ lines, fetched by a script and ignored by git. Never grep or glob here. To understand engine behaviour, read `native/README.md` first, then the curated snapshots below.
- **`research/sources/`** — read-only snapshots of Ardour, LMMS, Zrythm and Tracktion source, about 30,000 lines, kept as study material. It is tracked by git and therefore **is** searched by default, so exclude it deliberately. Reach it only through the index in [research/SOURCE_MAP.md](research/SOURCE_MAP.md), and only when prior art is explicitly wanted.

Neither tree is compiled or imported. Nothing in either is ever the answer to "where is this implemented".

## Where things live

Application code, `native/src`:

| Area | Files |
| --- | --- |
| Session model and engine ownership | `Session.h` declares everything; `Session.cpp` holds construction, project load/save and undo. Implementation is split across `Session*.cpp` by responsibility — notes, devices, automation, clips, presets, tracks, groups, transport. |
| Note grid UI | `StepGrid.*` — the 16-step pattern editor |
| Arrangement UI | `Arrangement.*`, `ArrangementGeometry.cpp`, `ClipGeometry.h` |
| Track groups: the model and the arrangement's band | `SessionGroups.cpp`, `ArrangementGroups.cpp` |
| Session view (clip launcher) UI, paused, see [SESSION-VIEW.md](SESSION-VIEW.md) | `SessionView.h`, `SessionView.cpp`, `SessionViewPainter.cpp`, `SessionViewGestures.cpp` |
| Scenes, clip slots and launching | `SessionSlots.cpp` |
| Mixer: track volume, pan, mute, solo, main output | `SessionMixer.cpp` |
| Device rack and editors | `DeviceRack.*` |
| Browser | `BrowserPanel.*` |
| App shell and lifecycle | `Main.cpp` |
| Project files | `ProjectFiles.*` |
| Playhead rendering | `Playhead.*` |
| Built-in devices | `native/src/devices/`, split `instruments/`, `audio/`, `midi/`. Declared once in `DeviceCatalog.cpp` |
| Sample and content library | `library/` at the repository root, found by `native/src/core/ContentLibrary.h` |
| Theme | `Theme.h` |

The UI depends on `Session`; `Session` knows nothing about the UI. Keep that direction.

`native/src` builds as three targets, not one: `RhinoCore` (`src/core`, JUCE only), `RhinoDevices` (`src/devices`, Tracktion and `RhinoCore`) and the application. A device may not include `Session.h` or any UI header, and `Session.h` reaches devices only through `DeviceCatalog.h`. That is what keeps editing a device from rebuilding the app, and it is worth preserving as more MIDI and audio FX arrive.

## Content is files, never compiled in

Samples, and the presets and patterns that will join them, live under `library/` at the repository root and are found at runtime by `ContentLibrary`. Nothing there is compiled. `juce_add_binary_data` expands an asset to roughly three bytes of C++ per byte of data and rebuilds all of it on a clean build, which is why `native/assets/` is now only the fonts and app icons — what the UI needs before it can read from disk. Audio under `library/` is stored with Git LFS, declared in `.gitattributes`; set that up *before* adding a new content type, because converting afterwards means rewriting history.

A device reading its own samples does so in `initialise()`, which is prepare-to-play and never the audio callback, and must survive the files being absent — `DrumDevice` logs and falls silent. Measured cost of that read for the TR-808 kit: 525 KB in 1.2 ms warm, once per device instance.

## Adding a device is three edits

A device is its source under `native/src/devices/`, a line in that target's `CMakeLists.txt`, and an entry in `DeviceCatalog.cpp`. The catalog carries the id, engine type name, display name, kind, browser group, blurb, colour and pattern key, and the browser, the drop targets, the rack's add menu, the engine registration and the instrument rules all read it. Nothing in `Session` or the UI should ever need editing to add one; if it does, that is the bug. `--self-test` holds the line by asserting every browsable catalog device has a browser row and can be added.

Never self-register a device from a static initialiser: a static library's unreferenced objects are dropped by the linker and the registration disappears without an error.

`Session::addDevice(id, track)` is the one way to add a device. The `Instrument`, `AudioEffect` and `MidiEffect` enums survive only as shorthand for the devices that predate the catalog, converted to ids in `DeviceIds.h` and nowhere else. New devices get no enum.

The main track is addressed as `trackCount()`, one past the last audio track, so every track-indexed call reaches it without a sentinel that `DeviceTarget` would read as invalid. `Session::pluginListForTrack` resolves that index to either a track's plugin list or the edit's master list. It carries effects only; the engine's `canBeAddedToMaster` is what refuses the rest.

A new document is one empty track named `Track 1`, plus the pinned main row. That track runs no instrument, so it is neither a MIDI nor an audio track until something lands on it: an instrument makes it one and renames it, a sample makes it the other. Nothing else is created, so no index is reliably "the audio track" — `importAudio` with no target adds a track of its own, and anything else that needs one must say which.

A track has exactly one instrument, matching Live and Logic. Dropping an instrument replaces the one already there and removes it; the track's clips are untouched, so the pattern survives the swap. Never cache an instrument pointer across a switch. An instrument drop changes the track and its name and nothing else: it does not create a clip. Clips are created by double-clicking a lane or pressing Ctrl+A, and only tracks that run an instrument can hold them.

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

## Debugging a crash that only one project file triggers

`RhinoDAW.exe` opens a `.rhinoedit` passed as its first argument (see `Application::initialise`), which turns "it crashes when I open my project" into a headless repro that runs in about ten seconds. Everything below follows from having that loop.

**Name the faulting module before blaming anything.** Windows records it, and it is the difference between debugging Rhino and debugging a plugin:

```powershell
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddHours(-6)}
```

A project that loads a VST invites the assumption that the VST is at fault. The log said `RhinoDAW.exe` faulting in `RhinoDAW.exe`, which ruled that out in one step. The same entry gives a fault offset, and an identical offset across attempts means the crash is deterministic and worth bisecting rather than a race.

**Bracket it with `rhino.log`** (`%APPDATA%\Rhino\rhino.log`). `ProjectFiles.cpp` writes `Opening <name>...` before the load and `Opened <name>` after it, so a session with the first and not the second puts the fault inside that call. A different project opening successfully in the same session is the strongest possible hint that the data, not the build, is the trigger.

**Bisect the XML, and always run a known-good control.** Strip one structure at a time — notes, plugin nodes, plugin state — and re-run. Keep a project that opens in the rotation every time: a harness that silently stops reproducing is worse than no harness. In the case this was written for, removing every `<NOTE>`, the whole VST node, and switching `rhinoPatternInstrument` changed nothing, which is what pointed at track *count* — one audio track against the demo's four, and an unguarded `tracks[1]`.

**Re-run before believing a non-crash.** A single clean run is noise: a launch can sit on a dialog or lose a race and look like success. Two orders and a repeat of the same file cost a minute and prevent a wrong conclusion. One such false pass nearly sent this investigation at the wrong file.

`juce::Array::operator[]` returns a default-constructed value for an out-of-range index rather than asserting in Release, so an out-of-bounds track or plugin lookup surfaces as a null dereference somewhere later. When a guard like `if (tracks.size() > 1)` protects one access, check every other access to the same index — the bug here was a read that was guarded and a write that was not.

## When the same source renders differently on two machines

Graphics drivers carry per-application profiles keyed on the **executable's
filename**, and they apply them to any binary that happens to match. Naming the
app `Rhino.exe` handed it NVIDIA's profile for Rhinoceros 3D, the CAD package,
whose settings corrupt this app's Direct2D repaints: every interaction left
stale regions behind, and they accumulated until the window was unreadable.
`PRODUCT_NAME` in `native/CMakeLists.txt` is therefore `RhinoDAW`, and the
product name the user sees comes from `getApplicationName()` instead.

The profile database is readable, so a suspected collision can be confirmed
rather than guessed -- search `%ProgramData%\NVIDIA Corporation\Drs\*.bin` for
the executable name encoded as UTF-16LE.

Two habits earned this one, and both generalise:

**Copy the binary under a second name before theorising.** Byte-identical files
differing only in filename isolate the environment from the build in one step.
A hash of both is the whole proof, and it costs seconds.

**A machine that works is a control, not a consolation.** The laptop rendered
correctly throughout. That was not luck: it has hybrid graphics, so a 2D app
runs on the Intel iGPU and no NVIDIA profile is ever consulted. Comparing the
two machines' GPUs is what put the driver in frame at all.

Timelines lie. An OS update and a GPU driver each looked convincing here
because they sat near the right dates; both were wrong. What settled it was
restoring the deleted folder and running the old binary beside the new one.
Prefer a control you can execute over a correlation you can only argue.

## Verifying pitch, level or timbre

Claims about what the synth *sounds* like get settled by measuring rendered audio, never by reading the DSP. `tuningSuite()` in `ForgeTests.cpp` is the pattern: render through `Core`, FFT it, find the fundamental by peak interpolation, compare against the note's nominal frequency. It cannot agree with the oscillator by construction because it never reads the phase accumulator, which is exactly what makes it worth having.

The same method works on files. A `.wav` decodes with `wave` plus `numpy`, and `ffmpeg` for anything else is bundled with software already installed here — search for `ffmpeg.exe` under `%LOCALAPPDATA%\Programs` before concluding it is unavailable. To tell a played note from a harmonic, compare a partial's amplitude against the ratio its position in the series predicts: a saw's second harmonic sits at 0.50 of the fundamental, so a measured 0.46 is a harmonic and a measured 0.99 is a note sitting on top of one.

## Commit and push workflow

- The user explicitly wants regular pushes and good Git practices. At meaningful completed milestones, make focused commits and push to the current branch's upstream. Routine commits and pushes are authorized without repeated confirmation.
- Before committing, inspect status and the diff, run relevant existing checks, and ensure the commit contains only intended changes. Preserve unrelated user edits and never include secrets, build output, caches, or downloaded dependencies.
- Use clear commit messages describing the result. Avoid accumulating a large amount of completed work locally; push verified milestones before handing them back to the user.
- Confirm the destination remote and branch before pushing. If the remote has moved, inspect and reconcile safely; never force-push or rewrite shared history without explicit authorization.
- Match validation to the change: native behavior changes need relevant native checks; documentation-only edits need a diff review, not a full build. Do not add tests that merely mirror low-impact changes.
- Report what was committed and pushed, the relevant validation, and any blockers. Never describe a local commit as pushed until the push succeeds.
