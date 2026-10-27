#pragma once
#include <vector>

namespace rhino
{
// Runs a nonlinearity at two or four times the sample rate, so the harmonics
// it makes above the original Nyquist are filtered away instead of folding
// back down as aliases.
//
// Each doubling is a linear-phase half-band FIR, designed at prepare() by
// windowing a sinc with a Kaiser window, so no coefficient table is copied
// in from anywhere. A half-band filter has every other tap zero, which the
// polyphase loops below skip. The first doubling is the steep one: it passes
// 20 kHz at 48 kHz and stops 80 dB down from 28 kHz, where the image of 20 kHz
// lands. A second doubling only has to reject the band its predecessor already
// emptied, so it is short.
//
// Linear phase means a fixed delay, and the delay is padded to a whole number
// of samples at the outer rate. A device mixing its dry signal back in delays
// the dry by latencySamples() and the two line up exactly.
//
// Plain C++ with no JUCE, so a test can sweep it with sines.
class Oversampler
{
public:
    // 1, 2 or 4. One is a pass-through with no latency.
    void prepare(int factor, int channels, int maximumBlockSize);
    void reset();

    int factor() const { return ratio; }
    // Up and down together, in samples at the outer rate.
    int latencySamples() const { return latency; }

    // Upsamples `count` samples of one channel into the inner buffer and
    // returns it: count * factor() samples, to be processed in place.
    float* up(int channel, const float* input, int count);
    // Brings the inner buffer back down into `output` (count samples).
    void down(int channel, float* output, int count);

private:
    struct Stage
    {
        std::vector<float> sideTaps;    // the nonzero taps either side of the centre, nearest first
        int halfLength = 0;             // sideTaps.size(), K in the comments of the .cpp
        // Per channel: the upsampler's input history, and the downsampler's
        // even and odd input histories, each a ring written twice so a run of
        // taps is always contiguous.
        struct Channel
        {
            std::vector<float> upHistory, evenHistory, oddHistory;
            int upAt = 0, evenAt = 0, oddAt = 0;
        };
        std::vector<Channel> channels;
    };

    static Stage design(int halfLength, double stopbandDb, int channels);
    static void upStage(Stage&, int channel, const float* input, float* output, int count);
    static void downStage(Stage&, int channel, const float* input, float* output, int count);

    int ratio = 1;
    int latency = 0;
    std::vector<Stage> stages;
    // The inner buffers: one per stage boundary and channel.
    std::vector<std::vector<float>> twice, fourTimes;
    // Pads the round trip to a whole outer sample (see prepare()).
    std::vector<std::vector<float>> pad;
    std::vector<int> padAt;
    int padLength = 0;
};
}
