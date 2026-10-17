#pragma once

#include "ForgeMerliMaterial.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

// The Merli controls that are drawn every frame: the knob's face and the paw
// that stands in for a lamp. They are kept apart from the material because
// nothing in the chassis or the plates wants them, and because what is on the
// frame path has to cost what Forge's does — so everything here is fills and
// strokes of small shapes, with no image and no clip push.
namespace rhino::forge::ui::merli
{
// How far out the knob's bronze ring reaches and how far in it ends, as shares
// of the knob's radius. The outer edge is the body Forge draws at 0.81:
// onModRing measures the mod ring's grab band from it and the mod ring itself
// sits at 0.88 in the clear air outside, so Merli changes nothing a hand has
// learned. The cap stops a little inside the ring so a hairline of the collar
// colour separates the two.
inline constexpr float knobRingOuter = 0.81f;
inline constexpr float knobRingInner = 0.70f;
inline constexpr float knobCapShare = 0.68f;

// The knob as the mockup draws it: one dark cap lit from the upper left inside
// a bronze ring, the ring doubling as the value track, and a cream tick at the
// cap's rim where the value is. The shadow under it is the caller's, shared
// with Forge, and so is the modulation ring outside it.
inline void drawKnobFace(juce::Graphics& g, juce::Rectangle<float> knob, float angle,
                         float startAngle, float endAngle, juce::Colour accent, bool enabled)
{
    const auto centre = knob.getCentre();
    const auto radius = knob.getWidth() * 0.5f;
    if (radius < 2.0f) return;
    const auto polar = [centre] (float distance, float polarAngle)
    {
        return centre + juce::Point<float>(std::sin(polarAngle) * distance,
                                            -std::cos(polarAngle) * distance);
    };
    const auto dim = enabled ? 1.0f : 0.4f;

    // The ring is an annulus and not a disc for the cap to cover, so that when
    // the knob is disabled the ring's alpha and the collar's never stack where
    // one lies under the other. Lit from the upper left, as the plates are.
    const auto outer = knob.reduced(radius * (1.0f - knobRingOuter));
    const auto inner = knob.reduced(radius * (1.0f - knobRingInner));
    juce::Path ring;
    ring.addEllipse(outer);
    ring.addEllipse(inner);
    ring.setUsingNonZeroWinding(false);
    juce::ColourGradient bronze(knobRimLit.withAlpha(dim), outer.getX(), outer.getY(),
                                knobRimDark.withAlpha(dim), outer.getRight(), outer.getBottom(), false);
    g.setGradientFill(bronze);
    g.fillPath(ring);

    // The value track runs on the ring itself: dark over the travel still
    // ahead, lit in the module's accent from the start of the travel to the
    // value. The travel still ahead lets nearly all of the bronze through, so
    // a knob at its minimum still wears the ring the mockup draws on every
    // knob. The two spans meet at the value with square ends, which is what
    // makes the boundary between them the reading.
    const auto trackRadius = radius * (knobRingOuter + knobRingInner) * 0.5f;
    const auto trackWidth = radius * (knobRingOuter - knobRingInner);
    const auto span = [&] (float from, float to, juce::Colour colour)
    {
        if (to - from < 0.002f) return;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f, from, to, true);
        g.setColour(colour);
        g.strokePath(arc, juce::PathStrokeType(trackWidth));
    };
    span(angle, endAngle, switchSlot.withAlpha(0.15f * dim));
    span(startAngle, angle, accent.withAlpha(0.85f * dim));

    // The stops at the two ends of the travel, over whichever span reaches
    // them: the mockup's are a darker orange than its lit arc, which is why
    // they stay visible on it.
    g.setColour(knobArcDot.withAlpha(dim));
    for (const auto stop : {startAngle, endAngle})
        g.fillEllipse(juce::Rectangle<float>(2.0f, 2.0f).withCentre(polar(trackRadius, stop)));

    // The cap. The ring's hole is filled with the collar colour first, so the
    // sliver between the cap's edge and the ring's inner edge is the collar
    // and never the plate showing through, and the stroke on the cap's edge
    // is what makes that collar a full pixel at every knob size. The dome's
    // gradient runs the diagonal, light to dark, with its middle stop at the
    // centre: a dome lit from one corner rather than from above.
    const auto cap = knob.reduced(radius * (1.0f - knobCapShare));
    const auto capAlpha = enabled ? 1.0f : 0.55f;
    g.setColour(knobCollar.withAlpha(capAlpha));
    g.fillEllipse(inner);
    juce::ColourGradient dome(knobDomeLit.withAlpha(capAlpha), cap.getX(), cap.getY(),
                              knobDomeDark.withAlpha(capAlpha), cap.getRight(), cap.getBottom(), false);
    dome.addColour(0.5, knobDomeMid.withAlpha(capAlpha));
    g.setGradientFill(dome);
    g.fillEllipse(cap);
    g.setColour(knobCollar.withAlpha(capAlpha));
    g.drawEllipse(cap, 1.0f);

    // The pointer: a tick at the cap's rim, three wide and a short way in. Its
    // length is never less than its width, so that on the smallest knobs it
    // stays a tick rather than thinning to a point; square-ended, so it stops
    // at the rim instead of lapping onto the collar.
    const auto capRadius = radius * knobCapShare;
    const auto reach = juce::jmax(3.0f, capRadius * 0.15f);
    juce::Path tick;
    tick.startNewSubPath(polar(capRadius, angle));
    tick.lineTo(polar(capRadius - reach, angle));
    g.setColour(knobPointer.withAlpha(enabled ? 1.0f : 0.35f));
    g.strokePath(tick, juce::PathStrokeType(3.0f));
}

// A paw standing in for a lamp. Lit, it takes the colour it is given and
// catches the light along its top; at rest it is the paw's own tint, faint,
// so the module reads as off without the paw going missing. The mockup draws
// every paw at full strength and shows no off state, and a control that looks
// the same on and off is not a control. It throws no halo: at the strength a
// halo can have over a dark plate, it read as a shadow round the paw.
inline void drawPawLamp(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour lit, bool on,
                        float restAlpha)
{
    if (!on)
    {
        drawPaw(g, box, pawTint, restAlpha);
        return;
    }
    // The highlight is the same paw a hair higher, so what shows of it is a
    // crescent along the top of each toe and of the pad — the light that lies
    // on the plates' lips, lying on the paw too.
    drawPaw(g, box.translated(0.0f, -0.7f), lit.interpolatedWith(pawHighlight, 0.75f), 1.0f);
    drawPaw(g, box, lit, 1.0f);
}
}
