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

## [2026-10-05] update | Rhino FM, the first instrument on the device SDK; .rnd files are presets
- created: pages/rhino-fm.md (four operators, eight routings, the index scale and its Bessel check, the Nyquist guard and depth taper, mono and pedal, the electric-piano defaults, cost)
- updated: pages/built-in-devices.md (Rhino FM in the table; it replaces Rhino Wave; three devices on the SDK)
- updated: pages/native-device-standard.md (.rnd corrected to mean a device's presets, with each device a folder in the browser; the user's priorities; Rhino FM done; remaining steps)
- updated: index.md (Rhino FM, and the standard's summary)
