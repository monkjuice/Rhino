# Rhino Forge

Rhino Forge is Rhino's independent synthesizer project. It is a VST3 and
standalone JUCE application that deliberately owns no Tracktion or Rhino-DAW
types.

The panel is a set of modules — two oscillators, sub, noise, a filter, four
envelopes, six LFOs, global voicing, eight macros, the modulation matrix, the
mixer and three effects racks — each in its own box with its own enable.

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

Five tabs in the title bar: `OSC`, `TABLE`, `MATRIX`, `MIX` and `FX`. The first
three switch **only the two oscillators** — the wavetable editor and the matrix
take turns in their columns, while sub, noise and the filter hold their places
either side. `MIX` and `FX` take the whole signal row instead, because each is a
view of everything upstream rather than a panel that sits beside it. The lower
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

`FX` is three racks — one on the main output and one on each bus — chosen by the
named cards in the module's header. A rack is four slots and the signal runs
down them, top to bottom. A slot holds any of six types, the same type can sit
in two slots, and every slot declares the same twelve controls whatever is in
it: a type, two mode fields, six general knobs, a bypass, a mix and a level.

What those six knobs *mean* belongs to the type, declared once in
[core/ForgeFx.h](core/ForgeFx.h) and read three times — by the DSP, by the panel
labelling them, and by the readout turning 0.6 into 480 milliseconds. A knob the
type does not use is taken off the panel rather than greyed, because a reverb
has no fourth knob at all.

A slot is filled by clicking its **name plate** and choosing from the list, so a
slot's identity and its one structural choice are the same object. Each type has
a colour and a mark of its own — a reverb's decaying burst, a delay's fading
repeats, a distortion's flattened peaks — and the slot wears both: on the plate,
on every knob in the row, and lit down the left edge of the shelf it sits on. A
rack is meant to be read by colour down its four rows before a word on it is.

A slot's two mode fields are drawn as whatever the choice in front of you
actually is. Two or three named states — `PLATE / HALL`, `NORMAL / PING-PONG`,
`OFF / PRE / POST` — are stacked, with the live one lit; the names are words of
very different lengths, and a column gives each of them the field's full width
rather than cropping the long ones. More than three, as with the distortion's
eight shapes, is a name between two arrows: the arrows step, the name opens the
list. Each carries the label the
type gives it, so the field says what it is choosing as well as what is chosen.

A knob a mode has made meaningless greys out rather than disappearing: the
distortion's FREQ and Q while its filter is off, an equaliser band's gain once
that band is a pass shape. That is a different thing from a knob the type does
not have at all, which comes off the panel entirely — "not just now" against
"not ever, while this is in the slot".

Each slot draws itself in a strip beside its mode fields: a reverb's decay
envelope, a delay's repeats falling away across the two channels, a chorus's two
taps swinging across one cycle, a distortion's transfer curve against the
diagonal that no distortion would be, an equaliser's response, a filter's
corner. Every one is computed from the arithmetic the engine actually runs — the
distortion curve is `fxShape` called per pixel, the equaliser's is the magnitude
of the very biquads `setBand` builds — so a display cannot claim one thing while
the slot does another.

Choosing a type also sets that type up, the way adding a module in Serum loads
its default preset. One parameter default cannot serve seven types — 100% wet is
right for an equaliser and wrong for a reverb on the main output — so what a
type opens on lives beside the type. A type arriving from a preset or from a
host's automation lane is left exactly as it came.

That shape is what lets a rack hold anything anywhere. Naming every control of
every type in every slot would be several hundred parameters with nearly all of
them dead at any moment, so a host's automation lane reads "MAIN 2 KNOB 3"
rather than "Reverb Damp" — the same trade Serum makes, for the same reason.

The racks run on the summed voices rather than inside them, the way an insert
after the synth does. A per-voice source pointed at a rack knob therefore has to
resolve to a single value, and Core takes the loudest voice's — the voice the
envelope and LFO displays already follow. An LFO in OFF, which free-runs and is
shared by every voice, is the source that drives a rack cleanly.

Each oscillator carries two **warp** stages under its knobs, applied in the
order they are drawn: a mode chosen from a menu grouped the way the Serum
manual groups it, and a knob setting how deep that mode goes. Twenty-six modes
across six families — a window sync, nine ways of bending where in the cycle the
table is read, three pitch-tracked filters on the waveform itself, eight
waveshapers, and four kinds of phase modulation from elsewhere in the voice. The
arrows beside a field step through the list without opening it.

Every mode leaves the wave exactly as it was at the depth it calls neutral, so a
mode can be chosen and then opened up rather than the other way round; four of
them go both ways and are neutral at twelve o'clock, and a double-click on the
depth returns it to whichever of the two its mode means. The oscillator's own
display draws the warp it is running — but only the modes that are honestly a
picture of a table, which is the same line the manual draws when it says the 2D
view shows Sync, Alt Warp and Distortion. Both depths are modulation
destinations; the modes are not, because sweeping a list of twenty-six unrelated
modes is a stutter rather than a modulation.

Forge does not oversample, so a warp that brightens a wave instead reads a
duller band-limited copy of the table: each mode declares how much extra
bandwidth it is about to ask for, and the oscillator picks its copy for a note
that much higher. That reduces aliasing rather than removing it, and the modes
that put a step in the waveform — FLIP and QUANTIZE — are where it still shows.

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
| `core/ForgeFx.h` | What an effects rack is: the types, what each one's controls are called, and what a normalised knob means in each. No DSP. |
| `core/ForgeFxDsp.h` | The racks, rendered. A slot carries every type's state, sized once at `prepare`, because a type changes while audio is running. |
| `ui/ForgeFxDisplay.h` | What each effect draws of itself, from the same functions that render it. |
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
