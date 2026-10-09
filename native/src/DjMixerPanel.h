#pragma once
#include "Session.h"
#include "DjControls.h"
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
class DjMixerPanel final : public juce::Component
{
public:
    explicit DjMixerPanel(Session&);
    // Strips follow the deck count; every control re-reads its value.
    void sync();
    // At the view's timer rate: the meters and the tempo readout.
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void(juce::String)> status;

    static constexpr int preferredHeight = 560, masterWidth = 246, crossfaderHeight = 44;
    static constexpr int stripWidth = 68, minimumStripWidth = 50;
    int preferredWidth() const;
    int minimumWidth() const;

private:
    friend int runArrangementTest();
    struct Strip
    {
        int channel = 0;
        juce::Label name;
        juce::Slider trim, comp, high, highMid, lowMid, low, filter, send;
        DjPad cue {"CUE", palette::djCue}, fx {"FX", palette::midiEffect};
        DjMeter meter;
        DjFader fader {true, palette::volume};
        DjPad side {"THRU", palette::activeNeutral};
    };
    void buildStrips(int count);
    void dressStrip(Strip&);
    static float eqDbFor(double knob);
    static double knobForEqDb(float decibels);
    void layoutMaster(juce::Rectangle<int> area);
    juce::String bpmReadout() const;
    void stepFxBeats(int direction);

    Session& session;
    std::vector<std::unique_ptr<Strip>> strips;
    int stripPitch = stripWidth;
    // The master column.
    juce::Slider masterLevel, masterHigh, masterMid, masterLow, resonance;
    DjMeter masterMeterLeft, masterMeterRight;
    DjPad faderCurve {"NORMAL", palette::activeNeutral};
    // The headphones and the mic.
    juce::Slider cueMix, cueLevel;
    DjPad monoSplit {"MONO SPLIT", palette::activeNeutral};
    juce::Slider micLevel, micHigh, micLow;
    DjPad micMode {"MIC OFF", palette::recordAccent};
    DjMeter micMeter {false};
    // The send/return unit.
    juce::ComboBox sendType;
    juce::Slider sendSize, sendTime, sendTone, sendMix;
    // The beat effect.
    juce::ComboBox fxType, fxTarget;
    DjPad fxLow {"LOW", palette::midiEffect}, fxMid {"MID", palette::midiEffect}, fxHigh {"HI", palette::midiEffect};
    DjPad fxBeatDown {"<", palette::activeNeutral}, fxBeatUp {">", palette::activeNeutral};
    DjPad fxAuto {"AUTO", palette::djCue}, fxTap {"TAP", palette::activeNeutral};
    juce::Slider fxTime, fxDepth;
    DjPad fxOn {"ON / OFF", palette::midiEffect};
    DjFader crossfader {false, palette::activeNeutral};
    DjPad crossfaderCurve {"SMOOTH", palette::activeNeutral};
    // Where the painted titles and readouts go, set by resized().
    juce::Rectangle<int> masterTitle, bpmArea, isolatorTitle, phonesTitle, micTitle, sendTitle, fxTitle, beatReadout;
    juce::String drawnBpm;
    bool updating = false;
};
}
