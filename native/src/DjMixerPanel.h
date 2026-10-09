#pragma once
#include "Session.h"
#include "DjControls.h"
#include <memory>
#include <vector>

namespace rhino
{
// The booth's mixer, a DJM laid out across the middle of the view: a strip
// per deck - trim, the three isolator bands, the colour filter, cue and the
// effect send, its meter and fader, and which side of the crossfader it is
// on - then the master section with its isolator and the beat effect, and
// the crossfader along the foot.
class DjMixerPanel final : public juce::Component
{
public:
    explicit DjMixerPanel(Session&);
    // Strips follow the deck count; every control re-reads its value.
    void sync();
    // At the view's timer rate: the meters.
    void tick();
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void(juce::String)> status;

    static constexpr int stripWidth = 68, masterWidth = 150, crossfaderHeight = 44;
    int preferredWidth() const;

private:
    friend int runArrangementTest();
    struct Strip
    {
        int channel = 0;
        juce::Label name;
        juce::Slider trim, high, mid, low, filter;
        DjPad cue {"CUE", palette::djCue}, fx {"FX", palette::midiEffect};
        DjMeter meter;
        DjFader fader {true, palette::volume};
        juce::TextButton side;
    };
    void buildStrips(int count);
    void dressStrip(Strip&);
    static float eqDbFor(double knob);
    static double knobForEqDb(float decibels);
    void showFxMenus();

    Session& session;
    std::vector<std::unique_ptr<Strip>> strips;
    juce::Label masterLabel, fxLabel;
    juce::Slider masterLevel, masterHigh, masterMid, masterLow, resonance, cueMix;
    DjMeter masterMeterLeft, masterMeterRight;
    juce::ComboBox fxType, fxBeats, fxTarget;
    juce::Slider fxDepth;
    DjPad fxOn {"ON", palette::midiEffect};
    DjFader crossfader {false, palette::activeNeutral};
    DjPad crossfaderCurve {"CUT", palette::activeNeutral};
    bool updating = false;
};
}
