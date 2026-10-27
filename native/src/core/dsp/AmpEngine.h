#pragma once
#include "BiquadFilter.h"
#include "Oversampler.h"
#include <array>
#include <vector>

namespace rhino
{
// Amp, after Live's: seven guitar amplifiers behind one set of controls.
//
// Each model is a voicing of the same circuit, read left to right as a valve
// amp is built:
//
// - an input coupling capacitor, and a bright capacitor whose treble lift
//   fades as Gain comes up;
// - one to four triode stages, each an asymmetric soft clipper that inverts,
//   with the coupling capacitor and Miller roll-off around it;
// - the passive Treble/Middle/Bass network itself, the analog transfer
//   function Yeh and Smith derived for the '59 Bassman's tone stack, with each
//   model's own component values, made digital by the bilinear transform. It
//   is passive and every knob's effect reaches the others: turning up Treble
//   takes energy from the lows, as the manual says;
// - Presence, a treble shelf in the power stage;
// - a push-pull power stage that Volume drives, whose supply sags under load;
// - the output transformer's band.
//
// Everything nonlinear runs at four times the sample rate. The dry signal is
// delayed by the oversampler's latency before Dry/Wet mixes it back.
class AmpEngine
{
public:
    enum class Model { clean, boost, blues, rock, lead, heavy, bass };
    // The chooser's list; saved documents store a position in it, so append only.
    static constexpr const char* modelNames[] { "Clean", "Boost", "Blues", "Rock", "Lead", "Heavy", "Bass" };
    static constexpr int modelCount = 7;

    struct Settings
    {
        Model model = Model::clean;
        // The amp's knobs read 0 to 10, as Live's do.
        float gain = 5.0f, bass = 5.0f, middle = 5.0f, treble = 5.0f, presence = 5.0f, volume = 5.0f;
        // Each side of a stereo signal through an amp of its own, at twice the
        // cost; otherwise one amp plays the sum.
        bool dual = false;
        float dryWet = 1.0f;

        bool operator==(const Settings&) const = default;
    };

    static constexpr int oversampling = 4;
    static constexpr int maximumChannels = 2;

    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    void setSettings(const Settings&);
    // In place; channels past the second are left alone.
    void process(float* const* channels, int numChannels, int count);
    int latencySamples() const { return oversampler.latencySamples(); }

    // The tone stack's gain at `frequency`, in dB, for the model and knobs
    // given, from the same coefficients the audio runs through (made-up loss
    // included). For tests.
    static double toneStackDb(const Settings&, double sampleRate, double frequency);

private:
    // Third order, transposed direct form II, in double.
    struct ToneStack
    {
        std::array<double, 4> b {}, a {};
        double s1 = 0.0, s2 = 0.0, s3 = 0.0;
        double process(double x);
        void reset() { s1 = s2 = s3 = 0.0; }
    };
    struct OnePole
    {
        double coefficient = 0.0, state = 0.0, last = 0.0;
        double lowPass(double x) { return state += coefficient * (x - state); }
        // A coupling capacitor: what changes passes, what stays drains away.
        double highPass(double x)
        {
            state = coefficient * (state + x - last);
            last = x;
            return state;
        }
        void reset() { state = last = 0.0; }
    };
    static constexpr int maximumStages = 4;
    // One amp: everything that remembers anything between samples.
    struct Circuit
    {
        OnePole input;
        BiquadFilter bright, presence;
        std::array<OnePole, maximumStages> coupling, miller;
        ToneStack stack;
        OnePole transformerLow, transformerHigh1, transformerHigh2;
        double sag = 0.0;
        void reset();
    };
    void updateCoefficients();
    double run(Circuit&, double x);

    Settings settings;
    double rate = 48000.0, innerRate = 192000.0;
    // What each model's tone stack loses with its knobs centred, worked out
    // once at prepare() rather than whenever a knob turns on the audio thread.
    std::array<double, modelCount> stackMakeups {};
    Oversampler oversampler;
    std::array<Circuit, maximumChannels> circuits;
    // Smoothed per inner sample so a turned knob does not step.
    double preGain = 1.0, powerGain = 1.0, innerSmoothing = 0.001;
    double preGainTarget = 1.0, powerGainTarget = 1.0;
    double sagAttack = 0.001, sagRelease = 0.0001;
    double mix = 1.0, mixSmoothing = 0.004;
    std::array<std::vector<float>, maximumChannels> dryLines;
    int dryAt = 0;
    std::vector<float> mono, wet;
};
}
