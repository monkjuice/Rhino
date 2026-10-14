---
title: A JUCE drag finds its target in JUCE's window order, not the screen's
type: gotcha
summary: A drag that may leave its window picks its drop target by juce::Desktop's own list of windows, reordered only when JUCE hears a window come to the front, so a window kept above another by the OS (an owned window) loses drops to the window beneath once that one is clicked; Rhino re-raises its floating windows whenever the main window comes forward.
tags: [rhino, juce, ui, windows, drag-and-drop]
sources: []
updated: 2026-10-13
---

# A JUCE drag finds its target in JUCE's window order, not the screen's

A `juce::DragAndDropContainer` drag started with `allowDraggingToOtherJuceWindows` true puts its image on the desktop, and `findTarget` then resolves the component under the pointer with `findDesktopComponentBelow`, which walks `juce::Desktop`'s own list of desktop components from the end. That list is JUCE's bookkeeping, not the operating system's z-order: a window moves to its end only when its peer reports being brought to the front (`handleBroughtToFront` -> `Desktop::componentBroughtToFront`). `TreeView` starts its drags this way, so every drag out of Rhino's browser does. Read from JUCE at the pinned commit `37c894f`.

## What it broke

Since 2026-10-13 the Drum Rack window, while restored, is an owned window of the main window ([App shell and control bar](app-shell.md)), so Windows keeps it above. Clicking the main window activates it (`WM_ACTIVATE` -> `handleAppActivation` -> `handleBroughtToFront`), which puts the main window at the end of JUCE's list. On screen the Drum Rack window still sat on top, but a sample dragged from the browser onto its pads resolved to whatever main-window component lay under the pointer, and nothing happened; the same drag onto the face in the Device View worked. A file dragged from Explorer was never affected: the OS hit-tests that drop.

## The fix

`DeviceRack` (`ui/rack/DeviceRack.*`) privately inherits `juce::ComponentListener` and, in `parentHierarchyChanged`, listens to its top-level window (`watchedWindow`, a `SafePointer`, detached in the destructor). When that window comes to the front, `raiseFloatingWindows` calls `toFront(false)` on the plugin window and on the Drum Rack window while it floats (not maximised), only when the shell has wired `keepAboveShell`. `toFront(false)` ends in `handleBroughtToFront` too, so JUCE's list again matches the screen, without activating anything; on screen nothing moves, since an owned window is already above its owner. A hidden or minimised window is skipped, because `toFront` would restore a minimised one.

## The rule

Any JUCE window kept above another by platform means (owned windows, native parenting) needs this. `setAlwaysOnTop` windows do not: JUCE tracks those itself.

No automated test covers it: it needs real windows on the screen, and a test must not take the user's screen. Built on 2026-10-13; not yet tried by the user.

## Related

- [App shell and control bar](app-shell.md)
- [Device rack and device editors](device-rack.md)
- [Drum Rack](drum-rack.md)
- [Browser and library preview](browser.md)
