#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Every colour the panel draws with, named once, and its lettering.
namespace rhino::duck::ui
{
namespace colour
{
inline const juce::Colour panel { 0xff1b1d21 };
inline const juce::Colour panelEdge { 0xff2a2d33 };
inline const juce::Colour well { 0xff0e0f12 };
inline const juce::Colour wellLine { 0xff262930 };
inline const juce::Colour chip { 0xff2a2e35 };
inline const juce::Colour chipHover { 0xff363b44 };
inline const juce::Colour chipText { 0xffd3d7de };
inline const juce::Colour chipMuted { 0xff8a9099 };
inline const juce::Colour accent { 0xffff8a3d };
inline const juce::Colour accentDark { 0xff1c1208 };
inline const juce::Colour text { 0xffe9ebef };
inline const juce::Colour textDim { 0xff868c96 };
inline const juce::Colour wave { 0xffb6bcc6 };
inline const juce::Colour side { 0xffffb46e };
inline const juce::Colour knobBody { 0xff23262b };
inline const juce::Colour knobFace { 0xff2e3238 };
inline const juce::Colour knobRing { 0xff33373e };
inline const juce::Colour divider { 0xff30343b };
inline const juce::Colour marker { 0xffe0e4ea };
}

enum class Face
{
    header,    // the name on the panel and the chips: Chakra Petch SemiBold
    label,     // captions and readings: Rajdhani SemiBold
    reading    // numbers that change while you watch: IBM Plex Mono Medium
};

// Loaded once each and kept: parsing a font per repaint would be a frame
// cost for nothing.
inline juce::Typeface::Ptr typefaceFor(Face face)
{
    struct Loaded
    {
        juce::Typeface::Ptr header = juce::Typeface::createSystemTypefaceFor(
            BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
        juce::Typeface::Ptr label = juce::Typeface::createSystemTypefaceFor(
            BinaryData::RajdhaniSemiBold_ttf, BinaryData::RajdhaniSemiBold_ttfSize);
        juce::Typeface::Ptr reading = juce::Typeface::createSystemTypefaceFor(
            BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
    };
    static const Loaded loaded;
    switch (face)
    {
        case Face::header:  return loaded.header;
        case Face::label:   return loaded.label;
        case Face::reading: return loaded.reading;
    }
    return loaded.label;
}

inline juce::Font font(Face face, float height)
{
    return juce::Font(juce::FontOptions().withTypeface(typefaceFor(face)).withHeight(height));
}
}
