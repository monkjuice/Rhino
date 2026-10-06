---
title: A CachedValue can lag its own ValueTree
type: gotcha
summary: A ValueTree listener that reads a juce::CachedValue of the same property may run before the cache updates, so force the cache first; in Rhino the race failed only under parallel CTest.
tags: [rhino, juce, devices, undo, testing]
sources: []
updated: 2026-10-05
---

# A CachedValue can lag its own ValueTree

A `juce::CachedValue` refreshes itself through a `ValueTree::Listener` of its own. Any other listener on the same tree
may be called first, and if it reads the cached value it gets the old one. JUCE does not promise an order between
the two.

## What it broke

Commit `b595f38` made a device on the SDK update its engine parameter the moment its stored value changes:
`NativeDevice::valueTreePropertyChanged` calls `updateFromAttachedValue` on the slot's parameter
(`native/src/devices/sdk/NativeDevice.cpp`). An undo that *removes* the stored property, taking a control back to a
default that was never written, could still leave the undone value in the parameter, and the rack showed it.

It failed in every `ctest -j 3` run and passed in every sequential one, so the listener order varied with load; why
was not established. A sequential pass proved nothing here.

## The fix

Call `slot->stored.forceUpdateOfCachedValue()` before `updateFromAttachedValue` (commit `fbad188`). The conformance
probe in `native/src/tests/DeviceConformance.cpp` now undoes a change and asserts the property is absent from the
device state as well as that the value is back at its default, so the removal path is what it tests.

## The rule

- A listener that reacts to a property change and reads a `CachedValue` of that property forces the cache first, or
  reads the tree directly.
- Run an ordering-sensitive change under `ctest -j 3` as well as alone before believing it
  ([Build and test Rhino](build-and-test-rhino.md)).

## Related

- [The native device standard](native-device-standard.md)
- [Device rack and device editors](device-rack.md)
- [Writing Rhino tests](writing-rhino-tests.md)
