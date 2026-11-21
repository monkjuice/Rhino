---
title: A static juce::Font aborts every quit on macOS
type: gotcha
summary: On macOS JUCE's CoreText typeface unregisters itself in its destructor under a mutex owned by a function-local static, so a juce::Font or Typeface::Ptr held in a static of our own can outlive that mutex and lock it after it is destroyed; std::system_error is thrown from a destructor and the process aborts. Forge's standalone did so on every quit, while Windows, which keeps no such registry, never showed it. Keep fonts in a DeletedAtShutdown singleton, as JUCE's TypefaceCache does; Forge's fix, 195b93d, passed CI's quit check on 2026-11-20.
tags: [forge, juce, macos, fonts, shutdown, portability]
sources: []
updated: 2026-11-20
---

# A static juce::Font aborts every quit on macOS

## Symptom

On the first real Mac, Forge's standalone reported "quit unexpectedly" on every quit ([Forge's macOS build and installer](forge-macos-build.md#status)). The CI quit check reproduced it in run 44846507141 (`792745a`). The app exited with status 134 (Abort trap: 6) and printed:

```
libc++abi: terminating due to uncaught exception of type std::__1::system_error: mutex lock failed: Invalid argument
```

The runner wrote no crash report. Only the exit-status check caught it.

## Cause

This was read in JUCE 8.0.13's `modules/juce_graphics/native/juce_Fonts_mac.mm`, at the pinned commit, on GitHub. `CoreTextTypeface::~CoreTextTypeface()` calls `getRegistered().remove(ctFont)`. That takes a `std::scoped_lock` on a `std::mutex` inside a function-local static, `Registered registered`.

Forge held fonts in three function-local statics of its own:

- `static const Loaded loaded`: the four typefaces (`typefaceFor`, `ui/ForgeType.h`)
- `static std::vector<Entry> made`: every font made (`panelFont`, same file)
- `static std::vector<Metrics> cached`: `drawTrackedText`'s measurements, which hold `juce::Font`s (`ui/ForgeChrome.h`)

Statics are destroyed at exit in the reverse order of their construction. `made` was constructed before JUCE's registry, at the first `panelFont` call, and `loaded` after it. So at exit:

1. `loaded` is destroyed first, but the fonts in `made` still hold the typefaces.
2. The registry and its mutex are destroyed.
3. `made` is destroyed, and its fonts release the last references.
4. Each typeface's destructor locks the destroyed mutex. That returns `EINVAL`, a `std::system_error` is thrown from a destructor, and `std::terminate` aborts.

Windows' font code keeps no such registry, so it never showed there.

## Fix: commit 195b93d, 2026-11-20

`ui/ForgeType.h` gains `class FontStore final : private juce::DeletedAtShutdown`. It holds the four `juce::Typeface::Ptr`s, `std::vector<Made> made` and `std::vector<Tracked> tracked`. It declares `~FontStore() override { clearSingletonInstance(); }` and `JUCE_DECLARE_SINGLETON_INLINE(FontStore, false)`, the pattern of JUCE's own `TypefaceCache` in `juce_Font.cpp`.

- `typefaceFor` and `panelFont` read the store through `FontStore::getInstance()`. So does `drawTrackedText` in `ui/ForgeChrome.h`, whose cache is now `tracked`. `ForgeChrome.h` now includes `ForgeType.h`, which it did not before; its users had included the two side by side.
- JUCE deletes `DeletedAtShutdown` objects in `shutdownJuce_GUI`, while its registry, a static, still exists. So the typefaces go before the mutex. In a plugin, that happens when the last instance goes, and the next instance makes a new store.
- The store is used on the message thread only.

The regression test is the CI quit check, which failed on `792745a` without the fix. There is no unit test, because the failure needs macOS's registry and the process's own static destruction.

**Verified:**

- **The runner.** Run 44849124968 (`195b93d`) quit with exit status 0, with no warning that it failed to quit and no crash report. The same check on `792745a` aborted with 134.
- **Windows.** 22 of 23 Forge areas pass; `forge_theme` keeps its earlier two-pixel failure.
- **The real Mac.** Not yet confirmed: that waits for a reinstall from the pre-release.

## Rule

Never keep a `juce::Font`, a `juce::Typeface::Ptr`, or anything holding one, in a static. Put it in a `DeletedAtShutdown` singleton. This is the font counterpart of the rule for images ([A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)).

Rhino's own `native/src` has not been checked for the pattern, and it does not build on macOS yet.

## Related

- [Forge's macOS build and installer](forge-macos-build.md)
- [Forge editor (panel)](forge-editor.md)
- [A plugin's static Direct2D images deadlock NVIDIA's driver at unload](plugin-static-images-deadlock-on-unload.md)
- [Colours and typography](colours-and-typography.md)
