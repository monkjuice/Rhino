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
namespace theta::forge::ui
{
using theta::forge::ModSource;

enum class Display { none, oscillator, envelope, lfo };

// A knob is the default. A stepper is the compact field used where reading an
// exact value matters more than sweeping a range: tuning, filter type. A chip
// is a small on/off button, used for the filter's per-source routing. A rocker
// is a two-state switch that occupies a knob's footprint, so it lines up with
// the knobs beside it. A bar is a horizontal fill drawn from the middle of its
// range, for a signed amount read across a table row.
enum class Style { knob, stepper, chip, rocker, bar };

// Only the top row of the panel changes with the tabs. Everything a module
// declares as "always" stays put whichever tab is showing, which is what keeps
// the filter, the envelope, the LFO and the macros reachable while the matrix
// is open.
enum class Page { always, oscillators, matrix };

inline constexpr Page tabPages[] {Page::oscillators, Page::matrix};
inline constexpr int tabCount = 2;

inline const char* pageName(Page page)
{
    switch (page)
    {
        case Page::matrix:      return "MATRIX";
        case Page::oscillators: return "OSC";
        case Page::always:      break;
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
};

struct Row
{
    // Share of the module's control area, against the module's other rows.
    int weight;
    std::vector<Control> controls;
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
    // Everything below is optional, and only the three modules that need it
    // say anything: the tabbed pair at the top, and the macro column down the
    // right-hand side.
    Page page = Page::always;
    // How many grid rows the module covers. The macros are one tall column
    // beside two rows of modules rather than a box of their own.
    int rowSpan = 1;
    // A table reserves a strip above its rows for the column titles, so its
    // controls need carry no label each, and a gutter down the left for the
    // row numbers.
    int columnHeaderHeight = 0;
    int rowGutter = 0;
};

inline constexpr int gridColumns = 12;
inline constexpr int moduleGap = 8;
inline constexpr int headerHeight = 22;

// Row weights, top to bottom: the tabbed row, sources and filter and voicing,
// then envelope and LFO. The tabbed row is much the tallest because it carries
// either two oscillators with a display each or the whole modulation matrix.
inline const std::vector<int>& rowWeights()
{
    static const std::vector<int> weights {38, 26, 28};
    return weights;
}

// Share of a module's body given over to its display.
inline constexpr int displayPercent = 36;
// A knob never grows wider than this, however much room its module has.
inline constexpr int maxKnobWidth = 108;
inline constexpr int knobLabelHeight = 14;
// The value readout under every knob.
inline constexpr int readoutHeight = 16;
inline constexpr int stepperLabelHeight = 11;
inline constexpr int stepperHeight = 21;
inline constexpr int maxStepperWidth = 122;
inline constexpr int chipHeight = 20;
inline constexpr int maxChipWidth = 44;

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
        {"oscA", "OSC A", "MORPH", "oscAEnable", false, Display::oscillator, 0, 0, 6, false,
         {{26, {{"oscAOctave", "OCT", Style::stepper}, {"oscASemitone", "SEMI", Style::stepper},
                {"oscAFine", "FINE", Style::stepper}}},
          {74, {{"oscAPosition", "POSITION"}, {"oscAUnison", "UNISON"}, {"oscADetune", "DETUNE"},
                {"oscABlend", "BLEND"}, {"oscAPan", "PAN"}, {"oscALevel", "LEVEL"}}}},
         0, Page::oscillators},
        {"oscB", "OSC B", "MORPH", "oscBEnable", false, Display::oscillator, 0, 6, 6, false,
         {{26, {{"oscBOctave", "OCT", Style::stepper}, {"oscBSemitone", "SEMI", Style::stepper},
                {"oscBFine", "FINE", Style::stepper}}},
          {74, {{"oscBPosition", "POSITION"}, {"oscBUnison", "UNISON"}, {"oscBDetune", "DETUNE"},
                {"oscBBlend", "BLEND"}, {"oscBPan", "PAN"}, {"oscBLevel", "LEVEL"}}}},
         0, Page::oscillators},

        // The matrix takes the whole of the tabbed row, and reads as a table:
        // one row per slot, numbered down the side, the amount between the
        // source driving it and the control it moves. Only the first slot
        // names its columns, because those names are drawn once in the title
        // strip rather than above all eight rows.
        {"matrix", "MATRIX", "8 SLOTS", nullptr, true, Display::none, 0, 0, 12, false,
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
         0, Page::matrix, 1, columnTitleHeight, rowNumberGutter},

        {"sub", "SUB", "", "subEnable", false, Display::none, 1, 0, 1, false,
         {{100, {{"subLevel", "LEVEL"}}}}},
        {"noise", "NOISE", "", "noiseEnable", true, Display::none, 1, 1, 1, false,
         {{100, {{"noiseLevel", "LEVEL"}}}}},
        // The routing chips name their source the way Serum's do: A, B, S, N.
        {"filter", "FILTER", "", "filterEnable", false, Display::none, 1, 2, 3, false,
         {{30, {{"filterType", "TYPE", Style::stepper}, {"routeA", "A", Style::chip},
                {"routeB", "B", Style::chip}, {"routeSub", "S", Style::chip},
                {"routeNoise", "N", Style::chip}}},
          {70, {{"cutoff", "CUTOFF"}, {"resonance", "RES"}, {"drive", "DRIVE"}}}}},
        {"global", "GLOBAL", "VOICING", nullptr, false, Display::none, 1, 5, 5, false,
         {{100, {{"polyphony", "POLY", Style::knob, "mono"}, {"mono", "MONO", Style::rocker},
                 {"legato", "LEGATO", Style::rocker}, {"glide", "GLIDE"}, {"output", "OUTPUT"}}}}},

        // The macros are a column down the right-hand edge, two across and four
        // down, standing beside GLOBAL and LFO 1 rather than under them. They
        // are sources only: a macro reaches a control through a slot or not at
        // all.
        {"macros", "MACROS", "SOURCES", nullptr, false, Display::none, 1, 10, 2, true,
         {{25, {{"macro1", "1"}, {"macro2", "2"}}},
          {25, {{"macro3", "3"}, {"macro4", "4"}}},
          {25, {{"macro5", "5"}, {"macro6", "6"}}},
          {25, {{"macro7", "7"}, {"macro8", "8"}}}},
         0, Page::always, 2},

        {"env1", "ENV 1", "AMP", nullptr, false, Display::envelope, 2, 0, 5, false,
         {{100, {{"attack", "ATTACK"}, {"decay", "DECAY"}, {"sustain", "SUSTAIN"}, {"release", "RELEASE"}}}},
         static_cast<int>(ModSource::env1)},
        {"lfo1", "LFO 1", "SOURCE", nullptr, true, Display::lfo, 2, 5, 5, false,
         {{100, {{"lfoRate", "RATE"}}}},
         static_cast<int>(ModSource::lfo1)},
    };
    return declared;
}

// Whether a module is shown while the given tab is chosen.
inline bool onPage(const Module& module, Page page)
{
    return module.page == Page::always || module.page == page;
}

// Two modules can only collide if some tab shows both of them at once.
inline bool sharePage(const Module& a, const Module& b)
{
    return a.page == Page::always || b.page == Page::always || a.page == b.page;
}

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
        body.removeFromTop(body.getHeight() * displayPercent / 100);
        body.removeFromTop(4);
    }
    return body;
}

// The strip a module's display occupies, or an empty rectangle when it has none.
inline juce::Rectangle<int> displayBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    if (module.display == Display::none) return {};
    auto body = moduleArea.withTrimmedTop(headerHeight).reduced(10, 6);
    return body.removeFromTop(body.getHeight() * displayPercent / 100);
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

inline juce::Rectangle<int> cellBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto row = rowBounds(moduleArea, module, rowIndex);
    const auto& controls = module.rows[static_cast<size_t>(rowIndex)].controls;
    auto total = 0;
    for (const auto& control : controls) total += juce::jmax(1, control.weight);
    if (total <= 0) return row;

    auto x = row.getX();
    for (int i = 0; i < index; ++i)
        x += row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(i)].weight) / total;
    const auto width = row.getWidth() * juce::jmax(1, controls[static_cast<size_t>(index)].weight) / total;
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
                                      cell.getHeight() - knobLabelHeight - readoutHeight);
            }
        }
    }
    return juce::jmax(40, smallest);
}

// The label-plus-knob-plus-readout block, centred in its cell at the one size
// the whole panel shares.
inline juce::Rectangle<int> knobBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int rowIndex, int index, int diameter)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(diameter, knobLabelHeight + diameter + readoutHeight)
        .withCentre(cell.getCentre());
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

inline int knobDiameterFor(const Module& module, juce::Rectangle<int> moduleArea,
                           int rowIndex, int index, int shared)
{
    if (!module.compactKnobs) return shared;
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::jmax(24, juce::jmin(cell.getWidth() - 6,
                                     cell.getHeight() - knobLabelHeight - readoutHeight,
                                     shared * compactKnobPercent / 100));
}

inline juce::Rectangle<int> controlBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index, int diameter)
{
    switch (module.rows[static_cast<size_t>(rowIndex)].controls[static_cast<size_t>(index)].style)
    {
        case Style::stepper: return stepperBlock(moduleArea, module, rowIndex, index);
        case Style::bar:     return barBlock(moduleArea, module, rowIndex, index);
        case Style::chip:    return chipBlock(moduleArea, module, rowIndex, index);
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
