# Rhino wiki — index

The catalog of this wiki: every page, grouped by type, one line each (`- [Title](path) — summary`). Conventions:
[SCHEMA.md](SCHEMA.md). History: [log.md](log.md).

## Overview

- [Overview](overview.md) — Rhino and Rhino Forge at a glance: what they are, how they are built and where to start reading.

## Components

- [App shell and control bar](pages/app-shell.md) — Main.cpp's window, control bar, docked browser and single lower pane, plus the 30 Hz timer that polls what the engine never broadcasts.
- [Arrangement view](pages/arrangement-view.md) — The timeline UI, one class across eleven files, that previews every drag locally and leaves panel decisions to the shell.
- [Audio clip editor](pages/audio-clip-editor.md) — Edits one audio clip's own gain, pan, pitch, fades, mute and reverse as clip properties, with one undo step per knob drag.
- [Browser and library preview](pages/browser.md) — A six-section library rail, Drums first, whose rows leave only by drag, with samples and drum presets auditioned through a second audio callback.
- [Built-in devices](pages/built-in-devices.md) — Every device Rhino ships, what each one is and where its source lives, with Utility's details and the pitfalls the devices share.
- [Computer MIDI keyboard](pages/computer-keyboard.md) — Plays notes from the typing keyboard into the MIDI input with Forge's exact key mapping, so arming and recording need no special case.
- [Content library](pages/content-library.md) — Samples, device presets, drum kits and drum presets live as files under library/, found at runtime by ContentLibrary, audio stored with Git LFS, nothing compiled in.
- [DJ view and the booth](pages/dj-view.md) — Up to six CDJ-style decks either side of a DJM-style mixer, run by Rhino's own engine as a second audio callback, playing files or the song's tracks and groups bounced to audio and bounced again as they are edited.
- [Device catalog](pages/device-catalog.md) — The one table of Rhino's devices that the browser, drop targets, rack menu, engine registration and instrument rules all read.
- [Device presets (.rnd)](pages/device-presets.md) — A device's settings saved as a .rnd file by parameter id, filed under the device in the browser, dragged to add the device already set, and loaded or saved from its name bar.
- [Device rack and device editors](pages/device-rack.md) — The Device View strip that shows a track's chain and its per-device faces, the rebuild rule that shapes how a face is written, and the timer that follows automated knobs.
- [Drum Rack](pages/drum-rack.md) — A pad on every MIDI note, addressed by note, each a sample or one of eight synthesised drums with six automatable controls; the face shows a bank of sixteen beside a map of all 128 and opens maximised in a window of its own, and kits (.rdk), drum presets (.rdp) or dropped samples fill it.
- [Drum Rack sample editor](pages/drum-rack-sample-editor.md) — How a Drum Rack sample pad plays its file (one-shot, classic or slice), kept as undoable pad content rather than automatable controls, cut at transients or into equal parts, and spread onto pads of their own.
- [Forge arpeggiator](pages/forge-arpeggiator.md) — Stands in front of the voices, hands notes back through two callbacks, and opens as an overlay from a plate beside the keys.
- [Forge editor (panel)](pages/forge-editor.md) — One Editor class across fifteen files builds the panel from declared modules and decides what is visible in one place.
- [Forge engine (Core)](pages/forge-engine.md) — The header-only voice engine that allocates sixteen voices, modulates per voice per sample and scales the output by 0.28.
- [Forge filter](pages/forge-filter.md) — One per-voice filter of 34 types in six families, whose drawn response and audio come from the same header.
- [Forge MIDI learn](pages/forge-midi-learn.md) — Right-click learn binds a controller's knobs and pads to any control; bindings are kept per machine in MidiMap.xml, not in patches.
- [Forge mixer and effects racks](pages/forge-mixer-and-fx.md) — The MIX tab's nine channels and two busses, and three eight-slot effects racks that run on the summed voices.
- [Forge modulation: matrix, envelopes, LFOs and macros](pages/forge-modulation.md) — Eight matrix slots route envelopes, LFOs, macros and performance sources to any continuous control, per voice, in the knob's own range.
- [Forge noise module](pages/forge-noise.md) — A per-voice noise oscillator with nineteen generated sources in six families, each a colour through a character stage.
- [Forge oscillators and wavetables](pages/forge-oscillators.md) — Three identical oscillators, each reading its own band-limited wavetable, plus the table editor, file import and audio-thread hand-off.
- [Forge presets and state](pages/forge-presets-and-state.md) — Presets and host state carry one tree; a preset of another format version is refused, and within a version everything is reconciled.
- [Forge spectral oscillator](pages/forge-spectral.md) — An oscillator mode that resynthesises a loaded sample with a phase vocoder, so pitch and scan position move independently (M16).
- [Forge warp](pages/forge-warp.md) — Two warp stages per oscillator, 38 modes in nine families, chained through one read function and anti-aliased by duller tables.
- [Hosting Forge in Rhino](pages/forge-hosting.md) — How Rhino finds, loads and talks to the Forge VST3, and what does and does not cross the plugin boundary.
- [Mixer](pages/mixer.md) — One mixer serves both views, with a VolumeAndPanPlugin ending each chain, mute and solo on the track, and the master volume for Main.
- [Note editor (StepGrid)](pages/note-editor.md) — Edits the notes of the open MIDI clip in steps, with a draw mode, a step region and caches that tie it closely to Session.
- [Pattern presets](pages/pattern-presets.md) — The eleven built-in one-bar patterns, each a set of notes plus the instrument and patch that play them, and what dropping one does to a track.
- [Playhead rendering](pages/playhead.md) — Draws both playheads every display refresh from the audio graph's own position, repainting two narrow strips whose painters skip everything else, with a Direct2D fix.
- [Project files (.rhinoedit)](pages/project-files.md) — A .rhinoedit is the Tracktion edit's XML plus Rhino's own properties, saved from a snapshot on a worker and opened off-thread.
- [Recording and the count-in](pages/recording.md) — Arms tracks, not inputs, lets each take win the ground it lands on, and counts in with Rhino's own click while the playhead stands still.
- [Rhino Arp](pages/rhino-arp.md) — A built-in MIDI arpeggiator with beat or millisecond scheduling and a dedicated five-area rack face.
- [Rhino EQ](pages/rhino-eq.md) — Eight-band EQ whose drawn curve comes from the same coefficients as the audio, over a spectrum computed off the audio thread.
- [Rhino FM](pages/rhino-fm.md) — A four-operator FM synth in eight routings, written on the device SDK, whose modulation index is checked against Bessel functions and whose face shows its operators as tabs, its carriers in a routing diagram, and the waveform it makes.
- [Rhino Forge](pages/forge.md) — Rhino's independent wavetable and spectral synth, a VST3 and standalone app built from one JUCE AudioProcessor.
- [Rhino Tune](pages/rhino-tune.md) — Vocal pitch correction with YIN tracking and PSOLA shifting, real reported latency, and correction smoothed on the offset.
- [Rhino Vocoder and sidechains](pages/rhino-vocoder.md) — A channel vocoder whose carrier arrives as a sidechain from another track, and how Rhino routes a sidechain at all.
- [Rhino's build targets](pages/rhino-build-targets.md) — Rhino builds as three targets (RhinoCore, RhinoDevices, the app) so that editing a device does not rebuild the app.
- [Session view (paused)](pages/session-view.md) — A working clip launcher, built and tested but no longer reached from the shell, whose place the DJ view took and whose scene and slot model still runs in every project.
- [Session, the model](pages/session-model.md) — The message-thread facade over one Tracktion engine and edit, one class split across 28 files, where every rule and refusal lives.
- [Time warp](pages/time-warp.md) — Makes an audio clip follow the song's tempo, with five warp modes over two stretchers, a clip tempo and warp markers.
- [Track automation](pages/automation.md) — Per-track lanes stored as Rhino's own ValueTree children, naming their device by key rather than slot, and played by the engine from parameter curves the session mirrors after every change.
- [Track groups (bus tracks)](pages/track-groups.md) — Ctrl+G gathers tracks under a bus track that their audio feeds; the structure is positional and repaired after every reorder.
- [Track inputs and monitoring](pages/inputs-and-monitoring.md) — Each track names its own MIDI or audio input and its own Off, Auto or In monitoring, all resolved to engine devices by applyRecordArming.
- [Transport, tempo and loop](pages/transport.md) — Play and stop, the loop that is always on, the single tempo and the time signature, and what Rhino rescales by hand when the tempo moves.

## Concepts

- [Clips never overlap](pages/clip-placement.md) — The clip that just arrived wins the ground it lands on, and one function, Session::makeRoomForClip, enforces it for every path.
- [Device chain order](pages/device-chain-order.md) — A track's plugin list is its signal chain, ordered MIDI FX, instrument, audio FX; how every add and every drag keeps that order.
- [Pointer and selection rules](pages/pointer-and-selection.md) — One pointer vocabulary for the arrangement and the note editor, written once as shared predicates in SelectionInput.h.
- [Region editing](pages/region-editing.md) — Copy, cut, paste, duplicate and delete all act on a region, a span of time across a run of tracks, never on a set of clips.
- [The main track](pages/main-track.md) — The main output row is the edit's master plugin list, addressed as track index trackCount() so track-indexed calls need no sentinel.
- [Track and clip colours](pages/track-and-clip-colours.md) — Every new track is born with a palette colour; a clip wears its track's colour until C gives it its own rhinoClipColour.
- [Track kinds: audio and MIDI](pages/track-kinds.md) — Every track is audio or MIDI from creation; one property records it, one call reads it, and the model refuses mismatched content.

## Decisions

- [A deck's bounce renders on a worker against a copy of the document](pages/dj-bounce-on-a-copy.md) — A track or group bounced to a DJ deck renders on the booth's worker from a snapshot copy of the edit, never the live edit nor the message thread; the copy's plugins and the test runners' inline render are the price.
- [A group is an ordinary bus track](pages/group-is-a-bus-track.md) — A group's bus is a plain te::AudioTrack kept in getAudioTracks(), so every track-indexed path reaches it with no second code path.
- [A track's kind is fixed when it is made](pages/track-kind-fixed-at-creation.md) — A track declares audio or MIDI at creation, nothing dropped on it changes that, and every mismatched drop is refused in the model.
- [Content is files, never compiled in](pages/content-is-files.md) — Samples and other content live as files under library/ and are found at runtime; only fonts and app icons are embedded.
- [Forge is an independent VST3](pages/forge-is-an-independent-vst3.md) — Why Forge is its own JUCE plugin project, hosted by Rhino through Tracktion's external-plugin wrapper rather than built in.
- [Forge's engine splits into headers only](pages/forge-engine-headers-only.md) — Forge's engine splits into headers so renderSample stays inlined without LTCG, while the panel splits into .cpp files freely.
- [Forge's FX slots declare generic parameters](pages/forge-fx-slots-have-generic-parameters.md) — Every rack slot exposes the same twelve anonymous parameters, because a host's parameter list is fixed when the plugin is built.
- [No backward compatibility for .rhinoedit](pages/no-rhinoedit-back-compat.md) — Rhino owes older .rhinoedit documents nothing; a project that opens wrong is fixed in the current save and load path, not by a migration.
- [No track is special for being first](pages/pattern-track.md) — Rhino began as one pattern on one synth track; commit a04b407 retired every rule that singled out the first track, and each track's own chain now records what it plays.
- [One instrument per track](pages/one-instrument-per-track.md) — A track runs exactly one instrument; a new one replaces the old in place, and nothing may cache an instrument pointer.
- [The executable is RhinoDAW, not Rhino](pages/executable-named-rhinodaw.md) — The binary is RhinoDAW.exe because NVIDIA's driver profile for Rhinoceros 3D, keyed on Rhino.exe, corrupted the app's Direct2D repaints.
- [The native device standard](pages/native-device-standard.md) — Rhino's own devices are written on one SDK base, held to one conformance runner and given generated faces, with their presets as .rnd files; VST3 stays the format for outside instruments.
- [Tracktion Engine with a native JUCE UI](pages/tracktion-and-juce.md) — Rhino runs on Tracktion Engine with a hand-built JUCE interface, chosen in September 2026 over a custom engine, Qt Quick, WebView or Tauri.

## Guides

- [Adding a device to Rhino](pages/adding-a-device.md) — The three edits that add a device, the SDK base a new one is written on, the shape the older devices follow, and the enum-era code a new instrument still meets.
- [Build and test Forge](pages/build-and-test-forge.md) — Configure Forge against Rhino's JUCE checkout, build around the toolchain's traps, and run one test area at a time.
- [Build and test Rhino](pages/build-and-test-rhino.md) — Fetch the pinned engine, build with Visual Studio 2022, and run the six CTest cases without being fooled by a stale or locked build.
- [Comparing Forge with Serum](pages/comparing-forge-with-serum.md) — Settle "it sounds different in Serum" by capturing and measuring both synths, reading screenshots by pixel, and using Serum's own tables.
- [Debugging a crash only one project triggers](pages/debugging-a-crashing-project.md) — Turn a project file that crashes Rhino into a ten-second repro, name the faulting module, and bisect the XML against a control.
- [Development environment and reference material](pages/development-environment.md) — What the Windows development machine offers (shells, CMake, Python, ffmpeg, PDF tools, the reference manuals) and the habits it demands.
- [Driving Forge's standalone on Windows](pages/driving-the-forge-standalone.md) — Run, click and capture the Forge standalone without costing the developer their patch, pointer or screen, or use a headless test.
- [Proving a Forge change changed nothing](pages/proving-a-forge-change-changed-nothing.md) — Hash --render audio and --snapshot PNGs against a worktree build of the old revision, and time intended changes by --profile medians.
- [Reading MIDI hardware on Windows](pages/reading-midi-hardware-on-windows.md) — Enumerate and monitor a MIDI controller from PowerShell through winmm, and why a port that will not open usually means something else.
- [Reproducing a live timing bug offline](pages/reproducing-live-timing-offline.md) — Render the real project in a test at the audio device's block size, not the offline default, to catch bugs that live on block boundaries.
- [Seeing the UI without taking the screen](pages/headless-ui-snapshots.md) — Render a Rhino panel, the whole Rhino shell or Forge's editor to a PNG without a window, then measure it instead of eyeballing it.
- [Writing Rhino tests](pages/writing-rhino-tests.md) — Choose between a unit check and a workflow scenario, follow the rules that keep scenarios sharing one Session honest, and time the interface with --profile-ui.

## Conventions

- [Colours and typography](pages/colours-and-typography.md) — Rhino names chrome colours in Theme.h's palette and draws text in embedded Inter; Forge has its own metal look, accent colours and four faces.
- [Dependency direction](pages/dependency-direction.md) — Knowledge flows one way in both products, UI to model to devices to core, and every rule lives in the model so each UI path inherits it.
- [Directories not to read](pages/off-limits-directories.md) — native/.deps and research/sources are not Rhino's code and are never searched; Tracktion signatures are checked in curated header snapshots.
- [Documentation ownership](pages/documentation-ownership.md) — READMEs introduce and build the products; the wiki is the canonical home for engineering detail, decisions and gotchas.
- [Displays draw from the DSP](pages/displays-draw-from-the-dsp.md) — A curve on screen is computed by the same functions the audio runs, never from a second set of formulas, and tests hold the two together.
- [Git workflow](pages/git-workflow.md) — Focused, verified commits pushed at milestones; stage explicit paths since the user edits Forge in the same tree; never rewrite pushed history.
- [Hold ids, not pointers](pages/ids-not-pointers.md) — Anything kept beyond one call holds an id or a copy, never a raw engine pointer, because undo, moves, reloads and publishes free the object.
- [Keeping files small](pages/keeping-files-small.md) — Split a .cpp past about 600 lines or a second responsibility by defining one class across several translation units, not by inventing types.
- [Measure sound, don't read the DSP](pages/measure-sound-dont-read-dsp.md) — Claims about pitch, level or timbre are settled by rendering audio and measuring it by a route that cannot agree with the DSP by construction.
- [Real-time audio rules](pages/real-time-audio-rules.md) — The audio thread never allocates, locks, or touches files or UI; memory is sized at prepare and state crosses threads via atomics and queues.
- [Stored indices are append-only](pages/append-only-stored-indices.md) — Saved choice lists and parameter arrays are append-only, because inserting an entry silently changes what an old index means.
- [What a change costs the interface](pages/ui-cost-of-a-change.md) — Session announces every change synchronously to every listening panel, so a drag announces once, a hidden panel goes stale, a frame reads only the controls it shows, painters skip what a repaint does not reach, and --profile-ui measures it.

## Gotchas

- [A CachedValue can lag its own ValueTree](pages/cachedvalue-lags-its-tree.md) — A ValueTree listener that reads a juce::CachedValue of the same property may run before the cache updates, so force the cache first; in Rhino the race failed only under parallel CTest.
- [A CTest SegFault may be a stack overflow](pages/stack-overflow-reports-as-segfault.md) — CTest reports a Windows stack overflow (0xC00000FD) as a bare SegFault, and both test binaries put large objects on the stack.
- [A JUCE component listening to itself hears its own clicks twice](pages/juce-self-listener-hears-clicks-twice.md) — A component registered as its own mouse listener gets every event on itself twice, so every toggle on Rhino's device faces flipped on and straight back off; hear the children through a separate listener, and test clicks through real dispatch.
- [A juce::Button fires its click on a right-click too](pages/juce-button-fires-on-right-click.md) — juce::Button presses and clicks for any mouse button, so a key that carries a right-click menu also runs its left-click action unless mouseDown and mouseUp swallow the popup-menu press first, as the DJ console's DjPad does.
- [A juce::Path holding only a start point is empty](pages/juce-path-isempty-ignores-a-lone-point.md) — juce::Path::isEmpty() ignores startNewSubPath points, so a loop that asks it whether to start or continue a line starts a new subpath every time and strokes nothing; keep a flag of your own.
- [A layout that reads a child's preferred size must sync the child first](pages/sync-children-before-layout.md) — DjView laid itself out before syncing its mixer, so the layout read the mixer's width from the old strip count and a new deck's strip sat under the master section until the next resize; sync the children a layout measures first, and re-lay out when the measured size changes.
- [A new source file needs an explicit CMake configure](pages/cmake-does-not-reconfigure.md) — Both projects suppress CMake regeneration, so a file or a flag added to a CMakeLists is silently left out until you configure again.
- [App icons are baked at configure time](pages/app-icon-baked-at-configure.md) — JUCE turns the icon PNGs into an .ico at configure time, so editing the art and rebuilding still ships the old icon.
- [Build locks from MSBuild nodes and orphaned compilers](pages/orphaned-build-processes.md) — An object or source file that another process holds is usually another session's build, an idle MSBuild node or an orphaned cl.exe.
- [Editing only a scenario .inc does not rebuild the tests](pages/inc-edits-do-not-rebuild.md) — An edit to a workflow scenario alone has left the old test binary in place, so CTest passed a scenario that was never compiled.
- [Forge's panel repaints whole at 24 Hz](pages/forge-panel-repaint-cost.md) — Forge's editor repaints the entire panel 24 times a second, so anything added to its paint path is paid on every frame.
- [JUCE's isBold() is false for SemiBold](pages/juce-isbold-misses-semibold.md) — juce::Font::isBold() matches only the whole word Bold, so a LookAndFeel keyed on it serves the Regular cut for SemiBold text.
- [JUCE's rasteriser is not clip-invariant](pages/juce-rasteriser-not-clip-invariant.md) — A path's anti-aliased edge can come out differently under a different clip, so check a culled repaint against an unculled paint under the same clip, never against the whole paint.
- [juce::File::createOutputStream appends](pages/juce-output-stream-appends.md) — JUCE opens an existing file for output at its end, so rewriting a PNG or a render in place leaves the old content in front.
- [LNK1104 means a running binary holds the file](pages/locked-executable-lnk1104.md) — A link that fails with LNK1104 after every file compiled means a running RhinoDAW, DAW or Forge standalone holds the output.
- [Offline renders that never return](pages/renders-that-never-return.md) — Four known causes make an offline render hang for good, and the Moved render timeout failure is usually just a busy machine.
- [One knob diameter for the whole Forge panel](pages/forge-knob-diameter-is-panel-wide.md) — Every ordinary knob takes the tightest cell's size, so crowding one module shrinks them all; the test meant to catch it cannot fail.
- [Profile paint on a software image](pages/profile-paint-on-a-software-image.md) — On Windows a plain juce::Image is a Direct2D bitmap whose context costs about 3 ms a paint whatever is drawn, so time painters on juce::SoftwareImageType() and repaint what the app really invalidates.
- [Why a patch copied from Serum sounds different](pages/serum-patches-sound-different.md) — Check matrix polarity, PD warp depth, the filter's FREQ and the output stage before suspecting Forge's tuning or DSP.

## Sources

## Analyses

- [Hazards found while seeding the wiki](pages/known-hazards.md) — Probable bugs traced by reading the code on 2026-10-03; every Rhino item was fixed by 2026-10-05, and the four Forge items are still open.
