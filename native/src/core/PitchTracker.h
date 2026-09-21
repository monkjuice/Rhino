#pragma once
#include <vector>

namespace rhino
{
// Monophonic pitch tracking for the auto-tune device.
//
// The algorithm is YIN: the squared difference function over a window,
// normalised by its own cumulative mean, then the first minimum under a
// threshold.  The normalisation is what separates it from a plain
// autocorrelation -- it is why the tracker reports the fundamental of a saw or
// a vowel rather than its strongest partial, and why it comes with a usable
// confidence rather than just a peak height.
//
// The difference function is computed through an FFT.  Done directly it is
// window x lag multiply-adds per analysis, which at 48 kHz is hundreds of
// millions of operations a second for one voice; through a transform it is a
// few hundred thousand.
//
// Nothing here knows about the engine or about JUCE beyond the standard
// library, so a test can drive it with a synthesised tone and compare the
// answer against the frequency it generated.
class PitchTracker
{
public:
    // Which band the voice sits in.  This is Auto Shift's High / Mid / Bass:
    // a low note needs a longer window before two periods of it exist, so the
    // choice buys reach with latency and there is no setting that is free.
    enum class Range { High, Mid, Bass };
    static constexpr int rangeCount = 3;

    struct Estimate
    {
        float frequency = 0.0f;  // Hz; 0 when nothing periodic was found
        float midiNote = 0.0f;   // the fractional note for that frequency
        float clarity = 0.0f;    // 0..1, how periodic the window was
        bool voiced = false;     // clear enough and loud enough to correct
    };

    // Frame lengths at 48 kHz.  Everything else -- lag bounds, hop, latency --
    // is derived from these, and they scale with the sample rate.
    static int frameSizeFor(Range range, double sampleRate);
    static float lowestFrequency(Range);
    static float highestFrequency(Range);

    void prepare(double sampleRate, Range);
    void reset();

    // Appends mono samples and analyses whenever a hop has filled.  Safe to
    // call from the audio thread: prepare() does all the allocation.
    void process(const float* samples, int count);

    const Estimate& latest() const { return estimate; }
    int frameSize() const { return frame; }
    int hopSize() const { return hop; }
    // The longest period the tracker will report, which is what the shifter
    // has to be able to read around and therefore sets the device's latency.
    int longestPeriodSamples() const { return maxLag; }

private:
    void analyse();
    void transform(std::vector<float>& re, std::vector<float>& im, bool inverse) const;

    double sampleRate = 48000.0;
    Range range = Range::Mid;
    int frame = 2048;      // samples the analysis reads; also the FFT size
    int window = 1024;     // integration window, half the frame
    int hop = 256;
    int minLag = 48, maxLag = 1024;

    std::vector<float> history;   // ring of `frame` samples
    int writeIndex = 0, sinceAnalysis = 0;
    bool primed = false;

    // Scratch, all sized in prepare() so analyse() never allocates.
    std::vector<float> frameBuffer, cumulativeEnergy, difference, normalised;
    std::vector<float> realA, imagA, realB, imagB;
    std::vector<float> cosTable, sinTable;
    std::vector<int> bitReversed;

    Estimate estimate;
};
}
