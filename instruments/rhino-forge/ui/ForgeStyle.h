#pragma once

#include "ForgeModule.h"
#include "ForgeChrome.h"
#include "ForgeType.h"
#include "ForgePanels.h"
#include <functional>
#include <vector>
#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// The vocabulary every other look here is written in: the panel colours, the
// small geometry a control needs to know about itself, and the four colours a
// module's plate can be painted. Nothing here draws a whole control and
// nothing decides where one goes.
namespace rhino::forge::ui
{
inline const auto panel = juce::Colour(0xff111522);
inline const auto panelRaised = juce::Colour(0xff191d2d);
inline const auto line = juce::Colour(0xff34394e);

inline constexpr int handleWidth = 70;

// The lit strip along a module's top edge. Named because the cards that hang
// from that edge redraw it across themselves, and a card that continued a strip
// of a different thickness would read as sitting in front of the module rather
// than as part of it.
inline constexpr float moduleEdgeHeight = 3.0f;

// --- Fader geometry -----------------------------------------------------------
//
// Shared, because the look draws the thumb here and the value bubble is placed
// beside it: two readings of one position, which have to be the same one.
inline constexpr float faderThumbHeight = 7.0f;

// Where the thumb's centre sits, given how far down its travel it is. It never
// hangs off either end, so what it moves along is the slot less its own height.
inline float faderThumbY(juce::Rectangle<float> area, float proportionFromTop)
{
    const auto travel = juce::jmax(1.0f, area.getHeight() - faderThumbHeight);
    return area.getY() + faderThumbHeight * 0.5f + proportionFromTop * travel;
}

// How far down its travel a value sits, measured from the top: a fader is read
// as how far it has been brought up from the bottom.
inline float faderProportion(juce::Range<double> range, double plain)
{
    const auto span = range.getLength();
    return span > 0.0
        ? 1.0f - static_cast<float>(juce::jlimit(0.0, 1.0, (plain - range.getStart()) / span))
        : 1.0f;
}

// A card is square where it meets the module's top edge and round at its foot.
inline juce::Path cardOutline(juce::Rectangle<float> area)
{
    juce::Path path;
    path.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                             4.0f, 4.0f, false, false, true, true);
    return path;
}

// --- Knob geometry ----------------------------------------------------------
//
// Shared by the look that draws a knob and by the component that has to know
// what the mouse is over, so the ring you can see and the ring you can grab
// cannot drift apart.

// The 4px inset is the optical one every knob's circle sits inside.
inline juce::Rectangle<float> knobCircle(juce::Rectangle<int> rotaryArea)
{
    const auto area = rotaryArea.toFloat().reduced(4.0f);
    const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
    return juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
}

// Where the modulation ring is drawn, as a share of the knob's radius. The
// knob's body stops at 0.81, so the ring sits in clear air outside it.
inline constexpr float modRingRadius = 0.88f;

// The band the ring can be grabbed in: everything outside the body out to a
// little past the rim. Keeping it clear of the body is what lets the knob keep
// its own gesture — inside the circle still means "turn this".
inline bool onModRing(juce::Rectangle<int> rotaryArea, juce::Point<int> position)
{
    const auto knob = knobCircle(rotaryArea);
    const auto radius = knob.getWidth() * 0.5f;
    if (radius <= 0.0f) return false;
    const auto distance = position.toFloat().getDistanceFrom(knob.getCentre());
    return distance >= radius * 0.82f && distance <= radius * 1.06f;
}

// --- Numeric field geometry -------------------------------------------------
//
// A bar-style field has no rim to hang a ring on, so the modulation reaching it
// is drawn as a strip along its foot instead: same two readings the ring
// carries, laid out left to right across the field's own range rather than
// around a circle. Shared by the look and the component for the same reason the
// knob geometry above is.

// The strip itself, in the field's dead space below the glyphs, which is why it
// is thin enough to leave the number alone.
inline juce::Rectangle<float> modBarBounds(juce::Rectangle<float> field)
{
    return field.reduced(2.0f, 0.0f).withTop(field.getBottom() - 3.5f);
}

// The band the strip can be grabbed in. Taller than the mark it draws, because
// three pixels is not something a hand can aim at, and the field is only
// twenty-one tall to begin with — so the bottom third of it is the strip and
// the rest stays the field's own drag.
inline bool onModBar(juce::Rectangle<float> field, juce::Point<int> position)
{
    return field.withTop(field.getBottom() - 7.0f).contains(position.toFloat());
}

inline juce::Colour accentFor(const Module& module)
{
    return module.violet ? signalViolet : electricBlue;
}

// --- An oscillator's own colour ----------------------------------------------
//
// Every other module is the colour it was declared: the signal path is blue and
// the modulators are violet. An oscillator is the exception — it carries a
// colour you choose from its LED, so the two of them can be told apart at a
// glance on a patch that has both running, which is what the colour is for.
//
// Four, not a wheel. A picker would let you land on something that cannot be
// read against this chassis or told apart from the violet the modulators
// already own; these four are each unmistakable at a tick mark's width, which
// is the size the colour actually has to work at.
enum class PanelColour { red, orange, green, blue };

inline constexpr int panelColourCount = 4;

// Where a new oscillator starts.
inline constexpr PanelColour defaultPanelColour = PanelColour::green;

inline const char* panelColourName(PanelColour choice)
{
    switch (choice)
    {
        case PanelColour::red:    return "Red";
        case PanelColour::orange: return "Orange";
        case PanelColour::green:  return "Green";
        case PanelColour::blue:   return "Blue";
    }
    return "";
}

// Lifted off pure hues. A saturated red glows brown on a near-black chassis and
// a pure green reads as an indicator lamp rather than as a panel colour, so
// each of these is pulled toward white until it holds up both as a two-pixel
// LED and as a trace across a tube.
inline juce::Colour panelColourOf(PanelColour choice)
{
    switch (choice)
    {
        case PanelColour::red:    return juce::Colour(0xffff5c52);
        case PanelColour::orange: return juce::Colour(0xffffa235);
        case PanelColour::green:  return juce::Colour(0xff3ddc84);
        // The blue the rest of the panel is drawn in, so an oscillator set to
        // it is the panel's own colour rather than a fifth one close to it.
        case PanelColour::blue:   return electricBlue;
    }
    return electricBlue;
}

// A stored choice, which may have come from a preset written by a build that
// knew a different number of colours.
inline PanelColour panelColourFrom(int stored)
{
    return static_cast<PanelColour>(juce::jlimit(0, panelColourCount - 1, stored));
}

// The menu a module's LED opens. Built here rather than inside the editor so a
// test can read back what it offers: whether every colour is on it, whether the
// one in use is the one ticked, and whether each row carries its swatch. Item
// IDs are one-based, because a PopupMenu reports nothing chosen as zero.
inline juce::PopupMenu panelColourMenu(int current, const juce::String& title)
{
    juce::PopupMenu menu;
    if (title.isNotEmpty()) menu.addSectionHeader(title);
    for (int choice = 0; choice < panelColourCount; ++choice)
    {
        juce::PopupMenu::Item item(panelColourName(panelColourFrom(choice)));
        item.itemID = choice + 1;
        item.isTicked = choice == current;
        // The swatch is the point of the menu. The names are only there because
        // a colour on its own is not something you can be sure you clicked.
        item.colour = panelColourOf(panelColourFrom(choice));
        menu.addItem(item);
    }
    return menu;
}
}
