---
title: Forge is an independent VST3
type: decision
summary: Why Forge is its own JUCE plugin project, hosted by Rhino through Tracktion's external-plugin wrapper rather than built in.
tags: [forge, vst3, hosting, architecture]
sources: []
updated: 2026-10-05
---

# Forge is an independent VST3

## Context

Rhino's instrument work has two parts:

- **Milestone A** finishes Rhino Wave, Rhino's built-in synth: a Tracktion plugin in `native/src/devices/instruments/`. (Rhino Wave was removed on 2026-10-05; a new catalog-only built-in synth is to replace it.)
- **Milestone B** is Forge, a far larger synth modelled on Serum 2's workflow.

Making Forge another built-in device would have tied its DSP, state, automation and editor to Tracktion.

## Decision

Forge is developed as an independent VST3 and loaded through Tracktion rather than duplicated as a built-in Rhino device. In practice that means three things:

- **Its own project.** Forge has a separate CMake project, `instruments/rhino-forge/`. It builds a VST3 and a standalone app from one JUCE `AudioProcessor`. Parameter ids, versioned state and host automation are defined at that boundary.
- **JUCE only.** No Tracktion or Rhino header appears anywhere in Forge. The one thing it borrows is Rhino's pinned JUCE checkout (`RHINO_JUCE_DIR`).
- **Hosted as a plugin.** Rhino loads Forge through Tracktion's standard external-plugin wrapper and shows Forge's own editor.

## Why

Forge's DSP, state, automation surface and editor stay host-independent. Forge therefore runs in any VST3 host, and that is how it gets A/B-tested against Serum in another DAW ([Comparing Forge with Serum](comparing-forge-with-serum.md)).

The rejected alternative was a built-in Tracktion device like Rhino Wave. That approach is still used for every catalog device ([Device catalog](device-catalog.md)).

## Consequences

- **Rhino never names a Forge parameter.** Rhino finds the plugin by its description (`looksLikeForge` in `native/src/SessionExternalPlugins.cpp`). So a Forge parameter change can only affect Forge state saved inside a `.rhinoedit`, and Forge reconciles that state when it loads ([Hosting Forge in Rhino](forge-hosting.md), [Forge presets and state](forge-presets-and-state.md)).
- **A running host locks the VST3.** Relinking fails with LNK1104 while RhinoDAW or a DAW has the plugin loaded. Build the test binary and the standalone instead, and say the VST3 is stale ([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)).
- **Separate build and tests.** Forge has its own CMake configure step and its own test binary with 18 areas ([Build and test Forge](build-and-test-forge.md)).
- **Shipping needs a real host.** A VST3 counts as shipped only after it has been loaded and automated in at least one external host; a successful compile is not proof. Generated bundles are never committed.
- **Code crosses the boundary with effort.** Forge's planned arp pattern editor (M13c) cannot simply reuse Rhino's `StepGrid`. That editor calls into `Session` about a hundred times, and `Session.h` pulls in Tracktion. A Session-free note-grid core would have to be extracted first ([Note editor (StepGrid)](note-editor.md)).
- **There is no engine library.** The engine is header-only, and one source list is compiled into both the plugin and the tests ([Forge's engine splits into headers only](forge-engine-headers-only.md)).

## Related

- [Rhino Forge](forge.md)
- [Hosting Forge in Rhino](forge-hosting.md)
- [Dependency direction](dependency-direction.md)
- [Tracktion Engine with a native JUCE UI](tracktion-and-juce.md)
- [Build and test Forge](build-and-test-forge.md)
