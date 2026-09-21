#pragma once
#include "EqFilter.h"
#include "SpectrumAnalyser.h"
#include <array>
#include <vector>

// Eight bands of EQ and the tap that feeds the display behind them.
//
// Everything that decides how the device sounds is here, in RhinoCore, where
// a test drives it with a synthesised signal and measures what comes back --
// the same split Rhino Tune uses, and for the same reason: a claim about a
// filter is settled by measuring it, not by reading the coefficients.
namespace rhino
{

class EqEngine
{
public:
    static constexpr int bandCount = 8;

    // Which side of the chain the display is watching. Off is not a saving
    // worth having on the audio thread -- it is one memcpy -- but it is worth
    // having on the drawing thread, which stops asking for frames.
    enum class AnalyserMode { Off, Pre, Post };
    inline static constexpr int analyserModeCount = 3;

    struct Settings
    {
        std::array<EqBandSettings, bandCount> band {};
        float outputGainDb = 0.0f;
        float scalePercent = 100.0f;
        AnalyserMode analyser = AnalyserMode::Post;
    };

    void prepare(double sampleRate, int channels, int maxBlockSize);
    void reset();

    // Audio thread, at the top of a block.
    void setSettings(const Settings&);
    void process(float* const* data, int channels, int count);

    const SpectrumTap& tap() const { return spectrum; }
    double rate() const { return sampleRate; }

    // Scale rides every band at once, the way EQ Eight does, so the curve has
    // to be drawn from the scaled bands rather than the stored ones or the
    // display and the audio part company as soon as it leaves 100%.
    static EqBandSettings scaledBand(const EqBandSettings&, float scalePercent);

    // The whole chain at one frequency, in dB, output gain included. This is
    // what the panel draws, and it reaches the same coefficients the samples
    // go through.
    static double responseDbAt(const Settings&, double frequency, double sampleRate);

private:
    struct BandRuntime
    {
        // `stages` is the filter the band has; `leaving` is the one it had
        // until the type last changed, kept alive so the two can be
        // crossfaded. Frequency, gain and Q all move continuously and need
        // none of this -- a type does not, and swapping coefficients under a
        // running filter is a click.
        EqBandStages stages {}, leaving {};
        // Smoothed towards the settings so a knob sweep is a sweep rather
        // than a staircase. Frequency is smoothed in log space: halfway
        // between 100 Hz and 10 kHz should be 1 kHz, not 5 kHz.
        double logFrequency = 0.0, gainDb = 0.0, q = 0.71;
        float level = 0.0f, levelTarget = 0.0f;
        // 1 once the band is fully on its current filter. A second type
        // change part way through a crossfade drops the blend and starts
        // again from the filter that is leaving, which is a small step rather
        // than the click it replaces.
        float morph = 1.0f;
        EqFilterType type = EqFilterType::Bell;
        bool coefficientsValid = false;
    };

    // The two delays of one biquad, for the filter a band has and the one it
    // is fading out of. Double because a 20 Hz Butterworth high pass at
    // 48 kHz in single precision is audibly noisy.
    struct StageState
    {
        std::array<double, 2> current {}, leaving {};
    };

    void refreshCoefficients(BandRuntime&);
    void flushDenormals();

    Settings settings;
    std::array<BandRuntime, bandCount> runtime {};
    std::vector<std::array<std::array<StageState, eqMaxStagesPerBand>, bandCount>> state;
    double sampleRate = 48000.0;
    double outputGain = 1.0, outputGainTarget = 1.0;
    bool primed = false;
    SpectrumTap spectrum;
};

}
