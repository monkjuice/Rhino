#pragma once
#include "DjFader.h"
#include "DjJogWheel.h"
#include "DjMeter.h"
#include "DjPad.h"

// The DJ booth's own controls: a pad that lights, a fader with a cap, a
// level meter and the jog wheel, each in a header and a unit of its own.
// Painted components rather than stock ones, because a CDJ's pads and a
// DJM's faders say what they are by colour and by throw, and a TextButton
// or a LinearVertical slider in the theme's grey says neither.

namespace rhino
{
// Dresses a stock slider as one of the booth's knobs.
void dressDjKnob(juce::Slider& knob, juce::Colour accent, double minimum, double maximum, double resting,
                 const juce::String& tooltip);
}
