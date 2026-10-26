#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Every colour the panel draws with, named once, and its lettering. The
// surfaces, accents, waveforms and text follow the 2026-10-26 handoff's
// palette; the duck's own colours are the mark's.
namespace rhino::duck::ui
{
namespace colour
{
inline const juce::Colour panel { 0xff181b20 };        // the window behind the plates
inline const juce::Colour panelEdge { 0xff0b0c0e };
inline const juce::Colour plate { 0xff21252b };        // a raised section: the rail, the strip, the preset bar
inline const juce::Colour plateRaised { 0xff2b3037 };
inline const juce::Colour well { 0xff0e1013 };         // the recessed scope and value boxes
inline const juce::Colour wellLine { 0xff2d3238 };     // the scope's grid
// A step above the plates' lit top edge, so a chip there still reads.
inline const juce::Colour chip { 0xff32373f };
inline const juce::Colour chipHover { 0xff3d434c };
inline const juce::Colour chipText { 0xffd3d7de };
inline const juce::Colour chipMuted { 0xff8f969e };
inline const juce::Colour accent { 0xffff8a3d };
inline const juce::Colour accentHot { 0xffffa24a };
inline const juce::Colour accentDark { 0xff1c1208 };
inline const juce::Colour text { 0xffe3e5e8 };
inline const juce::Colour textDim { 0xff8f969e };
inline const juce::Colour textMuted { 0xff626971 };
inline const juce::Colour wave { 0xffd0d3d8 };         // the ducked output: Sub Out
inline const juce::Colour dry { 0xff989da5 };          // the same signal before the duck: Sub Dry
inline const juce::Colour side { 0xff9c5d35 };         // the sidechain: the kick
inline const juce::Colour knobBody { 0xff1d2025 };
inline const juce::Colour knobFace { 0xff3a3f47 };
inline const juce::Colour knobRing { 0xff33373e };
inline const juce::Colour divider { 0xff30343b };
inline const juce::Colour marker { 0xffe0e4ea };
inline const juce::Colour water { 0xffd9e0e4 };
inline const juce::Colour duckBody { 0xffffc93c };
inline const juce::Colour duckShade { 0xfff08a1c };
inline const juce::Colour duckBeak { 0xffff8a3d };
inline const juce::Colour duckHorn { 0xffbfc3c8 };
inline const juce::Colour duckInk { 0xff15181c };
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
