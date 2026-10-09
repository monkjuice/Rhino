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
};

// Linkwitz-Riley fourth order: two Butterworth second orders in series on
// each side, whose low and high halves add back to flat. Two of these make
// an isolator that can take a band out completely.
struct DjCrossover
{
    void set(double cutoff, double rate);
    void reset() noexcept;
    void split(int channel, float in, float& low, float& high) noexcept;

private:
    std::array<std::array<DjBiquad, 2>, 2> lows {}, highs {};
};

class DjIsolator
{
public:
    static constexpr float killDb = -90.0f;
    static constexpr double lowCrossoverHz = 250.0, highCrossoverHz = 3000.0;
    void prepare(double rate);
    void reset() noexcept;
    // Decibels per band; killDb or below is a kill.
    void setGainsDb(float low, float mid, float high) noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    DjCrossover lowSplit, highSplit;
    std::array<float, 3> target {1.0f, 1.0f, 1.0f}, current {1.0f, 1.0f, 1.0f};
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

// One channel of the mixer: trim, three-band isolator, colour filter, the
// fader, cue, the effect send and the crossfader side.
class DjChannelStrip
{
public:
    enum class CrossfaderSide : int { a = 0, through = 1, b = 2 };
    static constexpr float minimumTrimDb = -12.0f, maximumTrimDb = 12.0f;
    static constexpr float maximumEqDb = 6.0f;

    std::atomic<float> trimDb {0.0f};
    std::atomic<float> lowDb {0.0f}, midDb {0.0f}, highDb {0.0f};
    std::atomic<float> filter {0.0f}, resonance {0.2f};
    std::atomic<float> fader {1.0f};
    std::atomic<bool> cue {false}, fxOn {false};
    std::atomic<int> crossfaderSide {static_cast<int>(CrossfaderSide::through)};
    // The block's peak after the EQ and filter, before the fader, which is
    // what a mixer's channel meter shows.
    std::atomic<float> meter {0.0f};

    void prepare(double rate);
    void reset() noexcept;
    // Trim, isolator and filter in place; reports the meter.
    void processPreFader(float* left, float* right, int frames) noexcept;
    // The fader's gain for this block, smoothed, applied in place.
    void applyFader(float* left, float* right, int frames) noexcept;
    // How a fader position becomes a gain: a curve that keeps the top of
    // the throw gentle and the bottom usable.
    static float faderGainFor(float position) noexcept;

private:
    DjIsolator isolator;
    DjColourFilter colour;
    DjSmoothedGain trim, faderGain;
};

class DjCrossfader
{
public:
    // -1 is full left (A), 1 full right (B).
    std::atomic<float> position {0.0f};
    // 0 is a smooth equal-power blend, 1 a sharp cut for scratching.
    std::atomic<float> curve {0.0f};
    void gains(float& a, float& b) const noexcept;
};

class DjMasterSection
{
public:
    static constexpr float minimumLevelDb = -60.0f, maximumLevelDb = 6.0f;
    std::atomic<float> levelDb {0.0f};
    std::atomic<float> lowDb {0.0f}, midDb {0.0f}, highDb {0.0f};
    // 0 hears the cue bus only, 1 the master only.
    std::atomic<float> cueMix {0.0f};
    std::atomic<float> meterLeft {0.0f}, meterRight {0.0f};

    void prepare(double rate);
    void reset() noexcept;
    void process(float* left, float* right, int frames) noexcept;

private:
    DjIsolator isolator;
    DjSmoothedGain level;
};

// The beat effect: one unit, on one channel or on the master, timed to the
// beat of the master deck.
class DjBeatFx
{
public:
    enum class Type : int { echo = 0, delay, flanger, phaser, filter, reverb, roll, trans, count };
    static constexpr int typeCount = static_cast<int>(Type::count);
    static const char* typeName(Type);
    static constexpr double longestSeconds = 8.0;

    std::atomic<int> type {static_cast<int>(Type::echo)};
    // The time, in beats: 1/16 to 4.
    std::atomic<float> beats {0.5f};
    // Level or depth, 0..1, as the DJM's one knob.
    std::atomic<float> depth {0.5f};
    std::atomic<bool> on {false};
    // -1 the master, else a channel.
    std::atomic<int> target {-1};

    void prepare(double rate, int maximumBlock);
    void reset() noexcept;
    // beatSeconds is one beat at the master tempo; beatPosition a running
    // count of beats for the swept effects to lock their phase to.
    void process(float* left, float* right, int frames, double beatSeconds, double beatPosition) noexcept;

private:
    // phase is where in its cycle a swept effect is as the block begins,
    // 0 to 1, from the master's beat count.
    void processEcho(float* l, float* r, int frames, double delaySeconds, float depth, bool active, bool pingPong) noexcept;
    void processFlanger(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processPhaser(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processFilter(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;
    void processReverb(float* l, float* r, int frames, float depth, float decay, bool active) noexcept;
    void processRoll(float* l, float* r, int frames, double lengthSeconds, float depth, bool active) noexcept;
    void processTrans(float* l, float* r, int frames, double periodSeconds, float depth, double phase, bool active) noexcept;

    double rate = 48000.0;
    int maxDelay = 0;
    std::array<std::vector<float>, 2> line {};
    int writeIndex = 0;
    DjSmoothedGain wet, dry;
    int lastType = -1;
    bool wasActive = false;
    // Echo and delay loop filters.
    std::array<float, 2> loopLow {}, loopHigh {};
    // Flanger and filter sweeps.
    std::array<float, 2> ic1 {}, ic2 {};
    std::array<std::array<float, 4>, 2> phaserState {};
    // Reverb.
    std::array<std::array<std::vector<float>, 4>, 2> combs {};
    std::array<std::array<int, 4>, 2> combIndex {};
    std::array<std::array<float, 4>, 2> combFilter {};
    std::array<std::array<std::vector<float>, 2>, 2> allpasses {};
    std::array<std::array<int, 2>, 2> allpassIndex {};
    // Roll.
    int rollFilled = 0, rollLength = 0, rollIndex = 0;
    bool rolling = false;
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
