#pragma once
#include <vector>

namespace rhino
{
// Pitch and formant shifting by time-domain pitch-synchronous overlap-add.
//
// PSOLA is what a pitch corrector wants and a phase vocoder is not: it cuts
// the signal into one grain per glottal pulse and re-spaces those grains, so
// the vocal tract resonances stay where they were however far the pitch moves.
// Formant shifting then falls out of the same machinery -- resampling a grain
// without changing where the grains are placed moves the resonances and
// leaves the pitch alone, which is the one thing a vocal effect is asked for
// that a plain transposer cannot do.
//
// It is monophonic by construction.  On a chord it will pick one note and
// smear the rest, which is why the device says vocals.
//
// All channels share one set of grain marks, taken from their sum, so a
// stereo signal stays coherent instead of drifting into two pitches.
class PsolaShifter
{
public:
    struct Target
    {
        // The period the tracker measured, in samples.  Grains are cut two of
        // these long, so this is also what sets the device's latency.
        float periodSamples = 240.0f;
        // Output frequency over input frequency: 2.0 is an octave up.
        float pitchRatio = 1.0f;
        // Where the resonances go, independently of the pitch.
        float formantRatio = 1.0f;
        // False for breath and consonants, where there is no period to be
        // synchronous with and the grains should just rebuild the input.
        bool voiced = false;
    };

    // The smallest delay a shifter can run at for a given period: grains are
    // laid down one length past the end of the block they belong to, and each
    // reaches a period and a quarter back into the input to find its pulse.
    static int minimumLatencyFor(int longestPeriod);

    // Sized once, for the longest period the device will ever be asked to
    // track.  Switching the tracking range afterwards is a configure() call,
    // not another prepare(): the range changes the latency and the grain
    // length, and neither may allocate on the audio thread.
    void prepare(double sampleRate, int channels, int maxBlockSize, int longestPossiblePeriod);
    void configure(int latency, int longestPeriodForRange);
    void reset();

    void setTarget(const Target& target) { pending = target; }

    // In place, one block.  `channels` may be fewer than prepare() was given.
    void process(float* const* data, int channels, int count);

    int latencySamples() const { return latency; }
    int maximumGrainHalfLength() const { return maxGrainHalf; }

private:
    void emitGrain(int channels);
    float readInput(int channel, double position) const;
    float hann(double phase) const;

    double sampleRate = 48000.0;
    int latency = 0, maxGrainHalf = 0, longestPeriod = 480, preparedChannels = 0;
    int capacityPeriod = 480, inputMask = 0, outputMask = 0;

    std::vector<std::vector<float>> input;   // one ring per channel
    std::vector<std::vector<float>> output;  // one accumulator per channel
    std::vector<float> mono;                 // the sum, for finding pulses
    std::vector<float> windowSum;            // shared: how much window covers each output sample
    std::vector<float> hannTable;

    Target pending, current;
    long long inputWritten = 0, outputProduced = 0;
    double synthPosition = 0.0, analysisPosition = 0.0, epochOffset = 0.0;
    bool started = false;
};
}
