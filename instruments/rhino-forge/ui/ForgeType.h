#pragma once

#include <BinaryData.h>
#include <cmath>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

// The panel's lettering.
//
// Four faces, and the choice between them is about what a piece of text is for
// rather than about how it should look. A module's name, a tab and a section
// heading are all the same kind of thing — they say what you are looking at —
// so they share a face. A label under a knob is a different kind of thing, and
// a number is a third: a reading changes while you watch it, and a
// proportional face makes it jitter as the digits swap width, which is the
// whole reason the readings are monospaced.
//
// Nothing outside this file names a typeface. Everything that draws text asks
// for a Face and a height, so re-typesetting the panel is editing this file.
namespace rhino::forge::ui
{
enum class Face
{
    // Module names, section headings, the page tabs. Chakra Petch SemiBold.
    header,
    // The label under a knob or over a field. Rajdhani Medium.
    label,
    // A button's text, and a label that has to carry more weight than the ones
    // around it. Rajdhani SemiBold.
    emphasis,
    // Anything that shows a value. IBM Plex Mono Medium.
    reading
};

// Loaded once each and kept. Typeface::createSystemTypefaceFor parses the font
// every time it is called, so doing it per repaint would be a frame cost for
// nothing — these never change.
inline juce::Typeface::Ptr typefaceFor(Face face)
{
    struct Loaded
    {
        juce::Typeface::Ptr header  = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
        juce::Typeface::Ptr label   = juce::Typeface::createSystemTypefaceFor(
            BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize);
        juce::Typeface::Ptr emphasis = juce::Typeface::createSystemTypefaceFor(
            BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
        juce::Typeface::Ptr reading = juce::Typeface::createSystemTypefaceFor(
            BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
    };
    static const Loaded loaded;

    switch (face)
    {
        case Face::header:   return loaded.header;
        case Face::label:    return loaded.label;
        case Face::emphasis: return loaded.emphasis;
        case Face::reading:  return loaded.reading;
    }
    return loaded.label;
}

// Rajdhani and Chakra Petch are both tall-x, narrow faces drawn for panels, and
// they run small for a given JUCE height: set to the size Inter was used at,
// the labels came out noticeably smaller than the design. This is the factor
// that puts them back, applied in one place so every call site keeps asking for
// the size it means rather than a corrected one.
inline constexpr float panelFontScale = 1.28f;

// Kept, not rebuilt.
//
// Constructing a juce::Font from a FontOptions resolves the face's metrics, and
// the panel asks for one at every piece of text it draws — fifty-odd times a
// frame, twenty-four times a second. Building them fresh each time cost seven
// points of one core, which took the panel from under its old baseline to over
// it; the panel only ever asks for a handful of distinct sizes, so they are
// made once and handed back.
//
// The cache is message-thread only, which is where painting happens. Nothing
// else may call this.
inline juce::Font panelFont(Face face, float height)
{
    struct Entry
    {
        Face face;
        float height;
        juce::Font font;
    };
    static std::vector<Entry> made;

    const auto wanted = height * panelFontScale;
    for (const auto& entry : made)
        if (entry.face == face && std::abs(entry.height - wanted) < 0.01f) return entry.font;

    juce::Font font(juce::FontOptions(typefaceFor(face)).withHeight(wanted));
    made.push_back({face, wanted, font});
    return font;
}

// The one string on the panel that is not set in any of the four: the unit mark
// in the title bar is CJK, the subsets carry no CJK, and the platform's own
// font fallback is what draws it. Asking for it by height alone is what lets
// that fallback happen.
inline juce::Font fallbackFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}
}
