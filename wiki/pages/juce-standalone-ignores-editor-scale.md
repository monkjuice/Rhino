---
title: JUCE's standalone holds its window to the editor's unscaled limits
type: gotcha
summary: JUCE's standalone window applies the editor's resize limits to its content in the window's own units, ignoring the editor's transform, so an editor shrunk with AudioProcessorEditor::setScaleFactor still gets a window at least the minimum's full size; Forge's panel went under a 13-inch Mac's Dock this way until e79b4b3 scaled the standalone with Desktop::setGlobalScaleFactor instead. JUCE's plugin wrappers do allow for the transform.
tags: [forge, juce, macos, ui, standalone]
sources: []
updated: 2026-11-20
---

# JUCE's standalone holds its window to the editor's unscaled limits

This was read in JUCE 8.0.13 at the pinned commit, on GitHub, in `juce_StandaloneFilterWindow.h` and `layout/juce_BorderedComponentBoundsConstrainer.cpp`.

The standalone window's `DecoratorConstrainer` is a `BorderedComponentBoundsConstrainer`. It wraps the editor's own constrainer (`getEditorConstrainer`) and applies it to the window's content bounds in the window's units, without the editor's transform.

So an editor scaled down with `AudioProcessorEditor::setScaleFactor` still gets a window held to the editor's unscaled minimum. Forge's panel, scaled to about 745 points, got a window 820 points tall plus its title bar, and on a 13-inch Mac its bottom sat under the Dock ([Forge editor (panel)](forge-editor.md#fitting-the-first-window-to-the-screen)).

JUCE's plugin wrappers do convert through the editor's local area (VST3's `checkSizeConstraint`, for example), so only the standalone is affected.

**What to do:** in a standalone, scale the whole UI with `Desktop::setGlobalScaleFactor`, as JUCE's own comment in `AudioProcessorEditor::editorResized` suggests. That keeps the window's limits and the editor in the same units. Forge does so since `e79b4b3` (2026-11-20), and keeps `setScaleFactor` in a plugin, where a global scale would reach the host's other JUCE windows.

## Related

- [Forge editor (panel)](forge-editor.md)
- [Forge's macOS build and installer](forge-macos-build.md)
