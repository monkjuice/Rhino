---
title: A JUCE component listening to itself hears its own clicks twice
type: gotcha
summary: A component registered as its own mouse listener gets every event on itself twice, so every toggle on Rhino's device faces flipped on and straight back off; hear the children through a separate listener, and test clicks through real dispatch.
tags: [rhino, juce, ui, input, testing]
sources: []
updated: 2026-10-07
---

# A JUCE component listening to itself hears its own clicks twice

`addMouseListener(this, true)` looks like the way for a component to hear its children's mouse events as well as its own. It does that, but JUCE then delivers every event on the component itself twice: once to its own `mouseDown` (and the rest), and again to the same object as a listener. JUCE's comment in `Component::addMouseListener` says so.

## What it broke

`DeviceEditorPanel`'s constructor made that call so that a right-click on a child knob would reach the panel, which opens the knob's automation menu ([Device rack and device editors](device-rack.md)). Every face then heard each click on itself twice:

- Every toggle drawn on a face went on and straight back off: the [Drum Rack](drum-rack.md)'s Loop, which the user reported could not be switched on, a pad's M and S, and [Rhino Tune](rhino-tune.md)'s LIVE and NAT.
- A button that sets a value, such as a play mode, hid it, because the second write changed nothing.
- The wheel over a face moved two steps a notch, and a double-click ran twice.

## The fix

Commit `a033e1a`: the deep listener is a separate member, `ChildMouse` (`DeviceEditorPanel.h`), a `juce::MouseListener` that passes `mouseDown`, `mouseDrag`, `mouseUp`, `mouseDoubleClick` and `mouseWheelMove` to the panel's handlers only when `event.eventComponent` is not the panel. The panel's own events arrive once, directly; the destructor removes the listener. Changed with it, as intended: the wheel over the Drum Rack's map and pads moves one row a notch, Rhino EQ's wheel goes half as far as before, and TO PADS spreads a sample's slices once.

To hear a component's children, hand `addMouseListener` another object that drops the component's own events, never the component itself. On 2026-10-07 `DeviceEditorPanel` was Rhino's only caller. Forge's editor registers itself on child controls with `false`, which is safe: the editor is not the component it listens to.

## Why no test caught it

Every face test called `panel->mouseDown` and its siblings directly, around JUCE's dispatch, so the listener never ran. `runDrumRackFaceTest` now also puts the `DeviceRack` on the desktop at (-10000, -10000), off the screen, and sends a press and, 400 ms later, a release through `ComponentPeer::handleMouseEvent` to Loop and to a pad's M, requiring each to toggle exactly once. It failed before the fix (Loop stayed off) and passed after. It needs a desktop peer, so it runs only when `RHINO_NATIVE_INPUT_TEST=1` is set ([Writing Rhino tests](writing-rhino-tests.md)).

## Related

- [Device rack and device editors](device-rack.md)
- [Writing Rhino tests](writing-rhino-tests.md)
- [Drum Rack](drum-rack.md)
- [Drum Rack sample editor](drum-rack-sample-editor.md)
- [Rhino Tune](rhino-tune.md)
- [Rhino EQ](rhino-eq.md)
