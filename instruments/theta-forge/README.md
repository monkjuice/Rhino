# Theta Forge

Theta Forge is Theta's independent synthesizer project. It is a VST3 and
standalone JUCE application that deliberately owns no Tracktion or Theta-DAW
types.

Forge is a synthesiser and nothing else. The panel is a set of modules — two
oscillators, sub, noise, a filter, one amp envelope, one LFO, global voicing,
eight macros and the modulation matrix — each in its own box with its own
enable. Effects are deliberately absent until the synth is finished.

Two tabs in the title bar, `OSC` and `MATRIX`, switch **only the top row** of
the panel: the oscillators and the matrix take turns in that one row, and
everything below it stays on screen either way. A module says which tab it
belongs to by declaring a `Page`; a module that declares nothing is always
shown. Switching tabs hides and shows components rather than rebuilding them,
so a knob the matrix is covering is still driven by the host and by its own
modulation slots while it is out of sight.

The editor saves and loads versioned `.forgepreset` files; host project state
remains independent and continues to use the VST3 state API.

It takes inspiration from the fast, visual sound-design workflow of modern
hybrid synths. It does not reuse Serum code, assets, names, presets, or UI.

**[PLAN.md](PLAN.md) is the build-out plan**: what is done, what is next, and
which decisions are already settled. Read it before changing the synth.

## Layout

| File | Holds |
| --- | --- |
| `core/ForgeCore.h` | The voice engine. No AudioProcessor, UI, state tree, filesystem, or allocation in `renderSample`. |
| `src/ForgeProcessor.*` | Parameters, host automation, state, preset files. |
| `ui/ForgeLayout.h` | What modules exist, what each contains, and where it sits. Pure geometry and declaration. |
| `ui/ForgeVisuals.h` | The knob look and the drawing primitives. Decides nothing about placement. |
| `src/ForgeEditor.*` | Walks the declared modules and builds the components. |
| `tests/ForgeTests.cpp` | Three CTest cases from one binary: `--layout`, `--presets`, `--engine`. |

Adding a control means declaring the parameter in `ForgeProcessor.cpp` and
naming it in a module in `ForgeLayout.h`. The layout test fails if the two
disagree in either direction.

## Build on Windows

First fetch the parent project's dependencies, then configure this directory:

```powershell
cmake -S instruments/theta-forge -B instruments/theta-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/theta-forge/build --config Release --parallel 2
ctest --test-dir instruments/theta-forge/build -C Release --output-on-failure
```

The standalone build at
`build/ThetaForge_artefacts/Release/Standalone/Theta Forge.exe` is the quickest
way to look at a change without a host.

The VST3 is emitted below `build/ThetaForge_artefacts/Release/VST3`. Install or
copy it only after validating it in a host; do not add generated plugin bundles
to Git.

Theta's development build discovers that bundle directly, then instantiates it
through Tracktion Engine's standard external-plugin wrapper. The editor shown in
Theta is this plugin's own editor; Forge has no dependency on Theta or Tracktion.
