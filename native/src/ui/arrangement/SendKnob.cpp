#include "SendKnob.h"
#include "model/Session.h"
#include "ui/shared/Theme.h"
#include <cmath>

namespace rhino
{
namespace
{
// Where the travel runs out: the position whose level is the session's floor.
const double floorPosition = std::exp(Session::minimumSendDb / 20.0);
}

SendKnob::SendKnob()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setNormalisableRange({Session::minimumSendDb, Session::maximumSendDb,
                          [](double, double, double position) { return dbForPosition(position); },
                          [](double, double, double decibels) { return positionForDb(decibels); }});
    // Nothing, which is what every send starts at.
    setDoubleClickReturnValue(true, Session::minimumSendDb);
    setMouseDragSensitivity(160);
    setValue(Session::minimumSendDb, juce::dontSendNotification);
}

void SendKnob::setLetter(const juce::String& letter)
{
    if (letter == returnLetter)
        return;
    returnLetter = letter;
    repaint();
}

double SendKnob::positionForDb(double decibels)
{
    return decibels <= Session::minimumSendDb ? 0.0 : juce::jlimit(0.0, 1.0, std::exp(decibels / 20.0));
}

double SendKnob::dbForPosition(double position)
{
    return position <= floorPosition ? Session::minimumSendDb
                                     : juce::jlimit(static_cast<double>(Session::minimumSendDb),
                                                    static_cast<double>(Session::maximumSendDb),
                                                    20.0 * std::log(position));
}

juce::String SendKnob::describe(double decibels)
{
    return decibels <= Session::minimumSendDb ? juce::String("-inf dB") : juce::String(decibels, 1) + " dB";
}

// A dark well, so the knob reads the same on any track's colour, ringed by the
// knob's travel and lit along it as far as the send is turned up.
void SendKnob::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto diameter = std::min(bounds.getWidth(), bounds.getHeight()) - 1.0f;
    if (diameter <= 4.0f)
        return;
    const auto well = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());
    const auto centre = well.getCentre();
    const auto rotary = getRotaryParameters();
    const auto proportion = static_cast<float>(valueToProportionOfLength(getValue()));
    const auto angle = rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
    const auto on = getValue() > Session::minimumSendDb;

    g.setColour(palette::displayInset);
    g.fillEllipse(well);
    const auto radius = well.getWidth() * 0.5f - 2.0f;
    juce::Path travel;
    travel.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotary.startAngleRadians, rotary.endAngleRadians,
                         true);
    g.setColour(palette::border);
    g.strokePath(travel, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (on)
    {
        juce::Path level;
        level.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotary.startAngleRadians, angle, true);
        g.setColour(palette::send);
        g.strokePath(level, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(on ? palette::text : palette::textDim);
    g.setFont(uiFontBold(9.0f));
    drawSnappedText(g, returnLetter, getLocalBounds(), juce::Justification::centred);
}

}
