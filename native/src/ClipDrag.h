#pragma once
#include "Session.h"
#include "Theme.h"

// Clips dragged between the arrangement and the DJ consoles. A timeline clip
// carries its id and a console's cell its track and scene; both are copies
// when they land, as every route between the two sets of clips is. The view
// switch in the control bar shows the other view while one of these, or a
// browser item, hovers it, which is how a drag crosses.

namespace rhino
{
inline juce::String arrangementClipDragDescription(te::EditItemID id)
{
    return "rhino-clip:" + id.toString();
}

inline te::EditItemID draggedArrangementClip(const juce::String& description)
{
    if (!description.startsWith("rhino-clip:")) return {};
    return te::EditItemID::fromString(description.fromFirstOccurrenceOf("rhino-clip:", false, false));
}

inline juce::String slotDragDescription(int track, int scene)
{
    return "rhino-slot:" + juce::String(track) + ":" + juce::String(scene);
}

inline bool draggedSlot(const juce::String& description, int& track, int& scene)
{
    if (!description.startsWith("rhino-slot:")) return false;
    const auto rest = description.fromFirstOccurrenceOf("rhino-slot:", false, false);
    track = rest.upToFirstOccurrenceOf(":", false, false).getIntValue();
    scene = rest.fromFirstOccurrenceOf(":", false, false).getIntValue();
    return true;
}

// Whether a drag may want the other view.
inline bool isCrossViewDrag(const juce::String& description)
{
    return description.startsWith("rhino-clip:") || description.startsWith("rhino-slot:")
        || description.startsWith("rhino-browser:");
}

// A small tile with the clip's name in its colour, to drag by. Without one
// JUCE drags a picture of the whole source component.
inline juce::ScaledImage clipDragImage(const juce::String& name, juce::Colour colour)
{
    juce::Image image(juce::Image::ARGB, 120, 22, true);
    juce::Graphics g(image);
    g.setColour(colour.withMultipliedBrightness(0.75f).withAlpha(0.92f));
    g.fillRoundedRectangle(image.getBounds().toFloat(), 3.0f);
    g.setColour(colour.contrasting(0.9f));
    g.setFont(uiFontBold(10.0f));
    drawSnappedText(g, name, image.getBounds().reduced(6, 0), juce::Justification::centredLeft, true);
    return juce::ScaledImage(image);
}
}
