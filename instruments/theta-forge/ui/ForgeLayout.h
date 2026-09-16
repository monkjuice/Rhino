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
// the knobs beside it.
enum class Style { knob, stepper, chip, rocker };

struct Control
{
    const char* id;
    const char* label;
    Style style = Style::knob;
    // Greyed out while this parameter is on. Polyphony means nothing in mono.
    const char* disabledBy = nullptr;
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
};

inline constexpr int gridColumns = 12;
inline constexpr int moduleGap = 8;
inline constexpr int headerHeight = 22;

// Row weights, top to bottom: oscillators, sources and filter and voicing,
// envelope and LFO, then the modulation matrix. The oscillators are tallest
// because each carries a display, a tuning strip and six knobs.
inline const std::vector<int>& rowWeights()
{
    static const std::vector<int> weights {32, 20, 24, 24};
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

inline const std::vector<Module>& modules()
{
    static const std::vector<Module> declared {
        {"oscA", "OSC A", "MORPH", "oscAEnable", false, Display::oscillator, 0, 0, 6, false,
         {{26, {{"oscAOctave", "OCT", Style::stepper}, {"oscASemitone", "SEMI", Style::stepper},
                {"oscAFine", "FINE", Style::stepper}}},
          {74, {{"oscAPosition", "POSITION"}, {"oscAUnison", "UNISON"}, {"oscADetune", "DETUNE"},
                {"oscABlend", "BLEND"}, {"oscAPan", "PAN"}, {"oscALevel", "LEVEL"}}}}},
        {"oscB", "OSC B", "MORPH", "oscBEnable", false, Display::oscillator, 0, 6, 6, false,
         {{26, {{"oscBOctave", "OCT", Style::stepper}, {"oscBSemitone", "SEMI", Style::stepper},
                {"oscBFine", "FINE", Style::stepper}}},
          {74, {{"oscBPosition", "POSITION"}, {"oscBUnison", "UNISON"}, {"oscBDetune", "DETUNE"},
                {"oscBBlend", "BLEND"}, {"oscBPan", "PAN"}, {"oscBLevel", "LEVEL"}}}}},

        {"sub", "SUB", "", "subEnable", false, Display::none, 1, 0, 1, false,
         {{100, {{"subLevel", "LEVEL"}}}}},
        {"noise", "NOISE", "", "noiseEnable", true, Display::none, 1, 1, 1, false,
         {{100, {{"noiseLevel", "LEVEL"}}}}},
        // The routing chips name their source the way Serum's do: A, B, S, N.
        {"filter", "FILTER", "", "filterEnable", false, Display::none, 1, 2, 5, false,
         {{30, {{"filterType", "TYPE", Style::stepper}, {"routeA", "A", Style::chip},
                {"routeB", "B", Style::chip}, {"routeSub", "S", Style::chip},
                {"routeNoise", "N", Style::chip}}},
          {70, {{"cutoff", "CUTOFF"}, {"resonance", "RES"}, {"drive", "DRIVE"}}}}},
        {"global", "GLOBAL", "VOICING", nullptr, false, Display::none, 1, 7, 5, false,
         {{100, {{"polyphony", "POLY", Style::knob, "mono"}, {"mono", "MONO", Style::rocker},
                 {"legato", "LEGATO", Style::rocker}, {"glide", "GLIDE"}, {"output", "OUTPUT"}}}}},

        {"env1", "ENV 1", "AMP", nullptr, false, Display::envelope, 2, 0, 6, false,
         {{100, {{"attack", "ATTACK"}, {"decay", "DECAY"}, {"sustain", "SUSTAIN"}, {"release", "RELEASE"}}}},
         static_cast<int>(ModSource::env1)},
        {"lfo1", "LFO 1", "SOURCE", nullptr, true, Display::lfo, 2, 6, 6, false,
         {{100, {{"lfoRate", "RATE"}}}},
         static_cast<int>(ModSource::lfo1)},

        // The matrix is three rows of eight: every slot's source above its
        // destination above its depth. Compact fields rather than knobs, so a
        // row of eight does not drag every knob on the panel down to its size.
        // Macros sit bottom left, two rows of four, and are sources only.
        {"macros", "MACROS", "SOURCES", nullptr, false, Display::none, 3, 0, 3, true,
         {{50, {{"macro1", "1"}, {"macro2", "2"}, {"macro3", "3"}, {"macro4", "4"}}},
          {50, {{"macro5", "5"}, {"macro6", "6"}, {"macro7", "7"}, {"macro8", "8"}}}}},
        {"matrix", "MATRIX", "8 SLOTS", nullptr, true, Display::none, 3, 3, 9, false,
         {{34, {{"mod1Source", "SOURCE", Style::stepper}, {"mod2Source", "", Style::stepper},
                {"mod3Source", "", Style::stepper}, {"mod4Source", "", Style::stepper},
                {"mod5Source", "", Style::stepper}, {"mod6Source", "", Style::stepper},
                {"mod7Source", "", Style::stepper}, {"mod8Source", "", Style::stepper}}},
          {33, {{"mod1Dest", "TARGET", Style::stepper}, {"mod2Dest", "", Style::stepper},
                {"mod3Dest", "", Style::stepper}, {"mod4Dest", "", Style::stepper},
                {"mod5Dest", "", Style::stepper}, {"mod6Dest", "", Style::stepper},
                {"mod7Dest", "", Style::stepper}, {"mod8Dest", "", Style::stepper}}},
          {33, {{"mod1Depth", "DEPTH", Style::stepper}, {"mod2Depth", "", Style::stepper},
                {"mod3Depth", "", Style::stepper}, {"mod4Depth", "", Style::stepper},
                {"mod5Depth", "", Style::stepper}, {"mod6Depth", "", Style::stepper},
                {"mod7Depth", "", Style::stepper}, {"mod8Depth", "", Style::stepper}}}}},
    };
    return declared;
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
    const auto height = available * weights[static_cast<size_t>(module.row)] / total;

    const auto columnWidth = content.getWidth() / gridColumns;
    const auto x = content.getX() + module.column * columnWidth;
    // The last column in a row absorbs the integer-division remainder so the
    // right edge lines up with the content instead of drifting inward.
    const auto reachesRightEdge = module.column + module.columnSpan == gridColumns;
    const auto width = reachesRightEdge ? content.getRight() - x : module.columnSpan * columnWidth;
    return juce::Rectangle<int>(x, y, width, height).reduced(moduleGap / 2, 0);
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

// Everything below the header and the display: the area the control rows share.
inline juce::Rectangle<int> controlArea(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleArea.withTrimmedTop(headerHeight).reduced(10, 6);
    if (module.display != Display::none)
    {
        body.removeFromTop(body.getHeight() * displayPercent / 100);
        body.removeFromTop(4);
    }
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

inline juce::Rectangle<int> cellBounds(juce::Rectangle<int> moduleArea, const Module& module,
                                       int rowIndex, int index)
{
    const auto row = rowBounds(moduleArea, module, rowIndex);
    const auto count = juce::jmax(1, static_cast<int>(module.rows[static_cast<size_t>(rowIndex)].controls.size()));
    const auto width = row.getWidth() / count;
    return {row.getX() + index * width, row.getY(), width, row.getHeight()};
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
// window the way a knob does.
inline juce::Rectangle<int> stepperBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index)
{
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 8, maxStepperWidth),
                                juce::jmin(cell.getHeight(), stepperLabelHeight + stepperHeight))
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

// A compact module's knobs are sized to their own cell; every other knob on
// the panel shares one diameter.
inline int knobDiameterFor(const Module& module, juce::Rectangle<int> moduleArea,
                           int rowIndex, int index, int shared)
{
    if (!module.compactKnobs) return shared;
    const auto cell = cellBounds(moduleArea, module, rowIndex, index);
    return juce::jmax(24, juce::jmin(cell.getWidth() - 6,
                                     cell.getHeight() - knobLabelHeight - readoutHeight));
}

inline juce::Rectangle<int> controlBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                         int rowIndex, int index, int diameter)
{
    switch (module.rows[static_cast<size_t>(rowIndex)].controls[static_cast<size_t>(index)].style)
    {
        case Style::stepper: return stepperBlock(moduleArea, module, rowIndex, index);
        case Style::chip: return chipBlock(moduleArea, module, rowIndex, index);
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
