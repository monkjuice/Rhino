#pragma once

#include "../core/ForgeCore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <vector>

// Forge's panel is described, not computed. Every module declares its own
// title, accent, display, grid position and knobs; the editor walks that
// declaration. Nothing outside this file works out where a control belongs
// from its index, which is what used to make adding or removing a parameter a
// renumbering exercise across three files.
namespace rhino::forge::ui
{
using rhino::forge::ModSource;

enum class Display { none, oscillator, envelope, lfo, filter };

// A knob is the default. A stepper is the compact field used where reading an
// exact value matters more than sweeping a range: tuning, filter type, a mixer
// channel's destination. A chip is a small on/off button, used for the filter's
// per-source routing. A rocker is a two-state switch that occupies a knob's
// footprint, so it lines up with the knobs beside it. A bar is a horizontal
// fill drawn from the middle of its range, for a signed amount read across a
// table row. A fader is the tall vertical one a mixer channel is balanced on,
// and the only control that is taller than it is wide.
enum class Style { knob, stepper, chip, rocker, bar, fader };

// The tabs. A page is one bit, so which tabs show a module is a set rather
// than a single answer — which is what the mixer needs: it takes the whole
// signal row, so SUB, NOISE and FILTER have to be absent from that one tab
// while staying put on the other three.
enum class Page { oscillators = 1, table = 2, matrix = 4, mix = 8 };

inline constexpr Page tabPages[] {Page::oscillators, Page::table, Page::matrix, Page::mix};
inline constexpr int tabCount = 4;

// Which tabs show a module. Not a Page: a module is on one page, or on all of
// them, or on all but one, and only a set says all three.
using PageSet = int;

inline constexpr PageSet only(Page page) { return static_cast<PageSet>(page); }

inline constexpr PageSet everyPage =
    only(Page::oscillators) | only(Page::table) | only(Page::matrix) | only(Page::mix);

// Shown wherever a module is not standing in its place. The mixer is the view
// of the sub, the noise and the filter, so those three name it here rather
// than being hidden by the mixer reaching over them.
inline constexpr PageSet everyPageBut(Page page) { return everyPage & ~only(page); }

inline const char* pageName(Page page)
{
    switch (page)
    {
        case Page::matrix:      return "MATRIX";
        case Page::oscillators: return "OSC";
        case Page::table:       return "TABLE";
        case Page::mix:         return "MIX";
    }
    return "";
}

struct Control
{
    const char* id;
    const char* label;
    Style style = Style::knob;
    // Greyed out while this parameter is on. Polyphony means nothing in mono.
    const char* disabledBy = nullptr;
    // Share of the row's width, against the row's other controls. A row of
    // like controls leaves this alone; a table row gives its amount bar more
    // room than the fields either side of it.
    int weight = 1;
    // Greyed out while this parameter is OFF, the mirror of disabledBy. A
    // division means nothing while the rate is set in Hertz, and the free rate
    // means nothing once it is set in beats, so the pair of them need the rule
    // both ways round.
    const char* enabledBy = nullptr;
    // Takes the cell of the control before it instead of a cell of its own.
    // Two controls in one cell are two readings of a single setting, never two
    // settings: LFO 1's rate is a knob in Hertz or a knob in beats, and exactly
    // one of the pair is live. A control in a shared cell is hidden while it is
    // not the one in charge rather than greyed out, because a greyed control
    // would be sitting on top of the live one.
    bool sharesCell = false;
};

struct Row
{
    // Share of the module's control area, against the module's other rows.
    int weight;
    std::vector<Control> controls;
    // How many identical sets of controls this row holds, one behind another in
    // the same cells. Six LFOs will not fit on the panel side by side, so the
    // module shows one at a time and a numbered selector in its header says
    // which — the same arrangement Serum uses for its own eight.
    //
    // Every bank has to declare the same controls in the same order, so the
    // controls vector is banks × the controls of one bank, laid out bank after
    // bank. The geometry divides the row between one bank's cells; the others
    // land on top and are hidden.
    int banks = 1;
};

struct Module
{
    const char* id;
    const char* title;
    const char* detail;
    // Null when the module has nothing to switch off: GLOBAL is always on.
    const char* enableId;
    bool violet;
    Display display;
    // Position in a twelve-column grid. Rows are weighted, not fixed height,
    // so the panel keeps its proportions at every allowed window size.
    int row, column, columnSpan;
    // Knobs sized to their own cell instead of the panel's shared diameter,
    // and left out of working that diameter out. The macros are deliberately
    // smaller than the controls they drive, as Serum's are.
    bool compactKnobs;
    std::vector<Row> rows;
    // A module that is itself a modulation source carries a drag handle in its
    // header. Zero means it is not one. Declared last so the modules that are
    // not sources need not mention it.
    int handleSource;
    // Everything below is optional, and only the modules that need it say
    // anything: the tabbed group at the top, and the macro column down the
    // right-hand side.
    PageSet pages = everyPage;
    // How many grid rows the module covers. The macros are one tall column
    // beside two rows of modules rather than a box of their own.
    int rowSpan = 1;
    // A table reserves a strip above its rows for the column titles, so its
    // controls need carry no label each, and a gutter down the left for the
    // row numbers.
    int columnHeaderHeight = 0;
    int rowGutter = 0;
    // A larger share of the body than displayPercent, for a module whose
    // display is the thing being read rather than a picture of it. Zero means
    // the panel's shared share.
    int displayShare = 0;
};

// The numbered cards that choose which bank a module is showing, laid along its
// header after the title or the drag handle. They hang from the module's top
// edge and take the whole height of the header, so the lit strip along that
// edge runs across them and the bank you are looking at reads as a tab of the
// module rather than as a button sitting on it.
inline constexpr int bankButtonWidth = 32;
inline constexpr int bankButtonGap = 4;

// Twenty-four rather than twelve. The panel is two rows of five and four
// modules, and twelve columns cannot cut either of those into the widths the
// modules actually want — a half-column of error is an eighth of SUB.
inline constexpr int gridColumns = 24;
inline constexpr int moduleGap = 8;
inline constexpr int headerHeight = 28;

// Row weights, top to bottom. Two rows, not three: the signal path across the
// top — sources, the two oscillators, the filter — and everything that moves it
// along the bottom. The tabs swap the oscillator pair for the matrix or the
// wavetable editor without disturbing either end of the row, which is what lets
// SUB, NOISE and FILTER stay put whichever tab is open.
//
// The two rows are the same height. Nothing in either of them wants the other's
// room more than its own does, and a panel cut in half across the middle is
// read as one thing where two unequal bands are read as a main part and a
// remainder.
inline const std::vector<int>& rowWeights()
{
    static const std::vector<int> weights {50, 50};
    return weights;
}

// Share of a module's body given over to its display, unless the module asks
// for more. Set by the oscillators, which are the only modules taking it: their
// waveform is the largest thing on the panel and the one a patch is judged by.
// A knob is a label and a circle now that no value is printed under it, so a
// control row needs less of the body than it did and the display takes what it
// no longer needs.
inline constexpr int displayPercent = 55;

// The share of its body a module actually gives its display.
inline int displayShareOf(const Module& module)
{
    return module.displayShare > 0 ? module.displayShare : displayPercent;
}

// A knob never grows wider than this, however much room its module has.
inline constexpr int maxKnobWidth = 108;
inline constexpr int knobLabelHeight = 14;
inline constexpr int stepperLabelHeight = 11;
inline constexpr int stepperHeight = 21;
inline constexpr int maxStepperWidth = 122;
inline constexpr int chipHeight = 20;
inline constexpr int maxChipWidth = 44;

// A fader is read as a distance, so it takes the whole height of its cell and
// only as much width as the track and its thumb need. The label sits above it
// on the same line a knob's does, so a strip of faders and a strip of knobs
// line up across the mixer.
inline constexpr int maxFaderWidth = 34;
inline constexpr int minFaderHeight = 54;

// A table: the gutter its row numbers sit in, the strip of column titles above
// its rows, and the caps that stop a field stretching the full width of the
// panel merely because the matrix has that width to spend.
inline constexpr int rowNumberGutter = 34;
inline constexpr int columnTitleHeight = 18;
inline constexpr int tableFieldHeight = 28;
inline constexpr int maxTableFieldWidth = 280;
inline constexpr int maxTableBarWidth = 460;

// The tabs sit in the title bar, clear of the wordmark on the left and of the
// preset controls on the right.
inline constexpr int tabTop = 24;
inline constexpr int tabHeight = 28;
inline constexpr int tabWidth = 104;
inline constexpr int tabGap = 6;
inline constexpr int tabStripLeft = 252;

inline juce::Rectangle<int> tabBounds(int index)
{
    return {tabStripLeft + index * (tabWidth + tabGap), tabTop, tabWidth, tabHeight};
}

inline const std::vector<Module>& modules()
{
    static const std::vector<Module> declared {
        {"oscA", "OSC A", "MORPH", "oscAEnable", false, Display::oscillator, 0, 4, 8, false,
         {{26, {{"oscAOctave", "OCT", Style::stepper}, {"oscASemitone", "SEMI", Style::stepper},
                {"oscAFine", "FINE", Style::stepper}}},
          {74, {{"oscAPosition", "POSITION"}, {"oscAUnison", "UNISON"}, {"oscADetune", "DETUNE"},
                {"oscABlend", "BLEND"}, {"oscAPan", "PAN"}, {"oscALevel", "LEVEL"}}}},
         0, only(Page::oscillators)},
        {"oscB", "OSC B", "MORPH", "oscBEnable", false, Display::oscillator, 0, 12, 8, false,
         {{26, {{"oscBOctave", "OCT", Style::stepper}, {"oscBSemitone", "SEMI", Style::stepper},
                {"oscBFine", "FINE", Style::stepper}}},
          {74, {{"oscBPosition", "POSITION"}, {"oscBUnison", "UNISON"}, {"oscBDetune", "DETUNE"},
                {"oscBBlend", "BLEND"}, {"oscBPan", "PAN"}, {"oscBLevel", "LEVEL"}}}},
         0, only(Page::oscillators)},

        // The matrix takes the two oscillators' columns — not the whole row,
        // because SUB, NOISE and FILTER sit either side of them and stay on
        // screen whichever tab is open. It reads as a table: one row per slot,
        // numbered down the side, the amount between the source driving it and
        // the control it moves. Only the first slot names its columns, because
        // those names are drawn once in the title strip rather than above all
        // eight rows.
        {"matrix", "MATRIX", "8 SLOTS", nullptr, true, Display::none, 0, 4, 16, false,
         {{1, {{"mod1Source", "SOURCE", Style::stepper, nullptr, 2},
               {"mod1Depth", "AMOUNT", Style::bar, nullptr, 3},
               {"mod1Dest", "DESTINATION", Style::stepper, nullptr, 2}}},
          {1, {{"mod2Source", "", Style::stepper, nullptr, 2},
               {"mod2Depth", "", Style::bar, nullptr, 3},
               {"mod2Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod3Source", "", Style::stepper, nullptr, 2},
               {"mod3Depth", "", Style::bar, nullptr, 3},
               {"mod3Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod4Source", "", Style::stepper, nullptr, 2},
               {"mod4Depth", "", Style::bar, nullptr, 3},
               {"mod4Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod5Source", "", Style::stepper, nullptr, 2},
               {"mod5Depth", "", Style::bar, nullptr, 3},
               {"mod5Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod6Source", "", Style::stepper, nullptr, 2},
               {"mod6Depth", "", Style::bar, nullptr, 3},
               {"mod6Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod7Source", "", Style::stepper, nullptr, 2},
               {"mod7Depth", "", Style::bar, nullptr, 3},
               {"mod7Dest", "", Style::stepper, nullptr, 2}}},
          {1, {{"mod8Source", "", Style::stepper, nullptr, 2},
               {"mod8Depth", "", Style::bar, nullptr, 3},
               {"mod8Dest", "", Style::stepper, nullptr, 2}}}},
         0, only(Page::matrix), 1, columnTitleHeight, rowNumberGutter},

        // The wavetable editor takes the same columns as the matrix does,
        // and declares no controls at all. Nothing on it is a parameter: a table
        // is data, not a value a host can automate, so the panel is one
        // component the editor drops into this box rather than a row of knobs
        // the framework lays out. The framework needs no special case for that
        // — a module with no rows simply has nothing to place.
        {"table", "WAVETABLE", "EDITOR", nullptr, true, Display::none, 0, 4, 16, false, {},
         0, only(Page::table)},

        // SUB and NOISE stand at the left-hand end of the signal row, beside the
        // oscillators they are mixed with, and FILTER at the right-hand end,
        // where everything above it arrives.
        // Absent from MIX, where the mixer's own SUB and NOISE strips stand in
        // their place: the mixer is a second view of these two sources rather
        // than a panel that happens to sit beside them.
        {"sub", "SUB", "", "subEnable", false, Display::none, 0, 0, 2, false,
         {{100, {{"subLevel", "LEVEL"}}}},
         0, everyPageBut(Page::mix)},
        {"noise", "NOISE", "", "noiseEnable", true, Display::none, 0, 2, 2, false,
         {{100, {{"noiseLevel", "LEVEL"}}}},
         0, everyPageBut(Page::mix)},
        // Four columns for three knobs, against the oscillators' eight for six:
        // the same width per knob, so nothing in the row is drawn at a size its
        // neighbours are not.
        //
        // Built like an oscillator, and for the same reason: a display of what
        // it is doing, a row of short controls under it, then its knobs. The
        // weights below are the oscillators' own, so the two kinds of module
        // line up down the whole of the top row instead of each being centred
        // in its own height.
        //
        // The routing chips name their source the way Serum's do: A, B, S, N.
        // TYPE is given twice a chip's width, because it is the one field here
        // that spells a word out.
        {"filter", "FILTER", "", "filterEnable", false, Display::filter, 0, 20, 4, false,
         {{26, {{"filterType", "TYPE", Style::stepper, nullptr, 2}, {"routeA", "A", Style::chip},
                {"routeB", "B", Style::chip}, {"routeSub", "S", Style::chip},
                {"routeNoise", "N", Style::chip}}},
          {74, {{"cutoff", "CUTOFF"}, {"resonance", "RES"}, {"drive", "DRIVE"}}}},
         0, everyPageBut(Page::mix)},
        // GLOBAL is a narrow column rather than a wide box, and it opens the
        // lower row: what the voice is, before anything that shapes it. Five
        // controls in a row want more width than this panel has to give beside
        // the envelope and the LFO, so they stack — the two numeric ones, then
        // the two switches, then OUTPUT on its own at the foot, which is where
        // it belongs anyway. Output is applied once after the voices are
        // summed; everything above it is per-voice.
        {"global", "GLOBAL", "VOICING", nullptr, false, Display::none, 1, 0, 3, false,
         {{33, {{"polyphony", "POLY", Style::knob, "mono"}, {"glide", "GLIDE"}}},
          {33, {{"mono", "MONO", Style::rocker}, {"legato", "LEGATO", Style::rocker}}},
          {34, {{"output", "OUTPUT"}}}}},

        // Nearly two thirds of the body each for the envelope's and the LFO's
        // display, against the oscillators' rather smaller share. Both of these are drawings of something
        // happening over time, and a shape read at a glance is what the module
        // is for; four knobs under it are how you change the shape, not how you
        // read it.
        //
        // Four envelopes in one module, one shown at a time, the same
        // arrangement the LFOs use and for the same reason: four boxes of this
        // size would not fit, and the display is the point of the module. ENV 1
        // is the amplitude and the rest are sources, but they are the same
        // control set, so they are the same bank repeated.
        //
        // No detail is declared: the header says which stage the envelope
        // showing is in, and at rest what that envelope is for — which is not
        // the same answer for ENV 1 as for the three behind it. The editor
        // supplies it.
        {"env", "ENV", "", nullptr, false, Display::envelope, 1, 3, 9, false,
         {{100, {{"env1Attack", "ATTACK"}, {"env1Decay", "DECAY"},
                 {"env1Sustain", "SUSTAIN"}, {"env1Release", "RELEASE"},

                 {"env2Attack", "ATTACK"}, {"env2Decay", "DECAY"},
                 {"env2Sustain", "SUSTAIN"}, {"env2Release", "RELEASE"},

                 {"env3Attack", "ATTACK"}, {"env3Decay", "DECAY"},
                 {"env3Sustain", "SUSTAIN"}, {"env3Release", "RELEASE"},

                 {"env4Attack", "ATTACK"}, {"env4Decay", "DECAY"},
                 {"env4Sustain", "SUSTAIN"}, {"env4Release", "RELEASE"}},
           envCount}},
         static_cast<int>(ModSource::env1), everyPage, 1, 0, 0, 62},
        // Six LFOs in one module, one shown at a time, chosen by the numbered
        // buttons in the header. Six boxes side by side would not fit, and six
        // that did would each be too small to read — Serum shows its eight the
        // same way for the same reason.
        //
        // Within a bank: RATE is one knob in one place, and UNIT decides what it
        // counts in. The two parameters behind it share a cell, so only the
        // reading in charge is on screen — Hertz free-running, or a division of
        // the host's beat. MODE is the other half of the same question, whether
        // the shape answers the keyboard at all.
        //
        // Written out rather than generated: this file is read to find out what
        // the panel is, and a loop would mean reading the loop instead. The
        // layout test holds every bank to the same shape.
        {"lfo", "LFO", "SOURCE", nullptr, true, Display::lfo, 1, 12, 9, false,
         {{100, {{"lfo1Shape", "SHAPE", Style::stepper},
                 {"lfo1Mode", "MODE", Style::stepper},
                 {"lfo1RateUnit", "UNIT", Style::stepper},
                 {"lfo1Rate", "RATE", Style::knob, "lfo1RateUnit"},
                 {"lfo1Division", "RATE", Style::knob, nullptr, 1, "lfo1RateUnit", true},

                 {"lfo2Shape", "SHAPE", Style::stepper},
                 {"lfo2Mode", "MODE", Style::stepper},
                 {"lfo2RateUnit", "UNIT", Style::stepper},
                 {"lfo2Rate", "RATE", Style::knob, "lfo2RateUnit"},
                 {"lfo2Division", "RATE", Style::knob, nullptr, 1, "lfo2RateUnit", true},

                 {"lfo3Shape", "SHAPE", Style::stepper},
                 {"lfo3Mode", "MODE", Style::stepper},
                 {"lfo3RateUnit", "UNIT", Style::stepper},
                 {"lfo3Rate", "RATE", Style::knob, "lfo3RateUnit"},
                 {"lfo3Division", "RATE", Style::knob, nullptr, 1, "lfo3RateUnit", true},

                 {"lfo4Shape", "SHAPE", Style::stepper},
                 {"lfo4Mode", "MODE", Style::stepper},
                 {"lfo4RateUnit", "UNIT", Style::stepper},
                 {"lfo4Rate", "RATE", Style::knob, "lfo4RateUnit"},
                 {"lfo4Division", "RATE", Style::knob, nullptr, 1, "lfo4RateUnit", true},

                 {"lfo5Shape", "SHAPE", Style::stepper},
                 {"lfo5Mode", "MODE", Style::stepper},
                 {"lfo5RateUnit", "UNIT", Style::stepper},
                 {"lfo5Rate", "RATE", Style::knob, "lfo5RateUnit"},
                 {"lfo5Division", "RATE", Style::knob, nullptr, 1, "lfo5RateUnit", true},

                 {"lfo6Shape", "SHAPE", Style::stepper},
                 {"lfo6Mode", "MODE", Style::stepper},
                 {"lfo6RateUnit", "UNIT", Style::stepper},
                 {"lfo6Rate", "RATE", Style::knob, "lfo6RateUnit"},
                 {"lfo6Division", "RATE", Style::knob, nullptr, 1, "lfo6RateUnit", true}},
           lfoCount}},
         static_cast<int>(ModSource::lfo1), everyPage, 1, 0, 0, 62},

        // --- The mixer -------------------------------------------------------
        //
        // Eight channels across the whole signal row, three grid columns each.
        // The MIX tab is the only one that takes the row entire: the mixer is
        // the view of SUB, NOISE and the filter, so those three modules stand
        // down rather than sitting beside strips of themselves.
        //
        // The order is the signal path read left to right, the same order the
        // OSC tab is in, and it ends where the signal does: the two busses the
        // sends arrive at, then the main output.
        //
        // A strip declares the same four rows whatever it carries, so TO sits
        // on one line across the mixer, the sends on the next, the pans on the
        // third and the faders along the foot. A channel with nothing to put in
        // a row leaves the row out and the rows below it keep their weights, so
        // its fader still lands on the same line as every other.
        //
        // Every strip is compact: its knobs are sized to its own cells and held
        // under the diameter the rest of the panel shares. Without that, eight
        // narrow channels would be the tightest cells on the panel and every
        // knob in Forge would shrink to match them.
        //
        // The header enable is the source's own — switching OSC A off here is
        // switching it off, exactly as Serum's mixer header does, which is also
        // what gives every channel the mute it has no separate control for.
        {"mixSub", "SUB", "", "subEnable", false, Display::none, 0, 0, 3, true,
         {{16, {{"routeSub", "TO", Style::stepper}}},
          {26, {{"subSend1", "BUS 1"}, {"subSend2", "BUS 2"}}},
          {26, {{"subPan", "PAN"}}},
          {32, {{"subLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixOscA", "OSC A", "", "oscAEnable", false, Display::none, 0, 3, 3, true,
         {{16, {{"routeA", "TO", Style::stepper}}},
          {26, {{"oscASend1", "BUS 1"}, {"oscASend2", "BUS 2"}}},
          {26, {{"oscAPan", "PAN"}}},
          {32, {{"oscALevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixOscB", "OSC B", "", "oscBEnable", false, Display::none, 0, 6, 3, true,
         {{16, {{"routeB", "TO", Style::stepper}}},
          {26, {{"oscBSend1", "BUS 1"}, {"oscBSend2", "BUS 2"}}},
          {26, {{"oscBPan", "PAN"}}},
          {32, {{"oscBLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixNoise", "NOISE", "", "noiseEnable", true, Display::none, 0, 9, 3, true,
         {{16, {{"routeNoise", "TO", Style::stepper}}},
          {26, {{"noiseSend1", "BUS 1"}, {"noiseSend2", "BUS 2"}}},
          {26, {{"noisePan", "PAN"}}},
          {32, {{"noiseLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // The filter's own channel. It has no TO field, because everything the
        // filter passes goes to the main output and the only other place it
        // could go is a bus, which the sends already reach. MIX shares the pan
        // row instead, where TYPE would have been.
        {"mixFilter", "FILTER", "", "filterEnable", false, Display::none, 0, 12, 3, true,
         {{16, {}},
          {26, {{"filterSend1", "BUS 1"}, {"filterSend2", "BUS 2"}}},
          {26, {{"filterPan", "PAN"}, {"filterMix", "MIX"}}},
          {32, {{"filterLevel", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // The two busses. Their sends row is empty: a bus receives sends, it
        // does not carry them, and where it goes afterwards is the TO field.
        {"mixBus1", "BUS 1", "", "bus1Enable", true, Display::none, 0, 15, 3, true,
         {{16, {{"bus1Dest", "TO", Style::stepper}}},
          {26, {}},
          {26, {{"bus1Pan", "PAN"}}},
          {32, {{"bus1Level", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        {"mixBus2", "BUS 2", "", "bus2Enable", true, Display::none, 0, 18, 3, true,
         {{16, {{"bus2Dest", "TO", Style::stepper}}},
          {26, {}},
          {26, {{"bus2Pan", "PAN"}}},
          {32, {{"bus2Level", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},
        // Where everything arrives. It is the OUTPUT knob from GLOBAL, drawn as
        // the fader at the end of the row: one setting, and the mixer is where
        // a final level is actually read against the channels feeding it.
        {"mixMain", "MAIN", "", nullptr, false, Display::none, 0, 21, 3, true,
         {{16, {}},
          {26, {}},
          {26, {}},
          {32, {{"output", "LEVEL", Style::fader}}}},
         0, only(Page::mix)},

        // The macros close the lower row, two across and four down in the
        // narrowest column the panel has: they are deliberately smaller than
        // the controls they drive, as Serum's are. They are sources only — a
        // macro reaches a control through a slot or not at all.
        {"macros", "MACROS", "SOURCES", nullptr, false, Display::none, 1, 21, 3, true,
         {{25, {{"macro1", "1"}, {"macro2", "2"}}},
          {25, {{"macro3", "3"}, {"macro4", "4"}}},
          {25, {{"macro5", "5"}, {"macro6", "6"}}},
          {25, {{"macro7", "7"}, {"macro8", "8"}}}}},
    };
    return declared;
}

// How many banks a module's controls are declared in. One unless it says
// otherwise, and the rows all have to agree — the layout test holds them to it.
inline int bankCount(const Module& module)
{
    return module.rows.empty() ? 1 : juce::jmax(1, module.rows.front().banks);
}

// One bank button in a module's header. They start after whatever the header
// already carries on the left: the enable LED, and the drag handle of a module
// that is itself a source.
inline juce::Rectangle<int> bankButtonBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                             int bank, int handleWidth)
{
    auto left = moduleArea.getX() + 10;
    if (module.enableId != nullptr) left += headerHeight;
    if (module.handleSource != 0) left += handleWidth + 8;
    return {left + bank * (bankButtonWidth + bankButtonGap), moduleArea.getY(),
            bankButtonWidth, headerHeight};
}

// Whether a module is shown while the given tab is chosen.
inline bool onPage(const Module& module, Page page)
{
    return (module.pages & only(page)) != 0;
}

// Two modules can only collide if some tab shows both of them at once, which
// is exactly the two sets of pages overlapping.
inline bool sharePage(const Module& a, const Module& b)
{
    return (a.pages & b.pages) != 0;
}

// --- What the window may be ---------------------------------------------------
//
// The panel is laid out by weight rather than at fixed sizes, so these are the
// range over which that has been checked rather than sizes anything is designed
// against. The layout test sweeps the whole of it.
inline constexpr int minPanelWidth = 1180;
inline constexpr int minPanelHeight = 820;
inline constexpr int maxPanelWidth = 2000;
inline constexpr int maxPanelHeight = 1180;

// Forge is a wide panel, and the limits say so: at every size it allows it is
// broader than it is tall. Two rows of modules over a keyboard is a shape that
// wants width, and height is what a plugin window inside a host has least of.
inline constexpr int defaultPanelWidth = 1440;
inline constexpr int defaultPanelHeight = 900;

inline constexpr int keyboardHeight = 80;

// The inset every edge of the panel shares: the wordmark, the preset controls,
// the modules and the keyboard all start here, so they line up down both sides.
// It is deliberately small, so the modules get the width instead of the frame.
inline constexpr int windowMargin = 12;

// Measured from the top of the window rather than from the margin, so the
// header keeps its own spacing when the margin changes.
inline constexpr int titleBarHeight = 92;
inline constexpr int keyboardGap = 26;

// The keyboard sits across the bottom, under everything.
inline juce::Rectangle<int> keyboardBounds(juce::Rectangle<int> bounds)
{
    return bounds.reduced(windowMargin, 0)
        .withTop(bounds.getBottom() - keyboardGap - keyboardHeight)
        .withHeight(keyboardHeight);
}

// The area the modules are laid out inside: below the title bar, above the
// keyboard.
inline juce::Rectangle<int> contentBounds(juce::Rectangle<int> bounds)
{
    return bounds.reduced(windowMargin)
        .withTrimmedTop(titleBarHeight - windowMargin)
        .withBottom(bounds.getBottom() - keyboardGap - keyboardHeight - 12);
}

inline juce::Rectangle<int> moduleBounds(juce::Rectangle<int> bounds, const Module& module)
{
    const auto content = contentBounds(bounds);
    const auto& weights = rowWeights();
    auto total = 0;
    for (const auto weight : weights) total += weight;
    const auto rows = static_cast<int>(weights.size());
    const auto available = content.getHeight() - moduleGap * (rows - 1);

    auto y = content.getY();
    for (int row = 0; row < module.row; ++row)
        y += available * weights[static_cast<size_t>(row)] / total + moduleGap;
    // A module spanning grid rows swallows the gaps between them, so the macro
    // column runs unbroken past the two rows of modules it stands beside.
    auto height = 0;
    for (int row = module.row; row < module.row + module.rowSpan; ++row)
    {
        if (row > module.row) height += moduleGap;
        height += available * weights[static_cast<size_t>(row)] / total;
    }

    const auto columnWidth = content.getWidth() / gridColumns;
    const auto x = content.getX() + module.column * columnWidth;
    // The last column in a row absorbs the integer-division remainder so the
    // right edge lines up with the content instead of drifting inward.
    const auto reachesRightEdge = module.column + module.columnSpan == gridColumns;
    const auto width = reachesRightEdge ? content.getRight() - x : module.columnSpan * columnWidth;
    return juce::Rectangle<int>(x, y, width, height).reduced(moduleGap / 2, 0);
}

// Everything inside a module below its header and its display: what the column
// titles, the row gutter and the control rows divide between them.
inline juce::Rectangle<int> moduleBody(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleArea.withTrimmedTop(headerHeight).reduced(10, 6);
    if (module.display != Display::none)
    {
        body.removeFromTop(body.getHeight() * displayShareOf(module) / 100);
        body.removeFromTop(4);
    }
    return body;
}

// The strip a module's display occupies, or an empty rectangle when it has none.
inline juce::Rectangle<int> displayBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    if (module.display == Display::none) return {};
    auto body = moduleArea.withTrimmedTop(headerHeight).reduced(10, 6);
    return body.removeFromTop(body.getHeight() * displayShareOf(module) / 100);
}

// The parameter an oscillator's display draws: the first knob of the module,
// which is POSITION by declaration in both oscillators.
inline const char* displaySourceId(const Module& module)
{
    for (const auto& row : module.rows)
        for (const auto& control : row.controls)
            if (control.style == Style::knob) return control.id;
    return nullptr;
}

// The strip carrying a table's column titles, or an empty rectangle when the
// module is not a table. It spans the gutter too, so the row numbers get a
// heading of their own.
inline juce::Rectangle<int> columnTitleBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    if (module.columnHeaderHeight <= 0) return {};
    return moduleBody(moduleArea, module).withHeight(module.columnHeaderHeight);
}

// Everything below the header, the display and the column titles, and right of
// the row gutter: the area the control rows share.
inline juce::Rectangle<int> controlArea(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleBody(moduleArea, module);
    body.removeFromTop(module.columnHeaderHeight);
    body.removeFromLeft(module.rowGutter);
    return body;
}

inline juce::Rectangle<int> rowBounds(juce::Rectangle<int> moduleArea, const Module& module, int rowIndex)
{
    const auto area = controlArea(moduleArea, module);
    auto total = 0;
    for (const auto& row : module.rows) total += row.weight;
    if (total <= 0) return area;

    auto y = area.getY();
    for (int i = 0; i < rowIndex; ++i)
        y += area.getHeight() * module.rows[static_cast<size_t>(i)].weight / total;
    const auto height = area.getHeight() * module.rows[static_cast<size_t>(rowIndex)].weight / total;
    return {area.getX(), y, area.getWidth(), height};
}

// The strip to the left of a table row, where its number is drawn.
inline juce::Rectangle<int> rowGutterBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                            int rowIndex)
{
    if (module.rowGutter <= 0) return {};
    const auto row = rowBounds(moduleArea, module, rowIndex);
    return {row.getX() - module.rowGutter, row.getY(), module.rowGutter, row.getHeight()};
}

// How many controls one bank of a row declares. Every bank declares the same
// ones, so this is the length of the row divided between them.
//
// A row can be empty: a mixer strip declares the same four rows as the strips
// beside it so their faders land on one line, and leaves out whatever it has
// nothing to put in. Such a row has no controls per bank rather than one,
// which is also what keeps rowHasKnobs from reading past the end of it.
inline int controlsPerBank(const Row& row)
{
    if (row.controls.empty()) return 0;
    const auto banks = juce::jmax(1, row.banks);
    return juce::jmax(1, static_cast<int>(row.controls.size()) / banks);
}

// Which bank a control belongs to, counting from zero. Always zero in a row
// that declares none.
inline int bankOf(const Module& module, int rowIndex, int index)
{
    return index / controlsPerBank(module.rows[static_cast<size_t>(rowIndex)]);
}

// The control whose cell this one occupies: itself, unless it shares the cell
// of the control before it, or it belongs to a bank behind the first — in which
// case it stands exactly where its opposite number in the first bank does.
inline int cellOwner(const Module& module, int rowIndex, int index)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = row.controls;
    // A control never shares a cell across a bank boundary: the control before
    // the first of a bank belongs to the bank in front of it.
    const auto first = (index / controlsPerBank(row)) * controlsPerBank(row);
    while (index > first && controls[static_cast<size_t>(index)].sharesCell) --index;
    return index;
}

// Whether this control's cell holds more than one control — either because it
// says it shares the one before it, or because the one after it shares this.
inline bool inSharedCell(const Module& module, int rowIndex, int index)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = row.controls;
    if (controls[static_cast<size_t>(index)].sharesCell) return true;
    const auto next = index + 1;
    // The control after the last of a bank belongs to the next bank, so it
    // cannot be sharing this one's cell.
    if (next % controlsPerBank(row) == 0) return false;
    return next < static_cast<int>(controls.size()) && controls[static_cast<size_t>(next)].sharesCell;
}

// A row is divided between its cells, not between its controls: a control that
// shares the cell before it takes no width of its own and lands exactly on top
// of the one it shares with.
inline juce::Rectangle<int> cellBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto row = rowBounds(moduleArea, module, rowIndex);
    const auto& declared = module.rows[static_cast<size_t>(rowIndex)];
    const auto& controls = declared.controls;
    // Divided between one bank's cells. The banks behind it declare the same
    // cells and land on top of them.
    const auto perBank = controlsPerBank(declared);
    auto total = 0;
    for (int i = 0; i < perBank; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            total += juce::jmax(1, controls[static_cast<size_t>(i)].weight);
    if (total <= 0) return row;

    // Reduced to its own bank, because every bank stands on the same cells.
    const auto owner = cellOwner(module, rowIndex, index) % perBank;
    auto x = row.getX();
    for (int i = 0; i < owner; ++i)
        if (!controls[static_cast<size_t>(i)].sharesCell)
            x += row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(i)].weight) / total;
    const auto width = row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(owner)].weight) / total;
    return {x, row.getY(), width, row.getHeight()};
}

// Every knob on the panel is drawn at one diameter, taken from whichever cell
// on the panel is tightest in either direction. Sizing each knob to its own
// module instead makes SUB's single knob several times the diameter of one in
// GLOBAL, which reads as a mistake rather than as emphasis.
inline int uniformKnobDiameter(juce::Rectangle<int> bounds)
{
    auto smallest = static_cast<int>(maxKnobWidth);
    for (const auto& module : modules())
    {
        const auto area = moduleBounds(bounds, module);
        for (int r = 0; r < static_cast<int>(module.rows.size()); ++r)
        {
            const auto& row = module.rows[static_cast<size_t>(r)];
            if (module.compactKnobs) continue;
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                if (row.controls[static_cast<size_t>(i)].style != Style::knob) continue;
                const auto cell = cellBounds(area, module, r, i);
                // The cell also has to hold the label above and the readout below.
                smallest = juce::jmin(smallest, cell.getWidth() - 6,
                                      cell.getHeight() - knobLabelHeight);
            }
        }
    }
    return juce::jmax(40, smallest);
}

// The label-plus-knob block, centred in its cell at the one size the whole panel
// shares.
//
// No value is printed under a knob. A readout costs every knob on the panel a
// line of height whether or not anyone is reading it, and it was the readout
// rather than the knob that set how small a macro could be drawn. The value is
// shown instead where the hand already is, in a bubble beside the knob being
// turned.
inline juce::Rectangle<int> knobBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index, int diameter)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    juce::ignoreUnused(module);
    return juce::Rectangle<int>(diameter, knobLabelHeight + diameter).withCentre(cell.getCentre());
}

// The circle inside a knob's block, which is everything below its label.
inline int knobDiameterOf(juce::Rectangle<int> block)
{
    return block.getHeight() - knobLabelHeight;
}

// A stepper is a fixed-height numeric field, so it does not scale with the
// window the way a knob does. Inside a table it carries no label of its own —
// the column titles say what it is — so it takes the height of its row instead
// of sharing that height with a label strip.
inline juce::Rectangle<int> stepperBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    if (module.columnHeaderHeight > 0)
        return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 12, maxTableFieldWidth),
                                    juce::jmin(cell.getHeight() - 6, tableFieldHeight))
            .withCentre(cell.getCentre());
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 8, maxStepperWidth),
                                juce::jmin(cell.getHeight(), stepperLabelHeight + stepperHeight))
        .withCentre(cell.getCentre());
}

// A bar is given more room than the fields beside it, because it is read as a
// distance rather than as a number.
inline juce::Rectangle<int> barBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                     int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 12, maxTableBarWidth),
                                juce::jmin(cell.getHeight() - 6, tableFieldHeight))
        .withCentre(cell.getCentre());
}

// A fader takes its cell's full height, less the label line above it. Unlike a
// knob it is not square and does not follow the panel's shared diameter: a
// mixer channel is balanced by how far the thumb has travelled, and that
// distance is worth every pixel of the cell.
inline juce::Rectangle<int> faderBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(juce::jmax(10, cell.getWidth() - 6), maxFaderWidth),
                                juce::jmax(1, cell.getHeight() - 2))
        .withCentre(cell.getCentre());
}

// A chip carries its own label, so unlike a knob or a stepper it needs no
// separate label strip above it.
inline juce::Rectangle<int> chipBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 8, maxChipWidth),
                                juce::jmin(cell.getHeight(), chipHeight))
        .withCentre(cell.getCentre());
}

// A compact module's knobs are sized to their own cell, and then held below the
// diameter the rest of the panel shares. The macro column is tall enough that
// sizing to the cell alone would make the macros the largest knobs in Forge,
// which is backwards: they drive the other controls, they do not outrank them.
inline constexpr int compactKnobPercent = 80;

// A compact module is the only one that stacks knobs several deep, so it is the
// only one that needs a gap between one block and the next: a knob sized to
// exactly its cell puts its readout against the label of the row below, which
// reads as a collision rather than as a column.
inline constexpr int compactKnobGap = 8;

inline int knobDiameterFor(const Module& module, juce::Rectangle<int> moduleArea,
                           int rowIndex, int index, int shared)
{
    if (!module.compactKnobs) return shared;
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::jmax(24, juce::jmin(cell.getWidth() - 6,
                                     cell.getHeight() - knobLabelHeight - compactKnobGap,
                                     shared * compactKnobPercent / 100));
}

// Whether a row mixes short controls in among full-height knobs. A row of like
// controls centres them all; a mixed row has a label line to line up with.
inline bool rowHasKnobs(const Module& module, int rowIndex)
{
    const auto& row = module.rows[static_cast<size_t>(rowIndex)];
    const auto perBank = controlsPerBank(row);
    for (int i = 0; i < perBank; ++i)
    {
        const auto style = row.controls[static_cast<size_t>(i)].style;
        if (style == Style::knob || style == Style::rocker) return true;
    }
    return false;
}

inline juce::Rectangle<int> controlBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index, int diameter)
{
    switch (module.rows[static_cast<size_t>(rowIndex)].controls[static_cast<size_t>(index)].style)
    {
        case Style::stepper:
        {
            auto block = stepperBlock(moduleArea, module, rowIndex, index);
            // Beside knobs, a stepper puts its label on their label line rather
            // than floating in the middle of its cell, which is what made LFO
            // 1's SHAPE and DIV sit lower than the SYNC and RATE beside them.
            if (module.columnHeaderHeight == 0 && rowHasKnobs(module, rowIndex))
            {
                const auto knob = knobBlock(moduleArea, module, rowIndex, index, diameter);
                block.setY(knob.getY() + knobLabelHeight - stepperLabelHeight);
            }
            return block;
        }
        case Style::bar:     return barBlock(moduleArea, module, rowIndex, index);
        case Style::chip:    return chipBlock(moduleArea, module, rowIndex, index);
        case Style::fader:   return faderBlock(moduleArea, module, rowIndex, index);
        // A rocker takes a knob's whole block so its label and readout sit on
        // the same lines as the knobs either side of it.
        case Style::rocker:
        case Style::knob: break;
    }
    return knobBlock(moduleArea, module, rowIndex, index,
                     knobDiameterFor(module, moduleArea, rowIndex, index, diameter));
}

// The rocker inside that block: narrow and tall, centred, and sitting exactly
// where a knob's circle sits. The 4px inset is the same one the knob's look
// applies, so the gap under the label is identical for both.
inline constexpr int knobOpticalInset = 4;

inline juce::Rectangle<int> rockerBounds(juce::Rectangle<int> knobArea)
{
    const auto area = knobArea.reduced(0, knobOpticalInset);
    const auto width = juce::jmax(14, juce::roundToInt(area.getHeight() * 0.46f));
    return juce::Rectangle<int>(width, area.getHeight()).withCentre(area.getCentre());
}
}
