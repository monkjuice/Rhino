---
title: JUCE's key codes are not constant expressions
type: gotcha
summary: juce::KeyPress::upKey and the other named key codes are static const int members defined in a .cpp, not constexpr, so they cannot go into a static constexpr array; build a table of them at run time.
tags: [rhino, juce, keyboard]
sources: []
updated: 2026-10-16
---

# JUCE's key codes are not constant expressions

`juce::KeyPress::upKey`, `downKey`, `deleteKey`, `escapeKey` and the other named key codes are declared `static const int` in JUCE's header and defined in a `.cpp`. They are not `constexpr`. Any constant expression that needs their value fails to compile, a `static constexpr` array of them included.

This came up on 2026-10-16. The note editor's [velocity lane](note-velocity-lane.md) needed a table of the keys to consume while a drag holds its undo step. The fix in `StepGrid::keyPressed` (`ui/notes/StepGrid.cpp`) is a local `const int` array, built at run time on each call. Plain characters such as `'V'` could go in a constexpr table, but the named codes cannot.

## Related

- [Note editor velocity lane](note-velocity-lane.md)
- [Note editor (StepGrid)](note-editor.md)
