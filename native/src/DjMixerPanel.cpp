#include "DjMixerPanel.h"
#include "DjMixer.h"
#include <algorithm>
#include <cmath>
#include <iterator>

// The booth's mixer: see DjMixerPanel.h.

namespace rhino
{
namespace
{
constexpr float beatChoices[] {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 3.0f / 4.0f, 1.0f, 2.0f, 4.0f};
const char* beatChoiceNames[] {"1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4"};
constexpr int knobSize = 28, knobLabelHeight = 10, keyHeight = 18, titleHeight = 14;

int beatChoiceIndex(float beats)
{
    int index = 5;
    for (int i = 0; i < static_cast<int>(std::size(beatChoices)); ++i)
        if (std::abs(beatChoices[i] - beats) < 0.001f)
            index = i;
    return index;
}

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

// A knob under a painted label: the label's ten pixels, the knob, a gap.
void knobRow(juce::Rectangle<int>& column, juce::Slider& knob)
{
    column.removeFromTop(knobLabelHeight);
    knob.setBounds(column.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize));
    column.removeFromTop(2);
}

// Two knobs side by side under their labels.
void knobPair(juce::Rectangle<int>& column, juce::Slider& first, juce::Slider& second)
{
    column.removeFromTop(knobLabelHeight);
    auto row = column.removeFromTop(knobSize);
    first.setBounds(row.removeFromLeft(row.getWidth() / 2).withSizeKeepingCentre(knobSize, knobSize));
    second.setBounds(row.withSizeKeepingCentre(knobSize, knobSize));
    column.removeFromTop(2);
}
}

// The EQ knob: off at the bottom, unity in the middle, +6 dB at the top,
// the bottom half a steep fall to a kill.
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
    // ---- the master column ----
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
    faderCurve.setTooltip("The channel faders' curve: gentle, normal or steep - how soon a fader comes up. Press to cycle.");
    faderCurve.onClick = [this]
    {
        const auto next = (session.djChannel(0).faderCurve + 1) % 3;
        for (int i = 0; i < Session::maximumDjDecks; ++i)
            session.setDjChannelFaderCurve(i, next);
    };
    // ---- the headphones and the mic ----
    dressDjKnob(cueMix, palette::activeNeutral, 0.0, 1.0, 0.0,
                "Headphones mix: the cued channels to the left, the master to the right, on outputs 3 and 4.");
    cueMix.onValueChange = [this] { if (!updating) session.setDjCueMix(static_cast<float>(cueMix.getValue())); };
    dressDjKnob(cueLevel, palette::volume, DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, 0.0,
                "Headphones level, in dB.");
    cueLevel.onValueChange = [this] { if (!updating) session.setDjCueLevel(static_cast<float>(cueLevel.getValue())); };
    monoSplit.setTooltip("Mono split: the cue in the left ear, the master in the right.");
    monoSplit.onClick = [this] { session.setDjMonoSplit(!session.djMaster().monoSplit); };
    dressDjKnob(micLevel, palette::recordAccent, DjMicSection::minimumLevelDb, DjMicSection::maximumLevelDb, 0.0,
                "Mic level, in dB. The mic is the audio device's first input.");
    micLevel.onValueChange = [this] { if (!updating) session.setDjMicLevel(static_cast<float>(micLevel.getValue())); };
    dressDjKnob(micHigh, palette::deviceAccent, -12.0, 12.0, 0.0, "Mic high shelf, in dB.");
    micHigh.onValueChange = [this] { if (!updating) session.setDjMicEq(1, static_cast<float>(micHigh.getValue())); };
    dressDjKnob(micLow, palette::deviceAccent, -12.0, 12.0, 0.0, "Mic low shelf, in dB.");
    micLow.onValueChange = [this] { if (!updating) session.setDjMicEq(0, static_cast<float>(micLow.getValue())); };
    micMode.setTooltip("The mic: off, on, or on with talkover, which ducks the master while the mic is spoken into. "
                       "Press to cycle.");
    micMode.onClick = [this] { session.setDjMicMode((session.djMic().mode + 1) % 3); };
    // ---- the send/return unit ----
    for (int i = 0; i < Session::djSendFxTypeCount(); ++i)
        sendType.addItem(Session::djSendFxTypeName(i), i + 1);
    dressBox(sendType, "The send effect, fed by every channel's SEND knob.");
    sendType.onChange = [this] { if (!updating && sendType.getSelectedId() > 0) session.setDjSendFxType(sendType.getSelectedId() - 1); };
    dressDjKnob(sendSize, palette::midiEffect, 0.0, 1.0, 0.5, "Send effect size: how long it rings on.");
    sendSize.onValueChange = [this] { if (!updating) session.setDjSendFxSize(static_cast<float>(sendSize.getValue())); };
    dressDjKnob(sendTime, palette::midiEffect, 0.0, 1.0, 0.5, "Send effect time: the delay's length, or a reverb's pre-delay.");
    sendTime.onValueChange = [this] { if (!updating) session.setDjSendFxTime(static_cast<float>(sendTime.getValue())); };
    dressDjKnob(sendTone, palette::midiEffect, 0.0, 1.0, 0.5, "Send effect tone: dark to the left, bright to the right.");
    sendTone.onValueChange = [this] { if (!updating) session.setDjSendFxTone(static_cast<float>(sendTone.getValue())); };
    dressDjKnob(sendMix, palette::midiEffect, 0.0, 1.0, 0.5, "How loud the send effect comes back into the master.");
    sendMix.onValueChange = [this] { if (!updating) session.setDjSendFxMix(static_cast<float>(sendMix.getValue())); };
    // ---- the beat effect ----
    for (int i = 0; i < Session::djFxTypeCount(); ++i)
        fxType.addItem(Session::djFxTypeName(i), i + 1);
    dressBox(fxType, "The beat effect.");
    fxType.onChange = [this] { if (!updating && fxType.getSelectedId() > 0) session.setDjFxType(fxType.getSelectedId() - 1); };
    dressBox(fxTarget, "What the effect is on: the master, or one channel.");
    fxTarget.onChange = [this] { if (!updating && fxTarget.getSelectedId() > 0) session.setDjFxTarget(fxTarget.getSelectedId() - 2); };
    const auto bandKey = [this](DjPad& key, int band, const juce::String& name)
    {
        key.setTooltip("The effect on the " + name + " band. Dark, the band passes dry.");
        key.onClick = [this, band] { session.setDjFxBand(band, !session.djFx().bands[static_cast<size_t>(band)]); };
    };
    bandKey(fxLow, 0, "low");
    bandKey(fxMid, 1, "mid");
    bandKey(fxHigh, 2, "high");
    fxBeatDown.setTooltip("A shorter beat for the effect.");
    fxBeatDown.onClick = [this] { stepFxBeats(-1); };
    fxBeatUp.setTooltip("A longer beat for the effect.");
    fxBeatUp.onClick = [this] { stepFxBeats(1); };
    fxAuto.setTooltip("Auto: the effect's time follows the beat. Dark, the TIME knob sets it.");
    fxAuto.onClick = [this] { session.setDjFxAutoTime(!session.djFx().autoTime); };
    fxTap.setTooltip("Tap the tempo the effect runs at when no deck plays.");
    fxTap.onClick = [this] { session.djTapTempo(); };
    dressDjKnob(fxTime, palette::midiEffect, 0.001, 4.0, 0.25, "The effect's time in seconds, when Auto is off.");
    fxTime.setSkewFactorFromMidPoint(0.25);
    fxTime.onValueChange = [this] { if (!updating) session.setDjFxTime(static_cast<float>(fxTime.getValue())); };
    dressDjKnob(fxDepth, palette::midiEffect, 0.0, 1.0, 0.5, "Level/depth of the effect.");
    fxDepth.onValueChange = [this] { if (!updating) session.setDjFxDepth(static_cast<float>(fxDepth.getValue())); };
    fxOn.setTooltip("The effect on or off. Echo and delay ring out when switched off.");
    fxOn.onClick = [this] { session.setDjFxOn(!session.djFx().on); };
    // ---- the crossfader ----
    crossfader.setTooltip("Crossfader: A to the left, B to the right. Channels set to THRU ignore it.");
    crossfader.setDefault(0.5f);
    crossfader.setDetent(0.5f);
    crossfader.setValue(0.5f, false);
    crossfader.onChange = [this](float v) { session.setDjCrossfader(v * 2.0f - 1.0f); };
    crossfaderCurve.setTooltip("Crossfader curve: a smooth blend, a straight one, or a sharp cut for scratching. Press to cycle.");
    crossfaderCurve.onClick = [this]
    {
        const auto curve = session.djMaster().crossfaderCurve;
        session.setDjCrossfaderCurve(curve < 0.25f ? 0.5f : curve < 0.75f ? 1.0f : 0.0f);
    };
    for (auto* component : std::initializer_list<juce::Component*>{&masterLevel, &masterHigh, &masterMid, &masterLow, &resonance,
                                                                   &masterMeterLeft, &masterMeterRight, &faderCurve, &cueMix, &cueLevel,
                                                                   &monoSplit, &micLevel, &micHigh, &micLow, &micMode, &micMeter,
                                                                   &sendType, &sendSize, &sendTime, &sendTone, &sendMix, &fxType,
                                                                   &fxTarget, &fxLow, &fxMid, &fxHigh, &fxBeatDown, &fxBeatUp, &fxAuto,
                                                                   &fxTap, &fxTime, &fxDepth, &fxOn, &crossfader, &crossfaderCurve})
        addAndMakeVisible(component);
    sync();
}

int DjMixerPanel::preferredWidth() const
{
    return static_cast<int>(strips.size()) * stripWidth + masterWidth + 8;
}

int DjMixerPanel::minimumWidth() const
{
    return static_cast<int>(strips.size()) * minimumStripWidth + masterWidth + 8;
}

void DjMixerPanel::stepFxBeats(int direction)
{
    const auto index = juce::jlimit(0, static_cast<int>(std::size(beatChoices)) - 1, beatChoiceIndex(session.djFx().beats) + direction);
    session.setDjFxBeats(beatChoices[index]);
}

void DjMixerPanel::dressStrip(Strip& strip)
{
    const auto channel = strip.channel;
    dressLabel(strip.name, "CH " + juce::String(channel + 1));
    dressDjKnob(strip.trim, palette::volume, DjChannelStrip::minimumTrimDb, DjChannelStrip::maximumTrimDb, 0.0,
                "Trim: the channel's gain before the EQ, in dB.");
    strip.trim.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelTrim(channel, static_cast<float>(strip.trim.getValue())); };
    dressDjKnob(strip.comp, palette::recordAccent, 0.0, 1.0, 0.0,
                "Compressor: one knob, as the V10's. Up brings the threshold down and the ratio up, with the loss made up.");
    strip.comp.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelComp(channel, static_cast<float>(strip.comp.getValue())); };
    const auto band = [this, channel](juce::Slider& knob, int index, const juce::String& name)
    {
        dressDjKnob(knob, palette::deviceAccent, 0.0, 1.0, 0.5, name + ": off at the bottom, +6 dB at the top, unity in the middle.");
        knob.onValueChange = [this, channel, index, &knob] { if (!updating) session.setDjChannelEq(channel, index, eqDbFor(knob.getValue())); };
    };
    band(strip.high, 3, "High, above 3 kHz");
    band(strip.highMid, 2, "High mid, 600 Hz to 3 kHz");
    band(strip.lowMid, 1, "Low mid, 150 to 600 Hz");
    band(strip.low, 0, "Low, below 150 Hz");
    dressDjKnob(strip.filter, palette::pan, -1.0, 1.0, 0.0,
                "Colour filter: a low-pass to the left of centre, a high-pass to the right, nothing in the middle.");
    strip.filter.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelFilter(channel, static_cast<float>(strip.filter.getValue())); };
    dressDjKnob(strip.send, palette::midiEffect, 0.0, 1.0, 0.0, "How much of the channel goes to the send effect.");
    strip.send.onValueChange = [this, channel, &strip] { if (!updating) session.setDjChannelSend(channel, static_cast<float>(strip.send.getValue())); };
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
        for (auto* component : std::initializer_list<juce::Component*>{&strip->name, &strip->trim, &strip->comp, &strip->high,
                                                                       &strip->highMid, &strip->lowMid, &strip->low, &strip->filter,
                                                                       &strip->send, &strip->cue, &strip->fx, &strip->meter,
                                                                       &strip->fader, &strip->side})
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
        strip->comp.setValue(state.comp, juce::dontSendNotification);
        strip->high.setValue(knobForEqDb(state.eqDb[3]), juce::dontSendNotification);
        strip->highMid.setValue(knobForEqDb(state.eqDb[2]), juce::dontSendNotification);
        strip->lowMid.setValue(knobForEqDb(state.eqDb[1]), juce::dontSendNotification);
        strip->low.setValue(knobForEqDb(state.eqDb[0]), juce::dontSendNotification);
        strip->filter.setValue(state.filter, juce::dontSendNotification);
        strip->send.setValue(state.send, juce::dontSendNotification);
        strip->cue.setLit(state.cue);
        strip->fx.setLit(state.fx);
        if (!strip->fader.isDragging())
            strip->fader.setValue(state.fader, false);
        strip->side.setLabel(state.crossfaderSide == 0 ? "A" : state.crossfaderSide == 2 ? "B" : "THRU");
        strip->side.setLit(state.crossfaderSide != 1);
    }
    const auto master = session.djMaster();
    masterLevel.setValue(master.levelDb, juce::dontSendNotification);
    masterHigh.setValue(knobForEqDb(master.highDb), juce::dontSendNotification);
    masterMid.setValue(knobForEqDb(master.midDb), juce::dontSendNotification);
    masterLow.setValue(knobForEqDb(master.lowDb), juce::dontSendNotification);
    cueMix.setValue(master.cueMix, juce::dontSendNotification);
    cueLevel.setValue(master.cueLevelDb, juce::dontSendNotification);
    monoSplit.setLit(master.monoSplit);
    if (count > 0)
    {
        const auto first = session.djChannel(0);
        resonance.setValue(first.resonance, juce::dontSendNotification);
        faderCurve.setLabel(first.faderCurve == 0 ? "GENTLE" : first.faderCurve == 2 ? "STEEP" : "NORMAL");
    }
    if (!crossfader.isDragging())
        crossfader.setValue((master.crossfader + 1.0f) * 0.5f, false);
    crossfaderCurve.setLabel(master.crossfaderCurve < 0.25f ? "SMOOTH" : master.crossfaderCurve < 0.75f ? "LINEAR" : "CUT");
    const auto mic = session.djMic();
    micLevel.setValue(mic.levelDb, juce::dontSendNotification);
    micHigh.setValue(mic.highDb, juce::dontSendNotification);
    micLow.setValue(mic.lowDb, juce::dontSendNotification);
    micMode.setLabel(mic.mode == 0 ? "MIC OFF" : mic.mode == 1 ? "MIC ON" : "TALKOVER");
    micMode.setLit(mic.mode != 0);
    const auto send = session.djSendFx();
    sendType.setSelectedId(send.type + 1, juce::dontSendNotification);
    sendSize.setValue(send.size, juce::dontSendNotification);
    sendTime.setValue(send.time, juce::dontSendNotification);
    sendTone.setValue(send.tone, juce::dontSendNotification);
    sendMix.setValue(send.mix, juce::dontSendNotification);
    const auto fx = session.djFx();
    fxType.setSelectedId(fx.type + 1, juce::dontSendNotification);
    fxTarget.setSelectedId(fx.target + 2, juce::dontSendNotification);
    fxLow.setLit(fx.bands[0]);
    fxMid.setLit(fx.bands[1]);
    fxHigh.setLit(fx.bands[2]);
    fxAuto.setLit(fx.autoTime);
    fxTime.setValue(fx.manualSeconds, juce::dontSendNotification);
    fxTime.setEnabled(!fx.autoTime);
    fxDepth.setValue(fx.depth, juce::dontSendNotification);
    fxOn.setLit(fx.on);
    repaint();
}

juce::String DjMixerPanel::bpmReadout() const
{
    const auto master = session.djMaster();
    if (master.bpm > 0.0) return juce::String(master.bpm, 2) + " BPM";
    return "TAP " + juce::String(master.tapBpm, 1);
}

void DjMixerPanel::tick()
{
    for (auto& strip : strips)
        strip->meter.feed(session.djChannel(strip->channel).meter);
    const auto master = session.djMaster();
    masterMeterLeft.feed(master.meterLeft);
    masterMeterRight.feed(master.meterRight);
    micMeter.feed(session.djMic().meter);
    // The tempo readout follows the master deck's fader; repainted only
    // when its text changes.
    if (const auto readout = bpmReadout(); readout != drawnBpm)
        repaint(bpmArea);
}

void DjMixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(palette::border);
    g.drawRect(getLocalBounds());
    const auto top = 4, bottom = getHeight() - crossfaderHeight - 8;
    // A rule between strips, and one before the master section.
    for (size_t i = 1; i < strips.size(); ++i)
        g.fillRect(4 + static_cast<int>(i) * stripPitch, top, 1, bottom - top);
    const auto masterX = getWidth() - masterWidth - 4;
    g.fillRect(masterX, top, 1, bottom - top);
    g.fillRect(4, getHeight() - crossfaderHeight - 2, getWidth() - 8, 1);
    g.setFont(uiFontBold(8.0f));
    g.setColour(palette::textDim);
    const auto knobLabel = [&g](juce::Component& knob, const juce::String& text)
    {
        drawSnappedText(g, text, knob.getBounds().withHeight(knobLabelHeight).translated(0, -knobLabelHeight),
                        juce::Justification::centred);
    };
    for (auto& strip : strips)
    {
        knobLabel(strip->trim, "TRIM");
        knobLabel(strip->comp, "COMP");
        knobLabel(strip->high, "HI");
        knobLabel(strip->highMid, "HI MID");
        knobLabel(strip->lowMid, "LOW MID");
        knobLabel(strip->low, "LOW");
        knobLabel(strip->filter, "FILTER");
        knobLabel(strip->send, "SEND");
    }
    knobLabel(masterLevel, "LEVEL");
    knobLabel(masterHigh, "HI");
    knobLabel(masterMid, "MID");
    knobLabel(masterLow, "LOW");
    knobLabel(resonance, "RESONANCE");
    knobLabel(faderCurve, "CH CURVE");
    knobLabel(cueMix, "MIX");
    knobLabel(cueLevel, "LEVEL");
    knobLabel(micLevel, "LEVEL");
    knobLabel(micHigh, "HI");
    knobLabel(micLow, "LOW");
    knobLabel(sendSize, "SIZE");
    knobLabel(sendTime, "TIME");
    knobLabel(sendTone, "TONE");
    knobLabel(sendMix, "MIX");
    knobLabel(fxTime, "TIME");
    knobLabel(fxDepth, "DEPTH");
    // The section titles.
    g.setFont(uiFontBold(9.0f));
    g.setColour(palette::textDim);
    drawSnappedText(g, "MASTER", masterTitle, juce::Justification::centred);
    drawSnappedText(g, "ISOLATOR", isolatorTitle, juce::Justification::centred);
    drawSnappedText(g, "HEADPHONES", phonesTitle, juce::Justification::centred);
    drawSnappedText(g, "MIC", micTitle, juce::Justification::centred);
    drawSnappedText(g, "SEND FX", sendTitle, juce::Justification::centred);
    drawSnappedText(g, "BEAT FX", fxTitle, juce::Justification::centred);
    // The readouts: the tempo the effect follows, and its beat.
    g.setColour(palette::displayInset);
    g.fillRect(bpmArea);
    g.fillRect(beatReadout);
    g.setColour(palette::displayText);
    g.setFont(uiFont(10.0f));
    drawnBpm = bpmReadout();
    drawSnappedText(g, drawnBpm, bpmArea, juce::Justification::centred);
    drawSnappedText(g, beatChoiceNames[beatChoiceIndex(session.djFx().beats)], beatReadout, juce::Justification::centred);
    // A and B at the crossfader's ends.
    g.setColour(palette::textDim);
    g.setFont(uiFontBold(9.0f));
    const auto foot = juce::Rectangle<int>(4, getHeight() - crossfaderHeight, getWidth() - 8, crossfaderHeight);
    drawSnappedText(g, "A", foot.withWidth(16), juce::Justification::centred);
    drawSnappedText(g, "B", juce::Rectangle<int>(crossfader.getRight(), foot.getY(), 16, foot.getHeight()), juce::Justification::centred);
}

void DjMixerPanel::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    auto foot = bounds.removeFromBottom(crossfaderHeight);
    crossfaderCurve.setBounds(foot.removeFromRight(56).reduced(0, 12));
    foot.removeFromRight(4);
    crossfader.setBounds(foot.withTrimmedLeft(16).withTrimmedRight(16).reduced(0, 6));
    bounds.removeFromBottom(4);
    const auto count = static_cast<int>(strips.size());
    stripPitch = count > 0 ? juce::jlimit(minimumStripWidth, stripWidth, (bounds.getWidth() - masterWidth) / count) : stripWidth;
    for (auto& strip : strips)
    {
        auto column = juce::Rectangle<int>(bounds.getX() + strip->channel * stripPitch, bounds.getY(), stripPitch, bounds.getHeight()).reduced(3, 0);
        strip->name.setBounds(column.removeFromTop(titleHeight));
        column.removeFromTop(4);
        for (auto* knob : {&strip->trim, &strip->comp, &strip->high, &strip->highMid, &strip->lowMid, &strip->low, &strip->filter, &strip->send})
            knobRow(column, *knob);
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
    layoutMaster(bounds.removeFromRight(masterWidth));
}

// The master section's three columns.
void DjMixerPanel::layoutMaster(juce::Rectangle<int> area)
{
    area.reduce(4, 0);
    const auto columnWidth = (area.getWidth() - 8) / 3;
    auto first = area.removeFromLeft(columnWidth);
    area.removeFromLeft(4);
    auto second = area.removeFromLeft(columnWidth);
    area.removeFromLeft(4);
    auto third = area;
    // One: the master and its isolator, the meters beside them, then the
    // filters' resonance and the faders' curve.
    masterTitle = first.removeFromTop(titleHeight);
    bpmArea = first.removeFromTop(titleHeight).reduced(2, 0);
    first.removeFromTop(4);
    auto meters = first.removeFromRight(20);
    first.removeFromRight(2);
    const auto metersTop = first.getY();
    knobRow(first, masterLevel);
    isolatorTitle = first.removeFromTop(knobLabelHeight);
    knobRow(first, masterHigh);
    knobRow(first, masterMid);
    knobRow(first, masterLow);
    const auto metersBottom = first.getY() - 2;
    masterMeterLeft.setBounds(meters.getX(), metersTop, 8, metersBottom - metersTop);
    masterMeterRight.setBounds(meters.getX() + 11, metersTop, 8, metersBottom - metersTop);
    first.removeFromTop(6);
    knobRow(first, resonance);
    first.removeFromTop(6);
    first.removeFromTop(knobLabelHeight);
    faderCurve.setBounds(first.removeFromTop(keyHeight));
    // Two: the headphones, then the mic.
    phonesTitle = second.removeFromTop(titleHeight);
    knobRow(second, cueMix);
    knobRow(second, cueLevel);
    monoSplit.setBounds(second.removeFromTop(keyHeight));
    second.removeFromTop(12);
    micTitle = second.removeFromTop(titleHeight);
    knobRow(second, micLevel);
    knobRow(second, micHigh);
    knobRow(second, micLow);
    micMode.setBounds(second.removeFromTop(keyHeight));
    second.removeFromTop(3);
    micMeter.setBounds(second.removeFromTop(6));
    // Three: the send unit, then the beat effect.
    sendTitle = third.removeFromTop(titleHeight);
    sendType.setBounds(third.removeFromTop(20));
    third.removeFromTop(3);
    knobPair(third, sendSize, sendTime);
    knobPair(third, sendTone, sendMix);
    third.removeFromTop(8);
    fxTitle = third.removeFromTop(titleHeight);
    fxType.setBounds(third.removeFromTop(20));
    third.removeFromTop(3);
    fxTarget.setBounds(third.removeFromTop(20));
    third.removeFromTop(3);
    auto bands = third.removeFromTop(keyHeight);
    const auto bandWidth = (bands.getWidth() - 4) / 3;
    fxLow.setBounds(bands.removeFromLeft(bandWidth));
    bands.removeFromLeft(2);
    fxMid.setBounds(bands.removeFromLeft(bandWidth));
    bands.removeFromLeft(2);
    fxHigh.setBounds(bands);
    third.removeFromTop(3);
    auto beat = third.removeFromTop(20);
    fxBeatDown.setBounds(beat.removeFromLeft(18));
    fxBeatUp.setBounds(beat.removeFromRight(18));
    beatReadout = beat.reduced(2, 0);
    third.removeFromTop(3);
    auto timing = third.removeFromTop(keyHeight);
    fxAuto.setBounds(timing.removeFromLeft(timing.getWidth() / 2 - 1));
    fxTap.setBounds(timing.withTrimmedLeft(2));
    third.removeFromTop(3);
    knobPair(third, fxTime, fxDepth);
    third.removeFromTop(3);
    fxOn.setBounds(third.removeFromTop(22));
}
}
