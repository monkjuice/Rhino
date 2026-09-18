# Rhino Forge

Rhino Forge is Rhino's independent synthesizer project. It is a VST3 and
standalone JUCE application that deliberately owns no Tracktion or Rhino-DAW
types.

Forge is a synthesiser and nothing else. The panel is a set of modules — two
oscillators, sub, noise, a filter, four envelopes, six LFOs, global voicing,
eight macros, the modulation matrix and the mixer — each in its own box with its
own enable. Effects are deliberately absent until the synth is finished.

The four envelopes share one module and the six LFOs share another, each showing
one at a time: numbered cards hanging from the module's top edge say which, and
the module's drag handle — another such card, carrying the name of whichever is
showing — drags it onto a knob. ENV 1 is the voice's amplitude; ENV 2–4 are
sources and nothing else, so one reaches a control through a modulation slot or
not at all. All four run in every voice, started by the same note and released by
the same key. An LFO set to TRIG or ENV runs inside each voice too, so a new note
starts its own copy and leaves the notes already sounding alone; one set to OFF
is a single free-running cycle shared by every voice and by the panel.

The panel is two rows of modules, the same height as each other, over an
eighty-eight key keyboard, and it is wider than it is tall at every size it
allows. The top row is the signal path read left to right — sub and noise, the
two oscillators, the filter. The bottom row is what shapes it, in the same
order: global voicing as a narrow column, then the envelopes, the LFOs and the
macros.

The filter shows its own response: the band it is passing filled under the
curve, the band it is taking out washed in above it, decade lines across the
audible range and the corner frequency marked and named. It is drawn from the
transfer functions of Core's own state-variable filter rather than from a
generic curve, so what the display claims is being removed is what is being
removed — including the peak resonance puts back at the corner.

No knob prints its value. A readout under every knob costs the panel a line of
height each, whether or not anyone is reading it, and it was the readout rather
than the knob that decided how small a macro could be drawn. The value appears
instead where the hand already is: a bubble beside the knob being turned,
naming the control and what it now reads, which stays for a moment after the
gesture so a wheel notch shows something too. The steppers and the matrix's
amount bars are unaffected — a field whose whole purpose is to be read exactly
still shows what it holds.

Four tabs in the title bar: `OSC`, `TABLE`, `MATRIX` and `MIX`. The first three
switch **only the two oscillators** — the wavetable editor and the matrix take
turns in their columns, while sub, noise and the filter hold their places either
side. `MIX` is the one that takes the whole signal row, because the mixer is the
view of those same sources rather than a panel that sits beside them. The lower
row never moves.

A module says which tabs it appears on by declaring a set of pages, so the
answer can be one tab, every tab, or every tab but one — which is what sub,
noise and the filter need. Switching tabs hides and shows components rather than
rebuilding them, so a knob the matrix is covering is still driven by the host
and by its own modulation slots while it is out of sight.

`MIX` is eight channels across the row, in the order the signal travels: SUB,
OSC A, OSC B, NOISE, FILTER, BUS 1, BUS 2, MAIN. Every source channel carries
where it goes, a send to each bus, a pan and a fader; the filter's channel adds
a blend against what was sent into it; the busses carry a level, a pan and a
destination. Nothing on it is a second copy of a setting — a channel's pan and
level are the module's own parameters, shown where a balance is actually read.

A channel's header enable is the source's own, exactly as Serum's mixer header
is, which is also what gives every channel its mute. The two busses are summing
points with a level and a place in the image; a bus can feed the other one or
the main output, and two pointed at each other is broken by the engine rather
than refused by the panel. They carry no effects yet: the FX racks that make a
bus worth sending to arrive with M11 and land on these channels without moving
them.

Each oscillator reads a wavetable of its own. `TABLE` draws on it: freehand or
straight lines on the selected frame, a strip of every frame below it, and add,
duplicate, remove, init, normalise and undo. A table can also be loaded from an
ordinary `.wav` of single-cycle frames — ten factory tables ship in
[tables/](tables/) — or dropped on the editor. A table that has been drawn on or
loaded travels inside the preset and inside host state, so a patch stays
self-contained when it moves between machines.

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
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2
ctest --test-dir instruments/rhino-forge/build -C Release --output-on-failure
```

The standalone build at
`build/RhinoForge_artefacts/Release/Standalone/Rhino Forge.exe` is the quickest
way to look at a change without a host.

The VST3 is emitted below `build/RhinoForge_artefacts/Release/VST3`. Install or
copy it only after validating it in a host; do not add generated plugin bundles
to Git.

Rhino's development build discovers that bundle directly, then instantiates it
through Tracktion Engine's standard external-plugin wrapper. The editor shown in
Rhino is this plugin's own editor; Forge has no dependency on Rhino or Tracktion.
