#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace rhino
{
// The mixer's DSP: what a DJM does to each deck and to the sum. Pure C++ in
// RhinoCore, so a test can push a tone through a strip and measure what a
// kill or a crossfader leaves of it. Every control is an atomic the message
// thread writes; the audio thread reads them once a block and smooths them.
//
// The controls follow the DJM-V10's panel: a channel has trim, a
// compressor, a four-band isolator, a colour filter, a send, a cue, the
// effect assign and a fader with a curve; the master has an isolator, the
// headphones a mix, a level and mono split; a mic comes in from the audio
// input with its own EQ and talkover; a send/return unit takes the sends;
// and the beat effect, in fourteen kinds, can be limited to the bands it
// is wanted on.

struct DjBiquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    float process(float x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void reset() noexcept { z1 = z2 = 0.0; }
    static DjBiquad lowPass(double cutoff, double rate, double q);
    static DjBiquad highPass(double cutoff, double rate, double q);
    static DjBiquad lowShelf(double cutoff, double rate, double gainDb);
    static DjBiquad highShelf(double cutoff, double rate, double gainDb);
};

// Linkwitz-Riley fourth order: two Butterworth second orders in series on
// each side, whose low and high halves add back to flat. Chained, these
// make an isolator that can take a band out completely.
struct DjCrossover
{
    void set(double cutoff, double rate);
    void reset() noexcept;
    void split(int channel, float in, float& low, float& high) noexcept;

private:
    std::array<std::array<DjBiquad, 2>, 2> lows {}, highs {};
};

// Three or four bands from chained crossovers, each with its own gain,
// from a full kill to +6 dB. Three bands split at 250 Hz and 3 kHz, the
// master's isolator; four at 150 Hz, 600 Hz and 3 kHz, a channel's EQ.
class DjIsolator
{
public:
    static constexpr float killDb = -90.0f;
    static constexpr int maximumBands = 4;
    void prepare(double rate, int bandCount);
    void reset() noexcept;
    int bandCount() const noexcept { return bands; }
    // Decibels per band; killDb or below is a kill.
    void setGainsDb(const float* decibels) noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    int bands = 3;
    std::array<DjCrossover, 3> splits;
    std::array<float, maximumBands> target {1.0f, 1.0f, 1.0f, 1.0f}, current {1.0f, 1.0f, 1.0f, 1.0f};
    float smoothing = 0.01f;
};

// The one-knob colour filter: left of centre a low-pass closing down from
// the top of the band, right of centre a high-pass opening up from the
// bottom, dead in the middle.
class DjColourFilter
{
public:
    void prepare(double rate);
    void reset() noexcept;
    // amount -1..1, resonance 0..1
    void set(float amount, float resonance) noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    double rate = 48000.0;
    float targetAmount = 0.0f, amount = 0.0f, targetResonance = 0.2f, resonance = 0.2f;
    std::array<float, 2> ic1 {}, ic2 {};
};

// The channel compressor as the DJM-V10 has it: one knob. Turning it up
// brings the threshold down from 0 dBFS to -30 and the ratio up to 4:1,
// with part of the loss made up, so a thin track fills out without a
// second knob to set.
class DjCompressor
{
public:
    void prepare(double rate);
    void reset() noexcept;
    void set(float amount) noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    float targetAmount = 0.0f, amount = 0.0f;
    float envelope = 0.0f, attack = 0.01f, release = 0.001f, smoothing = 0.01f;
};

struct DjSmoothedGain
{
    float current = 1.0f, target = 1.0f, coefficient = 0.01f;
    void prepare(double rate, double seconds) { coefficient = static_cast<float>(1.0 - std::exp(-1.0 / (seconds * rate))); }
    float next() noexcept
    {
        current += coefficient * (target - current);
        return current;
    }
};

inline float decibelsToGain(float db) noexcept
{
    return db <= DjIsolator::killDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

// One channel of the mixer.
class DjChannelStrip
{
public:
    enum class CrossfaderSide : int { a = 0, through = 1, b = 2 };
    static constexpr int bandCount = 4;   // low, low-mid, high-mid, high
    static constexpr float minimumTrimDb = -12.0f, maximumTrimDb = 12.0f;
    static constexpr float maximumEqDb = 6.0f;

    std::atomic<float> trimDb {0.0f};
    std::atomic<float> comp {0.0f};
    std::array<std::atomic<float>, bandCount> eqDb {};
    std::atomic<float> filter {0.0f}, resonance {0.2f};
    std::atomic<float> fader {1.0f};
    // 0 gentle, 1 normal, 2 steep - how fast the fader comes up.
    std::atomic<int> faderCurve {1};
    std::atomic<float> send {0.0f};
    std::atomic<bool> cue {false}, fxOn {false};
    std::atomic<int> crossfaderSide {static_cast<int>(CrossfaderSide::through)};
    // The block's peak after the EQ and filter, before the fader, which is
    // what a mixer's channel meter shows.
    std::atomic<float> meter {0.0f};

    void prepare(double rate);
    void reset() noexcept;
    // Trim, compressor, isolator and filter in place; reports the meter.
    void processPreFader(float* left, float* right, int frames) noexcept;
    // The fader's gain for this block, smoothed, applied in place.
    void applyFader(float* left, float* right, int frames) noexcept;
    // The send's gain for the block, smoothed.
    float sendGain() noexcept;
    static float faderGainFor(float position, int curve) noexcept;

private:
    DjCompressor compressor;
    DjIsolator isolator;
    DjColourFilter colour;
    DjSmoothedGain trim, faderGain, sendLevel;
};

class DjCrossfader
{
public:
    // -1 is full left (A), 1 full right (B).
    std::atomic<float> position {0.0f};
    // 0 a smooth equal-power blend, 0.5 linear-ish, 1 a sharp cut.
    std::atomic<float> curve {0.0f};
    void gains(float& a, float& b) const noexcept;
};

// The master and the headphones.
class DjMasterSection
{
public:
    static constexpr float minimumLevelDb = -60.0f, maximumLevelDb = 6.0f;
    std::atomic<float> levelDb {0.0f};
    std::atomic<float> lowDb {0.0f}, midDb {0.0f}, highDb {0.0f};
    // The headphones: 0 hears the cue bus only, 1 the master only; their
    // own level; and mono split, the cue on the left and the master on
    // the right.
    std::atomic<float> cueMix {0.0f};
    std::atomic<float> cueLevelDb {0.0f};
    std::atomic<bool> monoSplit {false};
    std::atomic<float> meterLeft {0.0f}, meterRight {0.0f};

    void prepare(double rate);
    void reset() noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    DjIsolator isolator;
    DjSmoothedGain level;
};

// The mic: the device's first input, shaped by a two-band EQ, switched
// off, on, or on with talkover, which ducks the master by 18 dB while the
// mic is spoken into.
class DjMicSection
{
public:
    enum class Mode : int { off = 0, on, talkover };
    static constexpr float minimumLevelDb = -60.0f, maximumLevelDb = 12.0f;
    static constexpr float talkoverDuckDb = -18.0f;
    std::atomic<float> levelDb {0.0f};
    std::atomic<float> lowDb {0.0f}, highDb {0.0f};   // +-12 dB shelves
    std::atomic<int> mode {static_cast<int>(Mode::off)};
    std::atomic<float> meter {0.0f};

    void prepare(double rate);
    void reset() noexcept;
    // Shapes the input into out (mono) and reports the gain the master
    // takes this block: 1, or the talkover duck while the mic is live.
    void process(const float* input, float* out, int frames, float& duck) noexcept;

private:
    double rate = 48000.0;
    DjBiquad low, high;
    DjSmoothedGain level, ducking;
    float envelope = 0.0f, lastLowDb = 0.0f, lastHighDb = 0.0f;
};

// A delay-line pitch shifter: two taps reading the line at the shifted
// rate, crossfaded where each wraps. Mono; one per channel.
class DjPitchShifter
{
public:
    void prepare(double rate);
    void reset() noexcept;
    void setSemitones(float semitones) noexcept;
    float process(float in) noexcept;

private:
    std::vector<float> line;
    int size = 0, write = 0, window = 0;
    double phase = 0.0, ratio = 1.0;
};

// The send/return unit the channels' sends feed: a short or long delay, a
// dub echo or a reverb, with size (feedback), time, tone and the level the
// return comes back at.
class DjSendFx
{
public:
    enum class Type : int { shortDelay = 0, longDelay, dubEcho, reverb, count };
    static constexpr int typeCount = static_cast<int>(Type::count);
    static const char* typeName(Type);
    std::atomic<int> type {static_cast<int>(Type::dubEcho)};
    std::atomic<float> size {0.5f}, time {0.5f}, tone {0.5f}, mix {0.5f};

    void prepare(double rate);
    void reset() noexcept;
    // The bus in, the return out, in place.
    void process(float* left, float* right, int frames) noexcept;
    // What the time knob means in seconds for the type.
    static double secondsFor(Type, float time) noexcept;

private:
    double rate = 48000.0;
    int maxDelay = 0, lastType = -1;
    std::array<std::vector<float>, 2> line {};
    int writeIndex = 0;
    std::array<float, 2> loopTone {};
    std::array<std::array<std::vector<float>, 4>, 2> combs {};
    std::array<std::array<int, 4>, 2> combIndex {};
    std::array<std::array<float, 4>, 2> combFilter {};
    std::array<std::array<std::vector<float>, 2>, 2> allpasses {};
    std::array<std::array<int, 2>, 2> allpassIndex {};
    DjSmoothedGain wet;
    float delaySmoothed = 0.0f;
};

// The beat effect: one unit, on one channel or on the master, timed to the
// beat of the master deck or to the time knob.
class DjBeatFx
{
public:
    enum class Type : int
    {
        delay = 0, echo, pingPong, spiral, helix, reverb, shimmer, flanger, phaser, filter, trans, roll, pitch, vinylBrake, count
    };
    static constexpr int typeCount = static_cast<int>(Type::count);
    static const char* typeName(Type);
    static constexpr double longestSeconds = 8.0;

    std::atomic<int> type {static_cast<int>(Type::echo)};
    // The time, in beats: 1/16 to 4, when timed automatically.
    std::atomic<float> beats {0.5f};
    std::atomic<bool> autoTime {true};
    std::atomic<float> manualSeconds {0.25f};
    // Level or depth, 0..1, as the DJM's one knob.
    std::atomic<float> depth {0.5f};
    std::atomic<bool> on {false};
    // -1 the master, else a channel.
    std::atomic<int> target {-1};
    // Which bands the effect is wanted on: the DJM's FX FREQUENCY keys.
    std::atomic<bool> bandLow {true}, bandMid {true}, bandHigh {true};

    void prepare(double rate, int maximumBlock);
    void reset() noexcept;
    // beatSeconds is one beat at the booth's tempo; beatPosition a running
    // count of beats for the swept effects to lock their phase to.
    void process(float* left, float* right, int frames, double beatSeconds, double beatPosition) noexcept;
    // The time the effect runs at, from the beats or the knob.
    double secondsFor(double beatSeconds) const noexcept;

private:
    void processDelay(float* l, float* r, int frames, double delaySeconds, float depth, bool active, int mode) noexcept;
    void processFlanger(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processPhaser(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processFilter(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processReverb(float* l, float* r, int frames, float depth, float decay, bool active, bool shimmer) noexcept;
    void processRoll(float* l, float* r, int frames, double lengthSeconds, float depth, bool active) noexcept;
    void processTrans(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processPitch(float* l, float* r, int frames, float depth, bool active) noexcept;
    void processBrake(float* l, float* r, int frames, double seconds, bool active) noexcept;

    double rate = 48000.0;
    int maxDelay = 0, maximumBlock = 512;
    std::array<std::vector<float>, 2> line {};
    int writeIndex = 0;
    DjSmoothedGain wet;
    int lastType = -1;
    std::array<float, 2> loopLow {}, loopHigh {};
    std::array<float, 2> ic1 {}, ic2 {};
    std::array<std::array<float, 4>, 2> phaserState {};
    std::array<std::array<std::vector<float>, 4>, 2> combs {};
    std::array<std::array<int, 4>, 2> combIndex {};
    std::array<std::array<float, 4>, 2> combFilter {};
    std::array<std::array<std::vector<float>, 2>, 2> allpasses {};
    std::array<std::array<int, 2>, 2> allpassIndex {};
    std::array<DjPitchShifter, 2> shifters;
    int rollFilled = 0, rollLength = 0, rollIndex = 0;
    bool rolling = false;
    // The brake: how far behind the write the read stands, and its speed.
    double brakeLag = 0.0, brakeSpeed = 1.0;
    bool braking = false;
    // The band limit: the dry kept aside and the wet split into bands.
    std::array<std::vector<float>, 2> dry {};
    DjIsolator bandLimit;
};

// Keeps the output inside the rails without a hard edge: everything up to
// -1 dB passes, and the last decibel is folded in.
inline float softClip(float x) noexcept
{
    constexpr float knee = 0.891f;   // -1 dB
    const auto magnitude = std::abs(x);
    if (magnitude <= knee) return x;
    const auto excess = (magnitude - knee) / (1.0f - knee);
    const auto folded = knee + (1.0f - knee) * std::tanh(excess);
    return x < 0.0f ? -folded : folded;
}
}
