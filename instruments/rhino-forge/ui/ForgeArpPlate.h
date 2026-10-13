#pragma once

#include "ForgeStyle.h"
#include <vector>

// What moves on the ARP plate beside the keys: the lamp, the lit face while
// the six panes are showing, the four slot buttons, and the notes falling
// through the well. The metal under all of it never changes and is drawn with
// the rest of the chassis in ForgePanels.h, where it is cached; this is the
// part on the frame path, and it is kept to fills of small shapes for that
// reason — nothing here strokes a path but the open state's outline.
namespace rhino::forge::ui
{
// One note on its way down the well. Born when the voices were told to play
// it and spent once it has gone through the floor. Everything about its fall
// is settled when it is made, so a frame only has to ask how old it is: the
// panel repaints whole at 24 Hz, and a spark that had to be stepped would
// have to be stepped by every one of those repaints.
struct NoteSpark
{
    // Where across the well it falls, 0 to 1: the note's place on the keyboard.
    float across = 0.0f;
    // How hard the note was struck, 0 to 1, which is how big and how bright
    // the spark is.
    float weight = 0.5f;
    // How fast it falls against the standard drop. Varied a little per spark
    // so a chord drifts down as several lights rather than as one bar.
    float pace = 1.0f;
    // How far its colour leans from the panel's violet towards white.
    float warmth = 0.0f;
    double bornMs = 0.0;
};

// How long a spark takes to reach the floor at a pace of one. Slow: the point
// of the well is to be watched rather than read, and a drop that is over
// before the eye has found it is a flicker, not a fall.
inline constexpr double sparkFallMs = 1500.0;

// As many as are ever kept. A chord under a fast arp with a long fall can
// have this many in the air, and past it the oldest is the one let go.
inline constexpr int maxSparks = 48;

// Where a spark has got to: 0 at the top of the well, 1 at the floor, and past
// 1 once it is spent.
inline float sparkProgress(const NoteSpark& spark, double nowMs)
{
    return static_cast<float>((nowMs - spark.bornMs) * static_cast<double>(spark.pace) / sparkFallMs);
}

inline void drawArpWaterfall(juce::Graphics& g, juce::Rectangle<int> wellBounds,
                             const std::vector<NoteSpark>& sparks, double nowMs)
{
    if (sparks.empty() || wellBounds.getWidth() < 8 || wellBounds.getHeight() < 8) return;
    const auto well = wellBounds.toFloat();
    juce::Graphics::ScopedSaveState state(g);
    // A spark enters from above the top edge and leaves through the floor, so
    // the well's edges are what cut it rather than its own fade.
    g.reduceClipRegion(wellBounds);

    for (const auto& spark : sparks)
    {
        const auto t = sparkProgress(spark, nowMs);
        if (t < 0.0f || t >= 1.0f) continue;
        // A little faster as it goes, the way anything falling does, and only a
        // little, so the fall reads as drifting rather than dropping.
        const auto drop = t * (0.7f + 0.3f * t);
        // Lit over its first tenth and put out over its last third, so it is
        // gone by the time it reaches the floor rather than cut off by it.
        const auto glow = juce::jlimit(0.0f, 1.0f, juce::jmin(t / 0.1f, (1.0f - t) / 0.3f));
        const auto width = 2.5f + 4.0f * spark.weight;
        const auto height = width * 1.9f;
        const auto x = well.getX() + 4.0f + spark.across * (well.getWidth() - 8.0f);
        const auto y = well.getY() - height * 0.5f + drop * (well.getHeight() + height);
        const auto body = juce::Rectangle<float>(width, height).withCentre({x, y});
        const auto colour = signalViolet.interpolatedWith(juce::Colours::white, 0.25f + 0.45f * spark.warmth);

        // The trail above the body, fading as it goes up: four steps of one
        // column rather than a gradient, which is all a trail this narrow can
        // show and a good deal cheaper to fill.
        const auto trail = 8.0f + 18.0f * spark.weight;
        for (int step = 0; step < 4; ++step)
        {
            const auto share = static_cast<float>(step) * 0.25f;
            g.setColour(signalViolet.withAlpha(glow * 0.2f * (1.0f - share)));
            g.fillRect(juce::Rectangle<float>(x - width * 0.3f, body.getY() - trail * (share + 0.25f),
                                              width * 0.6f, trail * 0.25f + 0.5f));
        }
        // The halo, the body, and a white heart: the three together are what
        // reads as a light rather than as a coloured mark.
        g.setColour(colour.withAlpha(glow * 0.16f));
        g.fillEllipse(body.expanded(width * 0.9f, width * 0.6f));
        g.setColour(colour.withAlpha(glow * 0.92f));
        g.fillRoundedRectangle(body, width * 0.5f);
        g.setColour(juce::Colours::white.withAlpha(glow * 0.55f));
        g.fillRoundedRectangle(body.reduced(width * 0.3f, height * 0.3f), width * 0.25f);
    }
}

// The four slot buttons, two by two. The lit one is faint on purpose: it says
// which slot is playing, and the lamp beside it is the only thing on this
// plate that should be bright.
inline void drawArpSlots(juce::Graphics& g, juce::Rectangle<int> componentBounds, int chosen)
{
    for (int slot = 0; slot < arpSlotCount; ++slot)
    {
        const auto box = arpSlotButtonBounds(componentBounds, slot).toFloat().reduced(0.5f);
        const auto on = slot == chosen;
        g.setColour(on ? signalViolet.withAlpha(0.14f) : juce::Colour(0xff0b0e18));
        g.fillRoundedRectangle(box, 3.5f);
        g.setColour(on ? signalViolet.withAlpha(0.6f) : line.withAlpha(0.8f));
        g.drawRoundedRectangle(box, 3.5f, 1.0f);
        g.setColour(on ? text : mutedText);
        g.setFont(panelFont(Face::emphasis, 12.0f));
        g.drawText(juce::String(slot + 1), box, juce::Justification::centred);
    }
}

// Everything on the plate that follows the arp's state. The lamp follows
// arpEnable, the face follows whether the panes are showing, the buttons
// follow the slot, and the well follows the notes. Lamp and face are drawn as
// different things — a lit lamp against a lit outline — because an arp
// running with its settings put away and an arp on screen that is switched
// off are both ordinary, and one highlight for both would leave you unable to
// tell which you were in.
inline void drawArpPlateState(juce::Graphics& g, juce::Rectangle<int> componentBounds,
                              bool on, bool open, int slot,
                              const std::vector<NoteSpark>& sparks, double nowMs)
{
    const auto face = arpPlateFace(componentBounds).toFloat();
    // Lit while the panes are showing, to the face's own chamfered outline
    // rather than a rounded box laid over it: the light is the well's, so it
    // follows the well's corners. And faint — the lamp says whether the arp is
    // running, and this only has to say the door is open.
    if (open)
    {
        g.setColour(signalViolet.withAlpha(0.05f));
        g.fillPath(chamferedPath(face, arpPlateFaceCut));
        g.setColour(signalViolet.withAlpha(0.32f));
        g.strokePath(chamferedPath(face.reduced(0.5f), arpPlateFaceCut - 0.5f), juce::PathStrokeType(1.0f));
    }

    // The same lamp the module headers wear, drawn here rather than borrowed,
    // because this one is on the chassis instead of on a module and has no
    // component of its own to carry it.
    const auto ledBounds = arpPlateLedBounds(componentBounds).toFloat();
    const auto led = ledBounds.reduced(5.0f);
    if (on)
    {
        g.setColour(signalViolet.withAlpha(0.16f));
        g.fillEllipse(led.expanded(3.5f));
    }
    g.setColour(on ? signalViolet : juce::Colour(0xff232838));
    g.fillEllipse(led);
    g.setColour(on ? juce::Colours::white.withAlpha(0.5f) : mutedText.withAlpha(0.45f));
    g.drawEllipse(led, 1.0f);

    auto name = arpPlateNameBounds(componentBounds).toFloat();
    name = name.withTrimmedLeft(ledBounds.getRight() - name.getX() + 6.0f);
    g.setFont(panelFont(Face::header, 14.0f));
    g.setColour(on ? text : mutedText);
    drawTrackedText(g, "ARP", name.removeFromTop(name.getHeight() * 0.62f), 1.8f,
                    juce::Justification::centredLeft);
    stamp(g, open ? "CLOSE" : "SETTINGS", name, 8.0f, 0.7f);

    drawArpSlots(g, componentBounds, slot);
    drawArpWaterfall(g, arpWaterfallBounds(componentBounds), sparks, nowMs);
}
}
