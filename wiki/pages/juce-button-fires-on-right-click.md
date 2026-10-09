---
title: A juce::Button fires its click on a right-click too
type: gotcha
summary: juce::Button presses and clicks for any mouse button, so a key that carries a right-click menu also runs its left-click action unless mouseDown and mouseUp swallow the popup-menu press first, as the DJ console's DjPad does.
tags: [rhino, juce, ui, input]
sources: []
updated: 2026-10-09
---

# A juce::Button fires its click on a right-click too

`juce::Button::mouseDown` presses the button and `mouseUp` fires `onClick` whichever mouse button was used; neither asks `event.mods.isPopupMenu()`. A `TextButton` given a right-click menu therefore does both things on the right-click: it opens the menu, and it runs the action a left-click runs.

## What it broke

The DJ console ([DJ view and the booth](dj-view.md)) gives several keys a second action on the right-click: a hot cue is cleared, Reload switches the automatic re-bounce off, Eject removes the deck, and Beat Loop offers its length. Built on stock `TextButton`s in the second pass of 2026-10-09, a right-click on Beat Loop chose a length *and* set a loop, and the other keys did the same with theirs.

## The fix

Every key with a second action is a `DjPad` (`native/src/DjControls.h`), which overrides `mouseDown` and `mouseUp`: a press with `isPopupMenu()` calls `onRightClick` and returns before `juce::Button::mouseDown` sees it, and the matching release is dropped the same way, so the button neither presses nor clicks. The menus that used to hang on `TextButton`s moved to `DjPad`s for this.

A new control that needs a right-click goes on `DjPad` or on a `Button` subclass with the same two overrides, never on a stock button with a listener bolted on: the listener hears the right-click, but so does the button.

## Related

- [DJ view and the booth](dj-view.md)
- [A JUCE component listening to itself hears its own clicks twice](juce-self-listener-hears-clicks-twice.md)
- [Pointer and selection rules](pointer-and-selection.md)
