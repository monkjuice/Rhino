#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
// A track's send to one return, as a card shows it: a knob small enough that
// a row of them fits under the track's name, with the return's letter in it,
// so the card says which return each one feeds without a label beside it.
//
// The value is the send in decibels, from Session::minimumSendDb, which is
// nothing, up to Session::maximumSendDb, the track's own level - the range
// Live's send knob has. The travel is the engine's fader law below unity,
// 20 ln(position) dB, so half a turn is about -14 dB and a quarter -28: the
// levels a send is usually set to get most of the knob.
class SendKnob final : public juce::Slider
{
public:
    SendKnob();

    void setLetter(const juce::String&);
    const juce::String& letter() const { return returnLetter; }

    static double positionForDb(double decibels);
    static double dbForPosition(double position);
    // "-12.0 dB", or "-inf dB" for a send of nothing.
    static juce::String describe(double decibels);

    void paint(juce::Graphics&) override;

    static constexpr int size = 20;

private:
    juce::String returnLetter;
};
}
