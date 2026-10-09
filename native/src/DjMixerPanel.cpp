#include "DjMixerPanel.h"
#include "DjMixer.h"
#include <algorithm>
#include <cmath>

// The booth's mixer: see DjMixerPanel.h.

namespace rhino
{
namespace
{
constexpr float beatChoices[] {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 3.0f / 4.0f, 1.0f, 2.0f, 4.0f};
const char* beatChoiceNames[] {"1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4"};

void dressLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(uiFontBold(9.0f));
    label.setColour(juce::Label::textColourId, palette::textDim);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
}

void dressBox(juce::ComboBox& box, const juce::String& tooltip)
{
    box.setTooltip(tooltip);
    box.setWantsKeyboardFocus(false);
}
}

// The isolator's knob: off at the bottom, unity in the middle, +6 dB at the
// top, the bottom half a steep fall to a kill.
float DjMixerPanel::eqDbFor(double knob)
{
    if (knob <= 0.01) return Session::djKillDb;
    if (knob < 0.5) return static_cast<float>(-30.0 * (0.5 - knob) / 0.49);
    return static_cast<float>(DjChannelStrip::maximumEqDb * (knob - 0.5) / 0.5);
}

double DjMixerPanel::knobForEqDb(float decibels)
{
    if (decibels <= Session::djKillDb) return 0.0;
    if (decibels < 0.0f) return 0.5 - 0.49 * std::min(1.0f, -decibels / 30.0f);
    return 0.5 + 0.5 * std::min(1.0f, decibels / DjChannelStrip::maximumEqDb);
}

DjMixerPanel::DjMixerPanel(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    dressLabel(masterLabel, "MASTER");
    dressLabel(fxLabel, "BEAT FX");
    dressDjKnob(masterLevel, palette::volume, DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, 0.0,
                "Master level, in dB.");
    masterLevel.onValueChange = [this] { if (!updating) session.setDjMasterLevel(static_cast<float>(masterLevel.getValue())); };
    const auto isolator = [this](juce::Slider& knob, int band, const juce::String& name)
    {
        dressDjKnob(knob, palette::deviceAccent, 0.0, 1.0, 0.5, "Master isolator " + name + ": off at the bottom, +6 dB at the top.");
        knob.onValueChange = [this, &knob, band] { if (!updating) session.setDjMasterEq(band, eqDbFor(knob.getValue())); };
    };
    isolator(masterHigh, 2, "high");
    isolator(masterMid, 1, "mid");
    isolator(masterLow, 0, "low");
    dressDjKnob(resonance, palette::pan, 0.0, 1.0, 0.2, "Resonance of every channel's colour filter.");
    resonance.onValueChange = [this]
    {
        if (updating) return;
        for (int i = 0; i < Session::maximumDjDecks; ++i)
            session.setDjChannelResonance(i, static_cast<float>(resonance.getValue()));
    };
    dressDjKnob(cueMix, palette::activeNeutral, 0.0, 1.0, 0.0,
                "Cue mix on the headphone outputs (3 and 4): the cued channels to the left, the master to the right.");
    cueMix.onValueChange = [this] { if (!updating) session.setDjCueMix(static_cast<float>(cueMix.getValue())); };
    for (int i = 0; i < Session::djFxTypeCount(); ++i)
        fxType.addItem(Session::djFxTypeName(i), i + 1);
    dressBox(fxType, "The beat effect.");
    fxType.onChange = [this] { if (!updating && fxType.getSelectedId() > 0) session.setDjFxType(fxType.getSelectedId() - 1); };
    for (int i = 0; i < static_cast<int>(std::size(beatChoices)); ++i)
        fxBeats.addItem(beatChoiceNames[i], i + 1);
    dressBox(fxBeats, "The effect's time, in beats of the master deck.");
    fxBeats.onChange = [this]
    {
        if (!updating && fxBeats.getSelectedId() > 0)
            session.setDjFxBeats(beatChoices[fxBeats.getSelectedId() - 1]);
    };
    dressBox(fxTarget, "What the effect is on: the master, or one channel.");
    fxTarget.onChange = [this] { if (!updating && fxTarget.getSelectedId() > 0) session.setDjFxTarget(fxTarget.getSelectedId() - 2); };
    dressDjKnob(fxDepth, palette::midiEffect, 0.0, 1.0, 0.5, "Level/depth of the effect.");
    fxDepth.onValueChange = [this] { if (!updating) session.setDjFxDepth(static_cast<float>(fxDepth.getValue())); };
    fxOn.setTooltip("The effect on or off. Echo and delay ring out when switched off.");
    fxOn.onClick = [this] { session.setDjFxOn(!session.djFx().on); };
    crossfader.setTooltip("Crossfader: A to the left, B to the right. Channels set to THRU ignore it.");
    crossfader.setDefault(0.5f);
    crossfader.setDetent(0.5f);
    crossfader.setValue(0.5f, false);
    crossfader.onChange = [this](float v) { session.setDjCrossfader(v * 2.0f - 1.0f); };
    crossfaderCurve.setTooltip("Crossfader curve: lit for a sharp cut, as for scratching; dark for a smooth blend.");
    crossfaderCurve.onClick = [this] { session.setDjCrossfaderCurve(session.djMaster().crossfaderCurve > 0.5f ? 0.0f : 1.0f); };
    for (auto* component : std::initializer_list<juce::Component*>{&masterLabel, &fxLabel, &masterLevel, &masterHigh, &masterMid,
                                                                   &masterLow, &resonance, &cueMix, &masterMeterLeft, &masterMeterRight,
                                                                   &fxType, &fxBeats, &fxTarget, &fxDepth, &fxOn, &crossfader,
                                                                   &crossfaderCurve})
        addAndMakeVisible(component);
    sync();
}

int DjMixerPanel::preferredWidth() const
{
    return static_cast<int>(strips.size()) * stripWidth + masterWidth + 16;
}

void DjMixerPanel::dressStrip(Strip& strip)
{
    const auto channel = strip.channel;
    dressLabel(strip.name, "CH " + juce::String(channel + 1));
    dressDjKnob(strip.trim, palette::volume, DjChannelStrip::minimumTrimDb, DjChannelStrip::maximumTrimDb, 0.0,
                "Trim: the channel's gain before the EQ, in dB.");
    strip.trim.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelTrim(channel, static_cast<float>(strip.trim.getValue())); };
    const auto band = [this, channel](juce::Slider& knob, int index, const juce::String& name)
    {
        dressDjKnob(knob, palette::deviceAccent, 0.0, 1.0, 0.5, name + ": off at the bottom, +6 dB at the top, unity in the middle.");
        knob.onValueChange = [this, channel, index, &knob] { if (!updating) session.setDjChannelEq(channel, index, eqDbFor(knob.getValue())); };
    };
    band(strip.high, 2, "High");
    band(strip.mid, 1, "Mid");
    band(strip.low, 0, "Low");
    dressDjKnob(strip.filter, palette::pan, -1.0, 1.0, 0.0,
                "Colour filter: a low-pass to the left of centre, a high-pass to the right, nothing in the middle.");
    strip.filter.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelFilter(channel, static_cast<float>(strip.filter.getValue())); };
    strip.cue.setTooltip("Cue: hear this channel on the headphone outputs whatever its fader does.");
    strip.cue.onClick = [this, channel] { session.setDjChannelCue(channel, !session.djChannel(channel).cue); };
    strip.fx.setTooltip("Send this channel through the beat effect when the effect is on it.");
    strip.fx.onClick = [this, channel] { session.setDjChannelFx(channel, !session.djChannel(channel).fx); };
    strip.fader.setTooltip("Channel fader.");
    strip.fader.setDefault(1.0f);
    strip.fader.onChange = [this, channel](float v) { session.setDjChannelFader(channel, v); };
    strip.side.setColour(juce::TextButton::buttonColourId, palette::control);
    strip.side.setColour(juce::TextButton::textColourOffId, palette::text);
    strip.side.setTooltip("Which side of the crossfader the channel is on: A, THRU (neither) or B. Click to cycle.");
    strip.side.setWantsKeyboardFocus(false);
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
        for (auto* component : std::initializer_list<juce::Component*>{&strip->name, &strip->trim, &strip->high, &strip->mid, &strip->low,
                                                                       &strip->filter, &strip->cue, &strip->fx, &strip->meter, &strip->fader,
                                                                       &strip->side})
            addAndMakeVisible(component);
        strips.push_back(std::move(strip));
    }
}

void DjMixerPanel::sync()
{
    const juce::ScopedValueSetter<bool> scope(updating, true);
    const auto count = session.djDeckCount();
    if (static_cast<int>(strips.size()) != count)
    {
        buildStrips(count);
        fxTarget.clear(juce::dontSendNotification);
        fxTarget.addItem("Master", 1);
        for (int i = 0; i < count; ++i)
            fxTarget.addItem("CH " + juce::String(i + 1), i + 2);
        resized();
    }
    for (auto& strip : strips)
    {
        const auto state = session.djChannel(strip->channel);
        const auto info = session.djDeckInfo(strip->channel);
        strip->name.setText(info.name.isNotEmpty() ? juce::String(strip->channel + 1) + "  " + info.name : "CH " + juce::String(strip->channel + 1),
                            juce::dontSendNotification);
        strip->trim.setValue(state.trimDb, juce::dontSendNotification);
        strip->high.setValue(knobForEqDb(state.highDb), juce::dontSendNotification);
        strip->mid.setValue(knobForEqDb(state.midDb), juce::dontSendNotification);
        strip->low.setValue(knobForEqDb(state.lowDb), juce::dontSendNotification);
        strip->filter.setValue(state.filter, juce::dontSendNotification);
        strip->cue.setLit(state.cue);
        strip->fx.setLit(state.fx);
        if (!strip->fader.isDragging())
            strip->fader.setValue(state.fader, false);
        strip->side.setButtonText(state.crossfaderSide == 0 ? "A" : state.crossfaderSide == 2 ? "B" : "THRU");
    }
    const auto master = session.djMaster();
    masterLevel.setValue(master.levelDb, juce::dontSendNotification);
    masterHigh.setValue(knobForEqDb(master.highDb), juce::dontSendNotification);
    masterMid.setValue(knobForEqDb(master.midDb), juce::dontSendNotification);
    masterLow.setValue(knobForEqDb(master.lowDb), juce::dontSendNotification);
    cueMix.setValue(master.cueMix, juce::dontSendNotification);
    if (count > 0)
        resonance.setValue(session.djChannel(0).resonance, juce::dontSendNotification);
    if (!crossfader.isDragging())
        crossfader.setValue((master.crossfader + 1.0f) * 0.5f, false);
    crossfaderCurve.setLit(master.crossfaderCurve > 0.5f);
    const auto fx = session.djFx();
    fxType.setSelectedId(fx.type + 1, juce::dontSendNotification);
    int beatId = 6;
    for (int i = 0; i < static_cast<int>(std::size(beatChoices)); ++i)
        if (std::abs(beatChoices[i] - fx.beats) < 0.001f)
            beatId = i + 1;
    fxBeats.setSelectedId(beatId, juce::dontSendNotification);
    fxTarget.setSelectedId(fx.target + 2, juce::dontSendNotification);
    fxDepth.setValue(fx.depth, juce::dontSendNotification);
    fxOn.setLit(fx.on);
    repaint();
}

void DjMixerPanel::tick()
{
    for (auto& strip : strips)
        strip->meter.feed(session.djChannel(strip->channel).meter);
    const auto master = session.djMaster();
    masterMeterLeft.feed(master.meterLeft);
    masterMeterRight.feed(master.meterRight);
}

void DjMixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(palette::border);
    g.drawRect(getLocalBounds());
    // A rule between strips, and one before the master section.
    for (size_t i = 1; i < strips.size(); ++i)
        g.fillRect(4 + static_cast<int>(i) * stripWidth, 4, 1, getHeight() - crossfaderHeight - 8);
    const auto masterX = getWidth() - masterWidth - 4;
    g.fillRect(masterX, 4, 1, getHeight() - crossfaderHeight - 8);
    g.fillRect(4, getHeight() - crossfaderHeight - 2, getWidth() - 8, 1);
    g.setFont(uiFontBold(9.0f));
    g.setColour(palette::textDim);
    const auto knobLabel = [&g](juce::Component& knob, const juce::String& text)
    {
        drawSnappedText(g, text, knob.getBounds().withHeight(10).translated(0, -10), juce::Justification::centred);
    };
    for (auto& strip : strips)
    {
        knobLabel(strip->trim, "TRIM");
        knobLabel(strip->high, "HI");
        knobLabel(strip->mid, "MID");
        knobLabel(strip->low, "LOW");
        knobLabel(strip->filter, "FILTER");
    }
    knobLabel(masterLevel, "LEVEL");
    knobLabel(masterHigh, "HI");
    knobLabel(masterMid, "MID");
    knobLabel(masterLow, "LOW");
    knobLabel(resonance, "RESO");
    knobLabel(cueMix, "CUE MIX");
    knobLabel(fxDepth, "DEPTH");
    const auto master = session.djMaster();
    g.setColour(palette::displayText);
    g.setFont(uiFont(10.0f));
    drawSnappedText(g, master.bpm > 0.0 ? juce::String(master.bpm, 2) + " BPM" : "-- BPM",
                    juce::Rectangle<int>(masterX + 4, 4, masterWidth - 8, 14), juce::Justification::centredRight);
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(9.0f));
    const auto foot = juce::Rectangle<int>(4, getHeight() - crossfaderHeight, getWidth() - 8, crossfaderHeight);
    drawSnappedText(g, "A", foot.withWidth(16), juce::Justification::centred);
    drawSnappedText(g, "B", foot.withTrimmedLeft(foot.getWidth() - 60).withWidth(16), juce::Justification::centred);
}

void DjMixerPanel::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    auto foot = bounds.removeFromBottom(crossfaderHeight);
    crossfaderCurve.setBounds(foot.removeFromRight(40).reduced(0, 10));
    foot.removeFromRight(4);
    crossfader.setBounds(foot.withTrimmedLeft(16).withTrimmedRight(4).reduced(0, 6));
    bounds.removeFromBottom(4);
    const auto knob = std::min(44, std::max(30, (bounds.getHeight() - 150) / 6));
    const auto knobStep = knob + 12;
    for (auto& strip : strips)
    {
        auto column = juce::Rectangle<int>(bounds.getX() + strip->channel * stripWidth, bounds.getY(), stripWidth, bounds.getHeight()).reduced(3, 0);
        strip->name.setBounds(column.removeFromTop(14));
        column.removeFromTop(10);
        for (auto* k : {&strip->trim, &strip->high, &strip->mid, &strip->low, &strip->filter})
        {
            k->setBounds(column.removeFromTop(knob).withSizeKeepingCentre(knob, knob));
            column.removeFromTop(knobStep - knob);
        }
        auto keys = column.removeFromTop(18);
        strip->cue.setBounds(keys.removeFromLeft(keys.getWidth() / 2 - 1));
        strip->fx.setBounds(keys.withTrimmedLeft(2));
        column.removeFromTop(4);
        strip->side.setBounds(column.removeFromBottom(18));
        column.removeFromBottom(3);
        strip->meter.setBounds(column.removeFromLeft(10).reduced(0, 2));
        column.removeFromLeft(4);
        strip->fader.setBounds(column);
    }
    auto master = bounds.removeFromRight(masterWidth).reduced(4, 0);
    master.removeFromTop(16);
    masterLabel.setBounds(master.removeFromTop(14));
    master.removeFromTop(10);
    auto knobs = master.removeFromTop(knob);
    const auto half = knobs.getWidth() / 2;
    masterLevel.setBounds(knobs.removeFromLeft(half).withSizeKeepingCentre(knob, knob));
    masterHigh.setBounds(knobs.withSizeKeepingCentre(knob, knob));
    master.removeFromTop(knobStep - knob);
    knobs = master.removeFromTop(knob);
    masterMid.setBounds(knobs.removeFromLeft(half).withSizeKeepingCentre(knob, knob));
    masterLow.setBounds(knobs.withSizeKeepingCentre(knob, knob));
    master.removeFromTop(knobStep - knob);
    knobs = master.removeFromTop(knob);
    resonance.setBounds(knobs.removeFromLeft(half).withSizeKeepingCentre(knob, knob));
    cueMix.setBounds(knobs.withSizeKeepingCentre(knob, knob));
    master.removeFromTop(knobStep - knob);
    auto meters = master.removeFromRight(26);
    masterMeterLeft.setBounds(meters.removeFromLeft(10).reduced(0, 2));
    meters.removeFromLeft(4);
    masterMeterRight.setBounds(meters.removeFromLeft(10).reduced(0, 2));
    master.removeFromRight(6);
    fxLabel.setBounds(master.removeFromTop(14));
    master.removeFromTop(3);
    fxType.setBounds(master.removeFromTop(20));
    master.removeFromTop(3);
    auto row = master.removeFromTop(20);
    fxBeats.setBounds(row.removeFromLeft(row.getWidth() / 2 - 2));
    master.removeFromTop(3);
    fxTarget.setBounds(master.removeFromTop(20));
    master.removeFromTop(14);
    row = master.removeFromTop(knob);
    fxDepth.setBounds(row.removeFromLeft(knob));
    row.removeFromLeft(8);
    fxOn.setBounds(row.withSizeKeepingCentre(row.getWidth(), 22));
}
}
