---
title: App icons are baked at configure time
type: gotcha
summary: JUCE turns the icon PNGs into an .ico at configure time, so editing the art and rebuilding still ships the old icon.
tags: [both, build, cmake, icons]
sources: []
updated: 2026-10-03
---

# App icons are baked at configure time

## Symptom

You edit the app icon art and rebuild. The build is green, but the taskbar still shows the old icon. It looks as if the new art did not help, which tempts you to redraw it again.

## Cause

Both projects set their icons with `ICON_BIG` and `ICON_SMALL`:

- Rhino, on `juce_add_gui_app` in `native/CMakeLists.txt`: `native/assets/rhino_app_icon_1024.png` and `rhino_app_icon_256.png`.
- Forge, on `juce_add_plugin` in `instruments/rhino-forge/CMakeLists.txt`: `ui/assets/forge_app_icon_512.png` and `forge_app_icon_256.png`.

JUCE's `juceaide` tool reads these at **configure** time and writes `<target>_artefacts/JuceLibraryCode/icon.ico` in the build tree. Nothing in the build depends on the source PNG, so `cmake --build` links the old `.ico` again. A missing source file at least fails the link ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)); a stale icon gives no error at all. Forge's 128, 64 and 32 px PNGs are never compiled in: JUCE builds its own size ladder from the two named files (`ui/assets/README.md`).

## What to do

Reconfigure, then build:

```
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build
cmake --build instruments/rhino-forge/build --config Release --target RhinoForge_Standalone
```

For Rhino, configure with `cmake -S native -B native/build`. Then check what the executable actually contains, not the art you edited:

```
Add-Type -AssemblyName System.Drawing
[System.Drawing.Icon]::ExtractAssociatedIcon($exe).ToBitmap().Save($png)
```

## Judging the art

These lessons come from aligning the two icons as a pair, in commits `a1fb632` and `47c734c` (2026-09-22):

- **Judge on the real background.** Composite the icon onto the taskbar grey `#222629` at 24, 32 and 48 px. Forge's mark was shaded for a light background, and 42% of its ink sat within 12 luminance levels of that grey. Its arms vanished and it looked too small, but the fault was contrast, not size.
- **Centre on a blend.** Place each icon 0.30 of the way from its bounding-box centre toward its visual-weight centroid. Forge's top-heavy mark has those two points 15.3% apart; Rhino's ring has them within 2%. Centred on the box, its mass sat high; centred on the centroid, its outline sat low.
- **Trust the eye over the metric.** Render the pair side by side on the taskbar grey with a centre line drawn through them. Cutting the centroid mismatch to 0.4% looked worse than leaving it at 3.4%, because the metric measured mass while the problem was the outline.
- **One problem, separate fixes.** Forge needed its tone lifted. Rhino's navy is darker than the taskbar, so lifting it would push it into the background's brightness range.

## Related

- [A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)
- [Build and test Rhino](build-and-test-rhino.md)
- [Build and test Forge](build-and-test-forge.md)
- [Content is files, never compiled in](content-is-files.md)
- [The executable is RhinoDAW, not Rhino](executable-named-rhinodaw.md)
