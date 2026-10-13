# Rhino wiki — log

Append-only history of this wiki, newest entry at the bottom. Every entry starts with `## [YYYY-MM-DD] op | Title`
(ops: `init`, `seed`, `ingest`, `query`, `lint`, `update`); see [SCHEMA.md](SCHEMA.md).

## [2026-10-03] init | Wiki created

## [2026-10-03] seed | Wiki seeded from the codebase
- source: the code in `native/` and `instruments/rhino-forge/`, the repository's docs (AGENTS.md, README.md, ARCHITECTURE.md, HANDOVER.md, SESSION-VIEW.md, INSTRUMENT_PLAN.md, native/README.md, research/, Forge README/PLAN/SPECTRAL/HANDOVER-M9B) and the agents' working notes; raw/ is empty. Every page was checked against the code on 2026-10-03.
- updated: overview.md, index.md
- created: pages/adding-a-device.md, pages/app-icon-baked-at-configure.md, pages/app-shell.md, pages/append-only-stored-indices.md, pages/arrangement-view.md, pages/audio-clip-editor.md, pages/automation.md, pages/browser.md, pages/build-and-test-forge.md, pages/build-and-test-rhino.md, pages/built-in-devices.md, pages/clip-placement.md, pages/cmake-does-not-reconfigure.md, pages/colours-and-typography.md, pages/comparing-forge-with-serum.md, pages/computer-keyboard.md, pages/content-is-files.md, pages/content-library.md, pages/debugging-a-crashing-project.md, pages/dependency-direction.md, pages/development-environment.md, pages/device-catalog.md, pages/device-chain-order.md, pages/device-rack.md, pages/displays-draw-from-the-dsp.md, pages/doc-drift.md, pages/driving-the-forge-standalone.md, pages/executable-named-rhinodaw.md, pages/forge-arpeggiator.md, pages/forge-doc-drift.md, pages/forge-editor.md, pages/forge-engine-headers-only.md, pages/forge-engine.md, pages/forge-filter.md, pages/forge-fx-slots-have-generic-parameters.md, pages/forge-hosting.md, pages/forge-is-an-independent-vst3.md, pages/forge-knob-diameter-is-panel-wide.md, pages/forge-midi-learn.md, pages/forge-mixer-and-fx.md, pages/forge-modulation.md, pages/forge-noise.md, pages/forge-oscillators.md, pages/forge-panel-repaint-cost.md, pages/forge-presets-and-state.md, pages/forge-spectral.md, pages/forge-warp.md, pages/forge.md, pages/git-workflow.md, pages/group-is-a-bus-track.md, pages/headless-ui-snapshots.md, pages/ids-not-pointers.md, pages/inc-edits-do-not-rebuild.md, pages/inputs-and-monitoring.md, pages/juce-isbold-misses-semibold.md, pages/juce-output-stream-appends.md, pages/keeping-files-small.md, pages/known-hazards.md, pages/locked-executable-lnk1104.md, pages/main-track.md, pages/measure-sound-dont-read-dsp.md, pages/mixer.md, pages/no-rhinoedit-back-compat.md, pages/note-editor.md, pages/off-limits-directories.md, pages/one-instrument-per-track.md, pages/orphaned-build-processes.md, pages/pattern-presets.md, pages/pattern-track.md, pages/playhead.md, pages/pointer-and-selection.md, pages/project-files.md, pages/proving-a-forge-change-changed-nothing.md, pages/reading-midi-hardware-on-windows.md, pages/real-time-audio-rules.md, pages/recording.md, pages/region-editing.md, pages/renders-that-never-return.md, pages/reproducing-live-timing-offline.md, pages/rhino-build-targets.md, pages/rhino-eq.md, pages/rhino-tune.md, pages/rhino-vocoder.md, pages/serum-patches-sound-different.md, pages/session-model.md, pages/session-view.md, pages/stack-overflow-reports-as-segfault.md, pages/time-warp.md, pages/track-and-clip-colours.md, pages/track-groups.md, pages/track-kind-fixed-at-creation.md, pages/track-kinds.md, pages/tracktion-and-juce.md, pages/transport.md, pages/writing-rhino-tests.md
- notes: stale doc statements are listed in pages/doc-drift.md and pages/forge-doc-drift.md; probable bugs traced but not reproduced are in pages/known-hazards.md.

## [2026-10-03] update | Forge's oscillator CRT loses its corner brackets
- updated: none (no page describes the oscillator CRT display)
- notes: `drawCrtScreen` in `instruments/rhino-forge/ui/ForgeDisplays.h` no longer draws L-shaped corner brackets inside the glass, and `drawDisplayBrackets` is gone (commit `0328983`). The user asked for this as a design preference, so do not put corner marks back on the CRT face. The graticule, zero axis, bezel glow and trace are unchanged.

## [2026-10-03] update | A switched-off Forge oscillator keeps its shared-cell knobs
- updated: pages/forge-editor.md (new "Rules that bite" entry: shared-cell hiding reads `inCharge`, not the module's switch; the label-counting trap in editor tests)

## [2026-10-03] update | Forge's plate legends give their strip to the knobs, and captions keep one size
- updated: pages/forge-editor.md (`Module` positional-initialiser trap; captions span their cell with no side border because `juce::Label` squashes then shrinks text that does not fit, guarded by `captionsKeepTheirSizeSuite`; plates have a 6 px `plateFootMargin` instead of the 20 px legend, commit `7be9a56`)
- updated: pages/forge-knob-diameter-is-panel-wide.md (shared diameter at 1440×900 now 46 px from 95×60 oscillator cells, was 43; the plate foot has no more height to give)

## [2026-10-03] update | The spectral oscillator keeps one pair of markers, the loop's
- updated: pages/forge-spectral.md (START/END and LS/LE removed, commit `dbd223d`; playback spans the whole sample; loop markers shown and draggable only when `spectralLoopReachesLoop`, hidden rather than faded; why one pair; `loopMarkersSuite`)
- updated: pages/forge-editor.md (a parameter dragged on a display must be listed in `displayParameters` for the layout test)
- updated: pages/inc-edits-do-not-rebuild.md (a `Copy-Item` restore keeps the older timestamp, so MSBuild keeps the object built from a temporary edit; touch or restore by editing)
- updated: pages/build-and-test-forge.md (pointer to that trap when proving a check can fail)

## [2026-10-03] update | START and END return for ONE-SHOT and MANUAL, and spectral gets a flat display
- updated: pages/forge-spectral.md (commit `bbc2b85` supersedes `dbd223d`'s one-pair decision: each loop mode reads exactly one marker pair, START/END for ONE-SHOT and MANUAL, the loop for the rest, and only that pair is drawn or grabbed; no value fields by user request; the pixel-rounded drag value trap in tests; new "The display" section on the square spectral well replacing the CRT tube)
- updated: pages/forge-editor.md (`displayParameters` now lists all four spectral markers)

## [2026-10-03] update | MANUAL shows only its playhead on the spectrogram
- updated: pages/forge-spectral.md (commit `ab7fd45`: `spectralMarkerPairOf` / `SpectralMarkerPair { none, run, loop }` replaces `spectralLoopReachesLoop`; MANUAL reads no marker and sweeps the whole sample at `spectralManualPosition(scan)`, shared by voice and panel; START/END for ONE-SHOT only; the playhead is drawn at rest in MANUAL, from the voice while a note plays; the MANUAL panel check)
- updated: pages/inc-edits-do-not-rebuild.md (the filter-hides-the-build-failure trap hit Forge via `Select-String` chained with `ctest`)
- updated: pages/build-and-test-forge.md (do not chain a filtered build with ctest; check `$LASTEXITCODE` first)
- updated: pages/development-environment.md (anchor regex identifier renames with `\b` at both ends)

## [2026-10-03] update | The bar ruler labels counts inside a bar
- updated: pages/arrangement-view.md (new bullet: `bar.count` labels and minor marks once bar numbers are a bar apart; a count is the signature's denominator note, not `beatsPerBar()`; the `finerRulerSpan` divisibility chain, `rulerLabelSpan` at 44 px and `rulerTickSpan` at 10 px down to a quarter count; it ignores the snap grid and leaves the time ruler alone)
- updated: pages/transport.md (the ruler counts in the numerator's notes, not `beatsPerBar()` quarters)

## [2026-10-03] update | Spectral loop modes run in from START, with START and END on screen
- updated: pages/forge-spectral.md (commit `b24d30a`: `spectralReadsRun` / `spectralReadsLoop` replace `spectralMarkerPairOf` / `SpectralMarkerPair`; START/END in ONE-SHOT and every loop mode, loop bracket in loop modes, MANUAL playhead only; voices start at START; loop clamped to the run in `SpectralSpan` and drawn and hit-tested at `heardLoop`; telling coinciding markers apart by height (bar, foot tab `markerTab`); drag limits; the four-commit marker history; new tests)
- updated: pages/development-environment.md (in PowerShell 5.1 a `git commit -m` here-string with double quotes splits into arguments; use `git commit -F`, and do not hide its stderr)

## [2026-10-03] update | Spectral START is separate from the loop, as in Serum
- updated: pages/forge-spectral.md (commit `61c515b` corrects `b24d30a`: Serum's loop-mode semantics from User Guide pp. 77-78; `spectralReadsStart` / `spectralReadsEnd` / `spectralReadsLoop` replace `spectralReadsRun`; `SpectralSpan::onset`, loop clamped only to the sample in loop modes; END is ONE-SHOT's alone; `orderedLoop` replaces `heardLoop`; new drag limits (START ≤ loop start, LE free past END); TAILED still unbuilt, an open question; history and the "read the manual first" lesson; new hop-by-hop tests)

## [2026-10-03] update | Forge note guide gives its inset face to two controls rows
- updated: pages/forge-editor.md (the keyboard-shelf note guide has no heading; its full inset face is two larger rows with a 6 px gap while bevel and rivets remain, and its state/MIDI logic is unaffected; corrected the Editor split to sixteen files including `ForgeEditorNoteGuide.cpp`)

## [2026-10-03] update | Forge note guide avoids unsupported Unicode glyphs
- updated: pages/forge-editor.md (accidentals, chord qualities and tooltip arrows use ASCII after the popup font/environment rendered Unicode glyphs broken; note-overlay contrast is intentionally stronger on both key colours)

## [2026-10-03] update | Rhino Arp redesign

- `pages/rhino-arp.md` — Recorded the standalone MIDI-effect boundary, parameter/default model, beat-domain realtime scheduling contract, dedicated editor ownership, and regression-test expectations.

## [2026-10-03] update | Wiki becomes the engineering documentation source
- updated: `overview.md` — Recorded mid-alpha status, the installer target, and the new documentation split.
- created: `pages/documentation-ownership.md` — Defined READMEs as product/setup entry points and the wiki as canonical engineering documentation.
- updated: `index.md` — Catalogued documentation ownership and removed the retired drift analyses.
- removed: `pages/doc-drift.md` — The legacy Rhino documents it audited were removed after consolidation into the wiki.
- removed: `pages/forge-doc-drift.md` — The legacy Forge documents it audited were removed after consolidation into the wiki.
- updated: `pages/adding-a-device.md` — Removed legacy-doc pointers; this page now carries the complete guidance.
- updated: `pages/app-shell.md` — Removed comparisons with retired and rewritten docs.
- updated: `pages/append-only-stored-indices.md` — Kept the warp-index decision without a retired plan citation.
- updated: `pages/arrangement-view.md` — Removed legacy-doc pointers and obsolete drift notes.
- updated: `pages/audio-clip-editor.md` — Kept lower-pane history without obsolete README comparisons.
- updated: `pages/automation.md` — Kept the track-automation history without a retired handover citation.
- updated: `pages/browser.md` — Removed the obsolete handover comparison.
- updated: `pages/build-and-test-forge.md` — Distinguished the README quick start from the canonical development guide.
- updated: `pages/build-and-test-rhino.md` — Distinguished the README quick start from the canonical development guide.
- updated: `pages/built-in-devices.md` — Removed the retired implementation-document pointer.
- updated: `pages/colours-and-typography.md` — Removed the retired implementation-document citation.
- updated: `pages/computer-keyboard.md` — Kept the current fallback contract without an obsolete doc comparison.
- updated: `pages/content-is-files.md` — Removed the retired implementation-document citation.
- updated: `pages/development-environment.md` — Removed outdated setup claims from deleted documents.
- updated: `pages/device-catalog.md` — Removed the retired long-form pointer.
- updated: `pages/device-chain-order.md` — Made the wiki page self-contained.
- updated: `pages/device-rack.md` — Removed obsolete README comparisons and retired long-form pointers.
- updated: `pages/displays-draw-from-the-dsp.md` — Preserved the design rule without citing a deleted document.
- updated: `pages/forge-arpeggiator.md` — Removed the retired milestone-plan citation.
- updated: `pages/forge-editor.md` — Removed stale README and plan comparisons while preserving concurrent note-guide guidance.
- updated: `pages/forge-engine-headers-only.md` — Removed legacy planning and README-drift notes.
- updated: `pages/forge-filter.md` — Removed retired plan and README comparisons.
- updated: `pages/forge-fx-slots-have-generic-parameters.md` — Kept the rejected alternative without a retired plan citation.
- updated: `pages/forge-hosting.md` — Made hosting and real-host validation guidance self-contained.
- updated: `pages/forge-is-an-independent-vst3.md` — Recast the decision and consequences without deleted architecture and instrument plans.
- updated: `pages/forge-knob-diameter-is-panel-wide.md` — Kept the sizing rationale without a retired plan citation.
- updated: `pages/forge-mixer-and-fx.md` — Removed the obsolete plan comparison.
- updated: `pages/forge-noise.md` — Removed the retired milestone citation.
- updated: `pages/forge-oscillators.md` — Removed obsolete handover and plan comparisons.
- updated: `pages/forge-presets-and-state.md` — Kept format history without deleted plan and handover references.
- updated: `pages/forge-spectral.md` — Made the spectral architecture and known warp gap canonical here.
- updated: `pages/forge-warp.md` — Removed retired plan and README comparisons.
- updated: `pages/forge.md` — Replaced the legacy document map with the wiki's canonical component map.
- updated: `pages/ids-not-pointers.md` — Removed the handover citation while keeping the stale-pointer failure history.
- updated: `pages/inputs-and-monitoring.md` — Removed obsolete README comparisons.
- updated: `pages/keeping-files-small.md` — Removed the handover/README comparisons and corrected the Editor file count to sixteen.
- updated: `pages/known-hazards.md` — Removed links to the retired drift analyses.
- updated: `pages/main-track.md` — Removed the handover citation while retaining the index pitfall.
- updated: `pages/mixer.md` — Made the fader and meter decisions self-contained.
- updated: `pages/no-rhinoedit-back-compat.md` — Removed the deleted instrument-plan citation.
- updated: `pages/note-editor.md` — Removed retired plan and README comparisons.
- updated: `pages/off-limits-directories.md` — Routed Tracktion research through the wiki and source map.
- updated: `pages/one-instrument-per-track.md` — Removed the deleted session-view citation.
- updated: `pages/pattern-presets.md` — Kept the slot-wide instrument consequence without the deleted session-view citation.
- updated: `pages/playhead.md` — Removed obsolete README comparisons.
- updated: `pages/pointer-and-selection.md` — Removed retired implementation-document and README comparisons.
- updated: `pages/project-files.md` — Kept export-history context without an obsolete README comparison.
- updated: `pages/real-time-audio-rules.md` — Made the realtime and polling rules canonical here.
- updated: `pages/recording.md` — Removed an obsolete README comparison.
- updated: `pages/region-editing.md` — Removed obsolete README and retired implementation-document comparisons.
- updated: `pages/rhino-build-targets.md` — Made the target layout canonical here.
- updated: `pages/rhino-eq.md` — Removed the retired implementation-document pointer.
- updated: `pages/rhino-tune.md` — Removed the retired implementation-document pointer.
- updated: `pages/rhino-vocoder.md` — Removed retired implementation-document pointers and preserved the correct test span.
- updated: `pages/session-model.md` — Kept split history without a deleted handover pointer.
- updated: `pages/session-view.md` — Made this page the resume guide and removed deleted handover/session-view references.
- updated: `pages/time-warp.md` — Made proxy and mode guidance self-contained.
- updated: `pages/track-kind-fixed-at-creation.md` — Removed obsolete README comparisons.
- updated: `pages/track-kinds.md` — Removed the retired stale-doc section.
- updated: `pages/tracktion-and-juce.md` — Grounded the decision in surviving research and current code.

## [2026-10-03] update | Forge filter remains stable under cutoff and modulation sweeps
- updated: pages/forge-filter.md (FAT's level-dependent damping is part of both sides of the implicit SVF solve; recorded the rapid-cutoff failure mode and live-cadence regression)
- updated: pages/forge-modulation.md (recorded the eight-slot, six-LFO multi-filter stress scenario and its finite, bounded and audible-output contract)

## [2026-10-04] update | Rhino Arp gains free-rate timing and a five-area face
- updated: `pages/rhino-arp.md` (documented the appended rate parameters, wall-clock millisecond scheduler, control layout and exact-spacing regression)
- updated: `pages/device-rack.md` (recorded that Arp-specific construction and styling live with its dedicated face)
- updated: `pages/append-only-stored-indices.md` (extended the append-only persistence rule to Rhino device parameter arrays)
- updated: `index.md` (catalogued Rhino Arp as a component and refreshed the append-only convention summary)

## [2026-10-05] update | Rhino Wave removed
- updated: pages/built-in-devices.md (dropped the Rhino Wave row; the Arp row links its own page)
- updated: pages/pattern-presets.md (eleven presets without the three Wave ones; a preset loads into the open clip's own track, not track 0)
- updated: pages/device-catalog.md (the catalog as of 2026-10-05, eight registered types, and patternKey no longer read)
- updated: pages/adding-a-device.md (dropped the instrument traps retired by a04b407 and b5d17e0)
- updated: pages/one-instrument-per-track.md (carriedInstrument carries every instrument by catalog entry)
- updated: pages/device-rack.md, pages/dependency-direction.md, pages/rhino-build-targets.md, pages/forge-is-an-independent-vst3.md (Rhino Wave mentions)
- updated: index.md (pattern presets summary)

## [2026-10-05] update | Catch-up on the 2026-10-05 commits: engine-played automation, the pattern track retired, model hazards closed, UI cost, Forge real-time fixes
- source: commits c9a37a6, 7e135cd, 34a7ce3, 89dfd1c, 492cc8b, 2a4830f, a04b407, 8dd3627, b5d17e0, 23de20c, e7c9554, c9d7d5d, fe163c8, d20faa4, 4416222, bac4059, 2df76f8, 63bb3b2, cf85e3f, b6c1496, 3f6bb4e, 32147e1, 2cc79de, 0d97e98, abdd73d, 2f9d5e0, 0f50919, 6eaa5ba, each checked against the code
- created: pages/ui-cost-of-a-change.md (synchronous announcements; drags announce once; hidden panels go stale via UiVisibility.h; painters cull; --profile-ui figures)
- created: pages/juce-rasteriser-not-clip-invariant.md (compare culled with unculled paints under the same clip; the cullRepaints seam and PartialRepaint.inc)
- created: pages/profile-paint-on-a-software-image.md (a plain juce::Image is a Direct2D bitmap on Windows; profile on SoftwareImageType; Forge's --profile caveat)
- updated: pages/automation.md (rewritten: the engine plays the lanes through mirrorAutomationToEngine; lanes and overrides keyed by rhinoDeviceKey; lanes go with a deleted device; the mirror must let go of plugins before the edit does)
- updated: pages/pattern-track.md (rewritten as the decision "No track is special for being first": what a04b407 retired and what replaced it; moved from Gotchas to Decisions)
- updated: pages/known-hazards.md (every Rhino hazard marked fixed with its commit, the Arp allocation fixed by the arp redesign rather than 492cc8b; the four Forge hazards still open)
- updated: pages/real-time-audio-rules.md (Arp breach gone; oversized blocks in pieces; ids resolved at construction; refill host buffers; the allocation counter and its HeapBlock blind spot; automation no longer polled)
- updated: pages/app-shell.md (30 Hz timer no longer plays automation; tempo field's Ctrl mid-drag; readout counts in the signature's note; hidden faces go stale; dropped a false README claim)
- updated: pages/playhead.md (painters cull to the strip, with measured gains; PartialRepaint.inc)
- updated: pages/track-groups.md, pages/group-is-a-bus-track.md (undo restores membership and routing; dissolveGroupInEdit; a track under a bus joins; bus refuses moved and pasted clips)
- updated: pages/mixer.md (drag- and paste-made lanes get a fader at once; fader drags announce once)
- updated: pages/clip-placement.md, pages/region-editing.md, pages/time-warp.md, pages/audio-clip-editor.md (moveClips, deleteClips, importAudioFilesAt; failures roll back whole; faithful clipboard snapshots; warp growth makes room; Repitch through a tempo change pinned)
- updated: pages/rhino-tune.md (latency from the device's own settings; dry hold through a range change; face settings via editDeviceSettings)
- updated: pages/rhino-eq.md, pages/rhino-vocoder.md (editDeviceSettings and the undo-free band selection; vocoder chunking and gate smoothing across blocks)
- updated: pages/writing-rhino-tests.md (8 MB stack, scopes closed per file, a document of its own, render only what you measure, --profile-ui)
- updated: pages/stack-overflow-reports-as-segfault.md, pages/cmake-does-not-reconfigure.md, pages/build-and-test-rhino.md (RhinoDAW's /STACK reserve and the configure it needs; --profile-ui is not a CTest case)
- updated: pages/device-rack.md (floating window lifecycle; editDeviceSettings; drag and hidden-rack exceptions to the rebuild rule; following automation; track 0 pitfall removed)
- updated: pages/built-in-devices.md, pages/device-catalog.md, pages/adding-a-device.md, pages/content-library.md (Utility not browsable; Drums read whole files and log unreadable ones; no output clamps; tails; latency from settings)
- updated: pages/forge-engine.md, pages/forge-arpeggiator.md, pages/forge-mixer-and-fx.md, pages/forge-modulation.md, pages/forge-spectral.md, pages/forge-oscillators.md, pages/forge-midi-learn.md (allocation-free blocks; the block promise; CC123 releases; mono legato; directNotes; arp overflow; isFxDestination range; rack NaN guard; spectral steal drains; spectrogram cache key; both stores collected)
- updated: pages/forge-panel-repaint-cost.md, pages/proving-a-forge-change-changed-nothing.md, pages/build-and-test-forge.md (tubes in the chrome layer; --profile paints on a plain image; --profile-audio; the allocation counter)
- updated: pages/transport.md, pages/note-editor.md, pages/session-model.md, pages/ids-not-pointers.md (loop span cleared on open; musicalPosition; hasPatternClip and repairPatternClip; fail-whole commands; never read a view's cache across a Session call; never keep a plugin past its edit)
- updated: pages/device-chain-order.md, pages/one-instrument-per-track.md, pages/session-view.md, pages/track-kind-fixed-at-creation.md, pages/track-kinds.md (MIDI effect placement fixed; lanes follow devices; slot paths refuse mismatched lanes; hidden session view goes stale)
- updated: pages/inputs-and-monitoring.md, pages/recording.md, pages/juce-output-stream-appends.md (trackWantsInput on rescan; nothing rebuilt under a take; createSample writes through a temporary file)
- updated: pages/project-files.md, pages/reproducing-live-timing-offline.md, pages/no-rhinoedit-back-compat.md, pages/arrangement-view.md (what restoreProject does now; live and rendered automation agree; slot-indexed lanes dropped; copy a ClipView across a Session call)
- updated: overview.md (history row and a reading pointer), index.md (new pages, the retitled decision, changed summaries)

## [2026-10-05] update | The native device standard: catalog factories, the SDK and the conformance runner
- created: pages/native-device-standard.md (the decision: one SDK base, one factory per catalog entry, one conformance runner, .rnd device files and generated faces still to come, no device-file compatibility until a production release)
- updated: pages/device-catalog.md (entries carry a create factory and are written with designated initialisers; patternKey gone; registerBuiltInTypes registers the factories; --device-test)
- updated: pages/adding-a-device.md (the factory is the whole registration; new devices are written on the SDK; the hand-written shape is the older one)
- updated: pages/build-and-test-rhino.md, pages/writing-rhino-tests.md (six CTest cases with native_device_conformance; run the cases through ctest)
- updated: index.md (the new decision and two summaries)

## [2026-10-05] update | Utility and Rhino Space move onto the device SDK
- updated: pages/native-device-standard.md (both devices on the base, proved byte-identical to the hand-written versions over 16 renders; per-block cost before and after; the remaining steps)
- updated: pages/adding-a-device.md (start a new device from Utility or Space; Bloom is the closest hand-written one)
- updated: pages/built-in-devices.md (which devices are on the SDK; how Utility declares its pass-through bus and smooths its gain)

## [2026-10-05] update | Rhino Arp's face reordered, Steps on a knob, Offset on a bar; the Device View's height floor
- source: commits d28a687, 67031ba
- updated: pages/rhino-arp.md (left-to-right order and arpLayout widths; orange arpSequence display; Steps knob and Distance disabled at 0 Steps; Offset bar with barFillsAcross and relative drag; slider styling in the shared styleControls; doubled "All" blemish; new DeviceRackTest assertions; snapshot painted with Theme; Related section)
- updated: pages/device-rack.md (named chain geometry and DeviceRack::minimumHeight of 219 px, with its test; links Rhino Arp)
- updated: pages/app-shell.md (lower pane floor per face, the write-back into lowerPaneHeight, the clamp untested)
- updated: pages/colours-and-typography.md (midiEffect and arpSequence tokens)
- updated: pages/headless-ui-snapshots.md (RHINO_ARP_SNAPSHOT; set a Theme on a snapshotted panel or it paints in LookAndFeel_V4)

## [2026-10-05] update | Rhino FM, the first instrument on the device SDK; .rnd files are presets
- created: pages/rhino-fm.md (four operators, eight routings, the index scale and its Bessel check, the Nyquist guard and depth taper, mono and pedal, the electric-piano defaults, cost)
- updated: pages/built-in-devices.md (Rhino FM in the table; it replaces Rhino Wave; three devices on the SDK)
- updated: pages/native-device-standard.md (.rnd corrected to mean a device's presets, with each device a folder in the browser; the user's priorities; Rhino FM done; remaining steps)
- updated: index.md (Rhino FM, and the standard's summary)

## [2026-10-05] update | Generated device faces
- updated: pages/device-rack.md (faces chosen by catalog id; the generated face for SDK devices and how it lays out)
- updated: pages/adding-a-device.md (a device on the SDK gets a generated face; name controls in full)
- updated: pages/native-device-standard.md (generated faces done; the undo-staleness and discrete-step bugs they found; remaining steps)

## [2026-10-05] update | Rhino FM's face: operator tabs, routing diagram, waveform
- updated: pages/rhino-fm.md (carriers and modulators; the face's tabs, diagram and picture; what they cost; the picture's tests)
- updated: pages/native-device-standard.md (tab groups and describe/DeviceDisplay on the SDK; the undo race on a removed value; stale 'not built yet' corrected)
- updated: pages/adding-a-device.md (tab groups and a described display)
- updated: pages/device-rack.md (tabs, the display, click and repaint behaviour)
- updated: index.md (Rhino FM summary)

## [2026-10-05] create | Device presets (.rnd)
- created: pages/device-presets.md (the format, where presets live, applying one in one undo step, browser rows, drops, the name-bar menu, tests)
- updated: pages/browser.md (device rows hold their presets; the device-preset drag kind)
- updated: pages/content-library.md (Presets/ and the user folder; summary)
- updated: pages/content-is-files.md (device presets are now files under library/)
- updated: pages/native-device-standard.md (presets built; remaining step)
- updated: pages/device-rack.md (the presets menu and preset drops on a panel)
- updated: index.md (new page; Content library summary)

## [2026-10-05] update | Gaps after Rhino FM's face and device presets
- created: pages/cachedvalue-lags-its-tree.md (the SDK undo race: a ValueTree listener read a stale CachedValue; failed only under ctest -j 3; forceUpdateOfCachedValue)
- updated: pages/native-device-standard.md (link to the new gotcha)
- updated: pages/build-and-test-rhino.md (a check failing only in parallel CTest)
- updated: pages/session-model.md (25 files, SessionDevicePresets; SessionPresets is pattern presets, and the overwrite it caused)
- updated: pages/pattern-presets.md, pages/device-presets.md (the two preset kinds and their files told apart; untested opened tree)
- updated: pages/headless-ui-snapshots.md (an opened TreeView snapshots with no children)
- updated: pages/rhino-fm.md (stroking was 285 of 789 µs, hence stroking at layout)
- updated: pages/browser.md (updated date)
- updated: index.md (new gotcha; Session summary)

## [2026-10-06] update | Drum Rack replaces Rhino Drums
- created: pages/drum-rack.md (16 sample-or-synth pads on C2-D#3, six controls, choke/mute/solo; PADS state and syncPads; what a pad keeps; .rdk/.rdp; session paths and drops; decisions: blank by default, synth pads not hosted instruments, 16 pads to match the note editor, kits not .rnd; the parameter-index trap for a seventh control; tests)
- updated: pages/built-in-devices.md (Drum Rack row; Rhino Drums section reduced to what carried over; on the SDK; summary)
- updated: pages/browser.md (six sections, Drums first; drumkit carries the .rdk path, new drum-preset kind; drum-preset audition; self-test counts Samples rows only; summary)
- updated: pages/content-library.md (Drums/Kits and Drums/Presets, userDrums and RHINO_USER_DRUMS_DIR, library: stored paths, drumTypes/drumTypeOf; samples read on the message thread; summary)
- updated: pages/content-is-files.md (kits and drum presets are files; content read off the audio thread, not in initialise(); samples named by place)
- updated: pages/device-catalog.md (catalog as of 2026-10-06 with Rhino FM and Drum Rack; nine factories; id Drums kept, type rhino.drumrack.v1)
- updated: pages/device-presets.md (ExactFloatText.h shared; the Drum Rack has kits instead of .rnd presets)
- updated: pages/device-rack.md (the Drum Rack face, pad drops, selected pad as view state, timer)
- updated: pages/headless-ui-snapshots.md (RHINO_DRUMS_SNAPSHOT)
- updated: pages/pattern-presets.md (a drum pattern loads the 808 Kit into a blank rack)
- updated: pages/note-editor.md (drum rows named by patternNoteName and repainted on drumRowNames; pitchName replaces drumLaneName)
- updated: pages/session-model.md (26 files, SessionDrums; summary)
- updated: pages/native-device-standard.md (Drum Rack on the SDK: content beside controls, its own face, primed for conformance; kits as the .rnd exception)
- updated: pages/real-time-audio-rules.md (counted stretches plus reader counts for the sample hand-off; flushing denormals, with measured strike costs)
- updated: pages/ui-cost-of-a-change.md (repaint what changed, cache shaped text, cheaper picture; Drum Rack face medians)
- updated: pages/writing-rhino-tests.md (FmTest and DrumRackTest; prime() for a device silent at its defaults)
- updated: pages/adding-a-device.md (content beside controls in a state child; prime(); faces chosen by catalog id)
- updated: pages/one-instrument-per-track.md (a drum clip carries its kit to a blank rack)
- updated: pages/track-kinds.md (addDrumSound refuses an audio track; a sample on a drum track goes to a pad)
- updated: pages/reproducing-live-timing-offline.md (DrumDevice replaced; the guard lives in DrumRackEngine::strike)
- updated: pages/rhino-build-targets.md (RhinoCore contents; which UI files include device headers; which core files include JUCE)
- updated: overview.md (library row, six CTest cases, history row)
- updated: index.md (new page; browser, built-in devices, content library and Session summaries)

## [2026-10-06] update | Drum Rack follow-ups; committing through git_commit
- updated: pages/git-workflow.md (the HARBIZ git_commit tool commits by pathspec: never pre-stage with git rm or git add; git restore --staged is the way out)
- updated: pages/adding-a-device.md (content is read on the message thread when it changes, no longer "in initialise()")
- updated: pages/drum-rack.md (a pad played from the face is a preview bit that prepare clears; the 808 Kit's read time; the saved kit and preset found under a temporary Drums root)
- updated: pages/content-is-files.md, pages/content-library.md (the 808 Kit's read time; AGENTS.md no longer quotes Rhino Drums' figure)
- updated: pages/device-rack.md (a menu for one spot on a face opens there; Rhino EQ's band-type menu still anchors to the whole panel)

## [2026-10-07] update | Drum Rack: a pad on every note, a MIDI map and a sample editor
- created: pages/drum-rack-sample-editor.md (one-shot, classic, slice; playback as undoable pad content, not automatable; PAD properties and file refusals; drag protocol; DrumSlicer; spreading slices to pads; shapeAt departs from displays-draw-from-the-dsp)
- created: pages/juce-path-isempty-ignores-a-lone-point.md (isEmpty() is true for a path holding only a start point; the invisible envelope line; a begun flag)
- updated: pages/drum-rack.md (128 pads addressed by note; bank and firstNote view state; kits stay on C2; 768 controls n<note><Control>; range reads and their cost; listener fan-out slowing setKit; the fallback editor reads the selected pad's six; sparse PADS; noteOff reaches the engine; kit format 2; showDrumBank, patternDrumLowestNote, firstEmptyPad from the bank; decision 128 pads over 16 movable; tests; summary)
- updated: pages/device-rack.md (the 852 px face across five units: map, bank, KEY readout, sample side, Slice to pads; firstNote view state; frames; desktop drops stop at 127; the fallback editor reads only the six it shows)
- updated: pages/headless-ui-snapshots.md (RHINO_DRUMS_SNAPSHOT at 852 px showing a sliced roll; the snapshot caught the invisible envelope)
- updated: pages/note-editor.md (drum rows follow the rack's bank; padNoteName and pitchName)
- updated: pages/ui-cost-of-a-change.md (a frame reads only the controls it shows; sample knob drags skip the session; Drum Rack medians for 2026-10-07; summary)
- updated: pages/session-model.md (SessionDrums banks and slices; parameter range reads; announced view state)
- updated: pages/content-library.md, pages/content-is-files.md (kit format 2 by note, factory kits converted; 808 Kit read now 5.3-6.3 ms warm, 51.6 ms cold)
- updated: pages/real-time-audio-rules.md (playback written field by field, torn read accepted; preview mask; solo count)
- updated: pages/writing-rhino-tests.md (look at the PNG; ask a face where its parts are; nudging drumNotesSeen)
- updated: pages/native-device-standard.md (many controls cost; per-item settings belong in content; pads declared in a Pads tab group)
- updated: pages/keeping-files-small.md (the Drum Rack face's five units and the device's two, each with an Internal.h)
- updated: pages/displays-draw-from-the-dsp.md (Drum Rack pictures; shapeAt is an untested closed-form exception)
- updated: pages/git-workflow.md (git_commit's hook refused a message over an "alternate data stream"; trigger not established)
- updated: overview.md (history row)
- updated: index.md (two new pages; Drum Rack and ui-cost summaries)

## [2026-10-07] update | Drum Rack: drag a pad onto another note to move or swap it
- updated: pages/drum-rack.md (Moving a pad: what travels with it, movePad's single batch and both samples re-read, moveDrumPad's undo names, selection and refusals; decision that clip notes and lanes stay on their notes, moving lanes rejected; six face units; checkMovingPads and the face test's drags)
- updated: pages/device-rack.md (sixth face unit DeviceEditorPanelDrumGestures.cpp; the pad drag: 4 px slack, drumNoteAt, lit pad or map cell, ghost beside the pointer, status reports)
- updated: pages/keeping-files-small.md (the face's six units; pointer handling split out at 694 lines)

## [2026-10-07] update | A device face hears each of its clicks once; Drum Rack Auto Select
- source: commits a033e1a, 60442e5
- created: pages/juce-self-listener-hears-clicks-twice.md (addMouseListener(this, true) delivered every event on DeviceEditorPanel twice: face toggles flipped back off, the wheel moved double, a double-click ran twice; ChildMouse passes on only the children's events; direct handler calls cannot see it; the RHINO_NATIVE_INPUT_TEST peer check)
- updated: pages/drum-rack.md (Auto Select: autoSelect view state, default on, no undo; tickDrums selects the lowest filled pad that arrived, never under a held button, idle while hidden; decision that it follows clips and leaves the bank; tests)
- updated: pages/writing-rhino-tests.md (rule 9: a direct handler call skips JUCE's dispatch; real clicks through ComponentPeer::handleMouseEvent off-screen, gated by RHINO_NATIVE_INPUT_TEST=1; Auto Select nudges drumNotesSeen)
- updated: pages/device-rack.md (ChildMouse and the self-listener rule in Gestures; the AUTO switch and Auto Select in the drum face bullet; autoSelect as view state; the gated real-click test)
- updated: pages/headless-ui-snapshots.md (checks that need a desktop peer stay off-screen at -10000,-10000, run only on request)
- updated: index.md (the new gotcha)

## [2026-10-07] update | The Drum Rack opens maximised in a window of its own
- source: commit 94d4d9f
- updated: pages/drum-rack.md (Its own window: the stacked layout from drumStackedHeight, following the rack by EditItemID, closing with the rack or the document, one window at a time, drops, automation, keys, two faces on one rack, the unverified cross-window browser drag; two kept picture widths; Edit no longer opens the fallback for a rack; summary; tests)
- updated: pages/device-rack.md (the name bar's window button with KEY left of it; Edit on a Drum Rack opens DrumRackWindow; how the rack owns and closes it; the window test)
- updated: pages/drum-rack-sample-editor.md (the editor across the window's width)
- updated: pages/ui-cost-of-a-change.md (pictures kept at two widths, and why)
- updated: pages/ids-not-pointers.md (the window keeps its rack's itemID)
- updated: pages/headless-ui-snapshots.md (RHINO_DRUM_WINDOW_SNAPSHOT)
- updated: index.md (the Drum Rack's summary)

## [2026-10-07] update | Drum Rack window: gaps left by the first record
- source: commit 94d4d9f
- updated: pages/drum-rack.md (F12 in the drum window toggles the main window's full screen, since every key goes to the shell)
- updated: pages/keeping-files-small.md (isDroppedSoundFile in BrowserIds.h; three copies of the extension list remain, and SessionDrums.cpp cannot take the UI-side header)
- updated: pages/development-environment.md (Pillow's getbbox on RGBA reads only alpha, so opaque snapshot diffs always look identical; convert to RGB)

## [2026-10-09] update | The DJ view and the booth
- source: branch feat/dj-view
- created: pages/dj-view.md (the booth as a second audio callback with Rhino's own engine, decks with hot cues, loops, quantised starts, master and sync with phase lock, the isolator mixer and beat effect, files read on a worker and tracks or groups bounced and re-bounced by beat, Live and Edit, the analysis of waveform, tempo, drops and key, what the document saves, decisions and what is not built)
- updated: pages/session-view.md (the DJ view took the switch; SessionView is built only by the tests), pages/app-shell.md (the second view, Tab, djPoll on the timer), index.md

## [2026-10-09] update | The DJ bounce on a copy of the document, and the gaps in the DJ record
- source: commits e4040dd and 363fd05 (branch feat/dj-view), checked against native/src/core/Dj*, SessionDj*, DjView*, Main.cpp and the tests
- created: pages/dj-bounce-on-a-copy.md (why the bounce moved from the message thread to the worker with a snapshot copy; DjBounceWork's ownership and the drain before drop; the 60 s budget; the MessageManagerLock that makes the test runners render inline; a group names its bus and its members)
- updated: pages/dj-view.md (the booth is built and attached on the first deck; the bounce bullet shortened to link the decision; removing a deck closes the gap and reloads the decks that moved; Live refuses audio tracks and tracks without an instrument; Live/Edit/Reload only on track decks; readouts repaint on text change; deck removal deferred with callAsync; which DJ changes dirty the document and why markDjModified bypasses markModified; the scratchpad reference replaced by the method; the stale Not-built item)
- updated: pages/renders-that-never-return.md (four causes, not three; the bounce's budget), pages/session-model.md (28 units; the SessionDj pair; markModified marks decks stale), pages/keeping-files-small.md (SessionDjInternal.h), pages/writing-rhino-tests.md (DjBooth.inc's place and its own document; import-onto-new-track renders belong before GroupBusReload), pages/headless-ui-snapshots.md (RHINO_DJ_SNAPSHOT), pages/ids-not-pointers.md (a deck holds an EditItemID and keys displays on generation), pages/recording.md (the booth is the third callback that clears its scratch buffer), pages/real-time-audio-rules.md (DjEngine's counted blocks and command queue; djPoll), pages/measure-sound-dont-read-dsp.md (DjTest's measurements; replicate wrong arithmetic outside the binary), pages/ui-cost-of-a-change.md (DjView waits while hidden), pages/app-shell.md (where the switch sits), index.md

## [2026-10-09] update | The DJM-V10 control set, the jog wheel and the mixer at one height
- source: branch feat/dj-view, the user's second request (the mixer's height fixed, a new channel mislaid until a resize, the V10's missing controls, a jog wheel on every console, a CDJ layout)
- updated: pages/dj-view.md (the channel's compressor, four-band EQ, send and fader curve; the headphones' level and mono split; the mic with talkover; the send/return unit; fourteen beat effects with band keys, manual time and tap; the deck's brake, scratch, cue search and per-deck quantize; the CDJ console with its jog wheel; the mixer at one height; sync before layout, the resize bug; what the document saves; not built)

## [2026-10-09] update | The second DJ pass checked against the code: the mic's gap, two gotchas, what the tests taught
- source: branch feat/dj-view, the V10 control set, the jog wheel, the CDJ console and the mixer at one height, checked against native/src/core/DjMixer*, DjEngine.h, SessionDj.cpp, SessionDjSources.cpp, SessionDjInternal.h, DjView.cpp, DjDeckPanel.cpp, DjControls.h, DjMixerPanel.cpp, tests/DjTest.cpp and DjBooth.inc
- created: pages/juce-button-fires-on-right-click.md (juce::Button clicks for any mouse button; DjPad swallows the popup-menu press in mouseDown and mouseUp; every console key with a second action is a DjPad)
- created: pages/sync-children-before-layout.md (DjView::sync laid out before mixer.sync(), so the layout read the old strip count; mixer first, and laidOutMixerWidth re-lays out when the measured width changes; why the view test did not see it)
- updated: pages/dj-view.md (the mic is wired inside DjEngine::process but DjBooth's callback still calls the three-argument form, so nothing feeds it in the app, moved to Not built; loading a track or a group now marks the document, correcting the 363fd05 note; the send unit's glide and the 100 blocks a test waits; the hidden jog's empty bounds; the DjPad rule; links to the three gotchas)
- updated: pages/juce-self-listener-hears-clicks-twice.md (DjDeckPanel is a second caller of addMouseListener(this, true), deduplicating by event.eventTime, fine for an idempotent select and not for a toggle)
- updated: pages/measure-sound-dont-read-dsp.md (what DjTest now measures; ask of each band what its crossovers can give, −30 dB outer and −15 dB inner for the LR4 four-band isolator; let smoothing settle before measuring)
- updated: pages/writing-rhino-tests.md (a containment check reads hidden children too, so a control the layout hides takes empty bounds)
- updated: pages/keeping-files-small.md (the DJ booth's six files past 600 lines; DjMixer.cpp at about 1,150 is the first to split)
- updated: pages/real-time-audio-rules.md (a delay's time is smoothed too: DjSendFx's glide and its reset on a type change)
- updated: pages/ui-cost-of-a-change.md (DjMixerPanel's tempo readout repaints only when its text differs from drawnBpm)
- updated: index.md (the two gotchas)
- notes: the entry "The DJM-V10 control set, the jog wheel and the mixer at one height" was first inserted before the bounce entry rather than appended, and was moved below it in the same session.

## [2026-10-09] update | The mic wired through the booth's callback
- source: branch feat/dj-view, the keeper's finding above, fixed in native/src/SessionDj.cpp, SessionDjInternal.h, Session.h and tests/Arrangement/scenarios/DjBooth.inc
- updated: pages/dj-view.md (the booth's callback hands the device's inputs to the engine; Session::djProcessOffline drives the booth through that callback, with an overload taking inputs, so the scenario feeds a tone and reads the mic's meter; the Not-built item reduced to the input chooser; the view scenario adds a third deck while the view stands and checks every strip clears the master section)

## [2026-10-09] update | The mic fix checked against the code; two stale lines corrected
- source: native/src/SessionDj.cpp, SessionDjInternal.h, Session.h and tests/Arrangement/scenarios/DjBooth.inc after the fix; the mixer and Not-built paragraphs of dj-view.md already matched them
- updated: pages/dj-view.md (the engine bullet no longer says the tests drive djProcessOffline "directly": it goes through the booth's own callback, inputs included)
- updated: pages/sync-children-before-layout.md (the view scenario now adds a third deck to the standing view and checks every strip clears the master section, replacing the note that no test covered the ordering)

## [2026-10-10] update | The lower pane and Space follow the focused deck
- source: branch feat/dj-view, the user's request that the clip and device panes follow the focused console and that the arrangement's transport give way to it; native/src/Main.cpp (followDeck, setSessionViewOpen, Space and F9, RecordButton disabled), DjView.*, DjDeckPanel.* (the focus mark), SessionDjSources.cpp (djDeckEditClip by the track's kind), tests/Arrangement/scenarios/DjBooth.inc
- updated: pages/app-shell.md (the transport dimmed and stopped in the DJ view, Space as the focused deck's play key, the pane following a console as it follows a card, the read-before-move gotcha in followDeck), pages/dj-view.md (the focus mark, focusDeck, togglePlayFocused)

## [2026-10-10] update | The focused-deck record checked against the code; one build tree only
- source: native/src/Main.cpp (followDeck, setSessionViewOpen, the Edit handler, RecordButton), DjView.*, DjDeckPanel.cpp, SessionDjSources.cpp (djDeckEditClip) and tests/Arrangement/scenarios/DjBooth.inc after the entry above; the user's instruction of 2026-10-10 that only native/build exist
- updated: pages/app-shell.md (the Edit key opens the audio editor for an audio track's clip; an open clip editor closes when the deck's track has no clip; the updated date)
- updated: pages/dj-view.md (the Edit key line names both editors and djDeckEditClip; the updated date)
- updated: pages/writing-rhino-tests.md (a shell behaviour is checked at the view's callback boundary, as DjBooth.inc does with deckSelected and togglePlayFocused; followDeck is unchecked)
- updated: pages/build-and-test-rhino.md, pages/orphaned-build-processes.md, pages/locked-executable-lnk1104.md (no second native/build-* tree: the user asked on 2026-10-10 that only native/build exist, and native/build-dj was deleted)

## [2026-10-10] update | The console's clips grid, and clips that cross the views
- source: branch feat/dj-view, the user's request for a clips grid on each console with drag and drop to and from the arrangement across the view switch, library drops held to the track's kind, and one-bar clips on a double-click; native/src/DjClipGrid.* (new), ClipDrag.h (new), DjDeckPanel.*, Main.cpp (ViewToggle), ArrangementGestures.cpp, ArrangementDrops.cpp, SessionSlots.cpp (createSlotClip, copyClipToTrackSlot, a target lane), SessionClips.cpp (findClip sees slots), SessionInternal.* (copyClipInto shared, trackHoldingClip), SessionDjSources.cpp (loadDjDeckSlot, the slot bounce), SessionDj.cpp (the slot saved), tests/Arrangement/scenarios/DjBooth.inc
- updated: pages/dj-view.md (a new section, The clips grid), pages/session-view.md (findClip and dragging between views no longer missing), pages/app-shell.md (the toggles take drags), pages/arrangement-view.md (a new section, Clips that leave the panel), AGENTS.md and README.md

## [2026-10-10] update | The view switch as two icons at the end of the bar
- source: branch feat/dj-view, the user's request with the two SVGs; native/assets/arrangement_view.svg and dj_view.svg (new, compiled in beside the fonts in native/CMakeLists.txt), Main.cpp (ViewToggle on IconButton, the right of the bar reordered: view switch, metronome, undo and redo), ControlBarIcons.* (setIdleColour), Theme.h (viewActive, viewIdle)
- updated: pages/app-shell.md (the bar's right-hand order and the switch's icons, colours and currentColor substitution)

## [2026-10-10] update | The clips-grid and view-switch record checked against the code
- source: native/src/DjClipGrid.*, ClipDrag.h, DjDeckPanel.cpp (the grid's layout and the console's drops), ArrangementGestures.cpp, ArrangementDrops.cpp, SessionSlots.cpp, SessionDjSources.cpp (loadDjDeckSlot, the slot bounce), SessionDj.cpp (the slot property), SessionInternal.cpp, BrowserPanel.cpp (file against sample drops), Main.cpp (ViewToggle, layoutControlBar), Theme.h, ControlBarIcons.h, native/CMakeLists.txt, tests/Arrangement/scenarios/DjBooth.inc and SessionView.inc, after the two entries above
- updated: pages/dj-view.md (DjClipGrid.* and ClipDrag.h in the file table; where the grid stands and when it takes empty bounds; a Drum Rack track takes a sound file, not a built-in sample, onto a pad; the slot among what the document saves)
- updated: pages/session-view.md (the routes across name copyClipToTrackSlot, the target lane, copyClipInto and createSlotClip; the updated date, which still read 2026-10-09)
- updated: pages/arrangement-view.md (one clip leaves, the primary selection; the updated date, which still read 2026-10-05)
- updated: pages/app-shell.md (a drop on a toggle itself does nothing)
- updated: pages/dj-bounce-on-a-copy.md (a slot deck's span is its clip's length, laid on the copy's timeline in place of the arrangement's clips)
- updated: pages/writing-rhino-tests.md (rule 10, leave the clip slots empty: SessionView.inc's first check follows DjBooth.inc; the grid takes empty bounds like the jog wheel; DjClipGrid and the DJ panels befriend the runner)
- updated: pages/content-is-files.md and index.md (the view switch's two SVGs are embedded beside the fonts, the summary says so), pages/colours-and-typography.md (the SVG icons as the exception to path glyphs), pages/cmake-does-not-reconfigure.md (a new binary-data asset fails at compile on the missing BinaryData symbol, not at link)

## [2026-10-10] update | The Clip toggle opens the focused deck's clip in the DJ view
- source: branch feat/dj-view, commit d745210; native/src/Main.cpp (ControlWindow::toggleClipEditor reads a new clipForPane(): in the DJ view Session::djDeckEditClip(djView.selectedDeck()), else the arrangement's selected clip, which the arrangement never has while the consoles show; the DJ-view refusal says to focus a deck playing a track of the song). The main agent had already written the clipForPane sentence into pages/app-shell.md's lower-pane section; checked against the code, it is right
- updated: pages/app-shell.md (the followDeck sentence said djDeckEditClip gives the first clip of the track's kind, which has been incomplete since the clips grid: it gives the cell the deck plays first, as the new sentence beside it says)
- updated: pages/dj-view.md (the Edit key line says the same, and names the Clip toggle as the other way to that clip)
- notes: pages/writing-rhino-tests.md and pages/ui-cost-of-a-change.md say nothing about the Clip toggle and needed no change; the shell stays unreachable by tests, so the fix is covered by the build and the suite only

## [2026-10-10] update | Three bugs from the console's clips: a bare track's cell, a copy's name, a delete under the editor
- source: branch feat/dj-view, the user's report; native/src/SessionDjSources.cpp (a MIDI track with no instrument bounces as silence of the span rather than failing), SessionSlots.cpp (uniqueClipNameOnTrack for copies into cells and new cells; copyClipInto sets the name after cloneFrom; deleteSlotClip, copyClipToTrackSlot and insertAudioFileInSlot call repairPatternClip), tests/Arrangement/scenarios/DjBooth.inc (the reported flow on a bare MIDI track with a Drum Rack and kit added after and a note drawn; the copy named on; a delete under the editor)
- updated: pages/dj-view.md (the clips grid section: the name rule, the silent bounce, the delete repairing the editor)

## [2026-10-10] update | The three clips-grid fixes checked against the code; where the renderer's refusal and the repair rule belong
- source: branch feat/dj-view, commit 4fa1f69; native/src/SessionDjSources.cpp (bounceDjDeck's silence path), SessionSlots.cpp (uniqueClipNameOnTrack, copyClipInto, the three repairPatternClip calls), tests/Arrangement/scenarios/DjBooth.inc. The main agent's clips-grid sentences in pages/dj-view.md are right on the name rule, the delete and the silence; one was narrower than the code
- updated: pages/dj-view.md (the silence sentence said "a cell": the check is any track deck, SONG or cell, on a bare MIDI track, sits after the no-clips refusal and names the engine's message; the delete sentence now lists the copy over a filled cell and the sample drop that repair too; a Decisions bullet for silence over a refusal, with the reason)
- updated: pages/dj-bounce-on-a-copy.md (the "Didn't find any audio to render" bullet now carries both known triggers, the bus without its members and the instrument-less MIDI track; the render paragraph says such a track is not rendered at all and how the silence is made and installed)
- updated: pages/session-view.md (the unique-name rule and copyClipInto's setName after cloneFrom beside createSlotClip; the findClip bullet says every slot path that removes a clip owes the editor a repairPatternClip, and which three call it)
- updated: pages/ids-not-pointers.md (the "every path that removes a clip" line names the slot paths as the exception that expired when findClip learnt slots)
- notes: pages/renders-that-never-return.md left alone, the renderer's refusal returns at once and is not a hang, and its message is recorded where it already was, on the bounce decision page. No index change, no summary moved

## [2026-10-10] update | A knob held on a deck's track is heard live
- source: branch feat/dj-view, the user's report that device changes reached a deck a bounce late where the arrangement answers at once, and their rule: favour the sound, re-render only when the knob is let go, on a parallel process; native/src/core/DjTrack.h (DjMidiEvent, DjTrack::midi), core/DjEngine.* (DjLiveSink, the per-deck sequencer, the bounce silenced while previewing), core/DjDeck.h (livePreview), SessionDjInternal.h (the job's notes, the booth as the sink, liveBeforePreview), SessionDj.cpp (the booth's noteOn/noteOff into the inputs' keyboard state), SessionDjSources.cpp (the notes gathered at bounce time, djBeginLivePreview/djEndLivePreview/djFinishPreview, finishDjLoad ending a preview, djPoll skipping a previewing deck, djDocumentChanged by track), SessionDevices.cpp (the gesture hooks; a parameter change names its track), Session.cpp and Session.h (markModified takes the track), tests/DjTest.cpp (checkLivePreview), tests/Arrangement/scenarios/DjBooth.inc (a knob held on the Drum Rack)
- updated: pages/dj-view.md (a new section, Live preview while a knob is held)

## [2026-10-10] update | The live-preview record checked against the code; the audio-thread sink and the notes a bounce carries
- source: branch feat/dj-view, commit 6f7fc8a; native/src/core/DjEngine.cpp (sequence, releaseSounding, the scratch buffers zeroed before the strip), core/DjTrack.h (DjMidiEvent, DjTrack::midi), SessionDj.cpp (DjBooth::noteOn/noteOff into the inputs' keyboardState), SessionDjSources.cpp (bounceDjDeck's note gathering, djBeginLivePreview/djEndLivePreview/djFinishPreview, finishDjLoad, djPoll, djDocumentChanged by track, setDjDeckLive returning a failed Result with the deck live), SessionDevices.cpp (the gesture hooks), Session.cpp and Session.h (markModified(track)), tests/DjTest.cpp (checkLivePreview), tests/Arrangement/scenarios/DjBooth.inc. The main agent's section in pages/dj-view.md is right except one phrase: only the poll refuses to bounce a previewing deck, a Reload or a cell change still does
- updated: pages/dj-view.md (the preview section: the poll-only skip, a bounce that fails ends the preview too, the setDjDeckLive Result-versus-state trap; a Decisions bullet for the live sound bypassing the booth's mixer, with the user's rule; a Not-built line for a tap of the live output into the strip; the stale bullet says a change naming its track marks only the decks playing it)
- updated: pages/real-time-audio-rules.md (the one judged exception: a sink called from the booth's audio thread into MidiKeyboardState and Tracktion's input queue, why it was accepted, and the sink's contract)
- updated: pages/dj-bounce-on-a-copy.md (a bounce carries the material's notes: what is gathered, where, how they ride the job, and that the silent bounce carries them too)
- updated: pages/session-model.md (markModified names a track so only its decks go stale), pages/computer-keyboard.md (the preview takes the same keyboardState entry from the audio thread)
- notes: no index change, no summary moved, no new page; the gotcha about setDjDeckLive's Result lives in the preview section beside the code that reads the flag

## [2026-10-10] update | The preview's fade, the preview held off while the song rolls, a smaller screen
- source: branch feat/dj-view, the user's report of a click on every knob touch and request for a smaller waveform display; native/src/core/DjEngine.h/.cpp (previewGains, the bounce fading into and out of a preview), SessionDjSources.cpp (no preview while the song's transport rolls, since the track then plays its own clips live), DjDeckPanel.cpp (the screen capped at a third of the console and 220 px), tests/DjTest.cpp (silence judged once the fade has passed)
- updated: pages/dj-view.md (the fade in the preview section; the screen rule in the view section)

## [2026-10-10] update | The fade, the held-off preview and the screen rule checked against the code
- source: branch feat/dj-view, commit 00aca9b; native/src/core/DjEngine.cpp (renderChunk: the fade before the strip, the 0.9999 threshold), core/DjMixer.h (DjSmoothedGain is a one-pole), SessionDjSources.cpp (djBeginLivePreview returning while the transport plays), DjDeckPanel.cpp (resized: the screen's 32 % share, its 220 px cap, the keys held at full size down to the screen's minimum), DjDeckDisplay.h (the minimum's parts), tests/DjTest.cpp (checkLivePreview). The main agent's two edits are right; the page did not record that no preview begins while the song's transport rolls
- updated: pages/dj-view.md (the preview section: the fade's shape and where it sits, the test's threshold, the transport-rolling refusal and its reason; the view section: how the screen's share yields to the keys' full size, the grid at 1240×720; the Decisions bullet and the Not-built line now describe the two hidden infrastructure devices and the rejected hidden-mixer device)
- notes: no index change, no summary moved, no new page

## [2026-10-10] update | Repeated knob gestures: Live kept on, the preview kept while held, a lighter copy, a crossfaded swap, no blank screen
- source: branch feat/dj-view, the user's report of clicks and a lagging display over repeated gestures and their rule to favour audio and smooth every transition; native/src/core/DjDeck.h/.cpp (the swap crossfade), core/DjEngine.cpp (the old material retired a block later), SessionDjInternal.h (liveCoolsAt, liveCooldownMs), SessionDjSources.cpp (the Live cooldown in djPoll and djFinishPreview, the preview kept while a gesture is held, the copy built without the tracks the bounce does not play, the copy timed), DjDeckDisplay.cpp and DjDeckPanel.cpp (the picture and the keys kept through a load), tests/DjTest.cpp (checkSwapCrossfade), tests/Arrangement/scenarios/DjBooth.inc (a second gesture before the landing; Live left on after; the old material kept through a cell switch)
- updated: pages/dj-view.md (a new section, What a knob touch costs, and what it no longer costs)

## [2026-10-10] update | The knob-gesture record checked against the code; the stripped copy, the routing-by-position trap, the extra retired block
- source: branch feat/dj-view, commit 73a32e6; native/src/core/DjDeck.cpp (adopt, render: swapFrom sized to ten milliseconds or the block, forgotten at the block's end and on the not-playing return), core/DjEngine.cpp (setTrack retiring at begun + 1, collect), SessionDjInternal.h (liveCoolsAt, liveCooldownMs), SessionDjSources.cpp (djFinishPreview starting the cooldown, djPoll ending it, djBeginLivePreview taking liveBeforePreview only with no cooldown pending, setDjDeckLive(false) clearing it, finishDjLoad keeping a preview while a gesture is held, bounceDjDeck stripping the state copy and re-routing each member by the bus's id, the copy timed into the bouncing log line), DjDeckDisplay.cpp and DjDeckPanel.cpp (sync through a load), tests/DjTest.cpp (checkSwapCrossfade, checkEngineHousekeeping), tests/Arrangement/scenarios/DjBooth.inc. The main agent's section is right on the mechanics; corrected the cooldown's start (the preview's end, not the release), the re-routing's scope (each member, by the bus's id), and the keys that stay lit (Live and Edit wait)
- updated: pages/dj-view.md (the knob-touch section made precise, with the cooldown trap in djBeginLivePreview and the measured copy time; the preview section no longer says finishDjLoad puts Live back; the material bullet and the Not-built line carry the extra retired block and the copy's new cost)
- updated: pages/dj-bounce-on-a-copy.md (the copy holds only the bounce's tracks and every bus, timed in the log; the stripped copy's routing set again by id, and why)
- updated: pages/track-groups.md (pitfall: a member's output names its bus by position, so a reduced copy must re-route), pages/group-is-a-bus-track.md (the consequence in one line)
- updated: pages/real-time-audio-rules.md (the counting rule is begun plus one, and what the extra block is for)
- notes: no index change, no summary moved, no new page

## [2026-10-10] update | A set worked on while it plays: edits mark their track, bounces land, the worker yields, the preview arms nothing, a MIDI screen
- source: branch feat/dj-view, the user's report that the deck fell silent while an Arp knob was held and that repeated gestures delayed everything, with rhino.log showing every edit re-rendering both decks and a Forge bounce cancelled under itself; native/src/SessionDevices.cpp, SessionDrums.cpp, SessionNotes.cpp (edits name their track; patternTrackIndex), SessionDjSources.cpp (a bounce in flight is let land and the deck bounces once more at landing; the preview injects into the track with no Live switch; the cooldown removed), SessionDjInternal.h and SessionDj.cpp (previewTracks, injectLiveMidiMessage, the worker at low priority), DjDeckDisplay.cpp (a MIDI track's notes drawn instead of its waveform), tests/Arrangement/scenarios/DjBooth.inc (Live untouched by a preview; an edit under a bounce in flight)
- updated: pages/dj-view.md (the preview section rewritten for the injection; the knob-touch section rewritten with the six causes and the MIDI screen)

## [2026-10-10] update | The set-worked-on record checked against the code; the preview's queue, the keyboard-state entry given up, and the pages the old sink left stale
- source: branch feat/dj-view, uncommitted; native/src/SessionDjInternal.h (DjBooth as an AsyncUpdater, PreviewNote, previewFifo of 1024, previewDropped, previewTracks as plain pointers, the worker at Priority::low), SessionDj.cpp (noteOn/noteOff into the fifo, deliverPreviewNotes injecting on the message thread, the drop logged once), SessionDjSources.cpp (bounceDjDeck's in-flight rule, finishDjLoad's bounceAgain on both paths, djBeginLivePreview/djFinishPreview, djPoll draining first and skipping a loading deck), Session.cpp (markModified to djDocumentChanged by track), SessionDevices.cpp, SessionDrums.cpp, SessionNotes.cpp (every edit names its track; addClipDevice the one that cannot), core/DjEngine.h and .cpp (the sink's contract, sequence), DjDeckDisplay.cpp (paintTile's note rows), tests/Arrangement/scenarios/DjBooth.inc. The main agent's two sections are right on the mechanics; the queue's size, the drop rule, the poll's drain, the plain pointers and why the keyboard-state entry was given up were missing, and the Decisions bullet still called the live sound "monitoring"
- updated: pages/dj-view.md (a "How the notes cross" paragraph in the preview section with the engine's thread rule and where it was read; the stale bullet and the knob-touch bullets say the poll skips a deck whose bounce is in flight and that the worker's priority is app-only; the clip-effect exception; the Decisions bullet corrected; the model table and the display line point at the new paths)
- updated: pages/real-time-audio-rules.md (the judged exception replaced by the fifo-and-async-wake pattern, and that the keyboard-state write went the same day)
- updated: pages/computer-keyboard.md (the preview no longer takes the keyboardState entry, and why), pages/session-model.md (every device, drum and note edit names its track)
- updated: pages/dj-bounce-on-a-copy.md (only a new source cancels a bounce; an edit under one lets it land and bounces again; the worker at low priority)
- updated: pages/off-limits-directories.md (a body is read at the pinned commit on GitHub, with the URL; summary changed), pages/inc-edits-do-not-rebuild.md (the mirror case: a stale binary fails a check just written)
- updated: index.md (the off-limits summary)
- notes: no new page. The suite's state when the brief was written: one build before the queue change passed everything but the new booth check, which failed for the re-bounce at landing that was then added; the rebuild with the queue and the re-bounce was still running, so nothing here claims the suite passes

## [2026-10-10] update | The rebuild passed
- source: the build with the preview queue and the re-bounce at landing, run through ctest; all six cases passed, the booth scenario with its two new checks among them
- updated: nothing else; this closes the open note in the entry above

## [2026-10-11] update | The session view removed; clip slots kept as the DJ consoles' cells
- source: the user's "I won't be using session view anymore, so you can discard that code" and "I don't care anymore about the session view, just arrangement and DJ view"; branch feat/dj-view, uncommitted; native/src/Main.cpp (Back to Arrangement and its anyTrackPlayingSlots poll gone, the sessionViewEnabled gate gone, the DJ view's switch renamed djViewOpen, setDjViewOpen, djToggle), Session.h and SessionSlots.cpp (the launching model, scene names, scene deletion, the scene watcher, the instrument-clip inserts and addScene gone; every cell edit names its track to markModified; the pattern and audio inserts grow the scenes after their refusals; SlotClip::colour is clipColour), SessionInternal.h/.cpp (presetColour and instrumentColour gone), DjClipGrid.cpp (no scene added before a drop; a cell with no colour painted in its track's), Arrangement.cpp (the clip menu's Copy to the track's DJ clips), native/CMakeLists.txt, tests/Arrangement/scenarios (ClipCells.inc and DrumKitDrops.inc new, SessionView.inc gone, SharedMixer.inc, UiCost.inc and ClipEdits.inc reworked), tests/UiProfile.cpp (a hidden DjView in the session view's place); a probe of the bounce's copy, run in the arrangement runner and then removed
- created: pages/clip-slots.md (pages/session-view.md rewritten under the name of what remains, and deleted)
- updated: pages/dj-view.md (the intro, the clips grid's + row and colour, the cell edits' marks), pages/dj-bounce-on-a-copy.md (a third trigger of "Didn't find any audio to render": a wave clip whose source is stored relative to the document's file does not resolve in the copy; found by the probe, not fixed), pages/app-shell.md, pages/ui-cost-of-a-change.md (the hidden-panel check now on a DjView; the arrangement still syncs while hidden), pages/writing-rhino-tests.md (rule 10 reduced to the shared session), pages/keeping-files-small.md, pages/session-model.md, pages/mixer.md (summary changed), pages/one-instrument-per-track.md, pages/pattern-presets.md, pages/track-and-clip-colours.md (the Leftover section closed), pages/track-kinds.md, pages/track-kind-fixed-at-creation.md, pages/known-hazards.md, pages/ids-not-pointers.md, pages/device-catalog.md, pages/adding-a-device.md, pages/dependency-direction.md, pages/main-track.md, pages/browser.md, overview.md, index.md (the new page; the mixer's summary); AGENTS.md, README.md and native/src/tests/README.md outside the wiki
- notes: every link to pages/session-view.md now points at pages/clip-slots.md or pages/dj-view.md; past log entries keep the old name. The full ctest run passed, six cases

## [2026-10-11] update | A pattern on a filled cell lets the note editor go; the cell checks the removal dropped, restored
- source: a review of the session view's removal; branch feat/dj-view, uncommitted; native/src/SessionSlots.cpp (insertPatternPresetInSlot takes a filled cell's clip out and repairs the editor before its insert, as copyClipToTrackSlot does); the pinned engine's insertClipWithState and ClipList (tracktion_ClipOwner.cpp at 4536d8a, read on GitHub: an insert into a ClipSlot removes the clip it holds whatever DeleteExistingClips says, before the new clip's state is added, and deleteObject only drops a reference); tests/Arrangement/scenarios/ClipCells.inc (a pattern dropped on the cell the editor has open, checked by the editor's id; a deleted cell reads empty, and the + row's new scene reaches every track, the two checks SessionView.inc made that went with it)
- updated: pages/clip-slots.md (the editors bullet: filling a filled cell is one of the removals, and the gap this closed), pages/dj-view.md (the clips grid's list of slot paths that repair the editor names the pattern drop)
- notes: no index change, no summary moved, no new page. A build with the new checks and without the fix failed the new scenario on "and leaves the editor on a clip that still exists"; with the fix the full ctest run passed, six cases

## [2026-10-11] update | Edits say what they changed; no bounce mid-gesture; a deck's copy for rendering, stripped, float, and told its document's file
- source: step 1 of the DJ live-chain work (the decks still play bounces; the live preview stays until a later step); branch feat/dj-view, uncommitted; native/src/Session.h (`ModelChange`, `markModified(track, changes)`, `markClipsModified`, `gestureHeld`, `djBouncesStarted`), every `markModified` call in `native/src/Session*.cpp` classified with its track; SessionInternal.cpp (`loadDocumentEdit`, `clipTrackIndex`), Session.cpp (`restoreProject` loads through it), SessionDjSources.cpp (`deckCopyState`, the copy `forRendering` with the document's file, a track deck's track straight out, 32-bit float, `finishDjLoad` and the poll held off while a gesture or a mouse button is down), SessionDj.cpp (no device for the booth under the runners, `ScopedNoDenormals` in its callback, `markDjModified` as `none`), core/DjEngine.cpp (`prepare` keeps what it retired); a probe in the arrangement runner (a document on disk: the copy and the reopened document both kept an empty file for a source stored relative to it; a document reopened from a path never written: the source stored as `..\name.wav` and resolved one folder too high); one run of the scenarios against the old routing of a grouped track's deck (it could not bounce at all); the pinned engine and JUCE read on GitHub (`Edit::Options` and its default resolver, `SourceFileReference`, `AudioClipBase::initialise`, `WaveNode::isReadyToProcess`, `ScopedTrackSoloIsolator`, `AudioFileUtils::createWriterFor`, the WAV writer's float rule)
- updated: pages/dj-view.md (the booth off the device under the runners, `prepare`, the staleness rule by kind, the gesture hold, the knob-touch section, the preview's poll rule, the document marks), pages/dj-bounce-on-a-copy.md (the copy as `deckCopyState` builds it, a track deck straight out, mute and solo out, `forRendering` with its own id and what that costs, the document's file up front, float, the triggers of "Didn't find any audio to render"), pages/ui-cost-of-a-change.md (no document copy mid-gesture), pages/session-model.md (what every command says it changed), pages/automation.md (the mirror skips `none`), pages/clip-slots.md, pages/project-files.md (`restoreProject` loads with its file), pages/real-time-audio-rules.md (the booth's denormals, `DjEngine::prepare`), pages/renders-that-never-return.md (a likely cause for the fourth hang), pages/writing-rhino-tests.md (where `DjBounces.inc` sits, reopened documents written to disk, the booth under the runners); AGENTS.md outside the wiki
- notes: no index change, no summary moved, no new page. `DjBooth.inc`'s preview check had passed only because the device thread also drove the booth and its larger block let the first preview block replay a note at beat 0; offline, a whole-track loop never wraps (the end-of-track test at length − 1 fires before the loop's end at length, a deck engine fix due in a later step), so its drawn note now sits mid-cell. The full ctest run passed, six cases, `native_arrangement_workflow` in 101 s of its 120 s limit

## [2026-10-11] update | Step 1's review: a clip's landing says devices only when it changes them; each deck's copies keep one temp folder; a missing source refused before a render
- source: three review findings on step 1 of the DJ live-chain work; branch feat/dj-view, uncommitted; native/src/SessionInternal.h and .cpp (`InstrumentSwitch`, reported by `switchTrackInstrument`; `checkClipFile`), SessionDrums.cpp (`fillBlankDrumRack` returns whether the rack took the kit), SessionClips.cpp (`editClipInEdit` reports `devicesChanged`; `editClip` and `moveClips` pass `devices` only then), SessionRegion.cpp (a paste's row gets `devices` only when the switch or the kit changed it), Session.h (`DjDeckInfo::behindOn`, `djCopyFolder` for the tests), SessionDjInternal.h, SessionDj.cpp and SessionDjSources.cpp (`DjBooth::copyIds` and `copyFolders`, `deckCopyState` writing the deck's id, `removeCopyFolders` in `djReset` and `~DjBooth`, `releaseFinishedBounces`, the bounce refusing a clip whose file is missing), SessionMerge.cpp (the same refusal before the render); the pinned engine read on GitHub at `4536d8a` (`Edit::getTempDirectory` and `initialise`, `TemporaryFileManager::purgeOrphanFreezeAndProxyFiles` and `cleanUp`, `AudioClipBase::getPlaybackFile`, `beginRenderingNewProxyIfNeeded` and `timerCallback`, `AudioFileInfo::needsCachedProxy`, `AudioProxyGenerator::beginJob` and `deleteProxy`, `Edit::cancelAllProxyGeneratorJobs`, `createNodeForAudioClip`, `WaveNode::isReadyToProcess`, `NodeRenderContext`'s leaf-node wait); the engine's temp folder on this machine (`%APPDATA%\Theda Native\Temporary`, three `edit_` folders, one a 9.9 MB `proxy_`); tests/Arrangement/scenarios/DjBounces.inc
- created: pages/edit-temp-folder.md
- updated: pages/dj-view.md (a landing's `devices` rule and the kinds a deck gathers in `DjDeckInfo::behindOn`; what `DjBounces.inc` reads; two stale lines about buses in the copy), pages/dj-bounce-on-a-copy.md (the deck's own id in place of a fresh one per bounce; a missing file refused; the copy's release), pages/renders-that-never-return.md (a fifth cause, a clip whose file is missing, and that the export does not refuse it yet; summary changed), pages/one-instrument-per-track.md (what the switch reports, and the rename it still makes on a landing), pages/session-model.md (`markClipsModified` and `devices`), index.md (the new page; the renders page's summary)
- notes: the rename noted on one-instrument-per-track.md - Ctrl+D on a clip of a track named after its kit renames the track "Drum Rack" - is older than this work and was left as it is. The full ctest run passed, six cases, `native_arrangement_workflow` in 99.7 s of its 120 s limit

## [2026-10-11] update | Step 1's second fix round: an instrument or a pattern dropped where it already plays marks devices only when it changes them
- source: the same three review findings, checked against the working tree, where the first fix round had made the changes, and against the pinned engine at `4536d8a` (`Edit::~Edit` and `cancelAllProxyGeneratorJobs`, `AudioProxyGenerator::beginJob` dropping a second job for a proxy already being made, `RenderManager::Job` cancelling only when its last holder lets go, `purgeOrphanFreezeAndProxyFiles` running whatever the edit's role, `createNodeForAudioClip` making nodes for muted clips, `AudioFileManager::getInfo` answering from its cache without looking at the file, `loadEditFromState`'s choice of id); native/src/SessionDevices.cpp (`addInstrumentDevice` marks `devices` only when the chain changed, `parameters` when a preset dropped with the instrument already there set its values, `none` when only the name followed); SessionPresets.cpp, SessionSlots.cpp and Session.h (`preparePresetTrack` reports whether it changed the chain, and the editor's, the lane's and the cell's pattern paths mark `devices` only then); tests/Arrangement/scenarios/DjBounces.inc
- updated: pages/dj-view.md (the instrument drop's and the pattern presets' marks; what `DjBounces.inc` reads), pages/one-instrument-per-track.md (the instrument drop), pages/pattern-presets.md (what `preparePresetTrack` reports), pages/clip-slots.md (a pattern's `devices` on a cell), pages/edit-temp-folder.md (what `Edit::~Edit` deletes), pages/renders-that-never-return.md (a file that is there and cannot be read still holds a render)
- notes: no index change, no summary moved, no new page. Builds without the fix failed the new checks: marking every instrument drop `devices` failed on "leave the deck's bounce current", fixing only the rename on "A Rhino FM preset dropped on it leaves the deck behind on values alone", and marking every pattern `devices` on "So do synth patterns loaded in the editor, dropped on the lane and on a cell". The full ctest run passed, six cases, `native_arrangement_workflow` in 114.2 s

## [2026-10-11] update | Step 2: a deck plays from a plan, lands at exact frames, follows the master's tempo, keeps its slot, and hands its material back when done
- source: step 2 of the DJ live-chain work (the decks still play bounces; the live preview stays until a later step); branch feat/dj-view, uncommitted; native/src/core (DjDeck.h with `DjDeckControls`; DjDeck.cpp, DjDeckTransport.cpp, DjDeckLoops.cpp and DjDeckRender.cpp: `plan` and `read`, loop points in `[0, length]`, the modulo wrap, `jumpTo` leaving a loop, the 5 ms declick, the end through the stop's fade, a swap by beat keeping a whole loop whole; DjEngine.h, DjEngine.cpp, DjEngineClock.cpp and DjEngineMix.cpp: command factories, `setDeckSource` and `setSlotActive` through the queue, every command at the top of the block, the prepared rate, block and generation, landings in phase at exact frames, sync on the tempo with the octave fold, the suspended correction and the held tempo; DjReleaseQueue.*: the ring of 64 and its backlog; DjLinearRamp.h; DjMixer.h and DjMixer.cpp: `DjSmoothedGain`'s snap, the compressor's floor), native/src/Session.h, SessionDjInternal.h, SessionDj.cpp and SessionDjSources.cpp (`slotOfDeck`; every public call by console position, everything inside by slot; `removeDjDeck` frees one slot, `addDjDeck` resets the one it takes, `djReset` empties every slot by command; the fader shows a held tempo, the tempo readout is the tempo), tests/DjTest.cpp (`DjDeckTest`, the one-piece player kept as a reference, the declick, landings, sync, slots, controls, denormals read from MXCSR, `checkBoothCost` with `operator new` counted), tests/Arrangement/scenarios/DjBooth.inc (a deck taken off from among four playing); a temporary probe of each mixer unit in silence, run in --self-test and removed; eighteen mutation builds, one fix undone each
- created: pages/dj-deck-engine.md
- updated: pages/dj-view.md (the DSP row, the hand-over and the slots, what a deck does, quantised starts, master and sync, removing a deck, the swap's crossfade, Related), pages/real-time-audio-rules.md (the acknowledged hand-over in place of counted blocks; the binary's own `operator new` count; the snap, the stall short of 1, and reading MXCSR), pages/keeping-files-small.md (the deck and the engine split, the session files and the test that wait), pages/writing-rhino-tests.md (a timed check spends its case's limit; `DjDeckTest`), index.md (the new page); AGENTS.md outside the wiki
- notes: the cost check before and after, per 512-frame block: 536 against 516-518 µs median, 795-801 against 734-769 µs at the 99th percentile, no allocation either way, and 2,763 against 2,707 µs over 30 s of silence. The silent tail is the mixer's isolators, colour filter and effects parking on denormals (the probe: a strip's isolator within ten blocks of silence), left for the mixer milestone; the booth's callback flushes them in the app. The committed check renders 10 s of silence, not 30, keeping --self-test at about 19 s of its 30 s limit. A start now lands like a jump, its position exact and in phase with the master's bar, where it used to be snapped to the deck's nearest bar. Every mutation failed its check, the jump's declick only once its check measured across the block boundary the command lands on. The full ctest run passed, six cases, `native_arrangement_workflow` in 104 s

## [2026-10-12] update | DJ decks heard live through their track's chain; the bounces, group decks and the knob-held preview removed
- source: the main agent's brief on the DJ rework (the user's report of 2026-10-11: an arrangement pattern on a deck, a Drum Rack knob and an Arp knob silencing it, Forge edits never reaching it; then "why do we need a simulation, keep it simple"); branch feat/dj-view, uncommitted; checked against native/src/core/DjTrackPort.*, DjEngine.h, DjEngineMix.cpp, DjTrack.h, DjDeckRender.cpp, devices/infra/DjTrackPortDevices.*, devices/DeviceCatalog.cpp, devices/CMakeLists.txt, Session.h, Session.cpp, SessionDj.cpp, SessionDjSources.cpp, SessionDjInternal.h, SessionInternal.h/.cpp, SessionMixer.cpp, SessionDevices.cpp, SessionTracks.cpp, DjDeckPanel.cpp, DjDeckPanelMenus.cpp, DjClipGrid.h, Main.cpp (`clipForPane`, `followDeck`), tests/DjTest.cpp (`checkTrackPort`), tests/DeviceConformance.cpp, tests/Arrangement/WorkflowTest.cpp and scenarios DjLiveChain.inc, DjBooth.inc, ClipCells.inc
- created: pages/dj-decks-play-live-through-the-track.md (the four faults and their causes; the hidden DJ Input and DJ Output and the port; why the devices are permanent, the undo index; costs; rejected: bounce plus preview, devices added on bind, message-thread note injection)
- updated: pages/dj-view.md (rewritten around the live chain: what a deck plays, Live, Edit, a new "Heard through the track" section with its limits and the history of the bounce and preview, the grid without SONG, the console without Reload or groups, what the document saves, decisions, not built)
- updated: pages/dj-deck-engine.md (the block through the port or the block delay; silent material; a new section on the port, `DjBlockDelay` and the sequencer; the stale forward-looking lines and the `REVIEW_NUMBERS` placeholder removed)
- updated: pages/dj-bounce-on-a-copy.md (marked superseded, with what still holds and where; kept for history)
- updated: pages/edit-temp-folder.md (the engine's rule kept and stated generally; the deck copies' sections made history)
- updated: pages/clip-slots.md (cells are the only clips a deck plays; a cell edit's mark and the clip signature; only the merge disables slots now)
- updated: pages/device-chain-order.md (the DJ ends in the order, `djInputEnd`, raw plugin indices one higher and the scenarios moved to slot 1; summary)
- updated: pages/real-time-audio-rules.md (the port's rings replace the preview's fifo and async wake; the reserved-array swap the arp and the DJ Input share; the poll)
- updated: pages/mixer.md (a deck takes the track ahead of the fader; `ensureTrackMixers` makes the DJ ends; a lane that skips it cannot carry a deck; summary)
- updated: pages/inputs-and-monitoring.md (a deck's Live, and monitored input heard through the deck's channel)
- updated: pages/session-model.md (the DJ files' jobs, the unbinding, what `djDocumentChanged` asks of a deck)
- updated: pages/ui-cost-of-a-change.md (the gesture hold now guards a deck's cell remake; the bounce's copy is history)
- updated: pages/writing-rhino-tests.md (the DJ scenarios' place, rule 10 without SONG, rule 11 on playing a chain in a runner, rule 12 on slot 1, no inline bounces)
- updated: pages/app-shell.md (`djDeckEditClip` gives only the cell played; no group decks; the poll's jobs)
- updated: pages/computer-keyboard.md (a deck's notes reach its track through the DJ Input, not an input entry)
- updated: pages/ids-not-pointers.md (a deck keeps the track's id and finds its devices again every poll)
- updated: pages/juce-button-fires-on-right-click.md (Reload gone)
- updated: pages/track-groups.md (a bus refuses a deck; the reduced copy is history)
- updated: pages/group-is-a-bus-track.md (the reduced copy is history)
- updated: pages/renders-that-never-return.md (the bounce's budget and `DjBounces.inc` are history; the export sentence corrected to the working tree, which now refuses a missing file and gives up on a stalled render: seen while editing, outside this change, recorded only as far as the page needed)
- updated: pages/device-catalog.md (the two DJ ends; eleven factories)
- updated: pages/built-in-devices.md (a row for the DJ ends; their channel answer flagged, read and not tested)
- updated: pages/rhino-build-targets.md (`infra/` in `RhinoDevices`; `SessionInternal.h` includes the DJ ends)
- updated: pages/keeping-files-small.md (the two DJ session files' new sizes)
- updated: overview.md (history row)
- updated: index.md (the new decision; summaries of dj-view, dj-deck-engine, mixer, device-chain-order, dj-bounce-on-a-copy and edit-temp-folder)
- notes: verified on 2026-10-12, after the review's fixes recorded in the next entry: all six ctest cases passed, `native_arrangement_workflow` in 102.1 s of its 120 s limit, `native_pattern_workflow` 48.8 s, `native_device_correctness` 21.3 s, `native_device_conformance` 6.2 s with the two hidden devices conformant as pass-through effects. Reported to the main agent from reading the code, and fixed by it the same day: the DJ Input's channel answer, stale comments in `DeviceCatalog.cpp`, `DjTrackPortDevices.h`, `Main.cpp`, `SessionSlots.cpp` and `SessionMixer.cpp`, and the unused `isDjPortDevice`

## [2026-10-12] update | The live chain's review: no stuck notes, a resyncing port, the DJ output placed by the channel strip, muted tracks that still run
- source: the coordinator's account of an independent review's findings and fixes on branch feat/dj-view, each confirmed in the code: devices/infra/DjTrackPortDevices.* (the input adds rather than replaces, answers its input count of channels, remembers and releases the notes it delivered), core/DjTrackPort.* (the `overflowed` flag and `resynced`), core/DjEngine.h and DjEngineMix.cpp (`wentThroughPort`), DjEngine.cpp (an emptied slot leaves its notes to the input), SessionInternal.h (`RhinoEngineBehaviour::shouldProcessMutedTracks`) and SessionInternal.cpp (`ensureDjPortDevices` before the first infrastructure plugin), Session.cpp (`buildStarterEdit` through `ensureNumberOfAudioTracks`, the Utility appended), SessionTracks.cpp (`appendTrack`, the Utility first), SessionDjSources.cpp (`bindDjTrack` sweeping by port, `djUnbindAll` clearing every end, a deleted track letting go of the material, the re-chosen cell a no-op, the re-stamped remake, the bar's length in the clip signature), SessionDj.cpp (`liveRestore`), tests/DjTest.cpp (`checkTrackPort`'s resync), tests/Arrangement/scenarios DjLiveChain.inc, AutomationLanes.inc, DjBoothEdges.inc and AudioChannels.inc; the engine's muting path as the coordinator read it at the pinned commit `4536d8a`, not re-read here; ctest as above, all six passing
- updated: pages/dj-decks-play-live-through-the-track.md (the passing run; the output's place; the input adds; costs: muted tracks' CPU with the engine's reasons, the old material through a new track's chain, the sidechain carrier and the monitored input; a section on what the review fixed; summary)
- updated: pages/dj-view.md (a re-chosen cell does nothing; a deck moving tracks keeps playing; the input adds and keeps mono; the output's place; no note left sounding; mute and solo; the bar's length in the signature and the re-stamped remake; no end left reading a port; a deleted track; limits; `liveRestore`; not built)
- updated: pages/dj-deck-engine.md (the overflow resync, the block delay restarted by `wentThroughPort`, an emptied slot's notes left to the input)
- updated: pages/device-chain-order.md (the DJ output before the first strip plugin; a new pitfall section on starter and added tracks ordering their strips differently, and the placement it broke; `AutomationLanes.inc` by type; summary)
- updated: pages/mixer.md (the deck ahead of the channel strip; Volume & Pan not always last; a rule on mute and solo silencing only the output; the starter tracks' unread meter; summary)
- updated: pages/built-in-devices.md (the DJ ends conformant as pass-through effects; the input's channel answer fixed)
- updated: pages/real-time-audio-rules.md (the port's overflow resync; the input's note-offs through the same swap)
- updated: pages/inputs-and-monitoring.md (`liveRestore`; the input adds, so a held track's monitored input plays, through the deck's channel)
- updated: pages/writing-rhino-tests.md (rule 11: only the mute flag can be asserted, the organ preset and chords that let levels compare, two decks need two tracks; rule 12: four scenarios at slot 1, hidden plugins found by type)
- updated: pages/session-model.md (`RhinoEngineBehaviour` now also keeps muted tracks running)
- updated: pages/recording.md (a mono take relies on the DJ input answering its input count too)
- updated: pages/rhino-vocoder.md (a track a deck holds is no carrier while the song is stopped)
- updated: pages/ids-not-pointers.md (stale readers found by the port; every end cleared on unbind)
- updated: index.md (summaries of the decision, device-chain-order and mixer)
- updated: log.md (the previous entry's verification note replaced with the run above, as the coordinator asked)
- notes: no new page; the chain-order difference went into pages/device-chain-order.md as a pitfall rather than a gotcha page of its own, since that page is where the order is described

## [2026-10-12] update | The live chain proven through Tracktion's real graph; the hosted device's silent start
- source: the coordinator's account, checked against tests/Arrangement/scenarios/DjLiveChain.inc, scenario "dj live: the song's own graph plays a deck's track while the song stands still" (a document of its own, its machine device released, Tracktion's hosted audio device initialised with 0 inputs and 2 outputs at 48 kHz and 512 and prepared, `dispatchPendingUpdates` after the start and again after `ensureContextAllocated` and `dispatchPendingUpdatesSynchronously`, then `hosted.processBlock` and `djProcessOffline` each block; the deck heard with the song stopped and the song's output silent, nothing while it rolls, the track back after stop, a muted and a solo-excluded track still on the deck, nothing sounding after an eject); the engine at the pinned commit `4536d8a` as the coordinator read it, not re-read here (`tracktion_DeviceManager.cpp`: `audioDeviceAboutToStart`, `isSuspended`, `prepareToStart`, the CPU limit, `addContext`'s wait; `Edit::playInStopEnabled` and `IDs::endToEnd`; `TransportControl::performStop`); the suite run twice afterwards, all six cases passing, `native_arrangement_workflow` in 95.6 s of its 120 s limit
- created: pages/hosted-device-silent-until-dispatched.md (the silent graph and its cause, the early outs on the same path, the recipe, a document of its own)
- updated: pages/dj-decks-play-live-through-the-track.md (play-in-stop stated with its engine reasons in place of "relied on and not exercised"; a section on what the real graph proves; the mute bullet borne out; summary)
- updated: pages/dj-view.md (the limits' "no test exercises" replaced by a bullet on the real graph's proof and play-in-stop)
- updated: pages/writing-rhino-tests.md (rule 11: the two ways a scenario runs a chain, by hand or through the hosted device; "only the mute flag can be asserted" removed; Related)
- updated: pages/reproducing-live-timing-offline.md (a section on running the live graph instead of a render)
- updated: pages/mixer.md (the mute rule heard through the real graph)
- updated: index.md (the new gotcha; the decision's summary)
- notes: this supersedes two notes in the entries above, that the song graph running a stopped track's plugins was relied on and untested, and that a test could assert only the mute flag. The comment at the head of DjLiveChain.inc still says the song's graph never runs there; that is the code's to fix, and was reported. pages/headless-ui-snapshots.md is about pictures, not audio, and was left alone

## [2026-10-12] update | The hosted-device scenario as committed: the machine device released first, no reopen
- source: the coordinator's account of commit b6fd822 on feat/dj-view, checked against tests/Arrangement/scenarios/DjLiveChain.inc (its head comment and the scenario "dj live: the song's own graph plays a deck's track while the song stands still"); devices/infra/DjTrackPortDevices.* end unchanged, the diagnostic counters removed
- updated: pages/hosted-device-silent-until-dispatched.md (how the silence was diagnosed; why the machine's device is released first; no reopen needed, and the reopen tried as a dead end)
- notes: confirmed right and left alone: pages/dj-decks-play-live-through-the-track.md (the real graph's play, stop, mute, solo and eject), pages/writing-rhino-tests.md, pages/reproducing-live-timing-offline.md, pages/dj-view.md and pages/mixer.md (no reopen step anywhere). The head comment of DjLiveChain.inc, reported stale in the previous entry, now says one scenario drives the engine's real graph

## [2026-10-12] update | A RhinoDAW freeze traced to Forge's static Direct2D images released at DLL unload
- source: the main agent's brief on investigating the user's freeze report. No repository code changed: a temporary scenario, native/src/tests/Arrangement/scenarios/DjDeleteHangRepro.inc, and its include in WorkflowTest.cpp were reverted without ever being built. The four statics were checked in instruments/rhino-forge/ui/ForgeChrome.h (`brushedGrain`, `fastenerImage`) and ui/ForgePerformanceWheel.h (`ForgePerformanceWheel::paint`)
- created: pages/plugin-static-images-deadlock-on-unload.md (the hang, the loader-lock deadlock, the four images, why pinning the module only moves the freeze to exit, the proposed fix in Forge, and what is unproven)
- created: pages/reading-a-hung-process.md (a hang told from a crash, a windowless frozen instance, `IsHungAppWindow` over `Responding`, dbghelp and symsrv from Visual Studio, `SizeOfStruct` 88, unloaded modules named by `GetMappedFileNameW`, classes named from RTTI with dumpbin, and the replay that was never built)
- updated: pages/forge.md (a known-issue section)
- updated: pages/forge-hosting.md (a pitfall: unloading Forge after its editor painted)
- updated: pages/forge-editor.md (a rule: no image outlives the editor)
- updated: pages/debugging-a-crashing-project.md (a freeze leaves no Application Error and no dump)
- updated: pages/locked-executable-lnk1104.md (the holder may be a frozen RhinoDAW with no visible window)
- updated: pages/profile-paint-on-a-software-image.md (the Direct2D backing's second consequence)
- updated: pages/development-environment.md (no debugger, and the tools that stand in; Git Bash rewrites slash options as paths)
- updated: index.md (the two new pages)
- corrected, after the coordinator's review the same day:
  - pages/plugin-static-images-deadlock-on-unload.md: the fix is proposed, not decided, since the user has not chosen one. The `std::shared_ptr` and `juce::ReferenceCountedObject` links are marked as read from the disassembly, not from symbols: a two-counter control block at +8 and +0xC, and a later frame loading the vftable.
  - pages/forge.md: the fix is proposed, not decided.
  - pages/reading-a-hung-process.md: section 1's command is replaced with the Windows Error Reporting query actually run, with what the hang looked like in it. A clause adds that `vftable[-1]` is an absolute pointer at the preferred base. The review confirmed both x64 details the first draft took from general MSVC knowledge: dumpbin's `/range` at preferred-base addresses, and the descriptor's offset at +12 with the name at +16.
- notes: recorded as believed, not proven: that the user's own freeze, deleting Forge from the rack after opening its window in the 18:30 session, is the same deadlock. That depends on whether JUCE's VST3 hosting frees the module when the last instance goes, still under research. No fix applied; the user gets the assessment first
## [2026-10-12] update | Arp note lifetime, retrigger semantics, and scales
- updated: pages/rhino-arp.md (stopped-clock and channel/pitch gate ownership, panic/reset handling, Hold-off defaults, repeat/retrigger semantics, shared scale choices, and conformance regression placement)

## [2026-10-13] update | Arp cadence, scale correction, and plain defaults
- updated: pages/rhino-arp.md (rate-preserving Beat retrigger, stopped timeline continuity, nearest-scale correction, shared output ownership, factory defaults, and timing regression coverage)

## [2026-10-13] update | native/src laid out by layer (commits 0bff66b and b68fa0e)
- source: the main agent's brief on branch chore/reorganise-native-src, checked against native/CMakeLists.txt, native/src/core/CMakeLists.txt, native/src/devices/CMakeLists.txt, the folders on disk (app/, model/, ui/shared/, core/dsp|dj|content/), the include spellings in ui/audioclip and tests/Arrangement/WorkflowTest.cpp, and model/SessionDeviceMacros.cpp (includes SessionInternal.h). AGENTS.md's "Where things live" already carried the new tree
- created: pages/source-tree-mirrors-the-layers.md (the layout table, the include rule per target root, why class prefixes stayed, the JUCE-module, prefix-stripping and native/-rename rejections, the Main.cpp split still to do, and what proving the move cost)
- updated: pages/rhino-build-targets.md (table by folder, one include root per target, the RhinoCore contents by subfolder)
- updated: pages/dependency-direction.md (the table's folders; a wrong-way include now reads wrong in its path)
- updated: pages/keeping-files-small.md (where each split class lives, units keep the class prefix, the ControlWindow plan not done)
- updated: pages/session-model.md (model/Session.h, 29 units, SessionPatternPresets and SessionDeviceMacros, the overwrite pitfall under the new name)
- updated: overview.md (repository layout rows; history row)
- updated: pages/build-and-test-rhino.md (a merge of two appends left devices/CMakeLists.txt unparseable; configure after a merge)
- updated: pages/git-workflow.md (git_commit refuses a directory pathspec; 407 explicit paths)
- updated: pages/headless-ui-snapshots.md (app/Main.cpp; shell and DJ PNG hashes are not a nothing-changed check, compare per pixel against a same-binary rerun)
- updated: pages/development-environment.md (the command hook refuses a heredoc holding a drive-letter path)
- updated, path references and folder notes only: pages/app-shell.md, pages/arrangement-view.md, pages/writing-rhino-tests.md, pages/adding-a-device.md, pages/device-catalog.md, pages/content-library.md, pages/built-in-devices.md, pages/browser.md, pages/automation.md, pages/audio-clip-editor.md, pages/clip-placement.md, pages/clip-slots.md, pages/colours-and-typography.md, pages/computer-keyboard.md, pages/content-is-files.md, pages/debugging-a-crashing-project.md, pages/device-rack.md, pages/device-chain-order.md, pages/dj-bounce-on-a-copy.md, pages/dj-view.md, pages/executable-named-rhinodaw.md, pages/forge-hosting.md, pages/forge-is-an-independent-vst3.md, pages/juce-button-fires-on-right-click.md, pages/inputs-and-monitoring.md, pages/juce-output-stream-appends.md, pages/juce-isbold-misses-semibold.md, pages/locked-executable-lnk1104.md, pages/mixer.md, pages/one-instrument-per-track.md, pages/pattern-presets.md, pages/playhead.md, pages/pointer-and-selection.md, pages/project-files.md, pages/recording.md, pages/region-editing.md, pages/rhino-eq.md, pages/rhino-tune.md, pages/rhino-vocoder.md, pages/time-warp.md, pages/sync-children-before-layout.md, pages/track-groups.md, pages/track-and-clip-colours.md, pages/ui-cost-of-a-change.md, pages/transport.md, pages/device-presets.md, pages/rhino-fm.md, pages/rhino-arp.md, pages/displays-draw-from-the-dsp.md, pages/dj-decks-play-live-through-the-track.md, pages/drum-rack-sample-editor.md, pages/drum-rack.md, pages/note-editor.md, pages/real-time-audio-rules.md
- updated: index.md (the new decision; session-model's summary now says 29 files)
- notes: devices/, tests/ and core/DeviceDisplay.h, core/SystemUsage.* did not move, so pages naming only those (native-device-standard, cachedvalue-lags-its-tree, append-only-stored-indices, cmake-does-not-reconfigure, inc-edits-do-not-rebuild, stack-overflow-reports-as-segfault, profile-paint-on-a-software-image, pattern-track) were left alone. Earlier entries of this log name the flat paths; they are history and stand

## [2026-10-13] update | Main.cpp split into MainWindow, MainComponent and its units; the shell's dead code removed (commit db88b5c and the chore after it)
- source: the coordinator's brief, checked against native/src/app/ on disk (Main.cpp with START_JUCE_APPLICATION at line 204, MainWindow.*, MainComponent.h and the six MainComponent*.cpp units, ControlBarButtons.h, AppPlatform.*), ui/shared/ComponentSnapshot.h, tests/TestRunners.*, the app list in native/CMakeLists.txt, and MainComponent.h's show*Menu taking juce::Component& (the chore's change). AGENTS.md was already updated by the coordinator and was not touched
- updated: pages/app-shell.md (first paragraph rewritten: Application alone in Main.cpp, MainComponent and what each of its six units holds, the header's member order, MainWindow, ControlBarButtons.h, AppPlatform.*, ComponentSnapshot.h, TestRunners.*; the dead code removed; MainWindow, MainComponent::resized and the timer named by unit; summary changed)
- updated: pages/keeping-files-small.md (MainComponent in "Where it is applied"; "Still to do" now names FloatingDeviceWindow alone)
- updated: pages/headless-ui-snapshots.md (the whole-shell section's reason is that no runner builds a MainComponent, not that the class is unreachable; writeSnapshot's new home)
- updated: pages/source-tree-mirrors-the-layers.md (the app/, ui/shared/ and tests/ rows; the split recorded as done, not pending)
- updated: pages/writing-rhino-tests.md (followDeck and the pane clamp stay unchecked because nothing in tests/ constructs a MainComponent)
- updated: pages/computer-keyboard.md, pages/dj-view.md, pages/recording.md, pages/track-kinds.md, pages/device-rack.md, pages/dependency-direction.md, pages/juce-output-stream-appends.md, pages/real-time-audio-rules.md (ControlWindow and Main.cpp references pointed at the unit that now holds each thing)
- updated: overview.md (shell row; the 2026-10-13 history row)
- updated: index.md (app-shell's summary)
- notes: pages/debugging-a-crashing-project.md and pages/executable-named-rhinodaw.md still say Application::initialise and getApplicationName are in app/Main.cpp, which remains true, so they were left alone; app-shell.md's "the song's transport is out of reach" is about the transport, not the class, and stands

## [2026-10-13] update | The `te` alias is Rhino's; a unit including Tracktion directly declares it (commit db88b5c)
- source: the main agent's brief on branch chore/reorganise-native-src, checked against native/src/app/AppPlatform.cpp (lines 2 and 4) and the eight device headers that declare the alias
- updated: pages/rhino-build-targets.md (under the include rules: `namespace te = tracktion::engine;` comes from Rhino's device headers via DeviceCatalog.h and Session.h, so app/AppPlatform.cpp, the first app unit to include tracktion_engine.h on its own, declares it itself; the `'te' is not a class or namespace name` failure)
- notes: native/src/tests/UiProfile.cpp line 438's comment now says the shell's pane layout is MainComponent's, which no runner builds, rather than "in Main.cpp, out of reach"; nothing to record beyond this line

## [2026-10-13] update | Forge's ARP plate moves between the keys and the note guide, gains four slots and a waterfall (commit 8f28dc7 on feat/forge-arp-slots-and-waterfall)
- source: the main agent's brief, checked against the working tree: instruments/rhino-forge/ui/ForgePlacement.h (keyboardBounds, arpPlateBounds, arpPlateFace, arpPlateNameBounds, arpSlotGridBounds, arpSlotButtonBounds, arpSlotAt, arpWaterfallBounds, noteGuidePlateBounds), ui/ForgeArpPlate.h and core/ForgeNoteFlash.h (new, both in CMakeLists.txt), ui/ForgePanels.h (drawArpPlateHousing, drawArpSky, the 3 ms comment), ui/ForgeVisuals.h, core/ForgeArp.h (arpSlotCount, the 25 ids), src/ForgeParameters.cpp (arpSlot), src/ForgeProcessorState.cpp (ARPSLOT nodes, applyArpSlot, migrated keeping the nodes), src/ForgeProcessorMidi.cpp (timerCallback), src/ForgeProcessor.cpp and .h (noteFlashes pushed from both routes, popNoteFlash, startTimerHz(60)), src/ForgeEditor.cpp (the drain at construction), src/ForgeEditorInput.cpp (mouseDown, gatherNoteSparks), src/ForgeEditorPages.cpp (chooseArpSlot), src/ForgeEditorSpectral.cpp (getTooltip), src/ForgeEditorPaint.cpp, ui/ForgeModules.h (chassisParameters), ui/ForgeTooltips.h, tests/ForgeTestsLayout.cpp, tests/ForgeTestsArp.cpp (slotSuite, flashSuite), tests/ForgeTestTools.cpp. The 8.6 ms idle figure and the well widths at 1440 and 2000 are the brief's measurements, not re-run
- created: pages/forge-arp-plate.md (where the plate stands and why it moved, the static/live split, the slots and the Processor's crossing, the flash queue and the sparks, the tests)
- updated: pages/forge-arpeggiator.md (six panes named; Slots section pointing at the new page; On the panel rewritten for the new position; M13b's bank field done, EDIT ALL and M13c still absent; summary changed)
- updated: pages/forge-presets-and-state.md (ARPSLOT nodes in the tree, the stale-live-node rule, liveArpSlot set after a load, additive within format 3)
- updated: pages/forge-editor.md (chassisParameters beside displayParameters; the plate painted on the chassis and hit-tested first; ForgeArpPlate.h in the visuals line)
- updated: pages/headless-ui-snapshots.md (ARP presses the name column, plays a chord, runs the timers by hand and waits so the PNG shows sparks)
- updated: pages/forge-panel-repaint-cost.md (the plate's housing in chassisLayer and its 3 ms live cost; the live pass's shape and the unattributed 8.6 ms idle reading)
- updated: pages/real-time-audio-rules.md (NoteFlashQueue beside MidiControlQueue)
- updated: pages/forge.md (the keyboard's range and the plate's position; M13b partly done)
- updated: overview.md (the 2026-10-13 history row)
- updated: index.md (the new page; forge-arpeggiator's summary)

## [2026-10-13] update | Forge's OSC warp modes read either other oscillator (commit d0a7e0f on feat/forge-warp-pick-source-oscillator)
- source: the main agent's brief, checked against instruments/rhino-forge/core/ForgeWarp.h (WarpSource, the five appended modes, warpModeCount 43, WarpCategory order, warpOtherSide), core/ForgeVoiceParts.h (warpSourceOscillator, warpModeName, warpModeOrder/Position/At), core/ForgeCore.h (renderOscillators' pointAt) and the callers in src/ForgeParameters.cpp, src/ForgeEditorWarp.cpp, src/ForgeEditor.cpp
- updated: pages/forge-warp.md (43 modes and the per-family counts; the appended previous-oscillator modes; new "Names and shown order" section; the Sources ring now goes both ways; the behavioural direction test; summary changed)
- updated: pages/append-only-stored-indices.md (warp modes as the second case where stored and shown order differ)
- updated: index.md (forge-warp's summary)

## [2026-10-13] update | Forge's table field on every oscillator and the TABLE tab, with NEW and SAVE (commit 8975d57 on feat/forge-table-browser)
- source: the main agent's brief, checked against instruments/rhino-forge/core/ForgeTableFiles.h, core/ForgeWavetable.h (touch titles CUSTOM), src/ForgeEditorTables.cpp, src/ForgeProcessorState.cpp (newTable, saveTable, importTable's tableNameOf), src/ForgeEditorPages.cpp (applyTableFieldStates from applyEnableStates), ui/ForgeTablePanel.h (tableMenu, toolbar order, undo steps), ui/ForgeFxVisuals.h (FxSelector::caption), ui/ForgeModules.h (tableStripHeight, the control-less displayTop row), ui/ForgePlacement.h (tableStripRow, wavetableTubeBounds), tests/ForgeTestsTable.cpp (tableLibrarySuite), .gitignore
- updated: pages/forge-oscillators.md (TABLE tab toolbar and undo steps; titles from tableNameOf; NEW, SAVE and its save-as rule; writeTableFile's Serum layout; new "The table field" section: root, listing order, menu, the decision that the table is title-matched rather than a parameter, tables/user git-ignored; summary changed)
- updated: pages/forge-editor.md (seventeen units with ForgeEditorTables.cpp; a control-less row carving a display; the table fields as the exception to FxSelector's copy rule, and when the disk is rescanned; summary changed)
- updated: pages/forge.md (tables/ row names the git-ignored user/ folder)
- updated: index.md (summaries of forge-oscillators and forge-editor)

## [2026-10-13] update | Forge's oscillator tube draws a modulated POSITION and warp depth (commit 2d14aba on main)
- source: the main agent's brief, checked against instruments/rhino-forge/src/ForgeEditor.h, src/ForgeEditorModulation.cpp, src/ForgeEditorPaint.cpp, src/ForgeEditorWarp.cpp, tests/ForgeTestsDisplays.cpp
- updated: pages/forge-editor.md (new rule: pictures of what sounds read Editor::modulatedValue, knobs read value)
- updated: pages/forge-modulation.md (On the panel: the tube follows the loudest voice and rests on the knob with no note)
- updated: pages/displays-draw-from-the-dsp.md (oscillator display is drawn at the modulated values; modulatedTraceSuite and why it uses a macro and paints only the tube)
- updated: pages/git-workflow.md (check the current branch before writing or committing; another agent switched it mid-task)

## [2026-10-13] update | Forge's LFOs gain a TYPE (NORMAL, PATH, S&H), a DIRECTION and a Y output (uncommitted on main)
- source: the main agent's brief, checked against instruments/rhino-forge/core/ForgeVoiceParts.h, core/ForgeLfoTable.h, core/ForgeMatrix.h, core/ForgeCore.h, src/ForgeProcessorLfo.cpp, src/ForgeProcessorState.cpp, src/ForgeEditorLayout.cpp, src/ForgeEditorPages.cpp, src/ForgeEditorLfo.cpp, src/ForgeEditorInput.cpp, ui/ForgeEnvelopeVisuals.h, ui/ForgeModules.h, tests/ForgeTestsLfo.cpp, tests/ForgeTestsDisplays.cpp
- created: pages/forge-lfos.md (the three types, travel vs position, lfoOutputAt and the Y sources, the S&H flight and SMOOTH, the footer without slack, the Y card, path editing, the ForgeLfoPath node, the shape-4 migration, the Serum provenance)
- updated: pages/forge-modulation.md (LFO 1-6 Y sources appended at 22-27; the modulators section defers to Forge LFOs)
- updated: pages/displays-draw-from-the-dsp.md (the LFO graph samples lfoOutputAt; a path is drawn from its route)
- updated: pages/append-only-stored-indices.md (the Y sources appended at the end; a new section on an entry leaving the end of a list, the S&H shape migrated to a type)
- updated: pages/forge-engine.md (per-voice LFO state is a travel, a stopped flag and a step flight; meterLfoSource and Core::lfoSteps look the staircase up rather than copying it)
- updated: pages/forge-editor.md (lfo{n}Type and lfo{n}Direction are display parameters; applyEnableStates relays out the header when the Y card comes or goes)
- updated: pages/forge-presets-and-state.md (the ForgeLfoPath node; migrated repairs a shape of 4)
- updated: index.md (Forge LFOs added under Components)

## [2026-10-13] update | Forge's matrix grows to thirty slots, eight on the table until a patch needs more
- source: the change itself, checked against instruments/rhino-forge/core/ForgeMatrix.h (modSlotCount, modParameterId, Modulation::settle), core/ForgeCore.h (the live-slot walks), src/ForgeProcessor.cpp (settle once a block), ui/ForgePlacement.h (the matrix section), ui/ForgeModules.h (completeMatrixRows), src/ForgeEditorMatrixView.cpp, src/ForgeEditorPages.cpp, src/ForgeEditorInput.cpp, tests/ForgeTestsLayout.cpp (the matrix sweep and matrixGrowsWithRoutingsSuite), tests/ForgeTestsModulation.cpp
- updated: pages/forge-modulation.md (thirty slots, no format bump; the per-block live list; the table's growth rule, fixed row height, whole-row window, wheel and reveal; summary changed)
- updated: pages/forge-editor.md (eighteen units with ForgeEditorMatrixView.cpp; the rack and the matrix as the two scrolled windows and what they share; summary changed)
- updated: index.md (summaries of forge-modulation and forge-editor)

## [2026-10-13] update | The Forge VST3 finds tables/ from inside its bundle
- source: the fix itself, checked against instruments/rhino-forge/core/ForgeTableFiles.h (tableRootAbove, tableRoot) and tests/ForgeTestsTable.cpp (tableLibrarySuite)
- updated: pages/forge-oscillators.md (the walk is ten folders; the plugin binary sits eight under the source tree, and the eight-step walk listed only the built-in ten in the VST3)

## [2026-10-13] update | Where the Forge VST3 binary sits, and two builds sharing Forge's build tree
- source: the same fix's brief, checked against instruments/rhino-forge/core/ForgeTableFiles.h and ui/ForgeTablePanel.h (lastFolder)
- updated: pages/forge-oscillators.md (LOAD's dialog shares the root, so it also opened at home in the VST3)
- updated: pages/build-and-test-forge.md (the plugin binary's path inside the bundle and its depth; Ableton Live 12 loads the build-tree VST3)
- updated: pages/orphaned-build-processes.md (C1083/D8040 when two builds share one tree, here an agent's and the user's own; re-run once the other finishes)

## [2026-10-13] update | Forge's /W4 build warnings fixed at their sites
- source: the change's brief, checked against instruments/rhino-forge/CMakeLists.txt (juce_recommended_warning_flags on both targets), ui/ForgeChrome.h and ui/ForgeStyle.h (the text, panel and line colours)
- updated: pages/build-and-test-forge.md (new Warnings section: fix at the site, why headers multiply the count, listing them from a worktree build, the C4459/C4458/C4244/C4996 causes and the writer API's stream leak; summary changed)
- updated: pages/colours-and-typography.md (never name a Forge UI local text, panel or line)
- updated: index.md (summary of build-and-test-forge)

## [2026-10-13] update | A held note hangs in Forge's arp waterfall and charges to ultra
- source: the change's brief (uncommitted on main, constants still open to tuning), checked against instruments/rhino-forge/ui/ForgeArpPlate.h, core/ForgeNoteFlash.h (HeldNotes) and tests/ForgeTestsArp.cpp
- updated: pages/forge-arp-plate.md (new "A held note hangs and charges" section: parking, charge, ultra, let-go; HeldNotes as a set rather than note-offs and why; the push/pop ordering; held means held by a voice; early releases unchanged; cap evicts released sparks only; sparkSuite, ARPHELD; summary changed)
- updated: pages/headless-ui-snapshots.md (the ARPHELD page, and the MidiBuffer that replays keyboardState's events unless cleared)
- updated: pages/real-time-audio-rules.md (held notes cross as an atomic set because a lost note-off would hang a light)
- updated: index.md (summary of forge-arp-plate)
