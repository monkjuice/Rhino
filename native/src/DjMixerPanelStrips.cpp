#include "DjMixerPanelInternal.h"

// The booth's mixer, its channel strips: each knob described once, and the
// strips built, shown and laid out from that. See DjMixerPanel.h.

namespace rhino
{
using namespace mixerpanel;

namespace
{
// A knob of a channel strip, described once: the caption painted over it,
// its colour, its range and resting place, what it says when hovered, what
// it sets on the booth and where it reads the booth's value back from.
struct StripKnob
{
    const char* caption;
    const juce::Colour* accent;
    double minimum, maximum, resting;
    const char* tooltip;
    void (*set)(Session&, int channel, double value);
    double (*read)(const Session::DjChannelState&);
};

const std::array<StripKnob, DjMixerPanel::stripKnobCount> stripKnobs {{
    {"TRIM", &palette::volume, DjChannelStrip::minimumTrimDb, DjChannelStrip::maximumTrimDb, 0.0,
     "Trim: the channel's gain before the EQ, in dB.",
     [](Session& s, int c, double v) { s.setDjChannelTrim(c, static_cast<float>(v)); },
     [](const Session::DjChannelState& st) { return static_cast<double>(st.trimDb); }},
    {"COMP", &palette::recordAccent, 0.0, 1.0, 0.0,
     "Compressor: one knob, as the V10's. Up brings the threshold down and the ratio up, with the loss made up.",
     [](Session& s, int c, double v) { s.setDjChannelComp(c, static_cast<float>(v)); },
     [](const Session::DjChannelState& st) { return static_cast<double>(st.comp); }},
    {"HI", &palette::deviceAccent, 0.0, 1.0, 0.5,
     "High, above 3 kHz: off at the bottom, +6 dB at the top, unity in the middle.",
     [](Session& s, int c, double v) { s.setDjChannelEq(c, 3, eqDbFor(v)); },
     [](const Session::DjChannelState& st) { return knobForEqDb(st.eqDb[3]); }},
    {"HI MID", &palette::deviceAccent, 0.0, 1.0, 0.5,
     "High mid, 600 Hz to 3 kHz: off at the bottom, +6 dB at the top, unity in the middle.",
     [](Session& s, int c, double v) { s.setDjChannelEq(c, 2, eqDbFor(v)); },
     [](const Session::DjChannelState& st) { return knobForEqDb(st.eqDb[2]); }},
    {"LOW MID", &palette::deviceAccent, 0.0, 1.0, 0.5,
     "Low mid, 150 to 600 Hz: off at the bottom, +6 dB at the top, unity in the middle.",
     [](Session& s, int c, double v) { s.setDjChannelEq(c, 1, eqDbFor(v)); },
     [](const Session::DjChannelState& st) { return knobForEqDb(st.eqDb[1]); }},
    {"LOW", &palette::deviceAccent, 0.0, 1.0, 0.5,
     "Low, below 150 Hz: off at the bottom, +6 dB at the top, unity in the middle.",
     [](Session& s, int c, double v) { s.setDjChannelEq(c, 0, eqDbFor(v)); },
     [](const Session::DjChannelState& st) { return knobForEqDb(st.eqDb[0]); }},
    {"FILTER", &palette::pan, -1.0, 1.0, 0.0,
     "Colour filter: a low-pass to the left of centre, a high-pass to the right, nothing in the middle.",
     [](Session& s, int c, double v) { s.setDjChannelFilter(c, static_cast<float>(v)); },
     [](const Session::DjChannelState& st) { return static_cast<double>(st.filter); }},
    {"SEND", &palette::midiEffect, 0.0, 1.0, 0.0,
     "How much of the channel goes to the send effect.",
     [](Session& s, int c, double v) { s.setDjChannelSend(c, static_cast<float>(v)); },
     [](const Session::DjChannelState& st) { return static_cast<double>(st.send); }},
}};
}

void DjMixerPanel::dressStrip(Strip& strip)
{
    const auto channel = strip.channel;
    strip.name.setText("CH " + juce::String(channel + 1), juce::dontSendNotification);
    strip.name.setFont(uiFontBold(9.0f));
    strip.name.setColour(juce::Label::textColourId, palette::textDim);
    strip.name.setJustificationType(juce::Justification::centred);
    strip.name.setInterceptsMouseClicks(false, false);
    for (size_t i = 0; i < stripKnobs.size(); ++i)
    {
        const auto& spec = stripKnobs[i];
        auto& knob = strip.knobs[i];
        dressDjKnob(knob, *spec.accent, spec.minimum, spec.maximum, spec.resting, spec.tooltip);
        knob.onValueChange = [this, channel, &knob, &spec] { if (!updating) spec.set(session, channel, knob.getValue()); };
    }
    strip.cue.setTooltip("Cue: hear this channel on the headphone outputs whatever its fader does.");
    strip.cue.onClick = [this, channel] { session.setDjChannelCue(channel, !session.djChannel(channel).cue); };
    strip.fx.setTooltip("Send this channel through the beat effect when the effect is on it.");
    strip.fx.onClick = [this, channel] { session.setDjChannelFx(channel, !session.djChannel(channel).fx); };
    strip.fader.setTooltip("Channel fader.");
    strip.fader.setDefault(1.0f);
    strip.fader.onChange = [this, channel](float v) { session.setDjChannelFader(channel, v); };
    strip.side.setTooltip("Which side of the crossfader the channel is on: A, THRU (neither) or B. Press to cycle.");
    strip.side.onClick = [this, channel] { session.setDjChannelCrossfaderSide(channel, (session.djChannel(channel).crossfaderSide + 1) % 3); };
}

void DjMixerPanel::buildStrips(int count)
{
    while (static_cast<int>(strips.size()) > count)
        strips.pop_back();
    while (static_cast<int>(strips.size()) < count)
    {
        auto strip = std::make_unique<Strip>();
        strip->channel = static_cast<int>(strips.size());
        dressStrip(*strip);
        for (auto* component : std::initializer_list<juce::Component*>{&strip->name, &strip->cue, &strip->fx, &strip->meter,
                                                                       &strip->fader, &strip->side})
            addAndMakeVisible(component);
        for (auto& knob : strip->knobs)
            addAndMakeVisible(knob);
        strips.push_back(std::move(strip));
    }
}

void DjMixerPanel::refreshStrips(bool meters)
{
    for (auto& strip : strips)
    {
        const auto channel = session.djChannel(strip->channel);
        if (meters) strip->meter.feed(channel.meter);
        for (size_t i = 0; i < stripKnobs.size(); ++i)
            strip->knobs[i].setValue(stripKnobs[i].read(channel), juce::dontSendNotification);
        strip->cue.setLit(channel.cue);
        strip->fx.setLit(channel.fx);
        if (!strip->fader.isDragging())
            strip->fader.setValue(channel.fader, false);
        if (channel.crossfaderSide != strip->shownSide)
        {
            strip->shownSide = channel.crossfaderSide;
            strip->side.setLabel(channel.crossfaderSide == 0 ? "A" : channel.crossfaderSide == 2 ? "B" : "THRU");
            strip->side.setLit(channel.crossfaderSide != 1);
        }
    }
}

// Each strip a column: its name, the eight knobs under their captions, cue
// and the effect assign, the meter beside the fader, and the crossfader
// side at the foot.
void DjMixerPanel::layoutStrips(juce::Rectangle<int> area)
{
    const auto count = static_cast<int>(strips.size());
    stripPitch = count > 0 ? juce::jlimit(minimumStripWidth, stripWidth, (area.getWidth() - masterWidth) / count) : stripWidth;
    const auto captionFont = uiFontBold(8.0f);
    for (auto& strip : strips)
    {
        auto column = juce::Rectangle<int>(area.getX() + strip->channel * stripPitch, area.getY(), stripPitch, area.getHeight()).reduced(3, 0);
        strip->name.setBounds(column.removeFromTop(titleHeight));
        column.removeFromTop(4);
        for (size_t i = 0; i < stripKnobs.size(); ++i)
        {
            knobRow(column, strip->knobs[i]);
            strip->captions[i].set(stripKnobs[i].caption, captionFont, captionOver(strip->knobs[i]), juce::Justification::centred);
        }
        auto keys = column.removeFromTop(keyHeight);
        strip->cue.setBounds(keys.removeFromLeft(keys.getWidth() / 2 - 1));
        strip->fx.setBounds(keys.withTrimmedLeft(2));
        column.removeFromTop(4);
        strip->side.setBounds(column.removeFromBottom(keyHeight));
        column.removeFromBottom(3);
        strip->meter.setBounds(column.removeFromLeft(8).reduced(0, 2));
        column.removeFromLeft(4);
        strip->fader.setBounds(column);
    }
}
}
