#pragma once
#include <array>
#include <cstdint>

// Rhino FM: four sine operators a voice, in one of eight routings.
//
// Everything that decides how the device sounds is here, in RhinoCore, where a
// test can play notes into it and measure what comes back -- the same split
// Rhino Tune, Rhino EQ and the Vocoder use. The device is a shell that turns
// its controls into Settings and its MIDI into calls.
//
// Operators are numbered 0..3 here and 1..4 on the face. In every routing a
// modulator has a higher number than the operator it feeds, so a voice works
// from operator 4 down and every input is ready by the time it is read.
// Operator 4 can also feed itself.
//
// Two things keep it clean at the top of the keyboard. An operator whose
// frequency reaches 45% of the sample rate is silent rather than folded back
// down. And above sampleRate / 24 (2 kHz at 48 kHz) every modulator's depth
// falls in proportion to the note's frequency, so a bright patch does not
// throw its sidebands past Nyquist on the highest notes. That is the job a
// DX7's keyboard level scaling was usually given by hand.
namespace rhino
{
class FmEngine
{
public:
    static constexpr int operators = 4;
    static constexpr int maxVoices = 16;
    static constexpr int algorithms = 8;
    // How far a modulator at full level and depth 1 swings its carrier's
    // phase, in cycles: an index of about 9.4.
    static constexpr float modulationCycles = 1.5f;
    // Operator 4's swing on itself at full feedback, in cycles. Averaged over
    // the last two samples, as a DX7 does, it stays a saw-like tone instead
    // of breaking into noise.
    static constexpr float feedbackCycles = 0.45f;
    // Every voice's output is scaled by this, so a single note at full level
    // is -12 dBFS and a chord has room above it.
    static constexpr float headroom = 0.25f;

    struct Operator
    {
        float ratio = 1.0f;         // to the note's frequency
        float detuneCents = 0.0f;
        float level = 1.0f;         // 0..1: a carrier's loudness, a modulator's depth
        float attack = 0.002f;      // seconds of linear rise
        float decay = 1.0f;         // seconds to close 60 dB of the gap to sustain
        float sustain = 0.0f;       // 0..1
        float release = 0.3f;       // seconds to fall 60 dB
        friend bool operator==(const Operator&, const Operator&) = default;
    };

    struct Settings
    {
        int algorithm = 4;
        float feedback = 0.0f;      // 0..1
        float depth = 1.0f;         // 0..2, scales every modulator
        float velocity = 0.6f;      // 0..1, how much a note's velocity scales its levels
        bool mono = false;
        float glide = 0.0f;         // seconds, between overlapping notes in mono
        float gain = 1.0f;          // linear
        std::array<Operator, operators> ops;
        friend bool operator==(const Settings&, const Settings&) = default;
    };

    static bool modulates(int algorithm, int from, int to);
    static bool isCarrier(int algorithm, int op);
    static int carrierCount(int algorithm);
    static const char* algorithmName(int algorithm);

    // Message thread, before any rendering.
    void prepare(double sampleRate);
    // Everything below is the audio thread's.
    void reset();
    void setSettings(const Settings&);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    // Releases every note, and lets go of the sustain pedal.
    void releaseAll();
    void setSustainPedal(bool down);
    void setPitchBend(float semitones);
    // Adds the next numSamples to left, and to right unless it is null.
    void render(float* left, float* right, int numSamples);
    int activeVoices() const;

private:
    enum class Stage : std::uint8_t { idle, attack, decay, release };

    // What a voice needs from an operator's settings, worked out once per
    // setSettings rather than per sample.
    struct Shape
    {
        float attackStep = 1.0f;
        float decayCoefficient = 0.0f;
        float releaseCoefficient = 0.0f;
        float sustain = 0.0f;
        float amplitude = 0.0f;     // level squared, times depth for a modulator
        double frequencyFactor = 1.0;
    };

    struct Voice
    {
        bool active = false;
        bool held = false;          // the key is down
        bool sustained = false;     // the key is up and the pedal holds the note
        bool incrementsValid = false;
        int note = 60;
        float velocityScale = 1.0f;
        float pitch = 60.0f, targetPitch = 60.0f;
        float incrementPitch = 0.0f;
        std::uint64_t started = 0;
        std::array<std::uint32_t, operators> phase {};
        std::array<std::uint32_t, operators> increment {};
        std::array<float, operators> amplitudeScale {};
        std::array<float, operators> envelope {};
        std::array<Stage, operators> stage {};
        float feedbackA = 0.0f, feedbackB = 0.0f;
    };

    // One sample of an operator's envelope.
    static float stepEnvelope(Stage&, float& level, const Shape&) noexcept;
    // Works the settings out into shapes and routing, whether or not they
    // changed: a new sample rate changes every coefficient.
    void applySettings();
    void trigger(Voice&, int note, float velocity);
    void release(Voice&);
    void updateIncrements(Voice&, float pitch);
    float renderVoice(Voice&);
    void pushHeld(int note);
    void removeHeld(int note);

    double sampleRate = 48000.0;
    const float* table = nullptr;
    Settings settings;
    std::array<Shape, operators> shapes {};
    std::array<std::uint8_t, operators> modulatorMask {};
    std::uint8_t carrierMask = 1;
    float carrierNorm = 1.0f;
    float feedbackAmount = 0.0f;
    float glideStep = 1.0f;
    float gainNow = 1.0f, gainSmoothing = 1.0f;
    bool gainPrimed = false;
    float taperFrequency = 2000.0f;
    double nyquistGuard = 21600.0;
    float pitchBend = 0.0f;
    bool sustainDown = false;
    std::uint64_t noteCounter = 0;
    std::array<Voice, maxVoices> voices {};
    // Mono's keys, oldest first, so letting go of one returns to the last.
    std::array<int, 16> held {};
    int heldCount = 0;
};
}
