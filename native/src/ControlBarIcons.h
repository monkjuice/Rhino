#pragma once
#include "Theme.h"
#include <functional>

// The control bar's glyphs and the button that wears them.
//
// Every icon here is a path built at draw time and scaled into the rectangle it
// is given, rather than a character handed to a font. A glyph the UI face does
// not carry - a transport symbol, an arrow - falls through to whatever the
// system offers, which is a different face at a different weight on every
// machine and the reason the old bar's play triangle and its undo arrow never
// looked like they belonged to the same interface. A path is the same shape
// everywhere and stays crisp at the sizes this bar uses.

namespace rhino::icons
{
// The metronome, traced from assets/metronome.svg. The drawing is kept in the
// SVG's own coordinates and scaled on use, so the asset and this stay in step:
// re-exporting the SVG means pasting its path data here and nothing else.
juce::Path metronome();

// Transport. Each is drawn inside a unit square and scaled by the caller.
juce::Path play();
juce::Path stop();
juce::Path returnToStart();

// The two curved arrows, which are one shape and its mirror. These draw
// rather than return a path, because an arrow is a stroked arc with a *filled*
// head: handing back one path put the head through strokePath with the arc and
// left it an outlined diamond on the end of a hook.
void drawCurvedArrow(juce::Graphics&, juce::Rectangle<float> area, bool pointingLeft, float lineWidth);

// The browser toggle: a narrow panel beside a wide one, both solid. Drawn
// rather than returned because it is two filled rectangles at a fixed size
// rather than artwork to be scaled - at fourteen pixels a traced window frame
// with a chevron beside it reads as a smudge, which is why this is back.
void drawSidebar(juce::Graphics&, juce::Rectangle<float> area);

// The chevron that means "a menu opens below this".
juce::Path chevronDown();

// Scales a path into an area, preserving its proportions, and strokes it at a
// width in *screen* pixels rather than one scaled down with the artwork. A
// stroke that scales with a 180-unit drawing reduced to sixteen pixels comes
// out at half a pixel and renders as grey; these glyphs are small and have to
// hold their weight.
void strokeFitted(juce::Graphics&, const juce::Path&, juce::Rectangle<float> area, float lineWidth);
void fillFitted(juce::Graphics&, const juce::Path&, juce::Rectangle<float> area);
}

namespace rhino
{
// A borderless button that paints one glyph.
//
// The bar has no button boxes any more: a control is its icon on the bar's own
// surface, and what it says about itself it says with brightness. Hover and
// press wash the hit area rather than outline it, which is what lets a fourteen
// pixel glyph keep a thirty-four pixel target without looking like a key on a
// keyboard. Disabled dims the glyph instead of removing it, so a bar with
// nothing to undo still reads as a bar with an undo in it.
// Not final: the stop button adds double-click handling on top of it.
class IconButton : public juce::Button
{
public:
    // How the glyph is put on the canvas. The rectangle is the button's own
    // bounds inset for the glyph; the colour is the state already resolved,
    // so a painter never has to ask whether it is hovered or off.
    using Painter = std::function<void(juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;

    explicit IconButton(const juce::String& name) : juce::Button(name) {}

    void setPainter(Painter p) { painter = std::move(p); repaint(); }
    // How much of the button the glyph fills. A transport glyph wants to be
    // small inside a comfortable target; a sidebar icon fills more of its own.
    void setGlyphInset(float proportion) { inset = juce::jlimit(0.0f, 0.45f, proportion); repaint(); }
    // The colour when the button is on. Left unset, an on button uses the
    // neutral active grey, which is what every control in the bar but record
    // and the browser toggle wants.
    void setActiveColour(juce::Colour c) { active = c; repaint(); }
    // Whether the hit area washes under the pointer. Off for the undo pair,
    // which the specification asks to carry no surface of any kind.
    void setWashesOnHover(bool shouldWash) { washes = shouldWash; }

    void paintButton(juce::Graphics&, bool highlighted, bool pressed) override;

private:
    Painter painter;
    float inset = 0.28f;
    juce::Colour active {palette::activeNeutral};
    bool washes = true;
};
}
