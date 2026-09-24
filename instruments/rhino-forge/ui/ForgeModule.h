#pragma once

#include "../core/ForgeCore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// What a module is made of, and how big each part of it is drawn.
//
// A Control names a parameter and says which of nine looks it wears; a Row is
// a line of them, optionally repeated as banks; a Module is rows in a box with
// a header, an enable and the set of tabs it appears on. None of it is a
// component and none of it is a rectangle yet — ForgePlacement.h turns it into
// one and ForgeVisuals.h draws it.
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
// and the only control that is taller than it is wide. A plate is a rack slot's
// name board: it carries the effect's mark, its name and its colour, and
// clicking it is how the slot is filled — so a slot's identity and its one
// structural choice are one object rather than a badge beside a field.
// A selector is a choice whose presentation follows how many choices there are:
// two or three are drawn as a switch with every state on screen, more as a name
// between two arrows. The cell is declared here; which of the two it becomes is
// the effect's business, because that is what knows how many there are.
// A wave is a grid of shapes drawn as themselves, for a choice whose options
// are pictures rather than words: the sub's six waveforms are recognised faster
// as six icons than read as six names, and they carry no label of their own for
// the same reason a chip does not.
enum class Style { knob, stepper, chip, rocker, bar, fader, plate, selector, wave };

// Whether a control of this style draws the modulation reaching it: a knob
// wears the ring outside its rim, a bar-style field the strip along its foot.
// Nothing else draws any of it, and that is what decides where a dragged source
// may be dropped — landing on a control that could not show the routing
// afterwards would read as the drag having failed.
//
// A fader is the one destination this leaves out. The matrix still reaches a
// channel's level from its own table; it is only the drop that stops at the
// mixer, until a fader has somewhere to put a reach.
inline constexpr bool showsModulation(Style style)
{
    return style == Style::knob || style == Style::stepper || style == Style::bar;
}

// The tabs. A page is one bit, so which tabs show a module is a set rather
// than a single answer — which is what the mixer needs: it takes the whole
// signal row, so SUB, NOISE and FILTER have to be absent from that one tab
// while staying put on the other three.
// The five tabs, and the arp. The arp is a page without being a tab: it is an
// overlay that stands on the modules it covers while everything else keeps its
// place, so it is never what `page` is set to and never appears in the title
// bar. It is in this enum because "which modules does this show" is the same
// question for it as for a tab, and answering it the same way is what lets the
// no-two-modules-overlap check go on being true — the arp shares a page with
// nothing, so nothing it covers is ever declared as showing beside it.
enum class Page { oscillators = 1, table = 2, matrix = 4, mix = 8, fx = 16, arp = 32 };

inline constexpr Page tabPages[] {Page::oscillators, Page::table, Page::matrix, Page::mix, Page::fx};
inline constexpr int tabCount = 5;

// Which tabs show a module. Not a Page: a module is on one page, or on all of
// them, or on all but one, and only a set says all three.
using PageSet = int;

inline constexpr PageSet only(Page page) { return static_cast<PageSet>(page); }

// Every tab — which is deliberately not every page. The arp is left out, so a
// module declaring `everyPage` stays on all five tabs and is still something
// the arp overlay may cover.
inline constexpr PageSet everyPage =
    only(Page::oscillators) | only(Page::table) | only(Page::matrix) | only(Page::mix) | only(Page::fx);

// Shown wherever a module is not standing in its place. Two tabs take the whole
// signal row — the mixer, which is the view of the sub, the noise and the
// filter, and the rack — so those three name both here rather than being
// hidden by something reaching over them.
inline constexpr PageSet everyPageBut(Page page) { return everyPage & ~only(page); }

inline constexpr PageSet everyPageBut(Page first, Page second)
{
    return everyPage & ~only(first) & ~only(second);
}

inline const char* pageName(Page page)
{
    switch (page)
    {
        case Page::matrix:      return "MATRIX";
        case Page::oscillators: return "OSC";
        case Page::table:       return "TABLE";
        case Page::mix:         return "MIX";
        case Page::fx:          return "FX";
        case Page::arp:         return "ARP";
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

// Where a row is laid out. Almost every row sits in the module's body, under
// the display and above the plate's legend. A seated row sits *inside* the
// display instead, along its top edge or its foot.
//
// The filter's two are the reason this exists. Which filter it is and what is
// routed into it are readings of the display rather than settings beside it —
// the curve on screen is that type, plotted for those sources — and a row of
// the body each is two rows the knobs do not get. Seated, they cost the plot a
// strip and the body nothing.
//
// A seated row's weight is its height in pixels, not a share of anything: what
// it holds is text at a size that does not scale, so neither does the room it
// needs. Everything else about it is ordinary — the same cells, the same
// blocks, the same components, the same attachments.
enum class Seat { body, displayTop, displayFoot };

struct Row
{
    // Share of the module's control area, against the module's other rows —
    // or, in a row seated inside the display, its height in pixels.
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
    // A strip of the row given over to a display, measured in the same weights
    // the cells are divided by, and how many cells stand in front of it.
    //
    // A module's own display sits above its controls and there is one of it;
    // a rack has several slots in one module and each wants its own, so this is
    // per row rather than per module. Zero means the row is all controls,
    // which every row but the rack's is.
    int displayWeight = 0;
    int displayAfter = 0;
    // Inside the module's display rather than under it. Last, so the rows that
    // are not seated need not mention it.
    Seat seat = Seat::body;
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
    // Wider bank cards, for a module whose banks have names rather than
    // numbers: the rack's three are MAIN, BUS 1 and BUS 2. The four envelopes
    // and the six LFOs are genuinely numbered and keep the narrow card.
    int bankWidth = 0;
    // What the legend along the plate's foot calls this module, where that is
    // not simply its title. The header has room for OSC A and the foot has room
    // for OSCILLATOR A, and a plate marked with the long name is what makes the
    // short one in the header read as an abbreviation rather than as the name.
    // Null means the title serves for both.
    const char* plateName = nullptr;
    // Modules sharing a group are drawn inside one plate: the group carries the
    // outer plate, its legend and its part number, and each module in it gets a
    // shallower inner panel instead of a plate of its own. The string is both
    // the key and what is stamped on the plate's foot, so a group needs no
    // declaration anywhere else.
    //
    // SUB and NOISE are one piece of hardware with two channels on it. Drawn as
    // two separate plates they read as two, which is the whole reason this
    // exists.
    const char* group = nullptr;
};

// The numbered cards that choose which bank a module is showing, laid along its
// header after the title or the drag handle. They hang from the module's top
// edge and take the whole height of the header, so the lit strip along that
// edge runs across them and the bank you are looking at reads as a tab of the
// module rather than as a button sitting on it.
inline constexpr int bankButtonWidth = 32;
inline constexpr int bankButtonGap = 4;

// How wide one module's bank cards are: its own width when it asks for one, and
// the numbered default otherwise.
inline int bankWidthOf(const Module& module)
{
    return module.bankWidth > 0 ? module.bankWidth : bankButtonWidth;
}

// Twenty-four rather than twelve. The panel is two rows of five and four
// modules, and twelve columns cannot cut either of those into the widths the
// modules actually want — a half-column of error is an eighth of SUB.
inline constexpr int gridColumns = 24;
inline constexpr int moduleGap = 8;
inline constexpr int headerHeight = 34;

// The strip along a plate's foot carrying its stamped legend: the module's
// long name on the left and its part number on the right, the way a piece of
// equipment is marked rather than labelled. It is reserved out of the module's
// interior rather than drawn over it, so no control can ever land on top of
// it, and it costs every module the same few pixels at every window size.
inline constexpr int plateFooterHeight = 20;

// Row weights, top to bottom. Two rows, not three: the signal path across the
// top — sources, the two oscillators, the filter — and everything that moves it
// along the bottom. The tabs swap the oscillator pair for the matrix or the
// wavetable editor without disturbing either end of the row, which is what lets
// SUB, NOISE and FILTER stay put whichever tab is open.
//
// The signal row has the taller housings in the reference; the modulation row
// keeps enough height for its displays and controls at the minimum window size.
inline const std::vector<int>& rowWeights()
{
    static const std::vector<int> weights {56, 44};
    return weights;
}

// Share of a module's body given over to its display, unless the module asks
// for more. Set by the oscillators, which are the only modules taking it: their
// waveform is the largest thing on the panel and the one a patch is judged by.
// A knob is a label and a circle now that no value is printed under it, so a
// control row needs less of the body than it did and the display takes what it
// no longer needs.
inline constexpr int displayPercent = 55;

// What the filter gives its own display, which is the largest share any module
// takes. Everything the module is set by except its six knobs is seated inside
// that display, so the share is buying the selector and the routing strip as
// well as the curve; what is left over is two knob rows of 68 pixels at the
// default window, which is exactly the height the panel's shared diameter is
// already set at by the oscillators.
inline constexpr int filterDisplayPercent = 55;

// What an oscillator gives its own display, which is less than the share it
// used to take. The warp row had to come from somewhere, and the choice was
// between a smaller picture and smaller knobs: the picture is still the
// largest thing on the panel at this share, and the knobs still come down from
// 63 pixels to 52 at the size the panel opens at, which is where the third row
// is actually paid for.
inline constexpr int oscillatorDisplayPercent = 44;

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

// The two strips a display seats a row in, and the room around them. The
// selector is taller than a chip because it is the one field on the module
// read at a glance rather than looked for, and it carries two arrows.
//
// The inset is one pixel, off the display's own inner edge: a seated control
// is part of the instrument's face, not a button lying on it, so it meets the
// frame rather than floating inside it. The gap is what separates a strip from
// the plot, and it is the only slack in the display's height.
inline constexpr int displaySelectorHeight = 26;
inline constexpr int displayButtonHeight = chipHeight;
inline constexpr int displaySeatInset = 1;
inline constexpr int displaySeatGap = 3;

// A fader is read as a distance, so it takes the whole height of its cell and
// only as much width as the track and its thumb need. The label sits above it
// on the same line a knob's does, so a strip of faders and a strip of knobs
// line up across the mixer.
inline constexpr int maxFaderWidth = 34;
inline constexpr int minFaderHeight = 54;

// A wave grid is read as pictures, so it wants room; but it is a picker, not a
// display, and left uncapped it would be the largest thing in the top row at a
// wide window. These are the sizes past which more room stops making a shape
// easier to recognise.
inline constexpr int maxWaveGridWidth = 132;
inline constexpr int maxWaveGridHeight = 168;

// A plate is a board rather than a field, so it takes the height of its row
// instead of a control's fixed height — and it carries its own name, so unlike
// a knob or a stepper it needs no label strip above it.
inline constexpr int maxPlateWidth = 200;
inline constexpr int maxPlateHeight = 56;

// A selector stacks its choices, so it is as tall as the most any mode field
// offers inline — three — and no wider than the longest of their names needs.
// A field of two gives each of them half of this rather than leaving a gap, and
// one with too many to stack draws a single line centred in it.
inline constexpr int selectorSegmentHeight = 18;
inline constexpr int selectorHeight = selectorSegmentHeight * 3;
inline constexpr int maxSelectorWidth = 124;

// A table: the gutter its row numbers sit in, the strip of column titles above
// its rows, and the caps that stop a field stretching the full width of the
// panel merely because the matrix has that width to spend.
// Wide enough for "BUS 1" rather than for a digit.
inline constexpr int fxBankWidth = 56;

// The share of a rack row given over to that slot's display. Three cells stand
// in front of it — the name plate and the two mode fields — so it lands in the
// same column down every slot, with the knobs to its right.
inline constexpr int fxDisplayWeight = 3;

// The FX page has a second reading of the slots down its left edge. At
// full width it names every effect and exposes its bypass/remove actions; when
// folded it becomes a narrow strip of the same marks. The rack controls
// keep the rest of the module, so folding the list gives dense patches their
// room back without taking the overview away altogether.
inline constexpr int fxListOpenWidth = 198;
inline constexpr int fxListFoldedWidth = 54;
inline constexpr int fxListGap = 8;
inline constexpr int fxViewButtonSize = 22;
inline constexpr int fxViewButtonGap = 5;
// A slot keeps the same physical height in the compact and expanded rack. The
// viewport decides how many fit; it never stretches four effects into whatever
// height happens to be available.
inline constexpr int fxSlotHeight = 76;
// A list row is one line of text tall, not a slot tall: the list is read down
// by name, and a full rack of eight fits beside the first two strips instead of
// beside all eight.
inline constexpr int fxListRowHeight = 26;
// The add action is furniture, not a rack slot: it stays pinned above the
// scrolling chain and empty space begins immediately below it.
inline constexpr int fxListAddHeight = 28;
// The strip down the rack's right edge its scroll thumb runs in.
inline constexpr int fxRackScrollGutter = 8;

inline constexpr int rowNumberGutter = 34;
inline constexpr int columnTitleHeight = 18;
inline constexpr int tableFieldHeight = 28;
inline constexpr int maxTableFieldWidth = 280;
inline constexpr int maxTableBarWidth = 460;

// The tabs sit in the title bar, clear of the wordmark on the left and of the
// preset controls on the right.
inline constexpr int tabTop = 42;
inline constexpr int tabHeight = 30;
inline constexpr int tabGap = 6;
inline int headerSplit(int width) { return width * 615 / 1000; }

inline juce::Rectangle<int> tabBounds(int index, int panelWidth = 1440)
{
    const auto left = panelWidth * 248 / 1000;
    const auto width = (headerSplit(panelWidth) - left - 30 - (tabCount - 1) * tabGap) / tabCount;
    return {left + index * (width + tabGap), tabTop, width, tabHeight};
}

// Slots five onward repeat the exact host-facing controls of the original
// four. Keep those legacy declarations readable below, and generate only the
// extension so increasing the rack does not require copying thirty-six ids per
// slot by hand. A deque owns the generated text because Control deliberately
// stores light-weight string pointers.
}
