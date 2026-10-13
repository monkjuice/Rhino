#include "DjMixerPanelInternal.h"
#include <cmath>
#include <iterator>

// The booth's mixer, its master section: the master and its isolator, the
// headphones, the mic, the send unit, the beat effect and the crossfader,
// each knob described once; how they are dressed, shown and laid out. See
// DjMixerPanel.h.

namespace rhino
{
using namespace mixerpanel;

namespace
{
constexpr float beatChoices[] {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 3.0f / 4.0f, 1.0f, 2.0f, 4.0f};
const char* beatChoiceNames[] {"1/16", "1/8", "1/4", "1/2", "3/4", "1", "2", "4"};

int beatChoiceIndex(float beats)
{
    int index = 5;
    for (int i = 0; i < static_cast<int>(std::size(beatChoices)); ++i)
        if (std::abs(beatChoices[i] - beats) < 0.001f)
            index = i;
    return index;
}

// What the master section shows of the booth, read once a refresh.
struct BoothValues
{
    Session::DjMasterState master;
    Session::DjMicState mic;
    Session::DjSendFxState send;
    Session::DjFxState fx;
    Session::DjChannelState first;   // its resonance is every channel's
};

// A knob of the master section, described once as a strip's knobs are; a
// skew's mid-point of zero means none.
struct MasterKnobSpec
{
    const char* caption;
    const juce::Colour* accent;
    double minimum, maximum, resting, skewMidPoint;
    const char* tooltip;
    void (*set)(Session&, double value);
    double (*read)(const BoothValues&);
};

const std::array<MasterKnobSpec, DjMixerPanel::masterKnobCount> masterKnobSpecs {{
    {"LEVEL", &palette::volume, DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, 0.0, 0.0,
     "Master level, in dB.",
     [](Session& s, double v) { s.setDjMasterLevel(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.master.levelDb); }},
    {"HI", &palette::deviceAccent, 0.0, 1.0, 0.5, 0.0,
     "Master isolator high: off at the bottom, +6 dB at the top.",
     [](Session& s, double v) { s.setDjMasterEq(2, eqDbFor(v)); },
     [](const BoothValues& b) { return knobForEqDb(b.master.highDb); }},
    {"MID", &palette::deviceAccent, 0.0, 1.0, 0.5, 0.0,
     "Master isolator mid: off at the bottom, +6 dB at the top.",
     [](Session& s, double v) { s.setDjMasterEq(1, eqDbFor(v)); },
     [](const BoothValues& b) { return knobForEqDb(b.master.midDb); }},
    {"LOW", &palette::deviceAccent, 0.0, 1.0, 0.5, 0.0,
     "Master isolator low: off at the bottom, +6 dB at the top.",
     [](Session& s, double v) { s.setDjMasterEq(0, eqDbFor(v)); },
     [](const BoothValues& b) { return knobForEqDb(b.master.lowDb); }},
    {"RESONANCE", &palette::pan, 0.0, 1.0, 0.2, 0.0,
     "Resonance of every channel's colour filter.",
     [](Session& s, double v)
     {
         for (int i = 0; i < Session::maximumDjDecks; ++i)
             s.setDjChannelResonance(i, static_cast<float>(v));
     },
     [](const BoothValues& b) { return static_cast<double>(b.first.resonance); }},
    {"MIX", &palette::activeNeutral, 0.0, 1.0, 0.0, 0.0,
     "Headphones mix: the cued channels to the left, the master to the right, on outputs 3 and 4.",
     [](Session& s, double v) { s.setDjCueMix(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.master.cueMix); }},
    {"LEVEL", &palette::volume, DjMasterSection::minimumLevelDb, DjMasterSection::maximumLevelDb, 0.0, 0.0,
     "Headphones level, in dB.",
     [](Session& s, double v) { s.setDjCueLevel(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.master.cueLevelDb); }},
    {"LEVEL", &palette::recordAccent, DjMicSection::minimumLevelDb, DjMicSection::maximumLevelDb, 0.0, 0.0,
     "Mic level, in dB. The mic is the audio device's first input.",
     [](Session& s, double v) { s.setDjMicLevel(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.mic.levelDb); }},
    {"HI", &palette::deviceAccent, -12.0, 12.0, 0.0, 0.0,
     "Mic high shelf, in dB.",
     [](Session& s, double v) { s.setDjMicEq(1, static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.mic.highDb); }},
    {"LOW", &palette::deviceAccent, -12.0, 12.0, 0.0, 0.0,
     "Mic low shelf, in dB.",
     [](Session& s, double v) { s.setDjMicEq(0, static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.mic.lowDb); }},
    {"SIZE", &palette::midiEffect, 0.0, 1.0, 0.5, 0.0,
     "Send effect size: how long it rings on.",
     [](Session& s, double v) { s.setDjSendFxSize(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.send.size); }},
    {"TIME", &palette::midiEffect, 0.0, 1.0, 0.5, 0.0,
     "Send effect time: the delay's length, or a reverb's pre-delay.",
     [](Session& s, double v) { s.setDjSendFxTime(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.send.time); }},
    {"TONE", &palette::midiEffect, 0.0, 1.0, 0.5, 0.0,
     "Send effect tone: dark to the left, bright to the right.",
     [](Session& s, double v) { s.setDjSendFxTone(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.send.tone); }},
    {"MIX", &palette::midiEffect, 0.0, 1.0, 0.5, 0.0,
     "How loud the send effect comes back into the master.",
     [](Session& s, double v) { s.setDjSendFxMix(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.send.mix); }},
    {"TIME", &palette::midiEffect, 0.001, 4.0, 0.25, 0.25,
     "The effect's time in seconds, when Auto is off.",
     [](Session& s, double v) { s.setDjFxTime(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.fx.manualSeconds); }},
    {"DEPTH", &palette::midiEffect, 0.0, 1.0, 0.5, 0.0,
     "Level/depth of the effect.",
     [](Session& s, double v) { s.setDjFxDepth(static_cast<float>(v)); },
     [](const BoothValues& b) { return static_cast<double>(b.fx.depth); }},
}};

void dressBox(juce::ComboBox& box, const juce::String& tooltip)
{
    box.setTooltip(tooltip);
    box.setWantsKeyboardFocus(false);
}

juce::String bpmReadout(const Session::DjMasterState& master)
{
    if (master.bpm > 0.0) return juce::String(master.bpm, 2) + " BPM";
    return "TAP " + juce::String(master.tapBpm, 1);
}
}

// The master section's knobs from their table, and its keys, boxes and
// crossfader.
void DjMixerPanel::dressMaster()
{
    for (size_t i = 0; i < masterKnobSpecs.size(); ++i)
    {
        const auto& spec = masterKnobSpecs[i];
        auto& knob = masterKnobs[i];
        dressDjKnob(knob, *spec.accent, spec.minimum, spec.maximum, spec.resting, spec.tooltip);
        if (spec.skewMidPoint > 0.0)
            knob.setSkewFactorFromMidPoint(spec.skewMidPoint);
        knob.onValueChange = [this, &knob, &spec] { if (!updating) spec.set(session, knob.getValue()); };
    }
    faderCurve.setTooltip("The channel faders' curve: gentle, normal or steep - how soon a fader comes up. Press to cycle.");
    faderCurve.onClick = [this]
    {
        const auto next = (session.djChannel(0).faderCurve + 1) % 3;
        for (int i = 0; i < Session::maximumDjDecks; ++i)
            session.setDjChannelFaderCurve(i, next);
    };
    monoSplit.setTooltip("Mono split: the cue in the left ear, the master in the right.");
    monoSplit.onClick = [this] { session.setDjMonoSplit(!session.djMaster().monoSplit); };
    micMode.setTooltip("The mic: off, on, or on with talkover, which ducks the master while the mic is spoken into. "
                       "Press to cycle.");
    micMode.onClick = [this] { session.setDjMicMode((session.djMic().mode + 1) % 3); };
    for (int i = 0; i < Session::djSendFxTypeCount(); ++i)
        sendType.addItem(Session::djSendFxTypeName(i), i + 1);
    dressBox(sendType, "The send effect, fed by every channel's SEND knob.");
    sendType.onChange = [this] { if (!updating && sendType.getSelectedId() > 0) session.setDjSendFxType(sendType.getSelectedId() - 1); };
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
    fxOn.setTooltip("The effect on or off. Echo and delay ring out when switched off.");
    fxOn.onClick = [this] { session.setDjFxOn(!session.djFx().on); };
    // A tap and the effect's switch act as the hand comes down, as the
    // DJM's do: a tapped tempo is the time between presses.
    fxTap.setTriggeredOnMouseDown(true);
    fxOn.setTriggeredOnMouseDown(true);
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
}

void DjMixerPanel::stepFxBeats(int direction)
{
    const auto index = juce::jlimit(0, static_cast<int>(std::size(beatChoices)) - 1, beatChoiceIndex(session.djFx().beats) + direction);
    session.setDjFxBeats(beatChoices[index]);
}

void DjMixerPanel::refreshMaster(bool meters)
{
    const BoothValues booth {session.djMaster(), session.djMic(), session.djSendFx(), session.djFx(), session.djChannel(0)};
    if (meters)
    {
        masterMeterLeft.feed(booth.master.meterLeft);
        masterMeterRight.feed(booth.master.meterRight);
        micMeter.feed(booth.mic.meter);
    }
    for (size_t i = 0; i < masterKnobSpecs.size(); ++i)
        masterKnobs[i].setValue(masterKnobSpecs[i].read(booth), juce::dontSendNotification);
    monoSplit.setLit(booth.master.monoSplit);
    if (!strips.empty() && booth.first.faderCurve != shownFaderCurve)
    {
        shownFaderCurve = booth.first.faderCurve;
        faderCurve.setLabel(shownFaderCurve == 0 ? "GENTLE" : shownFaderCurve == 2 ? "STEEP" : "NORMAL");
    }
    if (!crossfader.isDragging())
        crossfader.setValue((booth.master.crossfader + 1.0f) * 0.5f, false);
    const auto curve = booth.master.crossfaderCurve < 0.25f ? 0 : booth.master.crossfaderCurve < 0.75f ? 1 : 2;
    if (curve != shownCrossfaderCurve)
    {
        shownCrossfaderCurve = curve;
        crossfaderCurve.setLabel(curve == 0 ? "SMOOTH" : curve == 1 ? "LINEAR" : "CUT");
    }
    if (booth.mic.mode != shownMicMode)
    {
        shownMicMode = booth.mic.mode;
        micMode.setLabel(shownMicMode == 0 ? "MIC OFF" : shownMicMode == 1 ? "MIC ON" : "TALKOVER");
        micMode.setLit(shownMicMode != 0);
    }
    sendType.setSelectedId(booth.send.type + 1, juce::dontSendNotification);
    fxType.setSelectedId(booth.fx.type + 1, juce::dontSendNotification);
    fxTarget.setSelectedId(booth.fx.target + 2, juce::dontSendNotification);
    fxLow.setLit(booth.fx.bands[0]);
    fxMid.setLit(booth.fx.bands[1]);
    fxHigh.setLit(booth.fx.bands[2]);
    fxAuto.setLit(booth.fx.autoTime);
    masterKnobs[fxTime].setEnabled(!booth.fx.autoTime);
    fxOn.setLit(booth.fx.on);
    // The readouts: the tempo the effect follows - the master deck's, or the
    // tapped one when nothing plays - and its beat. The text is decided here
    // and painted as it was decided, so a paint for some other part of the
    // panel never takes up a value the screen does not show.
    if (const auto beat = beatChoiceIndex(booth.fx.beats); beat != shownBeat)
    {
        shownBeat = beat;
        beatLine.set(beatChoiceNames[beat], readoutFont, beatArea, juce::Justification::centred);
        repaint(beatArea);
    }
    if (auto readout = bpmReadout(booth.master); readout != shownBpm)
    {
        shownBpm = std::move(readout);
        bpmLine.set(shownBpm, readoutFont, bpmArea, juce::Justification::centred);
        repaint(bpmArea);
    }
}

// The master section's three columns, and the words over them.
void DjMixerPanel::layoutMaster(juce::Rectangle<int> area)
{
    area.reduce(4, 0);
    const auto columnWidth = (area.getWidth() - 8) / 3;
    auto first = area.removeFromLeft(columnWidth);
    area.removeFromLeft(4);
    auto second = area.removeFromLeft(columnWidth);
    area.removeFromLeft(4);
    auto third = area;
    const auto titleFont = uiFontBold(9.0f);
    const auto title = [&titleFont](ShapedLine& line, const juce::String& text, juce::Rectangle<int> box)
    {
        line.set(text, titleFont, box, juce::Justification::centred);
    };
    // One: the master and its isolator, the meters beside them, then the
    // filters' resonance and the faders' curve.
    title(masterTitle, "MASTER", first.removeFromTop(titleHeight));
    bpmArea = first.removeFromTop(titleHeight).reduced(2, 0);
    first.removeFromTop(4);
    auto meters = first.removeFromRight(20);
    first.removeFromRight(2);
    const auto metersTop = first.getY();
    knobRow(first, masterKnobs[masterLevel]);
    title(isolatorTitle, "ISOLATOR", first.removeFromTop(knobLabelHeight));
    knobRow(first, masterKnobs[masterHigh]);
    knobRow(first, masterKnobs[masterMid]);
    knobRow(first, masterKnobs[masterLow]);
    const auto metersBottom = first.getY() - 2;
    masterMeterLeft.setBounds(meters.getX(), metersTop, 8, metersBottom - metersTop);
    masterMeterRight.setBounds(meters.getX() + 11, metersTop, 8, metersBottom - metersTop);
    first.removeFromTop(6);
    knobRow(first, masterKnobs[resonance]);
    first.removeFromTop(6);
    first.removeFromTop(knobLabelHeight);
    faderCurve.setBounds(first.removeFromTop(keyHeight));
    // Two: the headphones, then the mic.
    title(phonesTitle, "HEADPHONES", second.removeFromTop(titleHeight));
    knobRow(second, masterKnobs[cueMix]);
    knobRow(second, masterKnobs[cueLevel]);
    monoSplit.setBounds(second.removeFromTop(keyHeight));
    second.removeFromTop(12);
    title(micTitle, "MIC", second.removeFromTop(titleHeight));
    knobRow(second, masterKnobs[micLevel]);
    knobRow(second, masterKnobs[micHigh]);
    knobRow(second, masterKnobs[micLow]);
    micMode.setBounds(second.removeFromTop(keyHeight));
    second.removeFromTop(3);
    micMeter.setBounds(second.removeFromTop(6));
    // Three: the send unit, then the beat effect.
    title(sendTitle, "SEND FX", third.removeFromTop(titleHeight));
    sendType.setBounds(third.removeFromTop(20));
    third.removeFromTop(3);
    knobPair(third, masterKnobs[sendSize], masterKnobs[sendTime]);
    knobPair(third, masterKnobs[sendTone], masterKnobs[sendMix]);
    third.removeFromTop(8);
    title(fxTitle, "BEAT FX", third.removeFromTop(titleHeight));
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
    beatArea = beat.reduced(2, 0);
    third.removeFromTop(3);
    auto timing = third.removeFromTop(keyHeight);
    fxAuto.setBounds(timing.removeFromLeft(timing.getWidth() / 2 - 1));
    fxTap.setBounds(timing.withTrimmedLeft(2));
    third.removeFromTop(3);
    knobPair(third, masterKnobs[fxTime], masterKnobs[fxDepth]);
    third.removeFromTop(3);
    fxOn.setBounds(third.removeFromTop(22));
    const auto captionFont = uiFontBold(8.0f);
    for (size_t i = 0; i < masterKnobSpecs.size(); ++i)
        masterCaptions[i].set(masterKnobSpecs[i].caption, captionFont, captionOver(masterKnobs[i]), juce::Justification::centred);
    faderCurveCaption.set("CH CURVE", captionFont, captionOver(faderCurve), juce::Justification::centred);
    if (shownBeat >= 0)
        beatLine.set(beatChoiceNames[shownBeat], readoutFont, beatArea, juce::Justification::centred);
    bpmLine.set(shownBpm, readoutFont, bpmArea, juce::Justification::centred);
}
}
