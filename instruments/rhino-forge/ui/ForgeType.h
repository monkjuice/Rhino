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

// Everything the panel keeps that holds a font: the four typefaces, the fonts
// made from them (panelFont) and drawTrackedText's measurements (ForgeChrome.h).
//
// Kept for as long as JUCE runs and given back when JUCE shuts down, never
// left to the end of the process as a static would be. On macOS a typeface,
// when the last font using it goes, takes itself out of JUCE's registry of
// loaded fonts under a mutex that is a static of JUCE's own; a static of
// Forge's made after that one outlives it, so on every quit a typeface locked a
// destroyed mutex and the app aborted ("mutex lock failed: Invalid argument").
// Windows keeps no such registry, which is why it only ever showed on a Mac.
// Message thread only, like everything that paints.
class FontStore final : private juce::DeletedAtShutdown
{
public:
    struct Made
    {
        Face face;
        float height;
        juce::Font font;
    };

    struct Tracked
    {
        juce::Font font;
        juce::String lettering;
        std::vector<float> widths;
    };

    // Loaded once each. Typeface::createSystemTypefaceFor parses the font every
    // time it is called, so doing it per repaint would be a frame cost for
    // nothing — these never change.
    juce::Typeface::Ptr header = juce::Typeface::createSystemTypefaceFor(
        BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
    juce::Typeface::Ptr label = juce::Typeface::createSystemTypefaceFor(
        BinaryData::RajdhaniMedium_ttf, BinaryData::RajdhaniMedium_ttfSize);
    juce::Typeface::Ptr emphasis = juce::Typeface::createSystemTypefaceFor(
        BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
    juce::Typeface::Ptr reading = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);

    std::vector<Made> made;
    std::vector<Tracked> tracked;

    ~FontStore() override { clearSingletonInstance(); }

    JUCE_DECLARE_SINGLETON_INLINE(FontStore, false)
};

inline juce::Typeface::Ptr typefaceFor(Face face)
{
    const auto& store = *FontStore::getInstance();
    switch (face)
    {
        case Face::header:   return store.header;
        case Face::label:    return store.label;
        case Face::emphasis: return store.emphasis;
        case Face::reading:  return store.reading;
    }
    return store.label;
}

// Rajdhani and Chakra Petch are both tall-x, narrow faces drawn for panels, and
// they run small for a given JUCE height: set to the size Inter was used at,
// the labels came out noticeably smaller than the design. This is the factor
// that puts them back, applied in one place so every call site keeps asking for
// the size it means rather than a corrected one.
inline constexpr float panelFontScale = 1.28f;

// The one size named here rather than at the place that draws it, because more
// than one file sets a control's name and they had drifted apart: a stepper's
// was drawn a point smaller than the knob's beside it, which is visible as a
// wobble along a row of mixed controls. Measured off the reference, where a
// knob's name stands 9 pixels tall at the width the panel opens at.
inline constexpr float controlLabelSize = 12.5f;

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
    auto& made = FontStore::getInstance()->made;

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
