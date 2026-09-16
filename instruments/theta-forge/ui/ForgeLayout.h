#pragma once

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
enum class Display { none, oscillator, envelope, lfo };

struct Knob
{
    const char* id;
    const char* label;
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
    std::vector<Knob> knobs;
};

inline constexpr int gridColumns = 12;
inline constexpr int moduleGap = 8;
inline constexpr int headerHeight = 22;

// Row weights, top to bottom: oscillators, sources and filter, modulation,
// global voicing. The two display-carrying rows are tallest; the rest are
// balanced so that no row ends up with knobs noticeably smaller than another.
inline const std::vector<int>& rowWeights()
{
    static const std::vector<int> weights {29, 21, 29, 21};
    return weights;
}

// Share of a module's body given over to its display.
inline constexpr int displayPercent = 36;
// A knob never grows wider than this, however much room its module has.
inline constexpr int maxKnobWidth = 108;
inline constexpr int knobLabelHeight = 14;

inline const std::vector<Module>& modules()
{
    static const std::vector<Module> declared {
        {"oscA", "OSC A", "MORPH", "oscAEnable", false, Display::oscillator, 0, 0, 6,
         {{"oscAPosition", "POSITION"}, {"unison", "UNISON"}, {"detune", "DETUNE"}}},
        {"oscB", "OSC B", "MORPH", "oscBEnable", true, Display::oscillator, 0, 6, 6,
         {{"oscBPosition", "POSITION"}, {"oscBLevel", "LEVEL"}, {"oscBTune", "TUNE"}}},

        {"sub", "SUB", "", "subEnable", false, Display::none, 1, 0, 2,
         {{"subLevel", "LEVEL"}}},
        {"noise", "NOISE", "", "noiseEnable", true, Display::none, 1, 2, 2,
         {{"noiseLevel", "LEVEL"}}},
        {"filter", "FILTER", "LOW PASS", "filterEnable", false, Display::none, 1, 4, 8,
         {{"cutoff", "CUTOFF"}, {"resonance", "RES"}, {"drive", "DRIVE"}}},

        {"env1", "ENV 1", "AMP", nullptr, false, Display::envelope, 2, 0, 6,
         {{"attack", "ATTACK"}, {"decay", "DECAY"}, {"sustain", "SUSTAIN"}, {"release", "RELEASE"}}},
        {"lfo1", "LFO 1", "FREE RUNNING", nullptr, true, Display::lfo, 2, 6, 6,
         {{"lfoRate", "RATE"}, {"lfoCutoff", "> CUTOFF"}, {"lfoPosition", "> POSITION"}, {"lfoPitch", "> PITCH"}}},

        {"global", "GLOBAL", "VOICING", nullptr, false, Display::none, 3, 0, 12,
         {{"polyphony", "POLY"}, {"mono", "MONO"}, {"legato", "LEGATO"}, {"glide", "GLIDE"}, {"output", "OUTPUT"}}},
    };
    return declared;
}

// The area the modules are laid out inside, below the title bar.
inline juce::Rectangle<int> contentBounds(juce::Rectangle<int> bounds)
{
    return bounds.reduced(30).withTrimmedTop(62);
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

inline juce::Rectangle<int> knobRowBounds(juce::Rectangle<int> moduleArea, const Module& module)
{
    auto body = moduleArea.withTrimmedTop(headerHeight).reduced(10, 6);
    if (module.display != Display::none)
    {
        body.removeFromTop(body.getHeight() * displayPercent / 100);
        body.removeFromTop(4);
    }
    return body;
}

inline juce::Rectangle<int> knobBounds(juce::Rectangle<int> moduleArea, const Module& module, int index)
{
    const auto row = knobRowBounds(moduleArea, module);
    const auto count = juce::jmax(1, static_cast<int>(module.knobs.size()));
    const auto width = row.getWidth() / count;
    return {row.getX() + index * width, row.getY(), width, row.getHeight()};
}

// Every knob on the panel is drawn at one size, taken from whichever module
// has the least room. Sizing each knob to its own module instead makes SUB's
// single knob several times the diameter of one in GLOBAL, which reads as a
// mistake rather than as emphasis.
inline int uniformKnobHeight(juce::Rectangle<int> bounds)
{
    auto smallest = std::numeric_limits<int>::max();
    for (const auto& module : modules())
        smallest = juce::jmin(smallest, knobRowBounds(moduleBounds(bounds, module), module).getHeight());
    return juce::jmax(48, smallest);
}

// The label-plus-knob-plus-readout block, centred in its cell at the one size
// the whole panel shares.
inline juce::Rectangle<int> knobBlock(juce::Rectangle<int> moduleArea, const Module& module,
                                      int index, int uniformHeight)
{
    const auto cell = knobBounds(moduleArea, module, index);
    return juce::Rectangle<int>(juce::jmin(cell.getWidth() - 6, maxKnobWidth),
                                juce::jmin(cell.getHeight(), uniformHeight))
        .withCentre(cell.getCentre());
}
}
