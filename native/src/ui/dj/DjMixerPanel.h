#pragma once
#include "model/Session.h"
#include "DjControls.h"
#include "ui/shared/ShapedText.h"
#include <array>
#include <memory>
#include <vector>

namespace rhino
{
// The booth's mixer, a DJM-V10 laid out across the middle of the view. A
// strip per deck carries what the V10's channel does, top to bottom: trim,
// the compressor, the four EQ bands, the colour filter, the send, cue and
// the effect assign, the fader with its meter, and the crossfader assign.
// The master section beside the strips is three columns: the master level
// and isolator with the meters, the filter resonance and the fader curve;
// the headphones and the mic; the send/return unit and the beat effect.
// The crossfader runs along the foot.
//
// The mixer stands at one height, as the hardware does: its strips do not
// stretch with the window. Its width follows the strips.
//
// Every value it shows - knobs, keys, faders, meters, the readouts - is read
// on the view's 30 Hz tick, so a value changed anywhere shows within a tick
// whether or not the session announced it, and each control repaints only
// when its own value moves. What the session announces changes only the
// panel's shape: how many strips, and what each deck plays. Each knob is
// described once, in a table in DjMixerPanel.cpp: its caption, colour,
// range, and the booth value it sets and shows.
class DjMixerPanel final : public juce::Component
{
public:
    explicit DjMixerPanel(Session&);
    // A strip for each deck, named for what it plays.
    void sync(const std::vector<Session::DjDeckInfo>& decks);
    void sync();
    // At the view's timer rate: every value the booth holds, the meters and
    // the readouts.
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 560, masterWidth = 246, crossfaderHeight = 44;
    static constexpr int stripWidth = 68, minimumStripWidth = 50;
    int preferredWidth() const;
    int minimumWidth() const;

    // The knobs, in the tables' order: a strip's top to bottom, and the
    // master section's.
    static constexpr int stripKnobCount = 8;
    enum MasterKnob
    {
        masterLevel, masterHigh, masterMid, masterLow, resonance, cueMix, cueLevel, micLevel, micHigh, micLow,
        sendSize, sendTime, sendTone, sendMix, fxTime, fxDepth, masterKnobCount
    };

private:
    friend int runArrangementTest();
    friend int runUiProfile();
    struct Strip
    {
        int channel = 0;
        juce::Label name;
        std::array<juce::Slider, stripKnobCount> knobs;
        std::array<ShapedLine, stripKnobCount> captions;
        DjPad cue {"CUE", palette::djCue}, fx {"FX", palette::midiEffect};
        DjMeter meter;
        DjFader fader {true, palette::volume};
        DjPad side {"THRU", palette::activeNeutral};
        int shownSide = -1;
    };
    // Shows every value the booth holds; with `meters`, feeds the meters too,
    // which fall and hold a peak by the tick.
    void refresh(bool meters);
    // DjMixerPanelStrips.cpp: a strip per deck, from the strip's knob table.
    void buildStrips(int count);
    void dressStrip(Strip&);
    void refreshStrips(bool meters);
    void layoutStrips(juce::Rectangle<int> area);
    // DjMixerPanelMaster.cpp: the master section, from its knob table.
    void dressMaster();
    void refreshMaster(bool meters);
    void layoutMaster(juce::Rectangle<int> area);
    void stepFxBeats(int direction);
    // Whether a repaint reaches an area; everything does with culling off.
    bool reaches(const juce::Graphics&, juce::Rectangle<int> area) const;

    Session& session;
    std::vector<std::unique_ptr<Strip>> strips;
    int stripPitch = stripWidth;
    std::array<juce::Slider, masterKnobCount> masterKnobs;
    std::array<ShapedLine, masterKnobCount> masterCaptions;
    // The master column.
    DjMeter masterMeterLeft, masterMeterRight;
    DjPad faderCurve {"NORMAL", palette::activeNeutral};
    // The headphones and the mic.
    DjPad monoSplit {"MONO SPLIT", palette::activeNeutral};
    DjPad micMode {"MIC OFF", palette::recordAccent};
    DjMeter micMeter {false};
    // The send/return unit.
    juce::ComboBox sendType;
    // The beat effect.
    juce::ComboBox fxType, fxTarget;
    DjPad fxLow {"LOW", palette::midiEffect}, fxMid {"MID", palette::midiEffect}, fxHigh {"HI", palette::midiEffect};
    DjPad fxBeatDown {"<", palette::activeNeutral}, fxBeatUp {">", palette::activeNeutral};
    DjPad fxAuto {"AUTO", palette::djCue}, fxTap {"TAP", palette::activeNeutral};
    DjPad fxOn {"ON / OFF", palette::midiEffect};
    DjFader crossfader {false, palette::activeNeutral};
    DjPad crossfaderCurve {"SMOOTH", palette::activeNeutral};
    // The painted words, shaped where resized() puts them: the section
    // titles, the fader curve's caption, A and B at the crossfader's ends,
    // and the two readouts, whose text the tick decides and the paint draws.
    ShapedLine masterTitle, isolatorTitle, phonesTitle, micTitle, sendTitle, fxTitle, faderCurveCaption, aLine, bLine;
    juce::Rectangle<int> bpmArea, beatArea;
    ShapedLine bpmLine, beatLine;
    juce::String shownBpm;
    int shownBeat = -1, shownFaderCurve = -1, shownCrossfaderCurve = -1, shownMicMode = -1;
    const juce::Font readoutFont {uiFont(10.0f)};
    bool updating = false;
    // Off for the check that a culled paint matches drawing everything.
    bool cullRepaints = true;
};
}
