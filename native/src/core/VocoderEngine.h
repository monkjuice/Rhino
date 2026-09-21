#pragma once
#include <array>
#include <atomic>
#include <vector>

// A channel vocoder: the modulator's spectrum, played by the carrier.
//
// Everything that decides how the device sounds is here, in RhinoCore, where a
// test can push two synthesised signals through it and measure what comes back
// -- the same split Rhino Tune and Rhino EQ use, and for the same reason: a
// claim about a filter bank is settled by measuring it, not by reading the
// coefficients.
//
// Two ideas shape the design.
//
// The carrier is normalised before it reaches the filter bank.  A vocoder's
// output is a product of two amplitudes, so left alone its level follows the
// carrier's as well as the voice's, and an external carrier is whatever the
// track it came from happens to be doing.  With the carrier held at a fixed
// RMS the output level tracks the *modulator*, which is what the ear expects:
// speak louder and it gets louder.
//
// The modulator is analysed in mono, the carrier is filtered per channel.  A
// voice arrives mono on a stereo track almost always, and analysing the sum
// costs half the filters; the carrier keeps its stereo image because the band
// gains are the same on both sides.
namespace rhino
{

class VocoderEngine
{
public:
    static constexpr int maxBands = 40;
    static constexpr int minBands = 4;
    // The two frequencies the bank is laid out between. Below and above these
    // a vocoder has nothing useful to say: speech energy runs out.
    static constexpr float lowestRangeHz = 20.0f, highestRangeHz = 20000.0f;

    struct Settings
    {
        int bands = 20;
        float lowHz = 100.0f;
        float highHz = 12000.0f;
        // Multiplies each band's width. 1 makes the bands meet at their -3 dB
        // points; below that they separate and the result is more ringing and
        // more intelligible, above it they overlap and it smears.
        float bandwidth = 1.0f;         // 0.25 .. 4
        // Moves the carrier's bank against the modulator's, in semitones. This
        // is the vocoder's formant control: the voice keeps its rhythm and
        // changes its size.
        float formantSemitones = 0.0f;  // -24 .. 24
        float attackMs = 2.0f;          // 0.1 .. 100
        float releaseMs = 30.0f;        // 1 .. 1000
        // Below this the modulator counts as silence and the bank shuts, which
        // keeps room tone from playing the carrier.
        float gateDb = -60.0f;          // -80 .. 0
        // Noise mixed into the carrier while the modulator is sibilant, so that
        // s and t survive a carrier that has no top.
        float unvoiced = 0.0f;          // 0 .. 1
        // Flattens each carrier band against its own energy, so a carrier with
        // a strong fundamental and little else still speaks.
        float enhance = 0.0f;           // 0 .. 1
        // How far the band gains are allowed to depart from their average. At
        // zero the carrier comes through the bank unshaped.
        float depth = 1.0f;             // 0 .. 1
        float outputGainDb = 0.0f;      // -24 .. 24
        float dryWet = 1.0f;            // 0 .. 1, dry being the modulator
    };

    // What the face draws: one lamp per band, plus whether there is anything
    // to draw at all.
    struct Readout
    {
        std::array<float, maxBands> level {};
        int bands = 0;
        bool carrierPresent = false;
        bool modulatorPresent = false;
    };

    void prepare(double sampleRate, int channels, int maxBlockSize);
    void reset();

    // Audio thread, at the top of a block.
    void setSettings(const Settings&);

    // `data` carries the modulator in and takes the result out. `carrier` is
    // the audio from the source track, or null when no source is wired -- in
    // which case the modulator passes through dry and the readout says so,
    // rather than the device going silent for a reason nothing explains.
    void process(float* const* data, const float* const* carrier, int channels, int count);

    Readout readout() const;

    // The bank's centre frequencies, which the face draws its lamps along.
    // Static so a test and the UI can both ask without owning an engine.
    static float bandCentreHz(int band, int bands, float lowHz, float highHz);

private:
    // One biquad, transposed direct form II, in double precision: a 60 Hz
    // bandpass at 48 kHz with a Q near 10 is audibly noisy in float.
    struct Biquad
    {
        double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        void setBandpass(double frequency, double q, double sampleRate);
    };
    struct BiquadState
    {
        double z1 = 0.0, z2 = 0.0;
        double process(const Biquad& c, double x)
        {
            const auto y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0.0; }
    };
    static constexpr int stages = 2;  // 12 dB/octave skirts either side

    struct Band
    {
        Biquad modulator, carrier;
        std::array<BiquadState, stages> modulatorState {};
        // One set of carrier states per channel: the band gain is shared, the
        // filtering is not, which is what keeps a stereo carrier stereo.
        std::array<std::array<BiquadState, stages>, 2> carrierState {};
        float envelope = 0.0f;
        float carrierEnvelope = 0.0f;
    };

    // The carrier is held here before the bank sees it, so the output level
    // follows the voice rather than whatever the source track is doing.
    static constexpr float carrierTargetRms = 0.25f;

    void rebuildBank();
    void flushDenormals();

    Settings settings;
    // What the bank was last built for. Rebuilding is coefficient arithmetic
    // over up to forty bands, so it happens when one of these moves rather
    // than every block.
    int builtBands = 0;
    float builtLow = 0.0f, builtHigh = 0.0f, builtBandwidth = 0.0f, builtFormant = 0.0f;
    double sampleRate = 48000.0;
    double builtRate = 0.0;

    std::array<Band, maxBands> band {};
    // Scratch, sized by prepare. The bank is walked band-outer and
    // sample-inner -- forty passes over a block rather than a block-long walk
    // through forty filters -- so everything the inner loop needs that is not
    // per-band is computed into one of these first.
    std::vector<float> monoModulator, gateScratch, noiseScratch, dryLeft, dryRight;
    std::array<std::vector<float>, 2> carrierScratch;

    // Broadband followers. The carrier's drives the normaliser, the
    // modulator's drives the gate, and the sibilance pair drives the unvoiced
    // noise.
    float carrierLevel = 0.0f, modulatorLevel = 0.0f;
    float sibilantLevel = 0.0f, fullLevel = 0.0f;
    double sibilanceLowpass = 0.0;
    // A 32-bit xorshift: the noise has to be cheap and does not have to be
    // good, and std::mt19937 allocates a 2.5 KB state per instance.
    unsigned int noiseSeed = 0x9e3779b9u;

    std::atomic<int> readoutBands {0};
    std::atomic<bool> readoutCarrier {false}, readoutModulator {false};
    std::array<std::atomic<float>, maxBands> readoutLevel {};
};

}
