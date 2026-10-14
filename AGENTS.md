# Rhino project memory

Rhino is a native desktop DAW: C++20, Tracktion Engine, JUCE. The application lives entirely in `native/src`. See [the root README](README.md) for the product and setup, and [the project wiki](wiki/index.md) for architecture, implementation contracts, decisions, guides and gotchas.

> **If you last worked on this before commit `105cd78`, read [Keeping files small](wiki/pages/keeping-files-small.md).** `Session.cpp`, `StepGrid.cpp` and `Arrangement.cpp` were split into focused translation units in September 2026. No header, signature or call site changed, but the file you remember editing has probably moved.

## Working preferences

- Build a polished native desktop DAW. Prioritize Windows while preserving macOS portability.
- Treat UI frame pacing and immediate pointer response as core requirements.
- Preserve existing native work. Avoid large mock projects or benchmark scaffolding unless requested.
- The product is Rhino and the repository is https://github.com/monkjuice/Rhino.git.

## Do not read these directories

Two large trees in this workspace are not Rhino's code. Reading or searching them wastes context and returns misleading results.

- **`native/.deps/`** — pinned JUCE and Tracktion checkouts, 400,000+ lines, fetched by a script and ignored by git. Never grep or glob here. To understand engine behaviour, read the relevant [wiki page](wiki/index.md) first, then use the curated snapshots through [research/SOURCE_MAP.md](research/SOURCE_MAP.md) only when prior art is explicitly needed.
- **`research/sources/`** — read-only snapshots of Ardour, LMMS, Zrythm and Tracktion source, about 30,000 lines, kept as study material. It is tracked by git and therefore **is** searched by default, so exclude it deliberately. Reach it only through the index in [research/SOURCE_MAP.md](research/SOURCE_MAP.md), and only when prior art is explicitly wanted.

Neither tree is compiled or imported. Nothing in either is ever the answer to "where is this implemented".

## Where things live

Application code, `native/src`, is laid out by layer, and the layers may only look down: `app` and `ui` see `model`, `model` sees `devices` and `core`, `devices` sees `core`, `core` sees JUCE alone.

```
native/src/
  app/          the shell: Main.cpp (the application object), MainWindow, MainComponent and its units, the control bar, the readout, project files, the typing keyboard, the Ableton Push
  model/        Session.h and the Session*.cpp units, the count-in click
  ui/shared/    Theme.h and the helpers every view draws with (Playhead, ShapedText, ClipDrag, ...)
  ui/arrangement/  ui/notes/  ui/dj/  ui/rack/  ui/browser/  ui/audioclip/   one folder per view
  core/         RhinoCore: dsp/ (the engines), dj/ (the booth's engine), content/ (the library and its file formats)
  devices/      RhinoDevices: sdk/ instruments/ audio/ midi/ infra/, declared once in DeviceCatalog.cpp
  tests/        the runners compiled into the app
```

Each target has one include root, and an include is spelled from the root of the target that owns the header: `"model/Session.h"`, `"ui/shared/Theme.h"` and `"app/ProjectFiles.h"` from `native/src`; `"dsp/EqFilter.h"`, `"dj/DjEngine.h"` and `"content/ContentLibrary.h"` from `core/`; `"audio/RhinoEqDevice.h"` from `devices/`. A header in the same folder is included bare. Files keep their class as a prefix (`ArrangementGestures.cpp`, never `Gestures.cpp`), so every basename stays unique and a class split across units still globs. Tests use the same spellings, never `../`.

| Area | Files |
| --- | --- |
| Session model and engine ownership | `model/Session.h` declares everything; `Session.cpp` holds construction, project load/save and undo. Implementation is split across `model/Session*.cpp` by responsibility — notes, devices, automation, clips, regions, presets, tracks, groups, transport. |
| Note grid UI | `ui/notes/StepGrid.*` — the 16-step pattern editor |
| Arrangement UI | `ui/arrangement/Arrangement.*`, `ArrangementGeometry.cpp`; the clip-edit arithmetic is `model/ClipGeometry.h`, a pure header `Session.h` includes |
| Playing MIDI from the typing keyboard | `app/ComputerKeyboard.*`, wired in `app/MainComponent.cpp`; the notes go in through `Session::sendMidiInputNote` |
| Playing and controlling Rhino from an Ableton Push 1: its pads in key or on a Drum Rack's bank, its display, lights, buttons and encoders. See [Ableton Push 1](wiki/pages/ableton-push.md) | The protocol is `app/PushProtocol.h`, a pure header; the controller is `app/PushController.*` with `PushControllerControls.cpp` (buttons and encoders) and `PushControllerSurface.cpp` (what it shows), owned and wired by `MainComponent`. The model side is `SessionControllers.cpp`: the Push's ports reserved from the engine, its `Push` input, and `sendControllerMidi`, the one `Session` call safe from any thread |
| Recording: arming a track, the count-in, and the clip a take becomes | `SessionRecording.cpp` in the model, `model/CountInClick.*` for the count-in click, the record button in `app/ControlBarButtons.h` and the record dot in `ArrangementSync.cpp` |
| Audio clip editor: one clip's own gain, pan, pitch, fades, mute and reverse | `ui/audioclip/AudioClipPanel.*` in the UI, `SessionAudioClips.cpp` in the model |
| Time warp: making an audio clip follow the song's tempo, its warp mode, its own tempo and its warp markers | `SessionWarp.cpp` in the model, `AudioClipWarp.cpp` in the UI. See [Time warp](wiki/pages/time-warp.md) |
| The control bar: tempo, time signature, transport, the readout | `app/MainComponentControlBar.cpp` lays it out in `layoutControlBar`; the fields you drag are `app/ControlBarFields.*`, the glyphs and the borderless button that wears them are `app/ControlBarIcons.*`, the stop and record buttons and the view switch are `app/ControlBarButtons.h`, and the readout is `app/TransportDisplay.*`. See [App shell and control bar](wiki/pages/app-shell.md) |
| Selecting a span of the timeline, and cut/copy/paste/duplicate on it | `ArrangementSelection.cpp` in the UI, `SessionRegion.cpp` in the model. The note editor uses the same rule in steps, in `StepGridEditing.cpp` |
| Track groups, which are bus tracks | `SessionGroups.cpp`, `ArrangementGroups.cpp` |
| Renaming a track card or a group band in place | `ArrangementRename.cpp` |
| Merging audio clips into one, Ctrl+J | `SessionMerge.cpp` in the model, `Arrangement::mergeSelected` in the UI. It renders, so it writes a file, as Bounce does |
| Bounce to New Track, Ctrl+B: selected clips, tracks or a group rendered to audio on new tracks under them, the sources muted | `SessionBounce.cpp` in the model (a track before its fader, a group after its bus), `ArrangementBounce.cpp` in the UI, and the clip and card menus. Clip mute (`Session::setClipsMuted`, drawn grey by `ArrangementPainter`) is the way back for a bounced clip |
| Auditioning a library sound | `SessionPreview.cpp` |
| Device presets (`.rnd`): saving, loading, adding a device from one | The file format is `core/content/DevicePreset.*`, found by `ContentLibrary::presets()`; the model is `SessionDevicePresets.cpp`; the name-bar menu is `ui/rack/DeviceEditorPanelPresets.cpp`. `SessionPatternPresets.cpp` is the older *pattern* presets. See [Device presets](wiki/pages/device-presets.md) |
| The DJ view: up to six CDJ-style decks either side of a DJM-style mixer, what the Arrange/DJ switch and Tab open. See [DJ view and the booth](wiki/pages/dj-view.md) | The engine is in `core/dj/`: `DjEngine.*` with `DjEngineClock.cpp` and `DjEngineMix.cpp` (decks into a mixer, the command queue, quantised starts, sync), `DjDeck.*` with `DjDeckTransport.cpp`, `DjDeckLoops.cpp` and `DjDeckRender.cpp` (one player: a plan, then a read), `DjReleaseQueue.*` (what the audio thread hands back), `DjTrackPort.*` (the rings a deck crosses into its track's chain and back by), `DjMixer.*`, `DjAnalysis.*` (waveform, tempo, drops, key), `DjTrack.h`; see [DJ deck engine](wiki/pages/dj-deck-engine.md). The two hidden devices that are a track's ends of that crossing are `devices/infra/DjTrackPortDevices.*`. The model is `SessionDj.cpp` and `SessionDjSources.cpp` with `SessionDjInternal.h`. The UI is `ui/dj/`: `DjView.*`, `DjDeckPanel.*`, `DjDeckDisplay.*`, `DjMixerPanel.*`, `DjClipGrid.*` (a console's clips grid, which is the track's clip slots) and `DjControls.h`; clips cross between the views by the descriptions in `ui/shared/ClipDrag.h` |
| A track's clip slots, which are the DJ consoles' cells, and the copies between them and the timeline. See [Clip slots](wiki/pages/clip-slots.md) | `SessionSlots.cpp` |
| Mixer: track volume, pan, mute, solo, main output | `SessionMixer.cpp` |
| Device rack and editors | `ui/rack/DeviceRack.*`, and the faces in `ui/rack/DeviceEditorPanel*.cpp`. A device is dragged by its name bar to reorder the chain; `Session::moveDevice` does the move and refuses one that would break MIDI FX → instrument → audio FX. See [Device chain order](wiki/pages/device-chain-order.md) |
| The knobs an external plugin (Rhino Forge) shows on the rack, and mapping more of them from its own window | `model/SessionMappedParameters.cpp` keeps the chosen list on the plugin's state by its own parameter ids, resolves it to the engine's parameters, and runs the mapping mode that listens to the plugin's window; `exposedParameterAt` in `SessionDeviceMacros.cpp` dispatches to it. The MAP button in the name bar and *Remove from rack* on a knob are in `ui/rack/DeviceEditorPanel.cpp`; `DeviceRack::openDevice` opens the window MAP maps from. See [Mapped parameters](wiki/pages/mapped-parameters.md) |
| Browser | `ui/browser/BrowserPanel.*`; `BrowserIds.h` beside it is the drop vocabulary the other views read |
| App shell and lifecycle | `app/Main.cpp` holds `Application` alone: the log, the test flags (dispatched by `tests/TestRunners.cpp`), the staged start-up and the `.rhinoedit` argument. `app/MainWindow.*` is the document window with the title-bar menus and full screen. `app/MainComponent.h` is the window's content, one class across `MainComponent.cpp` (construction and wiring), `MainComponentLayout.cpp`, `MainComponentControlBar.cpp`, `MainComponentPanes.cpp` (the lower pane and the view switch), `MainComponentCommands.cpp` (keys and menus) and `MainComponentTimer.cpp` (the 30 Hz poll and the readout). `app/AppPlatform.*` is the log path, the file association and the DirectSound avoidance, the one unit that includes `windows.h` |
| Project files | `app/ProjectFiles.*` |
| Playhead rendering | `ui/shared/Playhead.*` |
| Built-in devices | `native/src/devices/`, split `instruments/`, `audio/`, `midi/`. Declared once in `DeviceCatalog.cpp` |
| Eight-band EQ with a live spectrum (Rhino EQ) | The DSP is in `core/dsp/`: `EqFilter.h` (a pure header; the curve and the audio both read it), `EqEngine.*`, `SpectrumAnalyser.*`. The device is `devices/audio/RhinoEqDevice.*` and its face is `ui/rack/DeviceEditorPanelEq.cpp`. See [Rhino EQ](wiki/pages/rhino-eq.md) |
| Routing one track's audio into another track's device (a sidechain) | `SessionSidechain.cpp`. The device declares four input channels; the engine's graph builder does the rest |
| Which input a track takes, and whether you hear it | `SessionMidiInput.cpp` and `SessionAudioInput.cpp` in the model, monitoring in `SessionRecording.cpp`. On the card: the chooser and the On/Auto/Off control in `ArrangementSync.cpp` and `ArrangementGestures.cpp`, the control itself in `ui/arrangement/MonitorSelector.*` |
| Vocoder (Rhino Vocoder) | The DSP is `core/dsp/VocoderEngine.*`. The device is `devices/audio/VocoderDevice.*` and its face is `ui/rack/DeviceEditorPanelVocoder.cpp`. See [Rhino Vocoder and sidechains](wiki/pages/rhino-vocoder.md) |
| FM synth (Rhino FM), the first instrument on the device SDK | The DSP is `core/dsp/FmEngine.*`; the device is `devices/instruments/RhinoFmDevice.*` and declares its controls on the SDK, with no face of its own. The generated face shows its operators as tabs, and its `describe` supplies the routing diagram and waveform the face draws (`ui/rack/DeviceEditorPanelDisplay.cpp`, `core/DeviceDisplay.h`). See [Rhino FM](wiki/pages/rhino-fm.md) |
| Vocal pitch correction (Rhino Tune) | The DSP is in `core/dsp/`: `PitchTracker.*`, `PsolaShifter.*`, `ScaleQuantizer.h`, `AutoTuneEngine.*`. The device is `devices/audio/AutoTuneDevice.*` and its face is `ui/rack/DeviceEditorPanelAutoTune.cpp`. See [Rhino Tune](wiki/pages/rhino-tune.md) |
| The Drum Rack: a pad on every MIDI note, each a sample (one-shot, classic or sliced) or a synthesised drum; its kits (`.rdk`) and drum presets (`.rdp`) | The sound is in `core/dsp/`: `DrumRackEngine.*` for the pads, voices and play modes, `DrumSynth.*` for the synthesised drums, `DrumSlicer.*` for where Slice cuts, and `core/content/DrumKitFile.*` for the two files. The device is `devices/instruments/DrumRackDevice.*` and `DrumRackDeviceEditing.cpp`; its face, in `ui/rack/`, is `DeviceEditorPanelDrums.cpp` (the note map and the sixteen pads shown), `DeviceEditorPanelDrumGestures.cpp` (clicks, a pad dragged onto another to move or swap it), `DeviceEditorPanelDrumSample.cpp` and `DeviceEditorPanelDrumSampleControls.cpp` (the selected pad's sample editor), `DeviceEditorPanelDrumMenus.cpp` and `DeviceEditorPanelDrumParts.cpp`; `ui/rack/DrumRackWindow.*` opens the same face in a window of its own, 1024 by 720 and floating above the main window, the pads above the sample editor (the button in its name bar, or Edit); the model `SessionDrums.cpp`; drops onto pads arrive through `DeviceRack.cpp` and the window. Kits and drum presets live in `library/Drums/`, and the browser's Drums section lists them. See [Drum Rack](wiki/pages/drum-rack.md) |
| Sample and content library | `library/` at the repository root, found by `native/src/core/content/ContentLibrary.h` |
| Theme | `ui/shared/Theme.h` — the font rules, the stock-control look-and-feel, and `palette`, which is where every chrome colour in the app is named. Nothing should spell a surface, border or text colour as a hex literal |

The UI depends on `Session`; `Session` knows nothing about the UI. Keep that direction.

`native/src` builds as three targets, not one: `RhinoCore` (`src/core`, JUCE only), `RhinoDevices` (`src/devices`, Tracktion and `RhinoCore`) and the application. Each target's public include directory is its own root and nothing above it, so a device cannot spell an include of the model or the UI at all. A device may not include `Session.h` or any UI header, and `Session.h` reaches devices only through `DeviceCatalog.h`. That is what keeps editing a device from rebuilding the app, and it is worth preserving as more MIDI and audio FX arrive.

## Content is files, never compiled in

Samples, device presets (`library/Presets/<device id>/*.rnd`), drum kits and drum presets (`library/Drums/Kits/*.rdk`, `library/Drums/Presets/<kind of drum>/*.rdp`), and the patterns that will join them, live under `library/` at the repository root and are found at runtime by `ContentLibrary`. Nothing there is compiled. `juce_add_binary_data` expands an asset to roughly three bytes of C++ per byte of data and rebuilds all of it on a clean build, which is why `native/assets/` is only the fonts, the app icons and the two view-switch glyphs — what the UI needs before it can read from disk, and nothing bigger than a few hundred bytes beyond the fonts. Audio under `library/` is stored with Git LFS, declared in `.gitattributes`; set that up *before* adding a new content type, because converting afterwards means rewriting history.

A device reading its own samples does so on the message thread, never in the audio callback, and must survive the files being absent — the Drum Rack reads a pad's file when the pad's state changes (a drop, a kit, an undo, a document opening), hands the decoded sample to the audio thread without a lock, and logs and leaves the pad silent when the file is missing or unreadable. Measured cost of reading the 808 Kit's eight samples onto their pads: about 6 ms warm (5.3-6.3 ms on 2026-10-07, up from about 3 ms when the rack had sixteen pads; `--self-test` logs it).

## Adding a device is three edits

A device is its source under `native/src/devices/`, a line in that target's `CMakeLists.txt`, and an entry in `DeviceCatalog.cpp`. Write a new one on the SDK in `devices/sdk/`: derive from `NativeInstrument` or `NativeAudioEffect`, declare each control once as a `Param` member, and implement `prepare`, `clear` and `process`. Utility and Rhino Space are the examples; see [The native device standard](wiki/pages/native-device-standard.md). The catalog carries the id, engine type name, display name, kind, browser group, blurb, colour and, for Rhino's own devices, the factory that makes one (`.create = factory<YourDevice>()`), and the browser, the drop targets, the rack's add menu, the engine registration and the instrument rules all read it. Nothing in `Session` or the UI should ever need editing to add one; if it does, that is the bug. `--self-test` holds the line by asserting every browsable catalog device has a browser row and can be added.

Never self-register a device from a static initialiser: a static library's unreferenced objects are dropped by the linker and the registration disappears without an error.

`Session::addDevice(id, track)` is the one way to add a device. The `Instrument`, `AudioEffect` and `MidiEffect` enums survive only as shorthand for the devices that predate the catalog, converted to ids in `DeviceIds.h` and nowhere else. New devices get no enum.

The main track is addressed as `trackCount()`, one past the last audio track, so every track-indexed call reaches it without a sentinel that `DeviceTarget` would read as invalid. `Session::pluginListForTrack` resolves that index to either a track's plugin list or the edit's master list. It carries effects only; the engine's `canBeAddedToMaster` is what refuses the rest.

A track says what it is for when it is created. `Session::addTrack(TrackType)` takes `audio` or `midi`, names the track `Audio` or `MIDI`, and writes `rhinoTrackType` onto the track state — only for MIDI, because a track with no answer is an audio track and that is what every document written before this contains. `Session::trackType(track)` is the one place to ask, and it reads that property and nothing else. A MIDI track holds clips before it runs anything, so a new one is playable at once.

**A track's kind is fixed when it is made, and nothing dropped on it changes it.** `trackType` used to answer `midi` for any track running an instrument, which meant an audio lane silently became a MIDI one the moment a synth landed on it, and no gesture put it back. Every mismatched drop is now refused with a message instead, in the model so that every UI path inherits it: `addInstrumentDevice`, `addMidiEffectDevice` and `addDrumKit` refuse an audio track, `preparePresetTrack` refuses one a pattern would land on, `createClip` refuses one, `importAudioAt` refuses a MIDI track, and `editClip` and `pasteClipRegion` refuse a clip whose kind does not match the lane. A channel-strip facility is the one exception — it joins the chain rather than becoming what the track plays, so it goes on either kind, as audio effects do. The kind also gates what an armed track records (`trackRecordInput`) and what the empty-lane hint promises in `ArrangementPainter`.

Every path that makes a track goes through `Session::appendTrack(TrackType)`, so a track cannot exist without having said what it is for. A clip dragged below the last lane and a paste that runs off the bottom each make a lane of the arriving clip's kind, and a browser item dropped past the last lane makes the lane that item needs.

The arrangement's `+` button makes the person choose — `Arrangement::showAddTrackMenu` — and Ctrl+T in `app/MainComponentCommands.cpp` repeats the last choice, MIDI until the menu says otherwise. The choice is a preference of the machine, kept in the settings file beside the browser preview, and only the menu records it: a track created by a drop below the last lane does not count as a choice. Either way the new track lands directly under the selected card and becomes the selected one, so several added in a row stack in the order they were made; only with the main row selected does it go at the foot. `Session::addTrack(type, below)` holds that rule, groups included: under an open group's bus or any of its members the track joins the group, and under any part of a folded group it lands below the whole group, where it can be seen. `addAudioTrack()` survives as shorthand for the callers — an import, a drop — that genuinely want a track for audio.

A new document is four empty tracks — `MIDI`, `Audio`, `MIDI`, `Audio` — plus the pinned main row. Each declares its kind at creation, so a MIDI lane takes a clip straight away and an audio lane refuses one. `Session::buildStarterEdit` is the one place that stack is described, and `File > New project` asks for the very same thing. A track is named for its kind and nothing else, in `addTrack`, in the starter stack, and on the lanes a clip drop or a paste makes. The row's one-based position is printed beside the name by the card itself, so a number in the name was the same figure twice on every card. `importAudio` with no target fills the first free audio lane — free meaning empty, so nothing is ever landed on — and only brings a track of its own when every audio lane already holds a clip.

Every path that makes a track colours it: `Session::pickTrackColour` takes a palette entry at random, skipping the ones already on screen while any are left, and `addTrack`, the starter stack, a clip dragged below the last lane and a paste that runs off the bottom all call it. Under `--self-test` it takes the first unused entry instead, so a run that compares pixels is not asked to agree with a dice roll. A group bus is the exception: it takes the default band colour until someone gives it one.

Clips take their track's colour. A clip carries no colour of its own until someone presses C on it, which writes `rhinoClipColour` and cycles the palette, and then past the last entry back to nothing so it can rejoin its lane. The property is Rhino's because the engine has no way to say "no colour": `te::Clip::getColour` substitutes a default per clip type for anything unset, which is why every path that made a clip used to stamp a colour on it. `Session::clipColour` is the one place to ask, and `ArrangementPainter` darkens the track colour for the clip body — a card is a solid block behind dark text and a clip is a translucent fill behind light text, and one value cannot do both.

A track has exactly one instrument, matching Live and Logic. Dropping an instrument replaces the one already there and removes it; the track's clips are untouched, so the pattern survives the swap. Never cache an instrument pointer across a switch. An instrument drop changes the track and its name and nothing else: it does not create a clip, and it does not change what the track is — only a MIDI track is offered one in the first place, so taking the instrument off again leaves the MIDI track it was always on. Clips are created by double-clicking a lane or pressing Ctrl+A, and only MIDI tracks can hold them.

The DJ view is the shell's second view since 2026-10-09: the Arrange/DJ switch and Tab open `DjView` in the arrangement's place (`setDjViewOpen` in `app/MainComponentPanes.cpp`). The booth is Rhino's own engine on a second audio callback, so every deck has a transport of its own. A deck plays a file, or one of a track's cells heard live through the track's own chain: the cell's notes play the track's instrument, its sound goes through the track's effects, and the chain's output comes back into the deck's channel, so every knob - an external plugin's own window included - is heard as it turns. Nothing is rendered or bounced on the way. Every track carries the two hidden devices that do this from the moment it is made (`ensureDjPortDevices`, run by `ensureTrackMixers`): the input at plugin index 0 and the output where the track's own devices end and its channel strip (Utility, Volume & Pan, meter) begins. **So a track's first device is at raw plugin index 1, not 0** - anything that names a device by its raw `slot` must count the hidden input, and every path that inserts at the front of a chain starts after it (`djInputEnd`). They pass audio straight through unless a deck is bound to the track, while the song's transport rolls, and in any render. Every edit says which track it changed and what it changed (`Session::markModified(track, ModelChange)`); a new command passes both, and `ModelChange::none` for what changes no clip a deck plays. Read [DJ view and the booth](wiki/pages/dj-view.md) before touching any of it.

The DJ view and the arrangement are two presentations of one project, not two documents. Tracks, devices and the mixer are shared because both views read the same `Session`; never let a view cache a copy of that state. Selection, focus, scroll and zoom are per-view and should stay that way. The transport is not shared: every deck has its own, and the song's is out of reach while the decks show. Clips differ too: a console's cells are its track's clip slots, timeline clips belong to the arrangement, and the only way between them is to copy, through `copySlotClipToArrangement`, `copyClipToTrackSlot` and `copyClipToSlot` in `SessionSlots.cpp`, reached by dragging a clip through the view switch or from the arrangement's clip menu. Neither view should ever try to display the other's clips. The session view, a clip launcher over the same slots, was removed on 2026-10-11 at the user's request; read [Clip slots](wiki/pages/clip-slots.md) before touching the slots.

Every source file is listed explicitly in `native/CMakeLists.txt` — nothing is globbed. A new `.cpp` needs a line there or it silently will not compile.

## Keeping files small

Split a `.cpp` when it passes roughly 600 lines or gains a second responsibility. Prefer the mechanism already used here: **define one class across several translation units**, as `SessionTransport.cpp` does for `Session` and `ArrangementGeometry.cpp` does for `Arrangement`. That needs no header change, no change at any call site, and preserves `friend` declarations used by tests — only a new line in `CMakeLists.txt`.

When those units need to share helpers that were previously in an anonymous namespace, put them in a `*Internal.h` header next to the class — `SessionInternal.h`, `StepGridInternal.h`, `ArrangementInternal.h`. Those headers are private to the class's own translation units; nothing else should include them. Helpers shared by *different* classes get a normal header instead, as `BrowserIds.h` does for the arrangement and the device rack.

Extract a genuinely new type only when it buys testability. `ClipGeometry.h` is the model: a pure header with no JUCE or engine dependency, unit-tested in 40 lines without a `Session`.

One class is still defined inline inside a single `.cpp` and is the next thing worth separating: `FloatingDeviceWindow` in `ui/rack/DeviceRack.cpp`. The shell's `ControlWindow` was the other until 2026-10-13, when it became `MainComponent.h` and six units. Splitting it means converting inline method bodies to declaration plus definition, which is a real restructuring rather than a file move — do it deliberately, not as a side effect of another change.

## Tests

Six CTest cases, all the same binary with different flags. One of them, `native_device_conformance` (`--device-test`, `tests/DeviceConformance.cpp`), renders every device Rhino makes offline and holds it to the device standard; a device that cannot pass a check yet is listed in its `pending` table with the reason. Run them through `ctest` only: launched directly, a runner flag opened the full app on the user's screen. Build and run:

```powershell
cmake --build native/build --config Release --parallel 2
ctest --test-dir native/build -C Release --output-on-failure
```

Anything that renders must be included before `GesturesAndPersistence.inc`, which closes the audio device; a render attempted after it never returns and the runner times out.

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
