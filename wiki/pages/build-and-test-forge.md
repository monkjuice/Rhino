---
title: Build and test Forge
type: guide
summary: Configure Forge against the JUCE and zstd checkouts Rhino's script fetches, build around the toolchain's traps, fix /W4 warnings at their source, run one test area at a time, audition or convert any preset from the command line, and time a preset load and the browser with --profile-browser.
tags: [forge, build, testing, windows, warnings, vscode]
sources: []
updated: 2026-10-25
---

# Build and test Forge

Forge is its own CMake project under `instruments/rhino-forge/`, with no Tracktion or Rhino code, but it borrows
Rhino's pinned JUCE: `RHINO_JUCE_DIR` defaults to `native/.deps/juce`, so run `python native/scripts/fetch-dependencies.py`
first ([Build and test Rhino](build-and-test-rhino.md)). The Forge README has the newcomer quick start; this page carries
the development detail.

Since 2026-10-19 the same script also fetches zstd v1.5.7 (commit `f8745da`, BSD licensed) into `native/.deps/zstd`.
Forge needs it to open Serum 2 presets ([Serum 2's preset format](serum-2-preset-format.md)). `RHINO_ZSTD_DIR` points
at it, and configure stops with a `FATAL_ERROR` if it is missing, so a checkout fetched before that date must run the
script again. `CMakeLists.txt` builds only zstd's decompressor sources, as the static library `ForgeZstd`, with
`ZSTD_DISABLE_ASM=1`, because the amd64 Huffman loop is GNU assembler that MSVC rejects. Forge reads CBOR with its
own reader (`src/ForgeCbor.*`), not a library.

## Build

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2 -- /p:BuildInParallel=false
```

- `/p:BuildInParallel=false` serialises project-reference resolution, which otherwise fails silently with a
  `GetTargetPath` error.
- `--parallel 2` caps only how many projects build at once. The `RhinoForge*` projects compile with `/MP` and no count,
  so inside each one `cl.exe` runs a file per logical core. Where memory is short, as on the second machine, build with
  `--parallel 1 -- /p:BuildInParallel=false /p:CL_MPCount=2`. Blue screens or `nvlddmkm` timeouts during a build mean
  stop and cap it ([A build's `--parallel` does not cap the compiler](build-parallel-does-not-cap-compiler.md)).
- `CMAKE_SUPPRESS_REGENERATION ON` is set here too, so re-run the configure line after editing `CMakeLists.txt`
  ([A new source file needs an explicit CMake configure](cmake-does-not-reconfigure.md)).
- From VS Code, choose Forge as CMake Tools' *Active Project*: the shared, gitignored settings reuse this build tree
  and pass `/p:BuildInParallel=false`, and the launch target is `RhinoForge_Standalone`
  ([Build and test Rhino](build-and-test-rhino.md#from-vs-code) has the settings and the kit to pick).
- `VST3_AUTO_MANIFEST FALSE` is deliberate: the pinned JUCE generator's manifest-helper project fails before the VST3
  target runs. Development hosts do not need the manifest; re-enable it for release packaging.
- Every source compiles against the precompiled `src/ForgePch.h` (JUCE and the standard library, no Forge header),
  roughly halving a translation unit's cost.
- A full rebuild took about eight minutes (2026-09-22), and any `core/` or `ui/` header change is one, because the
  engine is headers only ([Forge's engine splits into headers only](forge-engine-headers-only.md)). Start such a build in
  the background: a foreground call that times out kills MSBuild and leaves orphaned `cl.exe` processes holding source
  files ([Build locks from MSBuild nodes and orphaned compilers](orphaned-build-processes.md)).

Outputs land under `instruments/rhino-forge/build/RhinoForge_artefacts/Release/`: `Standalone/Rhino Forge.exe` and
`VST3/Rhino Forge.vst3`. The plugin's binary is `VST3/Rhino Forge.vst3/Contents/x86_64-win/Rhino Forge.vst3`, and
that, not the host, is what `currentExecutableFile` names inside the plugin: eight folders below
`instruments/rhino-forge`, against five for the standalone, so anything found by walking up from the binary must walk
that far ([Forge oscillators and wavetables](forge-oscillators.md)). Hosts load this build-tree copy directly: RhinoDAW
by its discovery ([Hosting Forge in Rhino](forge-hosting.md)), and Ableton Live 12 on the development machine because
its plugin database (`%LOCALAPPDATA%\Ableton\Live Database\Live-plugins-1.db`, checked 2026-10-13) records that path.
The VST3 cannot relink while RhinoDAW or a DAW has it loaded, which is the normal state
mid-session. Move the held binary aside first (`Move-Item` it to `Rhino Forge.vst3.inuse`, inside
`Contents/x86_64-win`; first done 2026-10-22) and `RhinoForge_VST3` relinks; failing that, build
`--target RhinoForgeTests RhinoForge_Standalone` and report the VST3 as stale
([LNK1104 means a running binary holds the file](locked-executable-lnk1104.md)). Never commit a bundle, and install one
only after validating it in a host.

## Warnings

`RhinoForge` and `RhinoForgeTests` both link `juce::juce_recommended_warning_flags` (MSVC `/W4`). Fix a warning at its
site rather than suppressing it or lowering the level (decided 2026-10-13, when a full build's 890 warning lines came
from 24 sites, all in Forge's own code and none in JUCE).

- Headers inflate the count. The editor, processor and most test translation units of both targets include `ui/*.h`,
  so one warning in a ui header prints about 40 times.
- An up-to-date build prints nothing, and MSBuild leaves no per-project log here. To list every warning without
  touching the working tree or a running `Rhino Forge.exe`, run a full build of `HEAD` in a scratch worktree, as
  [Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md) describes. It takes a few minutes.
  The MSB8029 it reports (intermediate or output directory under `Temp`) is harmless for a first build but not for an
  edit afterwards: on 2026-10-25 a control build in a `Temp` worktree did not recompile after a header was edited there,
  and only `--clean-first` picked the change up. Rebuild such a tree with `--clean-first` after any edit.
- The causes found so far:
  - C4459: `rhino::forge::ui` declares colour tokens named `text`, `panel` and `line` (`ui/ForgeTheme.h` since
    2026-10-16), so a local or parameter with one of those names hides it. Name them `textArea`, `label`,
    `lineTool` and the like ([Colours and typography](colours-and-typography.md)).
  - C4458: a local named after a member of its class, JUCE bases included: `properties` (`juce::Component`), `playHead`
    (`juce::AudioProcessor`), `samples` (`Processor`'s `SampleStore`). C4456 and C4457 catch a local that hides an
    outer local or a parameter.
  - C4244: `Graphics::drawHorizontalLine` and `drawVerticalLine` take `(int, float, float)`, so cast `Rectangle<int>`
    coordinates to `float`.
  - C4996: the raw-pointer `AudioFormat::createWriterFor(OutputStream*, ...)` is deprecated in the pinned JUCE. Use
    the `std::unique_ptr<OutputStream>&` overload with `AudioFormatWriterOptions`, as `native/src/model/SessionSamples.cpp`
    does. The writer takes the stream only on success, so the old calls leaked the stream whenever no writer was made.

## Test

One binary, one file per area, one CTest case per area: 23 areas as of 2026-10-25, `guide` the newest
(`forge_test_areas` in `CMakeLists.txt`; [Forge note guide](forge-note-guide.md)). `-R` is an unanchored regex, so `-R forge_fx` runs `forge_fxModulation`
too; `-R '^forge_fx$'` runs the one.

```powershell
cmake --build instruments/rhino-forge/build --config Release --target RhinoForgeTests --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release -R forge_fx --output-on-failure
ctest --test-dir instruments/rhino-forge/build -C Release -j 8 --output-on-failure
```

- While working, run your area (`forge_<area>`: one translation unit rebuilt, about a second of checks); at a
  milestone, run them all. `-j 8` is safe because no two areas share a `Processor`.
- Run directly, the binary takes area names with or without dashes (`fx`, `--fx`), several at once in the order given,
  or none to run everything; an unknown name exits 2 with the list. Only `--list` and the tools below need their dashes.
- Adding an area touches four files: the new `tests/ForgeTests<Area>.cpp`, its declaration in `tests/ForgeSuites.h`, a
  row in the table in `tests/ForgeTestMain.cpp`, and two lines of `CMakeLists.txt` (the file in `target_sources`, the
  name in `forge_test_areas`). Nothing self-registers; reconfigure afterwards.
- The test binary never reads the machine's settings. `main` (`tests/ForgeTestMain.cpp`) points
  `Processor::setSettingsFile` at `%TEMP%\RhinoForgeTests\Settings.xml` and deletes it before anything runs, so the
  suite and the tools render Forge even where the user picked Merli, and no run inherits an earlier one's choice
  ([Forge themes and Merli](forge-themes.md)).
- The test binary links with an 8 MB stack because suites keep several `Processor`s as locals; a CTest `SegFault` can
  still be an overflow ([A CTest SegFault may be a stack overflow](stack-overflow-reports-as-segfault.md)).
- To prove a new check can fail, break the code temporarily and then restore it by editing, not with `Copy-Item`. A
  copied-back file keeps the backup's older timestamp, so MSBuild keeps the object built from the broken code
  ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- Never filter the build and run `ctest` in one command (`cmake --build ... | Select-String ...; ctest ...`): a failed
  build leaves the old binary and CTest reports it all passed. Redirect the build to a log, check `$LASTEXITCODE`, and
  only then run CTest ([Editing only a scenario .inc does not rebuild the tests](inc-edits-do-not-rebuild.md)).
- `--snapshot`, `--profile`, `--profile-browser`, `--profile-audio` and `--render` are development tools in the same binary, not test cases
  ([Proving a Forge change changed nothing](proving-a-forge-change-changed-nothing.md)).
- `--profile-browser [width height]` (`tests/ForgeTestTools.cpp`, dispatched in `tests/ForgeTestMain.cpp`; since 2026-10-25,
  commit `ad945e1`) is the preset path's profile. It reads the real bank in Documents and the machine's
  Serum 2 and Vital folders, writes nothing, and prints each library listed cold and warm, every factory preset loaded
  through the call a click in the list makes (median and max, with the editor open), five converted Serum 2 and Vital
  loads, the browser opened and each library chosen, and the browser's frames. The same day `--profile` gained
  `whole panel`, `whole panel, chord held` and `tick, live regions`, painted with the children included
  ([Forge's tick repaints its live regions at 24 Hz](forge-panel-repaint-cost.md#measuring-it)). The figures are on
  [Forge preset browser and bank](forge-preset-browser.md#what-it-costs).
- `--profile-audio` on the everything patch with five notes, 2026-10-25: block 64 median 662 us (50% of real time),
  block 512 median 4.97 ms (46.5%), 0 allocations. Seen there and left alone: `Processor::arpSettings` makes about 27
  `getRawParameterValue` lookups per block (StringRef keyed, no allocation), and `juce::MidiKeyboardState::processNextMidiBuffer`
  takes a `CriticalSection` every block, JUCE's own design.
- Two more tools in `tests/ForgeTestRender.cpp` take a preset of any format: `.forgepreset`, `.SerumPreset` or `.vital`.
  - `--audition <preset> <out.wav>` renders four seconds at 48 kHz in 512-sample blocks, C1 (note 36) from 0 to
    0.6 s and a C major chord (C4 E4 G4) from 0.8 to 2.4 s, writes them to the WAV, and prints the conversion's
    warnings and the peak and RMS level. It is the quick way to hear a conversion, or to find one that is silent, and
    the render a factory preset is measured on: a numpy pass over the WAV gives the bass note's share of energy below
    60 and 120 Hz and the spectral centroid per frame ([Writing Forge factory presets](writing-forge-factory-presets.md)).
  - `--convert <preset> <out.forgepreset>` writes the converted patch.
- The test binary replaces the global `operator new` to count allocations (`AllocationCounter`,
  `tests/ForgeTestSupport.h`), which is how area `engine` proves a block allocates nothing. It cannot see
  `juce::HeapBlock`, so a growing `MidiBuffer` or `AudioBuffer` needs a check of its own
  ([Real-time audio rules](real-time-audio-rules.md)).

## Related

- [Rhino Forge](forge.md)
- [Forge is an independent VST3](forge-is-an-independent-vst3.md)
- [Driving Forge's standalone on Windows](driving-the-forge-standalone.md)
- [Seeing the UI without taking the screen](headless-ui-snapshots.md)
- [Hosting Forge in Rhino](forge-hosting.md)
