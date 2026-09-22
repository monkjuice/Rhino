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

The panel is two rows of modules, with a taller signal row, over a
seventy-six key keyboard, and it is wider than it is tall at every size it
allows. The top row is the signal path read left to right — sub and noise, the
two oscillators, the filter. The bottom row is what shapes it, in the same
order: global voicing as a narrow column, then the envelopes, the LFOs and the
macros.

The keyboard was eighty-eight keys until the arpeggiator arrived and gave up
its bottom octave for the **ARP** plate, which stands between the performance
wheels and the keys. Every key is the width it always was: the plate is sized as
the octave that came off, so the keys are still divided out of the span they
had. Pitch and modulation wheels share the metal plate at the lower left.
Drag up to raise either wheel. Pitch springs back to center with a two semitone
range; modulation holds its position and is available as **MOD WHEEL** in the
matrix. Host MIDI pitch bend and CC1 drive the same controls.

The metal chassis follows the reference's assembled construction: interlocking
header plates, segmented rails, a shared Sub/Noise housing, recessed legends,
and keyboard end plates. `ui/ForgePanels.h` draws that static furniture; the
editor caches it at the display scale, independently of the live controls.
Resizing rebuilds that layer at the current size and full display resolution;
headings and artwork are never temporarily downsampled or stretched from an
earlier frame. Fractional display scales preserve the cache's physical pixels.

The filter shows its own response: the band it is passing filled under the
curve, the band it is taking out washed in above it, decade lines across the
audible range and the corner frequency marked and named — plus the second
corner, more faintly, on a type that has one. It is drawn from the transfer
functions in `core/ForgeFilter.h`, which is the same header the engine's
coefficients are worked out in, so what the display claims is being removed is
what is being removed — including the peak resonance puts back at the corner.

Three of the types are not linear and so have no transfer function to draw. Each
of those draws the truest thing that can be said about it instead: unity for the
ring modulator and the diffusor, which take nothing out, and a hold's own sinc
for the sample and hold, which genuinely is what freezing a value does to a
spectrum. The first two are labelled PHASE ONLY on the display, because a flat
line means one thing on a diffusor and quite another on a low pass.

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
than refused by the panel. Each bus carries the effects rack shown on the FX
tab, so a send reaches the ordered chain selected by its BUS card.

`FX` is three racks — one on the main output and one on each bus — chosen by the
named cards in the module's header. A rack is eight slots and the signal runs
down them, top to bottom. A slot holds any of eight types, the same type can sit
in two slots, and every slot declares the same twelve controls whatever is in
it: a type, two mode fields, six general knobs, a bypass, a mix and a level.

The rack has a signal-flow list down its left side. A fixed **+ ADD EFFECT** at
its top fills the next slot; only assigned effects occupy rows below it. The
list repeats each effect's colour and mark, lets a module be bypassed or
removed, and reorders the chain by dragging. Removing one closes the chain
around it, leaving clean empty space after the final effect instead of an OFF
placeholder. The list folds to a mark-only rail when the editor needs the
width. The expand button at the right of the rack
header — or **Alt+F** on Windows — grows the rack through both module rows, so
more of the chain is visible without changing the keyboard or the title bar.
Every effect strip keeps a fixed, slightly roomier height in both views; the wheel over the list
scrolls by slots when all eight do not fit. Each rack remembers its own scroll
position. These are views of the same slot parameters, not copies of the rack,
and none of the view settings are saved in a preset.

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
rack is meant to be read by colour down its rows before a word on it is.

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
diagonal that no distortion would be alongside its PRE/POST LP/HP response, an equaliser's response, a filter's
corner, a compressor's transfer curve, or a phaser's moving notches. Every one
is computed from the arithmetic the engine actually runs — the
distortion curve is `fxShape` called per pixel, the equaliser's is the magnitude
of the very biquads `setBand` builds — so a display cannot claim one thing while
the slot does another.

Choosing a type also sets that type up, the way adding a module in Serum loads
its default preset. One parameter default cannot serve eight effect types — 100% wet is
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

The **filter** is thirty-four types across six families, chosen from a menu
grouped the way that menu is actually read — you know you want a ladder before
you know which one. BASIC is the five taps of one state-variable filter: low,
high, band, notch and peak. DUAL is twelve pairs of those in series, the first
on CUTOFF and the second on FREQ — its own frequency on the same scale, not an
interval, so a sweep drags one filter past a stationary other exactly as Serum's
does. LP+HP therefore has an edge you can place at each end, and LP+NT is a low
pass with a hole in it wherever you put it. MORPH is four sweeps through
three responses, which is the one filter movement no corner can give you.
ANALOG is four chains of poles with the last one's output fed back round them
through a saturator — a four-pole transistor ladder, a three-pole diode ladder
leaned the way a diode conducts, that ladder driven hard inside its own loop,
and a three-pole that keeps a little of the signal past the poles so the body
survives the resonance. RESONATORS are a tuned delay fed back, the same delay
against the dry signal so its teeth are nulls instead of peaks, and a chain of
all-passes. CHARACTER is the six that are filters only by where they sit: a
vowel bank the cutoff moves the mouth of, a ring modulator, a sample and hold,
an all-pass diffusor, a low pass with the damping taken out from under it until
it screams, and the delay loop with the diffusor inside it.

One knob beside TYPE carries whatever that type needs, the way a rack slot's
knobs carry whatever is in the slot: FREQ on a dual, MORPH on a morph, FAT on
the basic five and the clean ladders, PAIN, DAMP, STAGES, SHIFT, SPREAD, DIFF or
FEED on the rest. Its label, its readout and its double-click all come from the
type's own row in `filterTypes()`, so it cannot be labelled as one thing and
rendered as another. Choosing a type from the menu moves that knob to what the
new type opens on, but only when the type has changed what the knob is *for* —
stepping LOW to HIGH leaves it alone, and stepping LOW to LP+HP does not.

LOW, HIGH and BAND are still types 0, 1 and 2, and FREQ opens at nothing, which
is FAT off. A patch saved before any of the rest of this existed therefore loads
on the filter it was saved on and sounds the way it did, to the bit.

Each oscillator carries two **warp** stages under its knobs, applied in the
order they are drawn: a mode chosen from a menu grouped the way the Serum
manual groups it, and a knob setting how deep that mode goes. Thirty-eight modes
across nine families — a window sync, nine ways of bending where in the cycle
the table is read, three pitch-tracked filters on the waveform itself, eight
waveshapers, and then four kinds of cross-modulation from elsewhere in the
voice: FM on the carrier's frequency, linear or exponential; PD on its phase;
AM and RM on its level. The arrows beside a field step through the list without
opening it, and the foot of the menu swaps the two stages over.

FM and PD are separate families and not two names for one thing. FM moves the
rate the cycle runs at, so a deep setting bends the note and linear clamps at
zero rather than running backwards — the traditional FM every classic digital
synth had. PD moves where in the cycle the table is read, so the note stays
exactly where it was however deep it goes. The three sources are the other
oscillator, the sub and the noise, each of which must be switched on though its
level may be all the way down; PD adds a fourth, a stage reading its own output.

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

## LFO tables

Each of the six LFOs starts on **Default**, which contains the existing sine,
triangle, saw, square and sample and hold shapes. The name below the graph
opens the shape menu; the arrows beside it step through the basic and saved
shapes. Drag a point to make a **Custom** table; double-click empty space to add
a point, double-click a point to remove it, or right-click an interior point to
remove it. Hold Alt while dragging to snap to the grid. The two numbered fields
below the graph set columns and rows independently, from 2 to 32. Click an
arrow to change by one, drag a number vertically or use the wheel for larger
changes, and double-click a number to restore 8.

The name menu loads and saves `.forgelfo` tables. The save dialog starts in
`Documents/Rhino Forge/LFO Tables`, and tables in that folder appear under
**Saved shapes**. Custom tables also travel inside Forge presets and host
project state, so the project does not depend on the saved file.

## The arpeggiator

The **ARP** plate beside the keys does two things, exactly as Serum's does: the
circle switches the arpeggiator on, and the rest of the plate puts its settings
up. They are drawn differently — a lit lamp against a raised face — because an
arp running with its settings away and an arp on screen that is switched off are
both ordinary states.

Its settings are an **overlay, not a tab**. They stand on ENV and LFO, so
opening them covers the modulators and leaves GLOBAL and the macros either side
on screen: a patch is adjusted at the macros while the arp runs, and the
oscillators and the filter are what you want to keep watching while a pattern
plays. The five tabs are unaffected, and the arp opens over whichever of them is
showing. It is declared as a page all the same — `Page::arp`, which is never
what a tab is set to — because "which modules does this show" is the same
question for it as for a tab, and answering it the same way is what keeps the
no-two-modules-overlap check honest.

Six panes share one plate, in the order the manual groups them: the arp's own
switch and launch quantisation, the PATTERN, the TRANSPOSE range, PLAYBACK,
RETRIGGER and VELOCITY. The first is called ARP rather than GLOBAL, which is the
manual's name for it, because Forge already has a GLOBAL standing immediately
beside it.

Eighteen shapes, and **one list serves twice**: the order the held keys are
played in, and the order the transposition stages are visited in. Serum's SHAPE
field and its transpose-range menu offer the same vocabulary and mean the same
thing by it — an order to visit a set of things in — so `arpOrder` is called
once for the chord and once for the stages, and knows which it is doing neither
time.

The arp is **upstream of every voice rather than inside one**, so it is not part
of `Core` and `Core` knows nothing about it. It is fed the notes the host and
the panel's keyboard send, and it hands notes back through two callbacks — which
is what lets the whole of it be tested against a pair of lambdas with no
`Processor`, no `Core` and no audio device, so what the arp did is a list of
note numbers rather than a waveform to be measured.

Still to come, and each its own piece of work: the twelve launchable arp slots
per bank with their `EDIT ALL` and their bank presets, and the custom pattern
editor. `LAUNCH QUANT` is already real without the slots — it holds a started
arp to the next division of the host's bar.

It takes inspiration from the fast, visual sound-design workflow of modern
hybrid synths. It does not reuse Serum code, assets, names, presets, or UI.

**[PLAN.md](PLAN.md) is the build-out plan**: what is done, what is next, and
which decisions are already settled. Read it before changing the synth.

## Layout

| File | Holds |
| --- | --- |
| `core/ForgeCore.h` | The voice engine. No AudioProcessor, UI, state tree, filesystem, or allocation in `renderSample`. Four headers under it, listed at the top of it. |
| `core/ForgeArp.h` | The arpeggiator: the shapes, the clock, and the notes a held chord becomes. Stands in front of Core rather than inside it, and emits through callbacks so it can be driven without either. |
| `core/ForgeFx.h` | What an effects rack is: the types, what each one's controls are called, and what a normalised knob means in each. No DSP. |
| `core/ForgeFxDsp.h` | The racks, rendered. A slot carries every type's state, sized once at `prepare`, because a type changes while audio is running. |
| `core/ForgeFilter.h` | The filter: the thirty-four types, what each holds between samples, the one function that runs any of them, and the response the panel draws. Depends on nothing of Forge's, so the curve and the audio are read out of one file. |
| `ui/ForgeFxDisplay.h` | What each effect draws of itself, from the same functions that render it. |
| `ui/ForgeFilterVisuals.h` | The window the filter's response is drawn in: the axes, the grid, the fill and the corner markers. The arithmetic is `core/ForgeFilter.h`'s. |
| `src/ForgeProcessor.*` | What the host calls, and what it hands the engine. `ForgeParameters.cpp` declares every parameter; `ForgeProcessorState.cpp` carries state, presets and table files. |
| `ui/ForgeLayout.h` | What modules exist, what each contains, and where it sits. Pure geometry and declaration; four headers, listed at the top of it. |
| `ui/ForgeVisuals.h` | The knob look and the drawing primitives. Decides nothing about placement; seven headers, listed at the top of it. |
| `ui/ForgePanels.h` | Static metal housings, chassis rails, hardware and decorative lettering. |
| `src/ForgeEditor.*` | Walks the declared modules and builds the components. One class across nine files, by what each does. |
| `tests/` | One file per area, one CTest case each. See **Tests** below. |

`ForgeCore.h`, `ForgeLayout.h` and `ForgeVisuals.h` each include the headers
they were split into and list them at the top, so every existing include still
works and nothing had to move. Include the narrowest one that answers the
question — a test that draws nothing, or a header that only needs to know what
a `Module` is, should not be pulling in every knob look Forge has.

The engine splits into headers and never into translation units. `renderSample`
is compiled into the processor and stays inlined there; Release has no
link-time code generation, so a call across a `.cpp` boundary on the audio path
would be a real one. The panel is the opposite case and splits into translation
units freely: a frame spends its time in Direct2D, not in call overhead.

`src/ForgeEditor*.cpp` are one `Editor` defined across several translation
units, the way `Session` and `Arrangement` already are in the DAW: no header
change, no call site change, and a new file needs only a line in
`CMakeLists.txt`. `ForgeEditorInternal.h` carries what used to be the
anonymous namespace and is private to those files.

Adding a control means declaring the parameter in `src/ForgeParameters.cpp` and
naming it in a module in `ui/ForgeModules.h`. The layout test fails if the two
disagree in either direction.

## Tests

One binary, one file per area, one CTest case per file. The area's name is the
argument that selects it and the case that runs it, so an afternoon on the
effects is `tests/ForgeTestsFx.cpp`, `--fx` and `ctest -R forge_fx` — one
translation unit rebuilt and one second of checks — while a milestone is plain
`ctest`, which runs the lot.

```powershell
cmake --build instruments/rhino-forge/build --config Release --target RhinoForgeTests --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release -j 8 --output-on-failure
ctest --test-dir instruments/rhino-forge/build -C Release -R forge_fx --output-on-failure
```

No two areas share a `Processor`, so `-j 8` is safe, and it is what makes the
full run about five seconds rather than seventeen.

The arpeggiator's area is mostly not a render at all: `Arp` emits through
callbacks, so its shapes and its clock are checked against a pair of lambdas
that write note numbers into a vector, and only the last suite goes through a
`Processor` — because the one thing lambdas cannot check is that the notes reach
the voices.

`RhinoForgeTests --list` prints the areas. Three places hold the registry and
are edited together: `tests/ForgeSuites.h` declares each area's entry point,
the table in `tests/ForgeTestMain.cpp` maps a name to a function, and
`forge_test_areas` in `CMakeLists.txt` turns each name into a case. Nothing
self-registers and nothing is globbed.

Checks, parameter access, rendering a note and measuring what came back are
shared in `tests/ForgeTestSupport.*`; the FFT that settles pitch and timbre is
in `tests/ForgeTestSpectrum.*`. An area that draws nothing includes neither
`ForgeLayout.h` nor `ForgeVisuals.h`, which is most of why a one-file rebuild
costs what it does.

## Build on Windows

First fetch the parent project's dependencies, then configure this directory:

```powershell
cmake -S instruments/rhino-forge -B instruments/rhino-forge/build -G "Visual Studio 17 2022" -A x64
cmake --build instruments/rhino-forge/build --config Release --parallel 2 -- /p:BuildInParallel=false
ctest --test-dir instruments/rhino-forge/build -C Release --output-on-failure
```

The MSBuild property serializes project-reference resolution to avoid the
toolchain's silent `GetTargetPath` failure; compilation still uses two jobs.

The standalone build at
`build/RhinoForge_artefacts/Release/Standalone/Rhino Forge.exe` is the quickest
way to look at a change without a host.

For a windowless visual review, the test binary also accepts
`--snapshot output.png [width height [OSC|TABLE|MATRIX|MIX|FX|ARP [scale [preset.forgepreset]]]]`.
`ARP` is not one of the tabs — it presses the plate beside the keyboard, which
is the only way in, so the review takes the same route a hand does.
A preset is opened before the editor is built, which is the only way to
review anything the panel draws out of the patch rather than out of the
layout — a modulation ring, a macro's destination count, the name under it.
It captures the actual editor, including its controls and cached metal layer.
`--profile [width height]` reports what a frame costs instead, and
`--render out.raw [blocks]` writes a deliberately busy patch as raw interleaved
floats. None is a CTest case; they live in `tests/ForgeTestTools.cpp` and
`tests/ForgeTestRender.cpp`.

`--render` is how a change to the engine is shown to have changed nothing: hash
its output, build the other revision beside this one, hash that, and compare.
The same trick works on the panel with `--snapshot`, which writes a PNG of the
real editor. Both were what settled that splitting these files was free.

Every Forge source compiles against `src/ForgePch.h`, which holds JUCE and the
standard library and deliberately no Forge header. It roughly halves what a
translation unit costs, which is what makes one file per area cheaper than one
file for everything: twenty translation units now build in less time than
three did before it.

The VST3 is emitted below `build/RhinoForge_artefacts/Release/VST3`. Install or
copy it only after validating it in a host; do not add generated plugin bundles
to Git.

Rhino's development build discovers that bundle directly, then instantiates it
through Tracktion Engine's standard external-plugin wrapper. The editor shown in
Rhino is this plugin's own editor; Forge has no dependency on Rhino or Tracktion.
