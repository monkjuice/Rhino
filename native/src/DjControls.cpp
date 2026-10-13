#include "DjControls.h"

// The knob dressing the booth's panels share: see DjControls.h.

namespace rhino
{
void dressDjKnob(juce::Slider& knob, juce::Colour accent, double minimum, double maximum, double resting,
                 const juce::String& tooltip)
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    knob.setRange(minimum, maximum, 0.0);
    knob.setValue(resting, juce::dontSendNotification);
    knob.setDoubleClickReturnValue(true, resting);
    knob.setColour(juce::Slider::backgroundColourId, palette::control);
    knob.setColour(juce::Slider::trackColourId, accent);
    knob.setColour(juce::Slider::thumbColourId, palette::text);
    knob.setTooltip(tooltip);
    knob.setWantsKeyboardFocus(false);
    knob.setMouseClickGrabsKeyboardFocus(false);
}
}
