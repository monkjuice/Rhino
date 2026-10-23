#pragma once

#include "ForgeEditor.h"

// Private to the Editor's own translation units, in the spelling Rhino already
// uses for this: one class defined across several .cpp files, sharing what used
// to sit in an anonymous namespace. Nothing else should include it.
namespace rhino::forge
{
// The destination index a parameter corresponds to, or 0 if the matrix cannot
// point at it.
inline int destinationFor(const juce::String& parameterId)
{
    for (int i = 1; i < destinationCount; ++i)
        if (parameterId == destinations()[static_cast<size_t>(i)].id) return i;
    return 0;
}

inline juce::String slotParameter(int slot, const char* suffix)
{
    return modParameterId(slot, suffix);
}

// A thin rail and a thumb, for a view with more rows than it shows: the rack,
// the list beside it, and the matrix. Nothing is drawn while everything fits.
inline void drawScrollThumb(juce::Graphics& g, juce::Rectangle<int> rail,
                            int first, int visible, int total)
{
    if (visible >= total || rail.isEmpty()) return;
    const auto thumbHeight = juce::jmax(18, rail.getHeight() * visible / total);
    const auto travel = rail.getHeight() - thumbHeight;
    const auto top = rail.getY() + travel * first / (total - visible);
    g.setColour(ui::line.withAlpha(0.55f));
    g.fillRoundedRectangle(rail.toFloat(), 1.5f);
    g.setColour(ui::signalViolet.withAlpha(0.8f));
    g.fillRoundedRectangle(rail.withY(top).withHeight(thumbHeight).toFloat(), 1.5f);
}

// How many semitones of the piano the computer keys can reach at once. JUCE's
// default qwerty mapping is "awsedftgyhujkolp;" laid on the note offsets 0..16
// from the C of the mapping octave, so the reach is a C to the E an octave and
// a third above it.
inline constexpr int computerKeySpan = 17;

inline bool isFxModule(const ui::Module& module)
{
    return module.id != nullptr && module.id[0] == 'f' && module.id[1] == 'x' && module.id[2] == '\0';
}

// Which filter's TYPE or FREQ a control is, by its id, or -1 for any other
// control. Filter 1's ids are its old ones and filter 2's carry its number, so
// both are spelt by filterParameterId, once: the panel asks on every tick.
inline int filterIndexIn(const juce::String& id, const std::array<juce::String, filterCount>& ids)
{
    for (int filter = 0; filter < filterCount; ++filter)
        if (id == ids[static_cast<size_t>(filter)]) return filter;
    return -1;
}

inline std::array<juce::String, filterCount> filterIdsOf(const char* suffix)
{
    std::array<juce::String, filterCount> ids;
    for (int filter = 0; filter < filterCount; ++filter)
        ids[static_cast<size_t>(filter)] = filterParameterId(filter, suffix);
    return ids;
}

inline int filterOfTypeControl(const juce::String& id)
{
    static const auto ids = filterIdsOf("Type");
    return filterIndexIn(id, ids);
}

inline int filterOfFreqControl(const juce::String& id)
{
    static const auto ids = filterIdsOf("Freq");
    return filterIndexIn(id, ids);
}

inline bool isFilterTypeControl(const juce::String& id) { return filterOfTypeControl(id) >= 0; }
}
