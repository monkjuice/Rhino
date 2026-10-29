---
title: Tracktion refuses a second built-in type under a name it already has
type: gotcha
summary: te::PluginManager registers Tracktion's own built-ins (the compressor as "compressor") when the engine is built, before Session calls DeviceCatalog::registerBuiltInTypes, and registerBuiltInType silently ignores a type whose name is taken, so a subclass of a Tracktion plugin cannot take over its name and needs one of its own; Rhino's Compressor is rhino.compressor.v1 for this reason.
tags: [rhino, tracktion, devices, catalog]
sources: []
updated: 2026-10-28
---

# Tracktion refuses a second built-in type under a name it already has

## What happens

`te::PluginManager::initialise` registers Tracktion's built-in plugins, `te::CompressorPlugin` as `compressor` among them, when the engine is built. Rhino registers its own later, from `Session`'s constructor through `DeviceCatalog::registerBuiltInTypes` ([Device catalog](device-catalog.md)). `PluginManager::registerBuiltInType` refuses a type under a name it already holds, without an error or a log line, and the engine keeps making its own plugin for that name. Checked in `tracktion_PluginManager.cpp` at the pinned commit `4536d8a` on 2026-10-28.

So a Rhino subclass of a Tracktion plugin that returns the base's type name is never what the engine creates: the catalog's factory is ignored and the subclass's overrides never run.

## The workaround

Give the subclass a type name of its own and point the catalog entry's `typeName` at it, keeping the entry's `id`. [Compressor](compressor.md)'s `CompressorDevice` is `rhino.compressor.v1` (2026-10-28).

The cost: a document saved with the old name still names the engine's type, so it opens as the engine's plain plugin, which the catalog no longer names. Rhino accepts that ([No backward compatibility for .rhinoedit](no-rhinoedit-back-compat.md)).

## Related

- [Built-in devices](built-in-devices.md)
- [Adding a device to Rhino](adding-a-device.md)
- [Directories not to read](off-limits-directories.md), for checking a Tracktion body at the pinned commit
