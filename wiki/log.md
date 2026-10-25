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

## [2026-10-13] update | Arp waterfall held notes committed as f95557b, after visual review
- source: the follow-up brief, checked against instruments/rhino-forge/ui/ForgeArpPlate.h and tests/ForgeTestTools.cpp
- updated: pages/forge-arp-plate.md (commit f95557b, tuning caveat dropped; ~2 px slices capped at 64; colour capped at 0.85 of white and the column half way, two halos instead of one large one, and why)
- updated: pages/headless-ui-snapshots.md (commit f95557b; pump callPendingTimersSynchronously through a wait, since one call misses later ticks)

## [2026-10-13] update | Forge arp waterfall: a held note's light leaves, its trace carries the hold
- source: the change's brief (uncommitted on main, not yet built or tested), checked against instruments/rhino-forge/ui/ForgeArpPlate.h and tests/ForgeTestsArp.cpp
- updated: pages/forge-arp-plate.md (section rewritten as "A held note's trace hangs and charges": the user's feedback, no parking, charge from sparkChargeStart, the trace pours out after release, sparkSpent drops sparks, short holds identical to a let-go note, ultra on the trace, f95557b's halos gone; sparkSuite rewritten and the nearFloor/C4459 note; summary changed)
- updated: pages/headless-ui-snapshots.md (ARPHELD now shows traces in ultra, the lights gone)
- updated: pages/real-time-audio-rules.md (a lost note-off would leave a trace hanging, not a light)
- updated: index.md (summary of forge-arp-plate)

## [2026-10-13] update | Arp waterfall trace redesign committed as 90246e1 and verified
- updated: pages/forge-arp-plate.md, pages/headless-ui-snapshots.md (uncommitted/untested caveats replaced with commit 90246e1; 18 Forge CTest areas pass incl. sparkSuite, no-warning builds, ARPHELD reviewed by eye)

## [2026-10-13] update | Forge matrix reaches the envelope knobs and OCT/FINE
- source: the change's brief and the coordinator's follow-up (verified: no-warning build, all 18 Forge CTest areas pass; committed as e8702e4 and pushed to origin/main), checked against instruments/rhino-forge/core/ForgeMatrix.h, core/ForgePatch.h (tuningRatio, envelopeField) and core/ForgeCore.h (modulateEnvelopes, slotAmount, envScratch)
- updated: pages/forge-modulation.md (22 appended destinations, the env and tuning banks, envelopes moved before they run, one-sample self-feedback and the unguarded −100% case, no UI change needed, the envelope display gap, new suites; summary changed)
- updated: pages/forge-oscillators.md (OCT steps in whole octaves under modulation, why, nearest not floor, why the rounding lives in tuningRatio; FINE's knob stops on whole cents while the matrix moves it smoothly)
- updated: pages/forge-engine.md (per-voice order: LFOs read, envelopes, then the matrix; slotAmount returns false rather than zero)
- updated: pages/append-only-stored-indices.md (the destinations appended after noiseStereoDestination, no format bump)
- updated: pages/serum-patches-sound-different.md (OCT and FINE are destinations now; SEMI stays continuous)
- updated: pages/displays-draw-from-the-dsp.md (the envelope display draws knob values, not the modulated shape)
- updated: index.md (summary of forge-modulation)

## [2026-10-13] update | Mapped parameters: an external plugin's rack is a chosen list, grown by MAP from the plugin's window
- source: the change's brief (uncommitted on main; every target compiled, the link was blocked by a running RhinoDAW, rebuild in progress; ctest NOT yet run, so nothing here claims the tests pass), checked against native/src/model/SessionMappedParameters.cpp (defaultMappedIds, resolvedListFor, mapDeviceParameterInEdit, unmapDeviceParameter, ParameterMapper, flushParameterMapping, detach/drop), Session.h, SessionInternal.h, SessionDeviceMacros.cpp (exposedParameterAt), SessionDevices.cpp (deviceParameters, deleteDevice), Session.cpp (markModified, newProject, restoreProject, ~Session), ui/rack/DeviceEditorPanel.h/.cpp (mappable, mapButtonArea, toggleParameterMapping, genericColumns, preferredWidth, item 6), DeviceEditorPanelGenerated.cpp (the tooltip), DeviceRack.h/.cpp (openDevice, sync), tests/Pattern/scenarios/MappedParameters.inc, DeviceRackTest.cpp (the Forge block), WorkflowTest.cpp, native/CMakeLists.txt (30 Session*.cpp lines), instruments/rhino-forge/src/ForgeParameters.cpp (the spectral set declared per oscillator)
- created: pages/mapped-parameters.md (written by the main agent before this entry and verified; corrected here: hasMappedParameters also needs the wrapped processor loaded, the resolve cache holds eight lists, mapDeviceParameter refuses a non-automatable parameter and an id holding the separator, the same device's window reopened keeps its mapping, the scenario list completed, and a note that the suite had not yet run)
- updated: pages/device-rack.md (written by the main agent and verified; corrected: a mapping survives the same device's window being opened again)
- updated: pages/forge-hosting.md, pages/automation.md, pages/session-model.md, index.md (written by the main agent; verified against the code, no change needed)

## [2026-10-13] update | Mapped parameters verified: all six CTest cases pass
- source: the rebuild after the running RhinoDAW was closed, then `ctest --test-dir native/build -C Release` (6/6 passed; native_pattern_workflow 44.6 s with the mapped parameters scenario in its output, native_arrangement_workflow 103.4 s). DeviceParameters.inc's old check that the rack lists at least 35 Forge parameters was restated: Tracktion still exposes them all through the wrapped processor, and the rack shows nine.
- updated: pages/mapped-parameters.md (the tests-pending sentence replaced with the result)

## [2026-10-13] update | DeviceParameters.inc's restated Forge check, verified against the file
- source: native/src/tests/Pattern/scenarios/DeviceParameters.inc (`forgeProcessor->getParameters().size() >= 35` keeps the original message, then `forgeParameters.size() == 9` with a comment pointing at MappedParameters.inc; the whole block is still inside `if (parameterSession.isForgeAvailable())`), checked against the two previous entries, pages/mapped-parameters.md, pages/forge-hosting.md (its Pitfalls bullet still holds, no change) and pages/inc-edits-do-not-rebuild.md (already says to touch the runner, no change)
- updated: pages/mapped-parameters.md (the Tests section now says the second check holds the rack to exactly nine, so the default list's length is pinned in DeviceParameters.inc as well as MappedParameters.inc)

## [2026-10-13] update | Native Ableton Push 1 support (uncommitted on main; not yet run on the hardware)
- source: the change's brief (the build and ctest were still running), checked against native/src/app/PushProtocol.h, PushController.h/.cpp, PushControllerControls.cpp, PushControllerSurface.cpp, native/src/model/SessionControllers.cpp, Session.h, SessionMidiInput.cpp, SessionRecording.cpp, SessionDevices.cpp (endDeviceParameterGesture), SessionMixer.cpp, ui/rack/DeviceRack.h/.cpp, app/MainComponent.h/.cpp, app/Main.cpp, native/CMakeLists.txt (31 Session*.cpp lines), tests/PushProtocolTest.cpp and tests/Arrangement/scenarios/PushController.inc
- created: pages/ableton-push.md (why Rhino drives it rather than Tracktion, the ports and their reservation, threads, the pads as a keyboard, controls, gestures from the first turn, the surface, selection, lifecycle, tests; the unverified hardware points; an unplug mid-turn leaving a gesture open, read from the code)
- updated: pages/inputs-and-monitoring.md (reserved ports skipped by every input path, the `device:Push` input, hasHardwareMidiInput true while a Push is connected)
- updated: pages/reading-midi-hardware-on-windows.md (the Push's port names; a running RhinoDAW holds its User port, so close Rhino before a capture)
- updated: pages/app-shell.md (PushController as MainComponent's last member, startHardwareControllers not under --startup-test; Main.cpp's line count loosened, summary changed)
- updated: pages/session-model.md (SessionControllers, 31 units, sendControllerMidi the one call safe off the message thread, the Push's settings under test mode; summary changed)
- updated: pages/ui-cost-of-a-change.md (an end*Gesture always closes its gesture now; the stuck-depth bug the Push made reachable)
- updated: pages/device-rack.md (deviceSelected and selectPluginIndex), pages/computer-keyboard.md (sendControllerMidi as the sibling entry)
- updated: index.md (new page; app-shell and session-model summaries)

## [2026-10-13] update | A Push unplugged mid-turn no longer leaves a gesture open (uncommitted on main; not yet run on the hardware)
- source: the change's brief, checked against native/src/app/PushController.cpp (`closePorts` clears `touched`, `shift`, `muteHeld` and `soloHeld` and calls `endGestures` before releasing held notes; reached from `stop` and from `lookForPush` on unplug)
- updated: pages/ableton-push.md (the Gestures section and the removed open item written by the main agent and verified; the last section's heading now reads "Unverified", since nothing open remains)

## [2026-10-13] update | Push ports re-reserved on every engine rescan, after the first hardware run
- source: the change's brief (rhino.log from the first hardware run; fix built, not yet re-checked on the Push), checked against native/src/model/SessionControllers.cpp (`reserveMidiPorts`, `disableReservedMidiPorts`), Session.h and SessionMidiInput.cpp (`midiDevicesChanged`)
- updated: pages/ableton-push.md (intro no longer says it never ran; Ports: reservation re-applied on every rescan, and why: the first scan came after the Push connected and opened ports All Ins had left enabled, and JUCE shares a port's handle within a process, so the raw pads reached All Ins too; "Unverified" became "On the hardware": the first run, the fix awaiting a re-check, and the USB buzz that a different port cured)
- updated: pages/inputs-and-monitoring.md (the reservation's reason corrected from "one owner" to the in-process double delivery; re-applied by `midiDevicesChanged`)
- updated: pages/reading-midi-hardware-on-windows.md (a MIDI input is exclusive between processes, not within one)

## [2026-10-13] update | Rhino's plugin and Drum Rack windows float above the main window (built and tested; not yet tried by the user)
- source: the change's brief, checked against native/src/app/AppPlatform.h/.cpp (`keepWindowAbove`), app/MainComponent.cpp, ui/rack/DeviceRack.h/.cpp (`keepAboveShell`, `openDevice`, `openDrumWindow`), ui/rack/DrumRackWindow.h/.cpp (`floatAboveShell`, `resized`, `updateFloating`) and tests/Pattern/DrumRackWindowTest.cpp
- updated: pages/app-shell.md (new section: owned windows via `GWLP_HWNDPARENT`, always-on-top toggling rejected and why, the gotcha that the main window's native window must never be recreated while one is open, what checks it; AppPlatform lists `keepWindowAbove`)
- updated: pages/device-rack.md (`openDevice` floats the plugin window; `keepAboveShell` as the UI's one way to ask, and the drum window's `floatAboveShell`)
- updated: pages/drum-rack.md (floats while restored, not while maximised, and why; the cross-window drag still untested; the window test's new check)

## [2026-10-13] update | Browser drops reach the floating Drum Rack window again (built; not yet tried by the user)
- source: the change's brief (cause read from JUCE at 37c894f), checked against native/src/ui/rack/DeviceRack.h/.cpp (`parentHierarchyChanged`, `componentBroughtToFront`, `raiseFloatingWindows`, `watchedWindow`)
- created: pages/juce-drop-target-follows-its-own-window-order.md (a desktop drag resolves its target by juce::Desktop's own window list, not the OS z-order; how an owned window lost browser drops; the re-raise fix; applies to any window kept above by platform means; no automated test, since it needs real windows)
- updated: pages/app-shell.md (floating windows: the drag-order gotcha and its fix, linked)
- updated: pages/device-rack.md (the rack listens to its top-level window and re-raises the floating windows)
- updated: pages/drum-rack.md (floating bullet: the failed drop and the fix; the Unverified bullet now states TreeView's drag does leave its window)
- updated: index.md (new gotcha)

## [2026-10-13] update | Rhino sends Live's pad sensitivity to the Push on connect (built and tested; not yet tried by the user)
- source: the change's brief, checked against native/src/app/PushProtocol.h (`padSensitivitySysex`), app/PushController.cpp (`greet`), tests/PushProtocolTest.cpp and tests/Arrangement/scenarios/PushController.inc
- updated: pages/ableton-push.md (Ports: the greeting's order now includes the pad sensitivity; new Pad sensitivity section: the sysex layout, Live's default as sent, the rejected sensitivity page and why, Live's six curves and the threshold formula for later, velocity also depending on the instrument and Accent; Tests: the scenario checks the greeting; On the hardware: the feel under Rhino still unconfirmed)

## [2026-10-14] update | The Drum Rack window opens 1024 x 720 instead of maximised (built and under test; not yet tried by the user)
- source: the change's brief, checked against native/src/ui/rack/DrumRackWindow.h/.cpp (`openingWidth`, `openingHeight`, `openingBounds`, `show`)
- updated: pages/drum-rack.md (summary; Its own window: the opening size, `openingBounds`, why maximised hid the main window, `raiseFloatingWindows` left alone; floats from the moment it opens; the window test's `openingBounds` check)
- updated: pages/device-rack.md (the name-bar button and the Drum Rack window bullet no longer say maximised)
- updated: index.md (Drum Rack summary)

## [2026-10-14] update | A new track lands under the selected card
- source: the change's brief, checked against native/src/model/SessionTracks.cpp (`appendTrack`, `addTrack`) and ui/arrangement/Arrangement.cpp (`insertTrack`, `addTrackOfType`)
- updated: pages/track-kinds.md (`appendTrack`'s `after` and `addTrack`'s `below`/`added`; `+` and Ctrl+T go through `insertTrack` under `selectedTrack`, main row means the foot; the `markModified` rule; drop and paste lanes and `addAudioTrack` still append)
- updated: pages/track-groups.md (new bullet: under an open group joins by an explicit `trackGroupMemberID`, under a collapsed one lands after the whole group)
- updated: pages/arrangement-view.md (Cards: a new track lands under the selected card)

## [2026-10-14] update | Bounce to New Track (Ctrl+B) and clip mute (uncommitted on main, parent 81b1344)
- source: the change's brief, checked against native/src/model/SessionBounce.cpp, Session.h (`BounceResult`, the two bounce calls, `setClipsMuted`), SessionClips.cpp, SessionInternal.h/.cpp (`runRenderTask`, `nextRenderFile`), Session.cpp (`renderTimelineWav`), ui/arrangement/ArrangementBounce.cpp, Arrangement.cpp, ArrangementGestures.cpp, ArrangementPainter.cpp, app/MainComponentCommands.cpp, tests/Arrangement/scenarios/ClipBounce.inc and native/CMakeLists.txt
- created: pages/bounce-to-new-track.md (Live's rules for a track, a group and the main row; one render per track windowed into one clip per selected clip; the source muted in the same undo step; refusals; the message-thread render, 32-bit float, 15 s stall; Ctrl+B instead of Ctrl+T; open items)
- created: pages/bus-member-renders-nothing-alone.md (createNodeForEdit at 4536d8a skips a track routed to another track; the ScopedProperty workaround on the output name and the fader's `enabled`, and why it works and is safe)
- updated: pages/arrangement-view.md (twelve units with ArrangementBounce.cpp; Ctrl+B dispatches on Focus; muted clips drawn grey and the clip menu's Mute)
- updated: pages/session-model.md (32 units with SessionBounce; `runRenderTask` and `nextRenderFile` in SessionInternal)
- updated: pages/renders-that-never-return.md (the bounce's 15 s stall and `checkClipFile`; `runRenderTask` shared with the WAV export; ClipBounce.inc before GesturesAndPersistence.inc)
- updated: pages/track-groups.md (bouncing a group and a member; a member renders only as part of its bus)
- updated: pages/audio-clip-editor.md (bounce as merge's counterpart; mute is the property `setClipsMuted` sets, MIDI clips included)
- updated: index.md (two new pages; Arrangement view and Session summaries)

## [2026-10-14] update | Bounce to New Track renders in slices behind a modal loading screen
- source: the change's brief, checked against native/src/model/SessionBounce.cpp, Session.h, Session.cpp (`undo`, `redo`, `cancelBounce` callers), SessionInternal.cpp (`stopRenderWhenStalled`, `renderTaskOutcome`, `runRenderTask`), app/LoadingScreen.h/.cpp, app/MainComponent.cpp/.h, app/MainComponentLayout.cpp, app/ProjectFiles.cpp, ui/arrangement/ArrangementBounce.cpp and tests/Arrangement/scenarios/ClipBounce.inc
- created: pages/long-commands-run-in-slices.md (decision: stay on the message thread, slice per frame behind a modal screen rather than render the live edit from a worker; the edit guards; pointers held across slices; merge, open and export not yet behind it)
- updated: pages/bounce-to-new-track.md (summary; the stepped API, `BounceRun`/`RenderInFlight`, the wrappers; `runBounce`/`bounceFinished`; the edit guards; files named per job; the stepped and LoadingScreen tests)
- updated: pages/app-shell.md (summary; new section The loading screen: Task, modal, Esc, 24 of 33 ms, first slice waits a frame, raised last in `resized`, the bounce wiring, `requestClose`; open item for open, export and merge)
- updated: pages/renders-that-never-return.md (a stalled bounce holds the loading screen; the guard split into `stopRenderWhenStalled` and `renderTaskOutcome`, `runRenderTask` the two around a loop)
- updated: pages/session-model.md (SessionInternal render helpers; pitfall: the model guards only undo, redo and replacing the document while bouncing)
- updated: pages/ids-not-pointers.md (the bounce's pointers across slices as a deliberate exception)
- updated: pages/bus-member-renders-nothing-alone.md (`RenderInFlight` replaces `renderBounce`; why the overrides are safe across slices)
- updated: pages/ableton-push.md (`commandsAllowed` false while the loading screen runs)
- updated: index.md (new decision; App shell and Bounce summaries)

## [2026-10-15] update | Export WAV renders in slices behind the loading screen (b6758b4)
- source: the change's brief, checked against native/src/model/SessionExport.cpp, Session.h (`startExport` and its siblings, `ExportRun`), Session.cpp (undo, redo, the `cancelExport` callers), SessionBounce.cpp, SessionInternal.h (`runRenderTask`), app/ProjectFiles.h/.cpp, app/MainComponent.cpp/.h, native/CMakeLists.txt and tests/Arrangement/scenarios/ProjectFileMedia.inc
- updated: pages/long-commands-run-in-slices.md (summary; the export follows the bounce; guards cover both renders; temp-file swap; why 15 s rather than the worker's 60 s; only open and merge left)
- updated: pages/project-files.md (WAV export: `SessionExport.cpp`, the app-side hand-off, refusals, temp file, `renderTimelineWav` for tests only, `exportParameters`)
- updated: pages/app-shell.md (loading screen used by the export too; open item down to open and merge)
- updated: pages/renders-that-never-return.md (export's 15 s budget, `runRenderTask` left to `renderTimelineWav`, `checkTimelineFiles` settled)
- updated: pages/session-model.md (summary: 33 files; `SessionExport`; render helpers; the in-flight pitfall covers `isExporting()`)
- updated: pages/ableton-push.md (an export now runs behind the loading screen, not a disabled shell)
- updated: pages/bounce-to-new-track.md (a bounce start is refused while exporting)
- updated: pages/dj-bounce-on-a-copy.md (the export no longer renders on a worker)
- updated: index.md (Long commands and Session summaries)

## [2026-10-15] update | Level meters on every track card and the main row (commit 7a92d14; all six CTest cases pass, snapshot checked; main output reading and the running app unverified)
- source: the change's brief and the coordinator's follow-ups on the snapshot and the test run, checked against tests/Arrangement/scenarios/TrackMeters.inc, native/src/model/SessionMeters.cpp, SessionMixer.cpp (`ensureTrackMixers`), Session.h/.cpp (`releaseMeterTaps` callers), ui/shared/LevelMeter.h/.cpp, ui/arrangement/Arrangement.cpp (`updateMeters`, the meter layout), ArrangementSync.cpp, ArrangementPainter.cpp (waveform zoom), native/CMakeLists.txt and tests/Arrangement/WorkflowTest.cpp
- created: pages/level-meters.md (the meter behind every fader and `masterLevels`; `takeOutputLevels`, one reader per client, `MeterTap` keyed by itemID and checked against the live plugin; muted chains still measured, so gated on `shouldBePlayed()`; renders read as silence; starter Utility unmetered; the IEC scale, ballistics and pixel-gated repaint; the column and the vblank order; tests added, main reading untested)
- updated: pages/mixer.md (summary; meter row; `ensureTrackMixers` adds a meter; mute rule's meter consequence; "Not built" no longer lists meters)
- updated: pages/device-chain-order.md (summary; every track's strip now ends in a meter behind the fader; added tracks read Utility, Volume & Pan, meter; the Utility's trim shows only on added tracks' meters)
- updated: pages/arrangement-view.md (vblank order; the meter column and the name stopping short of it; waveforms already fill their height, the 50 px a default row leaves them, and quiet material)
- updated: pages/playhead.md (the `UpdateWindow` handoff also collects the meters' damage)
- updated: pages/session-model.md (summary: 34 files; `SessionMeters`; `releaseMeterTaps` before the edit goes)
- updated: pages/ids-not-pointers.md (meter taps keyed by id and checked against the plugin; released with the edit)
- updated: pages/colours-and-typography.md (`meterSafe`, `meterHot`, `meterClip`)
- updated: pages/ui-cost-of-a-change.md (meters repaint only on whole-pixel moves)
- updated: pages/dj-view.md (every track's channel strip has a meter, not only a starter's)
- updated: pages/headless-ui-snapshots.md (`RHINO_TRACK_METERS_SNAPSHOT`: the left 420 px of the arrangement at 2x, every meter fed a different level)
- updated: pages/juce-output-stream-appends.md (`TrackMeters.inc`'s snapshot deletes first too)
- updated: index.md (new page; Mixer, Device chain order and Session summaries)

## [2026-10-15] update | Meter at the start of the card, a wider card, and a fold arrow before every name (commit 14c9c77; all six CTest cases pass, snapshot checked)
- source: the change's brief, checked against native/src/ui/arrangement/Arrangement.h (the card constants), ArrangementGroups.cpp (`disclosureBounds`, `beginDisclosureGesture`, `paintDisclosure`), ArrangementGeometry.cpp (`laneHeightFor`), ArrangementRename.cpp (`trackNameBounds`), native/src/model/SessionTracks.cpp (`setTrackFolded`, `setTrackLaneHeight`) and tests/Arrangement/scenarios/GesturesAndPersistence.inc
- updated: pages/arrangement-view.md (the card's meter, controls and name columns at 264 px; buttons over faders; the fold arrow on every card, what it does on a bus and elsewhere, `rhinoTrackFolded`, a chosen height unfolding; replaces the right-edge meter column)
- updated: pages/level-meters.md (the meter opens each card at `cardMeterInset` plus the indent; the right-edge column of `7a92d14` and why it moved; the test and snapshot now cover the place and a folded row)
- updated: pages/track-groups.md (the bus's arrow moved from the left gutter to before its name; `paintGroupGutter` paints only the band; a member's arrow folds its own row)
- updated: pages/writing-rhino-tests.md (rule 8: never spell a lane's x in pixels; the hard-coded drag that the wider header broke)
- updated: pages/session-model.md (a row's fold is a view setting with no undo and `ModelChange::none`)
- updated: pages/headless-ui-snapshots.md (the meters snapshot folds one row)

## [2026-10-15] update | The start-up splash shows no title bar
- source: the change's brief, checked against native/src/app/Main.cpp and MainWindow.h/.cpp (`setTitleBarShown`, `captionHeight`)
- updated: pages/app-shell.md (The window: `setTitleBarShown`, the 560 x 292 splash and why, the bar back at stage 2 and on a failed start; the owned-window gotcha now names `setTitleBarShown` as layout only)
- updated: pages/headless-ui-snapshots.md (`RHINO_STARTUP_SNAPSHOT` writes the whole window, and names the splash rather than "the loading screen")

## [2026-10-15] update | A selected track's row lit by a wash instead of a left-edge strip; room around the meter (commit 83135f7; all six CTest cases pass, snapshot checked)
- source: the change's brief and the coordinator's follow-up, checked against native/src/ui/shared/Theme.h, ui/arrangement/ArrangementPainter.cpp (`paintTrackCards`, the main row), Arrangement.h (the card constants) and Arrangement.cpp (the main row's layout)
- updated: pages/arrangement-view.md (every selected track's card and lane take `selectedRowWash` under the bands, grid and clips; the working-track strip is gone; the main row's selected look; the check paints twice; the card now 272 px, the meter at 9 with 8 px before the controls at 24, controls column 148; the main row at 9, 24, 70, 136)
- updated: pages/colours-and-typography.md (`selectedRowWash`, a 7% white wash)
- updated: pages/level-meters.md (`cardMeterInset` 9, 8 px before the controls, the main row's meter at x 9)
- updated: pages/writing-rhino-tests.md (rule 13: compare a picture with itself under one changed state, from the selected-row check that misread a mark in another row)

## [2026-10-15] update | Per-keyboard velocity curves and a Preferences window (uncommitted)
- source: the change's brief, checked against native/src/model/VelocityCurve.h, SessionKeyboards.cpp, SessionControllers.cpp (`disableReservedMidiPorts`), SessionMidiInput.cpp, SessionRecording.cpp (`finishRecording`), app/MainComponent.h and MainComponentCommands.cpp, ui/preferences/*, ui/shared/Theme.h, native/CMakeLists.txt and tests/Arrangement/scenarios/KeyboardCurves.inc
- created: pages/keyboard-velocity-curves.md (the curves; why Rhino reads a curved port; All Ins plus the `(curved)` stand-in; Linear untouched; the port held while a curve is set; settings and start-up; `syncKeyboardPorts`; the Tracktion facts at `4536d8a`; states; tests)
- updated: pages/inputs-and-monitoring.md (a curved port's token resolves to its stand-in; curved keyboards beside reserved ports; `rearmListeningMidiTracks`)
- updated: pages/app-shell.md (Preferences replaces the Audio settings dialog, Ctrl+, and the tab rule; `startHardwareControllers` opens curved keyboards)
- updated: pages/ableton-push.md (the software curve now exists but refuses the Push's reserved ports)
- updated: pages/reading-midi-hardware-on-windows.md (the Launchkey's USB ids; the code 43 hub failure; Live holding the ports, error 7, Live's Log.txt, the missing rhino.log line)
- updated: pages/session-model.md (35 units, `SessionKeyboards`, curves suppressed under test; summary changed)
- updated: pages/colours-and-typography.md (`drawSnappedText` and why the Preferences tabs are buttons)
- updated: pages/headless-ui-snapshots.md (`RHINO_PREFERENCES_SNAPSHOT`)
- updated: index.md (new page; Session summary)

## [2026-10-15] update | A pasted audio clip keeps its stretch of the file: reverse written only when it differs (commit 50d87d5)
- source: the change's brief, checked against native/src/model/SessionRegion.cpp (`pasteClipRegion`) and SessionAudioClips.cpp (`setAudioClipReversed`)
- created: pages/writing-reverse-moves-a-clips-window.md (Tracktion's `reverseLoopPoints` on any `isReversed` write, absent to off included, at `4536d8a`; the quieter trimmed copies; why symmetric test windows missed it; the rule)
- updated: pages/region-editing.md (the paste writes reverse only for a reversed clip, first)
- updated: pages/audio-clip-editor.md (`setAudioClipReversed` writes only a real change)
- updated: index.md (new page)

## [2026-10-15] update | No clip shorter than a split may leave: making room and copying keep the 10 ms floor (commit 972cc0e)
- source: native/src/model/SessionRegion.cpp (`clearClipRegionInEdit`, `copyClipRegion`) and Tracktion's `split` in tracktion_ClipOwner.cpp at `4536d8a` (refuses a cut within 1 ms of an edge)
- updated: pages/clip-placement.md (a crossing clip is cut only when 10 ms stays outside, otherwise deleted whole; the engine's own 1 ms refusal)
- updated: pages/region-editing.md (a clip grazing the region by less than 10 ms is not copied; the one-sample-past-the-beat drum sample)

## [2026-10-15] update | Gaps after the 10 ms floor (commit 972cc0e): delete, the test, and a stale build from Copy-Item
- source: the change's brief, checked against native/src/model/SessionRegion.cpp (`copyClipRegion`, `clearClipRegionInEdit`), ClipGeometry.h and tests/Arrangement/scenarios/ClipSlivers.inc
- updated: pages/region-editing.md (delete and paste's clear take a clip reaching under 10 ms past an edge whole)
- updated: pages/clip-placement.md (only the copy path made one-sample slivers; `ClipSlivers.inc` pins both)
- updated: pages/inc-edits-do-not-rebuild.md (the `Copy-Item` restore trap recurred in Rhino on `SessionRegion.cpp`)
- updated: pages/build-and-test-rhino.md (a bullet on the `Copy-Item` timestamp trap, linking the gotcha)

## [2026-10-16] update | Cut, copy, paste, duplicate and delete on whole track cards (commit 7f546da)
- source: the change's brief, checked against native/src/model/SessionTrackClipboard.cpp, ui/arrangement/ArrangementTrackClipboard.cpp, Arrangement.h, ArrangementSelection.cpp, ArrangementGestures.cpp (`showTrackMenu`) and native/CMakeLists.txt
- created: pages/track-clipboard.md (`TrackClipboard` as values after `flushStateToValueTree`; `insertTrackCopies` as `Edit::copyTrack` does, `remapIDs` leaving outside references alone at `4536d8a`; new device keys and lanes, `rhinoArmed` stripped; a bus for its group, new group ids, placement; `removeTracks` and the Delete-on-a-bus change; one clipboard at a time; by-value card lists; selection after paste and delete; what the test leaves out)
- updated: pages/region-editing.md (summary; the region is the lanes' rule, cards move whole tracks; one clipboard of each, copying empties the other)
- updated: pages/arrangement-view.md (summary and body: thirteen units with `ArrangementTrackClipboard.cpp`; `Focus` now routes Ctrl+C, X and D too)
- updated: pages/track-groups.md (the bus stands for its group on the clipboard; Delete on a bus card deletes the group, Ungroup alone drops the bus; reconcile after track paste and delete)
- updated: pages/session-model.md (summary and body: 36 units with `SessionTrackClipboard`)
- updated: index.md (new page; summaries of region-editing, arrangement-view and session-model)

## [2026-10-16] update | A velocity lane in the note editor, after Live 12's velocity editor (uncommitted; suite still running)
- source: the change's brief, checked against native/src/ui/notes/VelocityLaneGeometry.h, StepGrid.h, StepGridVelocity.cpp, StepGridEditing.cpp, model/Session.h, SessionNotes.cpp, SessionDjSources.cpp (`gestureHeld`) and app/MainComponentLayout.cpp
- created: pages/note-velocity-lane.md (files and the pure header; layout above the scrollbar, the 8 px row floor, folding under 32 px, the divider; MIDI 1-127 instead of percent; absolute `setNoteVelocities` and group drags from the press values; the gesture bracket; no `restartPlayback`; marker gestures, draw-mode strokes held to the visible steps; dimmed out-of-window markers, whole-pixel drawing; what is not done yet)
- updated: pages/note-editor.md (summary; seven units; hold V in MIDI steps, Ctrl+Up/Down; a note dragged below the rows; a Velocity lane section; the shared gesture timer and the `setSelectedStates` repaint as pitfalls)
- updated: pages/pointer-and-selection.md (the lane's markers follow the shared rules; 3 px arming; right button and double-click inert; time-only autoscroll)
- updated: pages/ui-cost-of-a-change.md (the note-velocity gesture bracket; `gestureHeld` counts it; the two new `--profile-ui` timings)
- updated: pages/dj-view.md (`gestureHeld` list includes the note-velocity gesture)
- updated: pages/app-shell.md (the pane first opens at a fifth of the window, and the note editor's first appearance raises it to `StepGrid::openingHeight`, 261 px)
- updated: pages/colours-and-typography.md (the note editor's and the lane's palette tokens)
- updated: pages/writing-rhino-tests.md (the geometry runner calls `tests/VelocityLaneTest.*`)
- updated: pages/keeping-files-small.md (`VelocityLaneGeometry.h` as a pure header; `StepGrid.cpp` 679 and `StepGridGestures.cpp` 727 lines, past the line)
- updated: pages/development-environment.md (the Live manual's velocity editor pages; Bash commands mangled past about 7 KB; the HARBIZ hook refusing `.key` and `file:` in Workflow input; `near` and `far` macros from `windows.h`)
- updated: index.md (new page; note-editor summary)

## [2026-10-16] update | Velocity lane follow-up: suite passing, the review's fixes, and a JUCE key-code gotcha
- source: the coordinator's follow-up brief, checked against native/src/ui/notes/StepGrid.cpp (`keyPressed`), StepGridVelocityGestures.cpp, StepGridVelocity.cpp, StepGridEditing.cpp (`laneNoteFor` callers), app/MainComponent.cpp and MainComponentLayout.cpp (`paneHeightDragged`)
- updated: pages/note-velocity-lane.md (status: all six CTest cases pass twice, the user has not tried it; the three units split from 807 lines and the scenario names; `zoomAt` and `rebuildVisibleNotes` against the scrollbar folding the lane; the raise skips a dragged pane; a click is a press that never armed; a new section on holding the bracket safely: a second press ends the held gesture, keys consumed while it is held, stroke alignment and its O(N²) tick, hover after markers move)
- created: pages/juce-keypress-codes-not-constexpr.md (named `juce::KeyPress` codes are `static const int` defined in a .cpp, so no `static constexpr` array; a local `const int` array instead)
- updated: pages/note-editor.md (copy, cut and `effectiveStepRegion` read every selected note through `laneNoteFor`, after cut lost an out-of-window note from the clipboard; `mouseUp` keeps the timer for hold-V and subdivision; `setSelectedStates` repaints the insert line's column)
- updated: pages/app-shell.md (the opening raise applies only while `paneHeightDragged` is false)
- updated: pages/ui-cost-of-a-change.md (a second press could strand the velocity bracket; `StepGrid::mouseDown` ends a held gesture first)
- updated: pages/pointer-and-selection.md (a lane click is a press that never armed; a second press ends the held lane gesture)
- updated: pages/writing-rhino-tests.md (declare a listener a panel's teardown reaches before the panel)
- updated: index.md (new gotcha page)

## [2026-10-16] update | DJ effects rack moves below a larger mixer
- source: the change's brief, checked against native/src/ui/dj/DjView.cpp, DjMixerPanel.h, DjMixerPanelInternal.h, DjMixerPanel.cpp, DjMixerPanelMaster.cpp and native/src/tests/Arrangement/scenarios/DjBooth.inc
- updated: pages/dj-view.md (the full-height centre column; responsive 520-620 px upper mixer; crossfader boundary; SEND FX and BEAT FX rack below; two-column master; wider strips and larger controls; workflow layout contract)

## [2026-10-17] update | Forge's theme tokens (commit 425b253) and the Merli theme (working tree)
- source: the change's brief and the coordinator's two follow-ups (drawImage opacity; the second Merli fix pass), checked against instruments/rhino-forge/ui/ForgeTheme.h, ForgeMerliMaterial.h, ForgeMerliChassis.h, ForgeMerliControls.h, ForgePerformanceWheel.h, ForgePlacement.h (`identityPlateBounds`), ui/assets/README.md, the isMerli dispatches in ui/, src/ForgeProcessorSettings.cpp, ForgeProcessor.cpp (`createEditor`), ForgeEditorTheme.cpp, ForgeEditorInput.cpp, tests/ForgeTestMain.cpp, ForgeTestTools.cpp, ForgeTestsTheme.cpp and CMakeLists.txt
- created: pages/forge-themes.md (82 tokens, `earPink` removed, and `tokenBindings`; names kept when repainted; Settings.xml read once before the first Editor and why; the menu's two doors; the isMerli dispatch and the Merli files, with the second fix pass: `furOrigin`, the rim stopping short of the ears, `drawRoundedPlate`'s inside hairline and `topScrews`, no paw-lamp halo, the wheel and tyre washes through the art's alpha; rules that bite, drawImage at the current colour's alpha among them; the rebuilt fur tile and ears, linking ui/assets/README.md for the recipe; header plates at y 36 against Forge's 10 and 11; black keys and the contrast of legends and three known low-contrast labels; cost; the theme test area; the deferred frameless window)
- updated: pages/colours-and-typography.md (summary; Forge's colours are theme tokens in ForgeTheme.h; text/panel/line now live there)
- updated: pages/forge-editor.md (summary and body: nineteen units with ForgeEditorTheme.cpp; the four statics unchanged and Merli added none; a rule on tokens applied once and the isMerli dispatch)
- updated: pages/forge.md (the two themes and how to switch; 19 test areas)
- updated: pages/headless-ui-snapshots.md (the theme argument, `-` for no preset, exit 2 on an unknown theme, `--profile`'s theme)
- updated: pages/build-and-test-forge.md (19 areas; the scratch Settings.xml the test main uses; the C4459 names now in ForgeTheme.h)
- updated: pages/proving-a-forge-change-changed-nothing.md (the control build; OSC to FX hash, the ARP page does not)
- updated: pages/git-workflow.md (two sessions and one index: the silent revert, its fix and the rule)
- updated: index.md (new page; summaries of colours-and-typography and forge-editor)

## [2026-10-18] update | Forge FX racks: LEVEL takes modulation, drops reach the scrolled rack, selectors fit their strip
- source: the change itself, checked against instruments/rhino-forge/core/ForgeMatrix.h, core/ForgePatch.h, src/ForgeEditorInput.cpp (`controlAt`), src/ForgeEditorPaint.cpp, ui/ForgeControlBlock.h (`controlBlock`), tests/ForgeTestsFxModulation.cpp, ForgeTestsFx.cpp, ForgeTestsModulation.cpp, ForgeTestsLayout.cpp, ForgeSuites.h, ForgeTestMain.cpp and CMakeLists.txt
- updated: pages/forge-modulation.md (each rack slot's LEVEL is a destination; the FX LEVEL bank appended after tuning and why it is not an eighth per-slot entry; `isFxDestination` tests two ranges; rack drop targets mapped out of `fxControlViewport` and clipped to it, and the `forge_fxModulation` test)
- updated: pages/forge-mixer-and-fx.md (every rack knob, MIX and LEVEL is a destination; rack selectors centred in the slot's strip, and the layout sweep that holds it)
- updated: pages/append-only-stored-indices.md (the FX LEVEL bank as the rule applied)
- updated: pages/build-and-test-forge.md, pages/forge.md (20 areas; `-R forge_fx` also selects `forge_fxModulation`)

## [2026-10-18] update | Gaps after the FX rack commit (6b18e7b, de900a6)
- source: the change's brief, checked against instruments/rhino-forge/src/ForgeEditorInput.cpp (`controlAt`), ForgeEditorLayout.cpp, ForgeEditorPages.cpp and the wiki's other Forge pages
- updated: pages/forge-editor.md (the rack's controls are children of the clipping `fxControlViewport`, so a hit test or overlay maps them with `getLocalArea`)
- updated: pages/forge-is-an-independent-vst3.md (dropped the stale "18 areas" count)

## [2026-10-19] update | Forge's preset browser and bank, the factory set, and Serum 2 and Vital import
- source: the change's brief (work of 2026-10-18/19, uncommitted), checked against instruments/rhino-forge/src/ForgePresetBank.*, ForgePresetBrowser.h, ForgePresetBrowserInternal.h, ForgeEditorPresets.cpp, ForgeEditorTheme.cpp, ForgeProcessor.h, ForgeProcessorState.cpp, ForgePresetImport.h, ForgePresetImportInternal.h, ForgePresetImportSerum.cpp, ForgePresetImportVital.cpp, ui/ForgePresetPlate.h, ForgePanels.h, ForgeMerliChassis.h, presets/make-presets.py and Factory/, tests/ForgeTestsTheme.cpp, ForgeTestTools.cpp, ForgeTestRender.cpp, ForgeTestMain.cpp, CMakeLists.txt and native/scripts/fetch-dependencies.py
- created: pages/forge-preset-browser.md (the bank in Documents, the factory set copied in by `ensure`, read-only Serum 2 and Vital libraries, styles, favourites in Settings.xml, listing from file heads and `JsonHeadScanner`'s timings, the browser's layout, gestures and save rules, its tests)
- created: pages/forge-preset-import.md (one way in through `PatchWriter` and `applySavedState`, warnings instead of approximations, the shared depth, unipolar-recentre, BI and mod-of-mod rules, Vital's domains and tables, results and the uncalibrated Serum level, the `import` tests and the drive-root walk)
- created: pages/serum-2-preset-format.md (the container, embedded and referenced tables, the knob laws taken as facts from the GPLv3 serum2vital project)
- created: pages/buffered-child-breaks-pixel-stability.md (a buffered `PresetPlate` broke two fresh panels' byte-for-byte match under Direct2D; drawn unbuffered)
- updated: pages/forge-presets-and-state.md (summary; `PresetInfo` properties and the wrapper's attributes; `applySavedState` as the one path; file name as preset name; `presetRevision`; the factory set and `make-presets.py`)
- updated: pages/forge.md (preset browser, factory set and import done; `presets/` and 22 test areas; Serum presets converted, never shipped)
- updated: pages/forge-editor.md (summary: twenty units with ForgeEditorPresets.cpp; the title plate's `PresetPlate`, its doors and tooltip, the removed legend and moved hatch; the status label now transient)
- updated: pages/forge-themes.md (Merli's cat plate names the preset and the cat shrank to a third; factored plate bounds; the PresetPlate exempt from the same-rectangle check; Merli's commit `286e146`)
- updated: pages/build-and-test-forge.md (summary; zstd fetched and built as `ForgeZstd`, re-run the fetch script; 22 areas; `--audition` and `--convert`)
- updated: pages/headless-ui-snapshots.md (`PRESETS`, `SAVE` and `PRESETS:<LIBRARY>` with their scratch bank in the working directory; a Serum or Vital preset as the snapshot's patch)
- updated: pages/serum-patches-sound-different.md (Serum 2 stores plain gain; converted presets play about 11 dB quieter, uncalibrated)
- updated: pages/known-hazards.md (item 13, slot 1's stale destination, still open; the factory set and importers avoid it)
- updated: pages/off-limits-directories.md (`native/.deps` now also holds zstd)
- updated: overview.md (`presets/` in the layout; a history row for 2026-10-18/19)
- updated: index.md (four new pages; summaries of forge-editor, forge-presets-and-state and build-and-test-forge)

## [2026-10-19] update | Forge import's clamp warning names the end, and the Serum 2 sweep re-run
- source: the change's brief, checked against instruments/rhino-forge/src/ForgePresetImport.cpp (`PatchWriter::set`), ForgeParameters.cpp (`asSeconds`) and tests/ForgeTestsImport.cpp
- updated: pages/forge-preset-import.md (the clamp warning names the end it holds the value at, and why; merged the caller's Results bullet into the PatchWriter section; the re-run sweep: 50 silent, two at 0 dBFS)
- created: pages/juce-parameter-text-clamps-to-range.md (NormalisableRange clamps, so getText cannot print an out-of-range value)
- updated: index.md (new gotcha page)

## [2026-10-21] update | Forge FX rack strips redesigned per type, meters, readings and a FLANGER (7f3e81e..a350964)
- source: the change's brief, checked against instruments/rhino-forge/ui/ForgeFxStrips.h, ForgeFxDisplay.h, ForgeFxVisuals.h, core/ForgeFx.h, core/ForgeFxDsp.h, src/ForgeProcessor.cpp, ForgeEditorFx.cpp, ForgeEditorFxView.cpp, ForgeEditorLayout.cpp, ForgeEditorInput.cpp, ForgePresetImportSerumFx.cpp, ForgePresetImportVital.cpp, tests/ForgeTestsFx.cpp and ForgeTestsLayout.cpp
- created: pages/forge-fx-strip-plans.md (decision: `fxStripLayout` as a pure per-type plan on a 48-unit grid; spots, wells, panels; the refactor-first phase; the card as bypass with no BYP chip or replace gesture; `fxTypesShown`; `fxGenericStrip` fallback; `requireFxStripsHold`)
- updated: pages/forge-mixer-and-fx.md (summary; nine types with FLANGER as 9; strips and card-as-bypass; readings and the per-slot meter flow from `FxRack` through `processBlock` and `takeFxMeters` to `FxLive`; flanger and metering struck from the absent list; stale `controlBlock` selector note replaced)
- updated: pages/forge-fx-slots-have-generic-parameters.md (nine types; readouts that follow the DSP; `fxKnobLabel` PEAK; FLANGER's knob meanings)
- updated: pages/displays-draw-from-the-dsp.md (each strip picture's engine function; the sine-through-the-slot tests within 0.5 dB)
- updated: pages/forge-editor.md (rack slots laid out by strip plan; stale `displayWeight` removed; the rack's readings as the exception to "no knob prints its value", and why)
- updated: pages/forge-preset-import.md (Serum and Vital flangers now convert; Vital's centre-pitch-to-DELAY mapping)
- updated: pages/serum-2-preset-format.md (FXFlanger's keys; `kParamLevelOut` not read)
- updated: pages/append-only-stored-indices.md (FLANGER appended as `FxType` 9)
- updated: pages/forge-knob-diameter-is-panel-wide.md (rack knob sizes now derive from the shared diameter)
- updated: index.md (new decision page; forge-mixer-and-fx summary)

## [2026-10-21] update | Forge FX pictures dragged by their nodes; the generic pictures removed (f4241e4, dae2411)
- source: instruments/rhino-forge/ui/ForgeFxGestures.h, ui/ForgeFxDisplay.h, src/ForgeEditorFxView.cpp, src/ForgeEditorInput.cpp, tests/ForgeTestsFx.cpp
- updated: pages/forge-fx-strip-plans.md (the node gestures as pure functions, `Editor::fxWellAt`, one host gesture per knob; the fallback well now shows the type's mark, the old generic pictures deleted)
- updated: pages/displays-draw-from-the-dsp.md (gestures invert the drawn axes through `fxPictureBox`, read from the slot as pressed)
- updated: pages/forge-mixer-and-fx.md (pictures can be dragged by their nodes)

## [2026-10-21] update | Forge's FX pictures kept as images; --profile times a named page
- source: instruments/rhino-forge/src/ForgeEditorFxView.cpp, ui/ForgeFxDisplay.h, tests/ForgeTestTools.cpp, tests/ForgeTestsFx.cpp
- updated: pages/forge-panel-repaint-cost.md (`--profile` page and preset arguments; the `fxPictures` cache, its key rule, live marks drawn over it, and the measurements against the pre-redesign build)

## [2026-10-21] update | Forge FX picture gestures: gaps filled (f4241e4 and later)
- source: instruments/rhino-forge/ui/ForgeFxGestures.h, src/ForgeEditorFxView.cpp, src/ForgeEditorInput.cpp, tests/ForgeTestsFx.cpp
- updated: pages/forge-fx-strip-plans.md (what a press takes per picture; presses only inside the plot, because of the phaser's STAGES field; nodes jump to the press except the distortion's DRIVE handle, `fxWellFollowsPointer`, and the two cursors; the inverse knob laws; the phaser's 20 Hz axis against FREQ's 80 Hz floor that failed the first end-to-end test)
- updated: pages/development-environment.md (a command hook refuses shell commands with a token ending in `.key`; why the FX picture cache's field is `drawnFrom`)

## [2026-10-22] update | Forge FX pictures pasted at full brightness; taller strips and larger text (44e19bb)
- source: the change's brief, checked against instruments/rhino-forge/src/ForgeEditorFxView.cpp, src/ForgeEditorPaint.cpp, ui/ForgeModule.h, ui/ForgeFxStrips.h, ui/ForgeFxDisplay.h, ui/ForgePlacement.h, ui/ForgeMerliChassis.h, tests/ForgeTestsFx.cpp
- created: pages/juce-image-draws-at-current-opacity.md (gotcha: `drawImage*` composites at the last colour's opacity; the faded FX picture cache and why snapshots missed it; which other blits are safe; paint twice to test a cache)
- updated: pages/forge-panel-repaint-cost.md (the paste sets opacity 1; the cache test holds the kept frame to a drawn one; 9.0 ms idle and 62.9 ms resize, three strips shown)
- updated: pages/forge-fx-strip-plans.md (`fxSlotHeight` 92 px and why, the three-strip test at both window sizes, the strip lines tied to that height; text sizes; `rackKnobs` finds a knob by the caption's foot)
- updated: pages/forge-mixer-and-fx.md (the compressor meter's look, its end-to-end needle test, about 14 dB off the default patch, and the cache, not the art, behind "greyed out")
- updated: pages/headless-ui-snapshots.md (a snapshot paints once and never shows a kept layer)
- updated: index.md (new gotcha page)

## [2026-10-22] update | Forge keeps no image in a static, so RhinoDAW exits after Forge's window was open (f2a972e)
- source: the change's brief, checked against instruments/rhino-forge/ui/ForgeChrome.h, ui/ForgePerformanceWheel.h, tests/ForgeTestsTheme.cpp
- created: pages/plugin-dll-freed-at-next-peekmessage.md (gotcha: JUCE's dark-mode WH_CALLWNDPROC hook holds the plugin's DLL, so it detaches at the next PeekMessage, inside DestroyWindow's MSCTF teardown or OleUninitialize; the rhino.log tell; JUCE's GUI singletons die with the last plugin object, so only C++ statics reach the detach)
- updated: pages/plugin-static-images-deadlock-on-unload.md (the close-button ghost of 2026-10-22, confirmed from two processes' stacks; the fix applied through ImageCache and `cachedImage`, neither proposed option; ImageCache's purge rules and key spaces; `releasedImagesSuite`; snapshot, profile and end-to-end verification; deleting from the rack still not retested; summary)
- updated: pages/forge-hosting.md (both pitfalls: relinking the held VST3, the month-old VST3, the freeze fixed)
- updated: pages/forge.md (the known issue is fixed)
- updated: pages/forge-editor.md (the static-image rule as it now stands: getFromMemory or `cachedImage`, a row in the test's table)
- updated: pages/forge-themes.md (Merli's pattern is now Forge's)
- updated: pages/proving-a-forge-change-changed-nothing.md (a binary copied out of the build tree finds no tables/ and differs on OSC and TABLE; step 4 put back in the list)
- updated: pages/locked-executable-lnk1104.md (renaming works on the VST3; the month-old VST3 the other habit left; the shutdown ghost and its log; summary)
- updated: pages/build-and-test-forge.md (move the held VST3 aside to relink)
- updated: pages/reading-a-hung-process.md (the rhino.log tell; 64-bit ctypes argtypes; driving RhinoDAW through UI Automation, the two "Edit" buttons, WM_CLOSE as the close button's path; summary)
- updated: pages/driving-the-forge-standalone.md (UI Automation invokes JUCE buttons and menu items without the pointer)
- updated: index.md (new gotcha page; three summaries)

## [2026-10-22] update | Forge gains a second filter, M15 (63b2e1a)
- source: the change's brief, checked against instruments/rhino-forge/core/ForgeCore.h, core/ForgePatch.h, ui/ForgeModule.h, ui/ForgeModules.h, src/ForgeEditorPages.cpp, src/ForgeEditor.cpp, src/ForgePresetImportSerum.cpp, src/ForgePresetImportVital.cpp
- updated: pages/forge-filter.md (two filters: per-filter routes, parallel and `filterSerial` series, filter 1's ids kept and filter 2's appended, an unfed FILTER 2 skipped and reset when reached again, one two-bank module at the user's request, cached TYPE/FREQ ids, tests and the byte-identical proof against `ec69248`; summary)
- updated: pages/forge-mixer-and-fx.md (ten channels, F1/F2 TO chips, FILTER 1's TO is `filterSerial`; summary)
- updated: pages/forge-modulation.md (FILTER 2's seven destinations appended after the FX LEVEL bank, not rack destinations; filter 1's relabelled F1)
- updated: pages/forge-editor.md (`bankEnables` and `attachEnable`; cards numbered unless wider than 32 px; `bankTitleGutterOf` replaces the three-character title limit)
- updated: pages/forge-engine.md (per-filter buses and delay lines; FILTER 2 skipped when unfed, reset when fed again)
- updated: pages/forge.md (M15 done on 2026-10-22; FILTER's two banks on the signal row)
- updated: pages/forge-preset-import.md (both converters still fill FILTER 1 only; filling FILTER 2 is open)
- updated: pages/serum-2-preset-format.md (filter balance: one filter converted, FILTER 2 not yet filled)
- updated: pages/append-only-stored-indices.md (FILTER 2's destinations and parameters appended, filter 1's ids kept)
- updated: pages/forge-presets-and-state.md (FILTER 2 is additive inside format 3; an older patch opens it unfed)
- updated: index.md (two summaries)

## [2026-10-23] update | Merli's FORGE wordmark stands on the fur, with no plate (5adf969)
- source: the change's brief, checked against instruments/rhino-forge/ui/ForgeMerliChassis.h, ui/ForgeMerliMaterial.h and CMakeLists.txt
- updated: pages/forge-themes.md (new subsection: the plate and its grain removed, `drawWordmarkOnFur` and its unchanged box, `drawLetterShadows` and the padded software image `DropShadow` needs, `letterFeet`, `drawFurOverLetters` and the fixed seed, design units, the two rejected prototypes; `drawBackdrop` bullet; the whorl checked in the newly exposed fur; seven embedded PNGs, `merli_plate.png` left uncompiled, `merli_title_plate.png` kept for the cat; the stale ear-above-the-title-plate clause dropped; the 2026-10-23 profile, Merli's rebuilds now a few ms dearer)

## [2026-10-23] update | Cap the compiler's jobs when building; the second machine's blue screens
- source: the brief (no code changed), checked against instruments/rhino-forge/build/RhinoForge*.vcxproj and native/build/RhinoNative.vcxproj
- created: pages/build-parallel-does-not-cap-compiler.md (gotcha: `/MP` with no count, so `--parallel N` caps projects only; the `CL_MPCount=2` command and its measured run; the 0x116 and 0x119 signature, the starved memory, the driver timeouts that predated the build, the driver update; stop and cap on blue screens or `nvlddmkm` timeouts)
- updated: pages/build-and-test-forge.md (Build: "compiling still uses two jobs" corrected; the cap)
- updated: pages/build-and-test-rhino.md (Build: step 5, the cap)
- updated: pages/development-environment.md (the second machine: cores, memory, GPU, the blue screens, the driver update, the cap)
- updated: index.md (new gotcha page)

## [2026-10-23] update | Merli's ears stand at the ends of the head
- source: the change, instruments/rhino-forge/ui/ForgeMerliChassis.h (`earOverhang` 6 to 18.5), measured on --snapshot PNGs
- updated: pages/forge-themes.md (each ear's outer edge met the fur 12.5 px inside the window, so the band's square corner stood out past both ears; the art's box now hangs its 18.5 px outer margin past the window)

## [2026-10-23] update | A region no longer selects the clips it touches
- source: the change, native/src/ui/arrangement/ArrangementSelection.cpp (`selectRegionContents` replaced by `clipsToActOn`), Arrangement.cpp, ArrangementBounce.cpp, scenario RegionClipboard.inc
- updated: pages/region-editing.md (only a press on a header selects a clip; the whole-clip commands fall back to the region's clips; the clip menu keeps the region)
- updated: pages/pointer-and-selection.md (a clip's lower row is empty space, and a sweep across it selects no clip)

## [2026-10-23] update | Bounce reads clipsToActOn (follow-up to 558cede)
- updated: pages/bounce-to-new-track.md (`bounceSelected` bounces the picked clips, else the whole clips a dragged region runs across, through `Arrangement::clipsToActOn`; the region no longer selects them)

## [2026-10-23] update | Bounce a time selection as a span, as Live does
- source: commit cbf0cf0, native/src/model/SessionBounce.cpp (`startBounceRegion`, `BounceJob::region`), ArrangementBounce.cpp, Arrangement.cpp, scenario ClipBounce.inc; Tracktion at 4536d8a upstream; Live 12 manual pp. 418-420
- updated: pages/bounce-to-new-track.md (summary; the dispatch to `bounceRegion` and the clip menu; new section "A time selection": which tracks, muted clips left out, naming, the cut and mute inside one undo step, `splitClip`'s left/right rule, MIDI note chase; the span item removed from Not done)
- updated: pages/region-editing.md (bounce no longer falls back to the region's clips; it bounces the span)
- updated: pages/keeping-files-small.md (SessionBounce.cpp at 661 lines, and what splitting it needs)
- updated: index.md (Bounce to New Track's summary)

## [2026-10-23] update | The arrangement opens on two minutes and zooms to 1/16384, its adaptive grid matched to Live
- source: the change's brief, checked against native/src/ui/arrangement/Arrangement.h (`defaultViewSeconds`), Arrangement.cpp (`zoom`), ArrangementGrid.h (`adaptiveGridDivision`, `deepestZoomPixelsPerBeat`, `drawnGridBeats`), ArrangementPainter.cpp
- updated: pages/arrangement-view.md (new section "Zoom and the adaptive grid": the two-minute opening view and the zoom-out cap, the 1/16384 zoom-in floor held per beat, the nearest-division rule and why the old one read half an octave coarse, divisions past 1/64 outside the Fixed menu, the drawn grid coarsened under 4 px; open items at deep zoom: rulers, thumbnail peaks, float `xFor`, the grid button's squeeze)
- checked, not changed: pages/transport.md (its tempo and signature ranges already match the code)

## [2026-10-23] update | The arrangement's time row reads round times at every zoom (61822a1)
- source: the change's brief, checked against native/src/ui/arrangement/ArrangementGrid.h, ArrangementPainter.cpp (`paintTimeRuler`), native/src/ui/shared/WallClock.h, tests/Arrangement/ClipGeometryTest.cpp and scenario BrowserDrops.inc
- updated: pages/arrangement-view.md (new section "The time row": the round steps and their 88 px and 20 px spacings, the hand-over to the grid's chain at 0.5 ms readings, decimals of a millisecond, marks counted from the song's start and the 4096 cap on painted marks, the tests and `RHINO_TIME_ROW_SNAPSHOT`; the bar-ruler bullet no longer says a time sits under every bar number; "Open at deep zoom" now says only the bar ruler stops; Live comparisons corrected from a screenshot at Live's true maximum zoom: Live reaches at least about 650 px/ms, not about 115, so Rhino's deepest is shallower in time; Live's grid appears to stop at 1/8192 and draws denser than Rhino's √2 rule, kept so two minutes read in one-bar cells on the user's lane)
- updated: pages/headless-ui-snapshots.md (`RHINO_TIME_ROW_SNAPSHOT`)

## [2026-10-23] update | One deepest-zoom cell cuts, copies and deletes exactly; a new clip fills its grid cell (cf4e959, 3a0933c)
- source: the change's brief, checked against native/src/model/ClipGeometry.h (`minimumRegionPieceSeconds`), SessionRegion.cpp (`splitClipAt`, `copyClipRegion`, `clearClipRegionInEdit`), SessionBounce.cpp, SessionPatternPresets.cpp (`createClip`), ui/arrangement/ArrangementGrid.h (`drawnGridBeats`), Arrangement.cpp (`newClipBeats`, Ctrl+A), ArrangementGestures.cpp, tests ClipGeometryTest.cpp, ClipSlivers.inc and NoteEditor.inc
- updated: pages/region-editing.md (copy's grazing floor is `minimumRegionPieceSeconds`, a tenth of a region under 0.1 s; delete cuts through `splitClipAt`, and what the engine's 1 ms refusal used to delete)
- updated: pages/clip-placement.md (the floor scales for short spans; new paragraph on Tracktion's 1 ms split refusal and the hand-made cut in `splitClipAt`; `createClip` stops short of a clip ahead and takes the length asked, at least a bar; tag `tracktion`)
- updated: pages/pointer-and-selection.md (a double-click makes a clip filling the grid cell the pointer is in)
- updated: pages/bounce-to-new-track.md (Bounce keeps the fixed 10 ms and refuses a span under it while copy scales; its direct `splitClip` calls are safe only because of those floors)
- updated: pages/arrangement-view.md (new bullet: a new clip fills its grid cell from `snappedDown`; bar lines thinned by doubling; the adaptive grid keeps eight-bar cells, a one-bar cap rejected)

## [2026-10-23] update | The 120-track stress audit: --profile-stress, a cached track list, per-track clip views, the Session facade and its lint, deferred device resets
- source: the brief and the final figures sent after it, checked against native/src/tests/StressProfile.cpp, LayerLintTest.h/.cpp, TestRunners.cpp, tests/Arrangement/ClipGeometryTest.cpp, model/Session.h, Session.cpp, SessionTransport.cpp, SessionDjSources.cpp, ui/arrangement/Arrangement.h, ArrangementSync.cpp, ArrangementPainter.cpp, ui/shared/Playhead.h, app/MainComponent.cpp, MainComponentTimer.cpp, PushController.h/.cpp, devices/audio/*Device.h/.cpp, devices/sdk/NativeDevice.cpp and core/dsp/VocoderEngine.cpp; recorded before the commit, all six CTest cases passing
- created: pages/stress-audit-2026-10.md (analysis: the tool, its song and env knobs; the baseline, track-cache and final figures per change and the engine-side costs the work does not touch; what was found and done in four places; the open items; how to re-run)
- updated: pages/session-model.md (new section "Reading the document": the cached track list and its watcher, `trackRevision`, the facade methods, the relayed `transportState` and `undoHistory`, `setPointerHeld`; the self-call that overflowed every runner)
- updated: pages/ui-cost-of-a-change.md (new rule: a sync reads only the tracks the model marked, and what a command must say; `gestureHeld` told by the shell; the stress figures; the revision that the old note said the engine lacked)
- updated: pages/arrangement-view.md (clip views cached per track in `TrackClipCache`; notes in the sequence's own seconds drawn from the window's origin; `clipNameWidths` kept)
- updated: pages/dependency-direction.md (new section "The views ask Session, not the engine": the facade, the layer lint with its allowances and quirks, the exceptions still open)
- updated: pages/real-time-audio-rules.md (deferred reset on the four pre-SDK devices, the Push's fixed ring, `ScopedNoDenormals` on every SDK device, the Vocoder's followers; the open audio-thread items)
- updated: pages/ableton-push.md (the MIDI thread's queue is a fixed ring, not a vector under a SpinLock)
- updated: pages/writing-rhino-tests.md (`--profile-stress`, the layer lint in the geometry runner, the new friend and flag, `--self-test` at 21 s alone and 29 s after a profile)
- updated: pages/build-and-test-rhino.md (`--profile-stress`, the lint, the device-correctness margin)
- updated: pages/stack-overflow-reports-as-segfault.md (a runaway recursion from a bulk replace gives the same 0xC00000FD from every runner)
- updated: pages/playhead.md (the position reader is `Session::playheadSeconds`; `Playhead.h` lost the `TransportControl` forward declaration; clip-name widths kept across syncs)
- updated: pages/transport.md (new section "What the views read"; a note editor seek now sets the playback start; the tempo drag step's cost)
- updated: pages/app-shell.md (the timer tells the model the pointer state; the shell listens to the relayed broadcasters)
- updated: pages/native-device-standard.md (the pre-SDK devices now defer `reset()`; the SDK base holds a `ScopedNoDenormals`)
- updated: pages/known-hazards.md (a pointer to the audit's open items)
- updated: index.md (the new page; the summaries of six others)

## [2026-10-23] update | Forge's factory set gains a Drum n Bass folder: eleven growls, leads and subs (ee40418)
- created: pages/writing-forge-factory-presets.md (the script's vocabulary: `folder=`, `lfo(kind, direction, smooth)`, full WARP/FILTER/FX tables, `destination()` over every rack, the FX LEVEL bank and FILTER 2; the growls as mid-range layers over the subs; the MAIN rack on the whole voice sum, POST LP distortion and `COMP_GLUE`; FORMANT's vowel positions against the cutoff range's skew; LFO ±1; the reverse-saw yoi; the audition-and-measure check and its 2026-10-23 figures)
- created: pages/forge-presets-without-subpan-load-hotter.md (`predatesMixer` keys on a missing `subPan`, so the 18 originals load their sub 3 dB hot, Sub Foundation 0.45 as 0.64; left as is deliberately; every new preset merges `MIXED`)
- updated: pages/forge-presets-and-state.md (the factory set is 29 in two folders; the `subPan` mark is coarse)
- updated: pages/forge-preset-browser.md (the bank copies Factory with its subfolders, relative paths kept, so the folder lists with no browser change)
- updated: pages/build-and-test-forge.md (what `--audition` renders exactly, and the numpy pass over it)
- updated: pages/measure-sound-dont-read-dsp.md (a preset's claim measured on its audition render)
- updated: pages/forge-filter.md (FORMANT reads CUTOFF as a vowel on the log scale; the matrix moves it on the skew-0.25 range)
- updated: pages/forge.md (29 presets in the source layout; done on 2026-10-23)
- updated: index.md (the two new pages)

## [2026-10-23] update | One selection in the lanes, carried whole by any clip header (cff657d)
- source: the change's brief, checked against native/src/model/ClipGeometry.h (`RegionCut`, `cutClipByRegion`), SessionRegionMove.cpp (`moveClipRegion`), Session.h, SessionInternal.h, SessionClips.cpp (`moveClipsInEdit`), native/CMakeLists.txt, ui/arrangement/Arrangement.h, Arrangement.cpp, ArrangementSelection.cpp, ArrangementGestures.cpp, ArrangementGeometry.cpp, ArrangementSync.cpp, ArrangementBounce.cpp, ArrangementClipPainter.cpp, tests/Arrangement/WorkflowTest.cpp and scenario RegionMove.inc; Live 12 manual section 6.9
- updated: pages/region-editing.md (summary; "Where the region comes from" replaced by "One selection": the rectangle, clicking a header selects the clip's own span, Shift and Ctrl widen, the primary clip, `setSelection`, the selection follows the clip it is exactly (`followsPrimary` in `sync()`, the stale-span duplicate it prevents), C acts on the primary clip alone, which commands fall back to the primary and which never read it, what was removed and why one rectangle; Copy reads the rectangle only, so a point selection copies nothing and the old fallback to the selected clips' span is gone; Delete clears whatever made the selection and leaves the insert point; new Nudge operation; new section "Moving a selection": `cutClipByRegion` and its floors, refusals before any cut, the one transaction and its lane, split, clear and move steps, identities after Tracktion's split, the `RegionMove.inc` scenario)
- updated: pages/pointer-and-selection.md (a header press selects the clip's own span, a trim handle too; a sweep across part of a clip selects that part; Ctrl equals Shift on a header; the Shift-click on a lane and the `regionExtending` fix; a press inside carries by overlap and collapses only when wider than the clip; the move clamps, the refusal mark, the one `moveClipRegion` on release; the arrow nudge)
- updated: pages/arrangement-view.md (summary and intro: fourteen units with `ArrangementClipPainter.cpp`; "Preview, then one commit" now `moveClipRegion`, the grabbed piece as `original`/`preview`; the selection as a rectangle plus a primary id; the header carries the selection; new bullet on pieces and the refusal mark, `ClipView::midi`, `factsFor` past the end, the sync's follow-the-exact-clip rule, clamp and primary drop; `Focus` without `clip`)
- updated: pages/clip-placement.md (`cutClipByRegion` keeps the floor and serves preview and model; `splitClipAt` now in `SessionInternal.h`; `moveClipRegion` joins the paths that clear their rectangle)
- updated: pages/bounce-to-new-track.md (summary; `bounceSelected` bounces any range selection as a span, a clicked clip included; `bounceClips` only for a primary clip with no span; `selectedClips` gone)
- updated: pages/session-model.md (summary and intro: 37 units; `SessionRegionMove` and why it was split; the three helpers promoted into `SessionInternal.h`; `moveClipRegion` among the one-step and fails-whole commands)
- updated: pages/keeping-files-small.md (Arrangement at 14 units; promoting an anonymous-namespace helper when a second unit needs it; `SessionRegion.cpp` split at 630 and `ArrangementPainter.cpp` still at 631)
- updated: pages/writing-rhino-tests.md (rule 14: a move preview posed by hand needs a selection covering the clip, and why `Rendering.inc`'s drag check skips the columns under the selection's edges and clears the selection after; rule 15: a scenario that autoscrolls restores `trackScroll` as it restores the zoom, and sets the view after its fixtures exist, after `RegionClipboard.inc` left the stack 54 px up and `RegionMove.inc`'s first press landed above `lanesTop`; rule 16: copy before moving the insert point, as the reordered "clip clipboard" block of `GesturesAndPersistence.inc` does; checked against scenarios Rendering.inc, RegionMove.inc and GesturesAndPersistence.inc, `beginRegionGesture`, `copySelection`)
- updated: index.md (the summaries of Arrangement view, Bounce to New Track, Region editing and Session)

## [2026-10-24] update | A sweep across part of a MIDI clip names it primary and shows that part in the note editor (4bd0f45)
- source: the change's brief, checked against native/src/ui/arrangement/ArrangementSelection.cpp (`beginRegionGesture`, `endRegionGesture`, `nameSweptClip`), Arrangement.cpp (`cancelDrag`), Arrangement.h, ui/notes/StepGridEditing.cpp (`showTimeSpan`, `setStepRegionFromSelection`), StepGrid.cpp, app/MainComponent.cpp; commit 4bd0f45
- updated: pages/region-editing.md (new paragraph in "One selection": a finished sweep names the primary by the press's clip or the nearest, a Shift sweep keeps its primary, MIDI becomes the pattern clip, `clipSpanSwept`, audio clips too, a cancelled sweep, why on release; reverses `cff657d`; the tests that hold it)
- updated: pages/note-editor.md (new section "Showing a swept span": `showTimeSpan`'s frame, selection, order and hidden-editor catch-up, pitch left alone and why; pitfall: the step region gives way to the selected notes' span at the next session change)
- updated: pages/arrangement-view.md (`clipSpanSwept` among the callbacks; the shell reads `selectedClipID()` on every change)
- updated: pages/app-shell.md (the lower pane follows a sweep, the pane still never opens from a selection, deliberately; gotcha: the shell polls the selected clip, so a silent clear closed the editor)
- updated: pages/pointer-and-selection.md (a sweep names the primary clip on release)
- updated: pages/writing-rhino-tests.md (rule 17: `createClip` points the note editor at the new clip, so read `session.pattern().itemID` before making MIDI fixtures; the precondition `RegionMove.inc` failed)

## [2026-10-24] update | The note editor pans with the middle button (0725daa)
- source: the change's brief, checked against native/src/ui/notes/StepGridGestures.cpp (`Gesture::pan`, `panView`), StepGrid.h, StepGrid.cpp; commit 0725daa
- updated: pages/note-editor.md (Gestures: the middle-button pan, steps by the pixel and rows by whole rows, absolute anchors, drums sideways only, answers first in `mouseDown` and why, opens no note gesture and is never autoscrolled, `NotePan.inc`)
- updated: pages/pointer-and-selection.md (the middle button pans the note editor too)

## [2026-10-24] update | Paste tracks into another project (c790287)
- source: the change's brief, checked against native/src/model/SessionTrackClipboard.cpp (`insertTrackCopies`, `keepOnItsBeats`), Session.h (`TrackClipboard`, `documentSerial`), Session.cpp (`nextDocumentSerial`, `nameSourcesFor`), SessionInternal.h, SessionTransport.cpp; commit c790287
- updated: pages/track-clipboard.md (summary; a copy records `document` and `tempo` and names sources in full; new section "Into another document": the clipboard survives an open, the document serial, audio named for the destination, sidechains repointed or removed and the starter-stack id collision, the tempo rule restated from `remapEdit` and why, scenes, every copy prepared before any lands and why that is safe, the open items; the test line)
- updated: pages/transport.md (pasted tracks follow the same tempo rules by hand)
- updated: pages/region-editing.md (the region clipboard survives an open and leaves its beats at another tempo; open)
- updated: pages/writing-rhino-tests.md (rule 1: why an unwritten document names an import one folder too high, and to write the document before any check of where a source resolves)
- updated: index.md (Track clipboard's summary)

## [2026-10-24] update | Keep clips off the track cards when scrolled (137ccb5)
- source: the change's brief, checked against native/src/ui/arrangement/ArrangementClipPainter.cpp (`paintClipPiece`) and tests/Arrangement/scenarios/Rendering.inc; commit 137ccb5
- updated: pages/arrangement-view.md (new rule: anything painted on the lanes clips at `headerWidth`, because `xFor` runs left of the lanes and the cards are painted first; the clip painter's x-0 clip region and what it spilled; the row dividers the one deliberate exception; the `Rendering.inc` pixel check)

## [2026-10-24] update | Show a note that sounds into a clip from before its window (1818c5b)
- source: the change's brief, checked against native/src/model/SessionNotes.cpp (`shownStartStep`, `editorNotes`, `resizeNote`, `fillNoteToClipEnd`, `redistributeNotes`); commit 1818c5b
- updated: pages/note-editor.md (new section "A note that began before the window": Tracktion plays it from the clip's start, the grid and lane skipped it, `editorNotes` shows it from step 0, how resize, fill, divide and moves treat the hidden part, a note wholly before the window stays off the grid, show-what-plays chosen over silencing it, `ClipWindow.inc`)
- updated: pages/clip-placement.md (a note crossing a MIDI clip's window start still plays and is shown from step 0)

## [2026-10-24] update | Drop the empty-lane and empty-clip placeholders (c462431)
- source: the change's brief, checked against native/src/ui/arrangement/Arrangement.h (`TrackFacts`) and a search of native/src for the removed texts; commit c462431
- updated: pages/arrangement-view.md (new rule: empty lanes and empty MIDI clips are blank at the user's request; both wordings of the empty-lane hint and "Edit notes below" gone; `TrackFacts::holdsInstrument` and the sync's `trackHasInstrument` call removed with them)
- updated: pages/track-kinds.md (the empty-lane hint dropped from "What reads the kind")
- updated: pages/playhead.md (the empty-lane hint dropped from what the arrangement paints only where the repaint reaches)

## [2026-10-25] update | Note editor UX audit: the keys kept clear, a right-button menu, paste beside the source (4783232)
- source: the change's brief and the coordinator's addendum, checked against native/src/ui/notes/StepGrid.h, StepGrid.cpp (`keyPressed`, `focusLost`, `changeListenerCallback`), StepGridMenu.cpp, StepGridEditing.cpp, StepGridGestures.cpp (`mouseDown`, `mouseWheelMove`, `updateMarqueeSelection`), StepGridPainter.cpp (`paint`, `paintKeyboard`), StepGridVelocityGestures.cpp (`pitchNamedAt`, `setHoveredPitch`, `keyHoverArea`), ui/shared/Theme.h, app/MainComponent.cpp, app/MainComponentCommands.cpp and scenarios NoteKeys.inc, NoteMenu.inc and RegionMove.inc; commit 4783232
- updated: pages/note-editor.md (summary; eight units with `StepGridMenu.cpp`; `StepGrid::status` to the readout; Gestures: the right button opens `noteMenu`/`gridMenu` and never erases, erasing only by draw mode, Delete or the menu, double-click-to-delete rejected; Ctrl+E always consumed and why it used to split the arrangement's clip; Left/Right nudge; Ctrl+wheel zooms, a sideways wheel scrolls the clip; the hovered key lit and named; no focus frame; "The step region and the insert line": `insertStep`, what sets and clears it, `pasteStep`, Ctrl+D's local clipboard, why a paste wrote over its source; `showTimeSpan` sets the line and `shownClip`; pitfalls: clip the time axis at the keys' edge, the insert line survives a session change; new "Not done yet" from the audit)
- updated: pages/pointer-and-selection.md (the right button asks and never edits; only draw mode erases; the note editor's wheel and Left/Right; the lane's right button opens the marker's note's menu)
- updated: pages/note-velocity-lane.md (the right button's menus in the lane; hovering a marker names its key)
- updated: pages/region-editing.md (in the note editor a paste lands on the insert line, unlike the lanes, and why; the arrangement unchanged)
- updated: pages/colours-and-typography.md (the `keyHoverText` token)
- updated: pages/writing-rhino-tests.md (rule 18: catch a context menu through a `presentMenu` seam, as `NoteMenu.inc` does)
- updated: pages/headless-ui-snapshots.md (`RHINO_NOTE_KEYS_SNAPSHOT`; the Drum Rack face's sentence put back beside its variable)
- updated: pages/keeping-files-small.md (`StepGrid` at eight units; `StepGrid.cpp` 716, `StepGridGestures.cpp` 826, `StepGridEditing.cpp` 597 lines)
- updated: index.md (Note editor's summary)

## [2026-10-25] update | Note editor, second round: bars and beats, black keys, velocity shades, snapping with Alt, key clicks, the preview, the ruler's loop menu, a Ctrl+E chop set by dragging (f2ae477, c6da530)
- source: the change's brief and the coordinator's two addenda, checked against native/src/model/SessionNoteAudition.cpp, SessionNoteChop.cpp, Session.h, native/CMakeLists.txt, ui/notes/StepGrid.cpp (`changeListenerCallback`, `rebuildVisibleNotes`, `~StepGrid`), StepGridPainter.cpp (`stepsPerBeat`, `paintRuler`, `paint`), StepGridGestures.cpp (`updatePointer`, `mouseDown`, `moveDraggedNotesAt`, `resizeCurrentNoteTo`, `addNoteOffTheGrid`, `dragGesture`, `mouseUp`, `mouseWheelMove`), StepGridEditing.cpp (`selectPitch`, `chopSelection`, `beginSubdivision`, `adjustSubdivision`, `chopByMouse`), StepGridMenu.cpp (`rulerMenu`), StepGridVelocityPainter.cpp, ui/shared/Theme.h and scenario NoteGrid.inc; commits f2ae477 and c6da530
- updated: pages/note-editor.md (summary and tags; the two model units; how the page dates the two rounds; Gestures: Alt with a double-click, the menu's Chop on any pitch, Ctrl+E chops on the grid with its parts set by a left drag while Ctrl is held, what ends it and why it stays one undo step, `redistributeNotes` left to `ClipWindow.inc`, the hover version of `f2ae477` replaced by the drag in `c6da530`; the wheel always zooms; snapping by default with Alt to free moves and resizes, the half-step rule and why, no snap control; a key click selects its pitch; the ruler's LOOP menu; new section "Preview": the switch, what sounds, `injectLiveMidiMessage` on the message thread and why its jitter is fine here, the preference, never heard in the app; new section "Drawing": bars and beats from the clip's start, labels, ticks and the `roundToInt` catch, beat shading, three line shades, black-key rows, velocity shades and the velocity-only repaint, the pitch bar gone; a chop drops a note's hidden part before the window; "Not done yet" down to the arrangement's insert line, its ruler's menu put to the user, and the unheard preview)
- updated: pages/pointer-and-selection.md (a key click selects its pitch, Ctrl toggling; Alt with a double-click; only the arrangement's ruler still clears the loop; the wheel always zooms and ends a chop, whose parts a drag sets; snapping is the note editor's default and Alt frees it)
- updated: pages/note-velocity-lane.md (the lane's columns and darker rule follow the beat; markers keep their colour; Not done yet: note colour by velocity done in the grid, Alt now taken in the grid)
- updated: pages/session-model.md (summary and intro: 39 units; `SessionNoteChop` and `SessionNoteAudition`; the note preview among the preferences test mode suppresses)
- updated: pages/colours-and-typography.md (the seven grid tokens, `noteCellAlternate` brought closer and why, which of the painter's colours are still literals)
- updated: pages/headless-ui-snapshots.md (`RHINO_NOTE_GRID_SNAPSHOT`)
- updated: pages/transport.md (the note editor's ruler and shading count the signature's beat)
- updated: pages/keeping-files-small.md (two new `Session` units; `StepGrid.cpp` 760, `StepGridGestures.cpp` 912, `StepGridEditing.cpp` past the line at 699)
- updated: index.md (the summaries of Note editor and Session)

## [2026-10-25] update | Note editor: narrow keys beside a column of octave names, rows 2 px taller (363af51)
- source: the change's brief and the coordinator's hash, checked against native/src/ui/notes/StepGrid.h (`openingRowHeight`, `keyLabelWidth`, `openingHeight`), StepGridPainter.cpp (`paintKeyboard`), StepGrid.cpp (`timeAreaHeight`, when the scrollbar shows), StepGridGestures.cpp (`isOverKeyboard`) and scenario NoteGrid.inc; commit 363af51
- updated: pages/note-editor.md (opening height 293 px; the hovered key lit and named in the names' column; Drawing: the names' column, 24 px keys, only the Cs named and the 15 px every-key rule gone, drum rows unchanged; rows open 2 px taller, and why at about 10.9 px)
- updated: pages/note-velocity-lane.md (`openingHeight` 293 px, from 10 px rows)
- updated: pages/app-shell.md (the note editor's opening height, 293 px, from `openingRowHeight`)

## [2026-10-25] update | Forge's preset browser made fast: cheaper loads, a lister thread over kept scans, the tick repaints its live regions, the Processor includes nothing under ui/ (rhino-62, ad945e1)
- source: session rhino-62's brief and its addendum on the lister's mailbox, checked against instruments/rhino-forge/src/ForgeEditorPaint.cpp (`liveRegions`, `liveKey`), ForgeEditorInput.cpp (`repaintLive`), ForgePresetBrowser.h (`Lister`, `Scan`, `listing`, `Arrival`, `arrivalsLock`, `takeListings`), ForgePresetBank.h (`presetsIn`), ForgeThemeSettings.h, core/ForgePanelColours.h, ForgeProcessor.h (`isApplyingState`) and tests/ForgeTestTools.cpp (`--profile-browser`, the three new `--profile` lines); the before and after figures from the same tool, same machine, same day are on the browser and repaint pages; the pages were written while the work was uncommitted, and the hash arrived afterwards: ad945e1 on main, "perf(forge): make the preset browser fast, and the panel's tick cheap", which replaced every "uncommitted at the time of writing" marker
- created: pages/forge-processor-includes-no-ui.md (the decision: the theme choice as free functions the Editor runs first, the panel colour constants in core/, what a new setting or shared constant does)
- updated: pages/forge-preset-browser.md (cold-cache listing figures; `PresetEntry::size` and `presetsIn` with `previous` and `shouldStop`, `listingSuite`; the kept `chrome` and row-only hover repaints; "Listing on a thread": stale scans, the `Lister`, the `AsyncUpdater` mailbox and why, `takeListings`, teardown, `waitForListing`/`settleListing`; "What it costs": `--profile-browser` and the before figures)
- updated: pages/forge-presets-and-state.md (the 98 ms load split three ways and each fix: `applyTables` skipping an untouched table, `migrated` gathering ids once, `isApplyingState` for the burst; panel colours clamped by core/ForgePanelColours.h)
- updated: pages/forge-editor.md (the theme applied by `themeInForce`; two rules: the tick repaints only `liveRegions()` with `liveKey` for the whole-panel case and `liveRegionsSuite`, and the per-control reactions gated by `isApplyingState` with `showPresetResult` laying out once; link to the decision)
- updated: pages/forge-panel-repaint-cost.md (retitled "Forge's tick repaints its live regions at 24 Hz": the whole-panel repaint is history, what `repaintLive` does, the two rules that follow, the before figures, the new `--profile` lines; the link text in pages not touched here still reads the old title)
- updated: pages/forge-themes.md (the theme choice in `themeSettings::*`, read by the Editor's first member; `forgetLoadedForTests` in the tests)
- updated: pages/build-and-test-forge.md (`--profile-browser`, the new `--profile` lines, the 2026-10-25 `--profile-audio` figures and what was seen and left alone on the audio thread)
- updated: pages/headless-ui-snapshots.md (PRESETS pages wait for the listing through `takeListings`, no message loop; `--profile-browser`)
- updated: pages/forge-midi-learn.md (the atomic `bound` count behind `boundCount()`)
- updated: pages/forge.md (a dated status line; link to the decision)
- updated: pages/buffered-child-breaks-pixel-stability.md (forge_theme's two-pixel difference on OSC C's tube glow is on the committed tree fa07c22 too, proven by a control build, so it is independent of any buffered child and of this work; 21 of 22 areas pass)

## [2026-10-25] update | Note editor: rows of one fixed height that a short pane scrolls, drum rows that scroll and follow the bank, the marquee repainted away on release (d872f0b)
- source: the change's brief, checked against native/src/ui/notes/StepGrid.h (`baseRowHeight`, `minimumRowHeight`, `maximumRowHeight`, `maxPitchRows`, `shownDrumBank`, the heap caches, `openingHeight`), StepGrid.cpp (`rowHeight`, `computePitchRowCount`, `rowsTop`, `zoomPitchAt`, `scrollPitchBy`, `automaticLowestPitch`, `changeListenerCallback`, `rebuildVisibleNotes`), StepGridPainter.cpp (`paint`, `paintKeyboard`), StepGridGestures.cpp (`autoScrollDrag`, `panView`, `mouseUp`, `mouseWheelMove`), StepGridVelocity.cpp and VelocityLaneGeometry.h (`laidOutHeight`), StepGridVelocityGestures.cpp (`finishVelocityLaneGesture`), model/Session.h (`patternDrumLowestNote`) and scenarios NoteGrid.inc, NoteKeys.inc, NoteEditor.inc and VelocityLaneLayout.inc, with UiCost.inc and the DJ scenarios for how they log repaints; written while uncommitted, after which the coordinator's hash and addenda arrived: d872f0b on main, which replaced every "uncommitted" marker of this change, and the addenda (`setCachedComponentImage` repaints everything, so NoteGrid.inc now clears its log and fails with the release's repaint taken out; presses must aim at whole rows; tests/README.md's new wording) were checked against NoteGrid.inc, NoteGestures.inc, StepGridGestures.cpp (`mouseDown` in the ruler) and tests/README.md
- updated: pages/note-editor.md (summary and tags; opening height 309 px; the pitch zoom alone changes a row's height; drum rows pan both ways; Drawing: a row keeps one height and the pane decides how many show, `maxPitchRows` 128, the stretching it replaced, scenario editors sized for whole rows; rows stand on the lane through `rowsTop()`, and why the bottom; the automatic window's top whole row; the lane's 128 px; Drum rows: open on the bank, scroll, `shownDrumBank` and when a scroll is dropped; pitfalls: the caches on the heap, the marquee repainted on release and its check failing without it)
- updated: pages/note-velocity-lane.md (the 128 px floor is a least room for rows, not a squeeze of sixteen; `openingHeight` 309 px)
- updated: pages/app-shell.md (the note editor's opening height, 309 px, from `baseRowHeight`)
- updated: pages/drum-rack.md (the note editor's drum rows open on the bank, scroll, and follow its pages)
- updated: pages/pointer-and-selection.md (the middle button pans drum rows both ways)
- updated: pages/writing-rhino-tests.md (rule 2: the `StepGrid`'s float caches on the heap; rule 19: log a component's repaints through a stand-in `CachedComponentImage`, clearing the log after installing it because installing repaints everything, and the negative control; rule 20: press only whole rows, since the top one may be a sliver inside the ruler, and why the scenario editors are 300 px, or 320 px in NoteGestures.inc)
- updated: pages/stack-overflow-reports-as-segfault.md (a `StepGrid` no longer holds its float caches inline and is tens of kilobytes, as tests/README.md now says)
- updated: index.md (Note editor's summary)

## [2026-10-25] update | Note editor: no horizontal scrollbar, rows 2 px taller at 13 px (c257d42)
- source: the coordinator's brief and hash, checked against native/src/ui/notes/StepGrid.h (`baseRowHeight`, `openingHeight`, the comment on `syncHorizontalScroll`), StepGrid.cpp (`timeAreaHeight`, `zoomAt`, `rebuildVisibleNotes`, `syncHorizontalScroll`, `resized`, the playhead's strip), StepGridEditing.cpp (`showTimeSpan`), StepGridGestures.cpp (`mouseWheelMove`), tests/UiProfile.cpp and scenarios NoteEditor.inc, NoteGestures.inc, NoteMenu.inc, NoteSelection.inc, VelocityLaneLayout.inc and ProjectFileMedia.inc; commit c257d42
- updated: pages/note-editor.md (opening height 327 px; how the steps travel with no scrollbar; Drawing: rows 13 px, `openingHeight` exactly sixteen whole rows, the lane checked to end at the footer, a new bullet on the scrollbar's removal and why the rows' area no longer moves with the zoom; the swept span's order note)
- updated: pages/note-velocity-lane.md (the layout without a scrollbar; the lane folds by the pane's height alone and `resized()` updates the button; `openingHeight` 327 px)
- updated: pages/app-shell.md (the note editor's opening height, 327 px, with no scrollbar)
- updated: pages/writing-rhino-tests.md (rule 20: editors that need sixteen whole rows take `StepGrid::openingHeight`; a 300 px editor now shows 13 whole rows; the fixed heights that broke)
- updated: pages/build-and-test-rhino.md (the unreadable-sample export check's 10 s budget is a load flake too)

## [2026-10-25] update | Note editor: a drum clip's rows are its rack's assigned keys, Alt + wheel sets the row height (3418c1b)
- source: the coordinator's brief and hash, checked against native/src/model/Session.h (`patternNamedNotes`, `moveNotes` with lanes), SessionNotes.cpp (`moveNotes`), SessionPatternPresets.cpp (`patternNamedNotes`), native/src/ui/notes/StepGrid.h (`rowHeightReach`, `shownPitches`, the line helpers, `updateShownPitches`, `nudgeRowHeight`), StepGrid.cpp (`nudgeRowHeight`, `scrollPitchBy`, `computePitchRowCount`, `rowsTop`, the line helpers, `updateShownPitches`, `automaticLowestPitch`, `changeListenerCallback`, `keyPressed`), StepGridEditing.cpp (`transposeSelection`), StepGridGestures.cpp (`cellHit`, `moveDraggedNotesAt`, `mouseUp`, `mouseWheelMove`), StepGridMenu.cpp (Transpose), StepGridVelocityGestures.cpp (`pitchNamedAt`), WorkflowTest.cpp and scenarios NoteDrumRows.inc, NoteGrid.inc, NoteKeys.inc, NoteSelection.inc and VelocityLaneLayout.inc; commit 3418c1b
- created: pages/note-editor-drum-rows.md (which rows, and why the union, with Fold left for a separate control; lines and their helpers; moves by lanes, the pad items in Transpose and Shift's twelve rows; rows held still under a gesture; the layout, bank and scroll, moved here from the note editor's page; the tests and the snapshot)
- updated: pages/note-editor.md (summary; the intro's hashes; Up/Down by pads; Alt + wheel sets the row height; a drag into the room below rows that all fit; the names' column now on every row; Drawing: 8-18 px, `lineCount()`, the zoom's floor at the octave names' height, rows that all fit hang from the header, `lineAtY`/`yForLine`, the Alt + wheel check; the marquee anchors in lines; Drum rows cut to a summary that links the new page; a pitfall: a row is a line, not a pitch)
- updated: pages/drum-rack.md (the editor's rows are the rack's assigned keys; `patternNamedNotes`; related link)
- updated: pages/writing-rhino-tests.md (rule 20's history worded for NoteSelection.inc's new start; rule 21: from NoteSelection.inc on, the shared editor is on a piano clip of its own, and NoteKeys.inc's thin rows are the 8 px floor)
- updated: pages/pointer-and-selection.md (Alt + wheel sets the note editor's row height; the marquee anchors in lines)
- updated: pages/headless-ui-snapshots.md (the keys picture's rows at the 8 px floor; `RHINO_NOTE_DRUMS_SNAPSHOT`)
- updated: index.md (Note editor's summary; the new page)

## [2026-10-25] update | Arrangement: an arrow in a track card's bottom-right corner folds the automation lanes under it (99b4e24)
- source: rhino-a9's brief, checked against native/src/model/SessionTracks.cpp (`setTrackAutomationFolded`), SessionAutomation.cpp (`showTrackAutomation`), SessionInternal.h, native/src/ui/arrangement/Arrangement.h, ArrangementAutomation.cpp (`layoutRows`, `automationFoldBounds`, `beginAutomationFoldGesture`), ArrangementAutomationPaint.cpp (`paintAutomationFold`), ArrangementGestures.cpp (`mouseDown`), ArrangementRename.cpp (`trackNameBounds`) and scenario AutomationLanes.inc; commit 99b4e24; all six CTest cases pass (native_arrangement_workflow 57.5 s), and the three fold snapshots showed the open, folded and one-line cards as described
- updated: pages/automation.md (the session's own paragraph reviewed and completed: the button's ground, no arrow on a too-short row, the margin clear of the resize edge and armed stripe, `mouseDown` order, `ModelChange::none`, focus dropped on fold, only an own-lane reveal unfolds and outside the undo step, the test)
- updated: pages/headless-ui-snapshots.md (`RHINO_AUTOMATION_FOLD_SNAPSHOT`, written by the session: the band it captures and that it deletes old files)
- updated: pages/arrangement-view.md (folded lane rows are zero-height rows; the card's second arrow beside the name's fold)
- updated: pages/session-model.md (the automation fold among the view settings that skip undo and pass `ModelChange::none`)

## [2026-10-25] update | Return tracks and post-fader sends, with A Delay and B Reverb in a new document (b7bd73c)
- source: the coordinator's brief, its hash and addendum (the Delay's time is a CachedValue; the profiles were compiled, not run), checked against native/src/model/SessionReturns.cpp, Session.h, SessionInternal.h/.cpp (`trackReturnBusID`, `chainHeadEnd`, `ensureDjPortDevices`, `RhinoEngineBehaviour`), SessionDevices.cpp, SessionMixer.cpp, SessionTracks.cpp, SessionClips.cpp, SessionTransport.cpp, SessionTrackClipboard.cpp, SessionGroups.cpp, Session.cpp, native/src/ui/arrangement/SendKnob.h/.cpp, Arrangement.cpp, ArrangementSync.cpp, ArrangementGestures.cpp, ArrangementSelection.cpp, ui/shared/Theme.h, app/MainComponentCommands.cpp, tests UiProfile.cpp and StressProfile.cpp, and scenario ReturnTracks.inc; the Tracktion facts at 4536d8a are the brief's
- created: pages/return-tracks.md (the component: how a return and its sends are built, levels, starter returns, adding and deleting, the foot of the stack, lanes made above the returns, refusals, mute and solo, the cards, the tests, open issues)
- created: pages/return-track-at-the-foot-of-the-stack.md (decision: a return is an ordinary track kept at the foot, sends always present; returns pinned beside the main row and sends made on first use rejected)
- created: pages/send-ignores-mute-when-muted-tracks-process.md (gotcha: the default shouldProcessAuxSendWhenTrackIsMuted with muted tracks processed; the override; the graph rebuilt by followReturnSolo)
- updated: pages/mixer.md (summary; sends in the table; ensureTrackMixers ends with ensureReturnRouting; mute and solo silence sends; the claim that a fader drag is one undo step corrected; Not built replaced by Open issues: undoing a fader or send drag restores nothing)
- updated: pages/device-chain-order.md (summary; the aux return after the DJ input on a return, sends at the end after the meter; `isTrackInfrastructure`'s new types; `djInputEnd` versus `chainHeadEnd`)
- updated: pages/track-groups.md (a return cannot be grouped; reconcileTrackGroups carries returns to the foot)
- updated: pages/group-is-a-bus-track.md (returns follow the same pattern)
- updated: pages/arrangement-view.md (`SendKnob.*`; send knobs and return cards; the returns are past the last lane)
- updated: pages/session-model.md (summary: 40 files; `SessionReturns`; fader and send drags are not undoable; the aux-send behaviour; the newest playing track is `firstReturnTrack() - 1`)
- updated: pages/main-track.md (the returns sit directly above it; it has no sends)
- updated: pages/headless-ui-snapshots.md (`RHINO_RETURN_TRACKS_SNAPSHOT`)
- updated: pages/writing-rhino-tests.md (rule 22: `firstReturnTrack()`, the six-track starter document, the profiles fill to `firstReturnTrack()` and were compiled, not run)
- updated: pages/track-kinds.md (lanes land above the returns; the starter edit's returns; a return has no kind)
- updated: pages/track-clipboard.md (returns are not copied, duplicated or cut, but delete with their sends)
- updated: pages/region-editing.md (a return's row counts as past the last track)
- updated: pages/dj-view.md (a deck refuses a return track)
- updated: index.md (three new pages; summaries of Mixer, Device chain order and Session, the model)

## [2026-10-25] update | Arrangement: returns pinned with the main row, Main gets a card and a colour (5632644)
- source: the coordinator's brief and addendum for 5632644, checked against native/src/ui/arrangement/Arrangement.h (`LaneRow::pinned`, `firstPinnedTrack`, `returnsTop`, `rowArea`, `lastLaneBottom`, `laneOrSlot`, `headerFor`, `returnHeaders`, `resizingFromTop`, `defaultReturnLaneHeight`, `maximumReturnsShare`, `pinnedRuleThickness`), ArrangementGeometry.cpp, ArrangementAutomation.cpp (`layoutRows`, `rowBounds`, `rowAt`), Arrangement.cpp (`resized`), ArrangementGestures.cpp (`cardResizeEdgeAt`, `beginCardGesture`, `dragCardGesture`, `showMainMenu`), ArrangementSelection.cpp, ArrangementDrops.cpp, ArrangementPainter.cpp (Main's card, the block's rule), ArrangementSync.cpp, native/src/model/SessionTracks.cpp (`trackColour`, `setTrackColour`, `pickTrackColour`), Session.cpp, SessionInternal.h/.cpp (`mainColourID`), and scenarios ReturnTracks.inc and AutomationLanes.inc
- updated: pages/return-tracks.md (summary; a new section on the pinned block: layout, size cap, controls, gestures, painting, selections and clips; the cards scenario's new checks)
- updated: pages/return-track-at-the-foot-of-the-stack.md (summary; the rejected alternative stated precisely as taking returns out of the track list; a 2026-10-25 change section: drawn pinned at the user's request, model unchanged)
- updated: pages/arrangement-view.md (summary; the pinned block in the intro and in `rows`; Main's card and menu; the selected main row; the return cards bullet; a region stops at the last lane, the carried clip previewed in its slot)
- updated: pages/main-track.md (summary; Main can be coloured, `rhinoMainColour`; its card and menu; pinned last under the returns)
- updated: pages/track-and-clip-colours.md (summary; returns and the main row get a picked colour; Main counts as worn)
- updated: pages/headless-ui-snapshots.md (`RHINO_RETURN_TRACKS_SNAPSHOT` now shows the pinned block and Main's card)
- updated: pages/writing-rhino-tests.md (rule 15: the shared view is 354 px; rule 23: panels grow by the pinned block, a lane under it is covered, AutomationLanes.inc's panel, the stale index in TrackManagement.inc)
- updated: index.md (summaries of Return tracks and sends, the return decision, Arrangement view, The main track, Track and clip colours)

## [2026-10-25] update | Forge's note guide opens from a door on the shelf; taller, wider keys with every C named (049dc53)
- source: the user's request and commit 049dc53, checked against instruments/rhino-forge/ui/ForgeNoteGuide.h, ui/ForgeNoteGuidePanel.h, ui/ForgePlacement.h, ui/ForgeModule.h, ui/ForgeModules.h, src/ForgeEditorNoteGuide.cpp, src/ForgeEditorLayout.cpp, src/ForgeEditorPages.cpp, tests/ForgeTestsGuide.cpp and tests/ForgeTestsDisplays.cpp
- created: pages/forge-note-guide.md (the door, `Page::guide` on the arp's cells, the panel's pure layout and presses, the spelling of chords and degrees, the C labels, the 88 px keys and why not more, `forge_guide`, the GUIDE snapshot)
- updated: pages/forge-editor.md (the note guide section now points at the new page; `Page::guide` beside `Page::arp`)
- updated: pages/forge-arp-plate.md (the shelf is laid out from its right-hand end)
- updated: pages/forge-arpeggiator.md (`coveredBy`; the arp, the guide and the expanded rack close one another)
- updated: pages/build-and-test-forge.md (23 areas, `guide` the newest)
- updated: pages/headless-ui-snapshots.md (the GUIDE page)
- updated: pages/buffered-child-breaks-pixel-stability.md (summary; the two pixels follow the layout onto the LFO curve, a software canvas removes them, `chromeCacheSuite` paints on software)
- updated: index.md (the new page; the gotcha's summary)
