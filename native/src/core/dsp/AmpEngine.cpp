#include "AmpEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double k = 1.0e3, M = 1.0e6, nF = 1.0e-9, pF = 1.0e-12;

// The tone stack's parts: R1 the treble pot, R2 the bass pot, R3 the middle
// pot, R4 the slope resistor, C1 the treble cap, C2 the bass cap, C3 the
// middle cap.
struct StackParts
{
    double r1, r2, r3, r4, c1, c2, c3;
};

// Values as the amps these models are modelled on carry them. The British
// amp's tone circuit is not a Bassman stack at all; these are the values
// usually taken to stand in for it.
constexpr StackParts britishStack { 1.0 * M, 1.0 * M, 10.0 * k, 100.0 * k, 50.0 * pF, 22.0 * nF, 22.0 * nF };
constexpr StackParts twinStack { 250.0 * k, 250.0 * k, 10.0 * k, 100.0 * k, 250.0 * pF, 100.0 * nF, 47.0 * nF };
constexpr StackParts plexiStack { 250.0 * k, 1.0 * M, 25.0 * k, 33.0 * k, 270.0 * pF, 22.0 * nF, 22.0 * nF };
constexpr StackParts modernStack { 250.0 * k, 1.0 * M, 25.0 * k, 47.0 * k, 470.0 * pF, 20.0 * nF, 20.0 * nF };
constexpr StackParts vintageStack { 220.0 * k, 1.0 * M, 22.0 * k, 33.0 * k, 470.0 * pF, 22.0 * nF, 22.0 * nF };
constexpr StackParts bassmanStack { 250.0 * k, 1.0 * M, 25.0 * k, 56.0 * k, 250.0 * pF, 20.0 * nF, 20.0 * nF };

struct Voicing
{
    double inputHz;              // the input coupling capacitor's corner
    double brightDb;             // the bright cap's lift at Gain 0; none by Gain 10
    double gainLowDb, gainHighDb;
    int stages;                  // triode stages before the tone stack
    double interstageDb;         // gain into each stage after the first
    double asymmetry;            // how much sooner a stage clips on its positive swing
    double couplingHz;           // between stages: higher is a tighter low end
    double millerHz;             // each stage's own treble roll-off
    StackParts stack;
    double powerDb;              // the power stage's drive with Volume at 5
    double volumeSpanDb;         // Volume 0 to 10 moves the drive this far, centred on 5
    double powerAsymmetry;
    double hardness;             // the power stage's knee: 0 soft, 1 a fuzz
    double sag;                  // how far the supply drops under load
    double presenceDb;           // Presence at 0 and 10, either way
    double transformerLowHz, transformerHighHz;
    double outputDb;             // the stage after the transformer, which a saturated power stage peaks near
};

// Indexed by AmpEngine::Model. Each model's powerDb was set by measuring, so
// its defaults come out within a decibel or so of the level that went in, and
// AmpTest holds them to three. Every model shares outputDb, so a cranked power
// stage peaks in the same place, a few dB over full scale, whichever it is.
constexpr Voicing voicings[AmpEngine::modelCount] {
    // Clean: the "Brilliant" channel of a '60s British amp. One stage, bright,
    // clean until Gain is well up.
    { 40.0, 6.0, -6.0, 20.0, 1, 0.0, 0.30, 20.0, 12000.0, britishStack,
      -5.0, 36.0, 0.12, 0.0, 0.30, 6.0, 70.0, 9000.0, 3.0 },
    // Boost: the same amp's "Tremolo" channel, a second stage pushed.
    { 50.0, 3.0, 0.0, 26.0, 2, 6.0, 0.30, 30.0, 10000.0, britishStack,
      -11.0, 36.0, 0.12, 0.0, 0.30, 6.0, 70.0, 8000.0, 3.0 },
    // Blues: a bright '70s amp, whose power stage breaks up with Volume up.
    { 30.0, 6.0, 0.0, 26.0, 2, 2.0, 0.25, 20.0, 11000.0, twinStack,
      -5.0, 48.0, 0.05, 0.1, 0.50, 6.0, 50.0, 9000.0, 3.0 },
    // Rock: a classic 45 watt amp from the '60s.
    { 40.0, 2.0, -4.0, 30.0, 2, 10.0, 0.25, 30.0, 9000.0, plexiStack,
      -15.5, 36.0, 0.05, 0.0, 0.40, 6.0, 60.0, 8000.0, 3.0 },
    // Lead: the "Modern" channel of a high-gain amp. Four stages, and a tight
    // low end so they do not flub.
    { 80.0, 0.0, -30.0, 12.0, 4, 14.0, 0.18, 100.0, 7500.0, modernStack,
      -14.0, 36.0, 0.03, 0.0, 0.20, 6.0, 70.0, 7000.0, 3.0 },
    // Heavy: the same amp's "Vintage" channel: looser, and Volume adds to it.
    { 50.0, 0.0, -16.0, 24.0, 3, 12.0, 0.22, 60.0, 8000.0, vintageStack,
      -15.0, 48.0, 0.04, 0.2, 0.35, 6.0, 55.0, 7500.0, 3.0 },
    // Bass: a '70s PA. Deep, and fuzz when Volume is up.
    { 15.0, 2.0, -6.0, 22.0, 1, 0.0, 0.30, 15.0, 9000.0, bassmanStack,
      -7.5, 48.0, 0.08, 0.6, 0.60, 6.0, 30.0, 7000.0, 3.0 },
};

const Voicing& voicingOf(AmpEngine::Model model)
{
    return voicings[std::clamp(static_cast<int>(model), 0, AmpEngine::modelCount - 1)];
}

double gainOf(double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

double knob(float value)
{
    return std::clamp(static_cast<double>(value), 0.0, 10.0) / 10.0;
}

// tanh near enough, without the cost of one: a Pade approximant that meets
// one with zero slope at three and stays there.
double softClip(double x)
{
    if (x >= 3.0) return 1.0;
    if (x <= -3.0) return -1.0;
    const auto square = x * x;
    return x * (27.0 + square) / (27.0 + 9.0 * square);
}

// A triode stage: a positive swing drives the grid into conduction and
// flattens sooner than a negative one reaches cut-off, so the two halves clip
// at different heights. That difference is the even harmonics. The slope
// through zero is one either way, so a quiet signal is not distorted at all.
double triode(double x, double asymmetry)
{
    return x >= 0.0 ? softClip(x * (1.0 + asymmetry)) / (1.0 + asymmetry)
                    : softClip(x * (1.0 - asymmetry)) / (1.0 - asymmetry);
}

// The push-pull power stage: nearly symmetric, a soft knee, and for a model
// with hardness a harder one blended in, which is how the Bass model fuzzes.
double powerStage(double x, double asymmetry, double hardness)
{
    const auto soft = triode(x, asymmetry);
    if (hardness <= 0.0)
        return soft;
    const auto x2 = x * x, x4 = x2 * x2;
    const auto hard = x / std::sqrt(std::sqrt(std::sqrt(1.0 + x4 * x4)));
    return soft + (hard - soft) * hardness;
}

// The Bassman tone stack's transfer function, from Yeh and Smith,
// "Discretization of the '59 Fender Bassman Tone Stack" (DAFx 2006):
//   H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3)
// with t, m, l the treble, middle and bass pots' positions, 0 to 1.
struct AnalogStack
{
    double b1, b2, b3, a1, a2, a3;
};

AnalogStack analogStack(const StackParts& p, double t, double m, double l)
{
    const auto [r1, r2, r3, r4, c1, c2, c3] = p;
    AnalogStack s;
    s.b1 = t * c1 * r1 + m * c3 * r3 + l * (c1 * r2 + c2 * r2) + (c1 * r3 + c2 * r3);
    s.b2 = t * (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4)
         - m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)
         + m * (c1 * c3 * r1 * r3 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)
         + l * (c1 * c2 * r1 * r2 + c1 * c2 * r2 * r4 + c1 * c3 * r2 * r4)
         + l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3)
         + (c1 * c2 * r1 * r3 + c1 * c2 * r3 * r4 + c1 * c3 * r3 * r4);
    s.b3 = l * m * (c1 * c2 * c3 * r1 * r2 * r3 + c1 * c2 * c3 * r2 * r3 * r4)
         - m * m * (c1 * c2 * c3 * r1 * r3 * r3 + c1 * c2 * c3 * r3 * r3 * r4)
         + m * (c1 * c2 * c3 * r1 * r3 * r3 + c1 * c2 * c3 * r3 * r3 * r4)
         + t * c1 * c2 * c3 * r1 * r3 * r4
         - t * m * c1 * c2 * c3 * r1 * r3 * r4
         + t * l * c1 * c2 * c3 * r1 * r2 * r4;
    s.a1 = (c1 * r1 + c1 * r3 + c2 * r3 + c2 * r4 + c3 * r4) + m * c3 * r3 + l * (c1 * r2 + c2 * r2);
    s.a2 = m * (c1 * c3 * r1 * r3 - c2 * c3 * r3 * r4 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)
         + l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3)
         - m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3)
         + l * (c1 * c2 * r2 * r4 + c1 * c2 * r1 * r2 + c1 * c3 * r2 * r4 + c2 * c3 * r2 * r4)
         + (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4 + c1 * c2 * r3 * r4
            + c1 * c2 * r1 * r3 + c1 * c3 * r3 * r4 + c2 * c3 * r3 * r4);
    s.a3 = l * m * (c1 * c2 * c3 * r1 * r2 * r3 + c1 * c2 * c3 * r2 * r3 * r4)
         - m * m * (c1 * c2 * c3 * r1 * r3 * r3 + c1 * c2 * c3 * r3 * r3 * r4)
         + m * (c1 * c2 * c3 * r3 * r3 * r4 + c1 * c2 * c3 * r1 * r3 * r3 - c1 * c2 * c3 * r1 * r3 * r4)
         + l * c1 * c2 * c3 * r1 * r2 * r4
         + c1 * c2 * c3 * r1 * r3 * r4;
    return s;
}

// The bilinear transform, s = c (1 - z^-1) / (1 + z^-1) with c = 2 fs: each
// power of s becomes c^k (1 - z^-1)^k (1 + z^-1)^(3 - k) over a common
// (1 + z^-1)^3, and the coefficients are what those polynomials sum to.
void digitalStack(const AnalogStack& s, double sampleRate, std::array<double, 4>& b, std::array<double, 4>& a)
{
    const auto c = 2.0 * sampleRate, c2 = c * c, c3 = c2 * c;
    const auto b1 = s.b1 * c, b2 = s.b2 * c2, b3 = s.b3 * c3;
    const auto a1 = s.a1 * c, a2 = s.a2 * c2, a3 = s.a3 * c3;
    b = { b1 + b2 + b3, b1 - b2 - 3.0 * b3, -b1 - b2 + 3.0 * b3, -b1 + b2 - b3 };
    a = { 1.0 + a1 + a2 + a3, 3.0 + a1 - a2 - 3.0 * a3, 3.0 - a1 - a2 + 3.0 * a3, 1.0 - a1 + a2 - a3 };
    const auto scale = 1.0 / a[0];
    for (auto& value : b) value *= scale;
    for (auto& value : a) value *= scale;
}

// The pots as the knobs turn them. Bass is a log-taper pot, so its position
// is the exponential of the knob's; the others are linear.
AnalogStack stackFor(const Voicing& voicing, double treble, double middle, double bass)
{
    return analogStack(voicing.stack, treble, middle, std::exp((bass - 1.0) * 3.4));
}

double magnitudeAt(const std::array<double, 4>& b, const std::array<double, 4>& a, double frequency, double sampleRate)
{
    const auto w = 2.0 * pi * frequency / sampleRate;
    double numeratorRe = 0.0, numeratorIm = 0.0, denominatorRe = 0.0, denominatorIm = 0.0;
    for (int n = 0; n < 4; ++n)
    {
        numeratorRe += b[static_cast<size_t>(n)] * std::cos(w * n);
        numeratorIm -= b[static_cast<size_t>(n)] * std::sin(w * n);
        denominatorRe += a[static_cast<size_t>(n)] * std::cos(w * n);
        denominatorIm -= a[static_cast<size_t>(n)] * std::sin(w * n);
    }
    return std::sqrt((numeratorRe * numeratorRe + numeratorIm * numeratorIm)
                     / std::max(1.0e-30, denominatorRe * denominatorRe + denominatorIm * denominatorIm));
}

// The passive stack loses a lot, and how much depends on its parts. What it
// passes with every knob at the middle is made back up, so every model meets
// its power stage at a like level and the knobs then lift or cut from there.
double stackMakeup(const Voicing& voicing, double sampleRate)
{
    std::array<double, 4> b {}, a {};
    digitalStack(stackFor(voicing, 0.5, 0.5, 0.5), sampleRate, b, a);
    auto loudest = 1.0e-6;
    for (int i = 0; i < 48; ++i)
        loudest = std::max(loudest, magnitudeAt(b, a, 80.0 * std::pow(6000.0 / 80.0, i / 47.0), sampleRate));
    return 1.0 / loudest;
}

double onePoleCoefficient(double frequency, double sampleRate)
{
    return 1.0 - std::exp(-2.0 * pi * frequency / sampleRate);
}

double highPassCoefficient(double frequency, double sampleRate)
{
    return std::exp(-2.0 * pi * frequency / sampleRate);
}

constexpr double brightHz = 2500.0;
constexpr double presenceHz = 3500.0;
}

double AmpEngine::ToneStack::process(double x)
{
    const auto y = b[0] * x + s1;
    s1 = b[1] * x - a[1] * y + s2;
    s2 = b[2] * x - a[2] * y + s3;
    s3 = b[3] * x - a[3] * y;
    return y;
}

void AmpEngine::Circuit::reset()
{
    input.reset();
    bright.reset();
    presence.reset();
    for (auto& filter : coupling) filter.reset();
    for (auto& filter : miller) filter.reset();
    stack.reset();
    transformerLow.reset();
    transformerHigh1.reset();
    transformerHigh2.reset();
    sag = 0.0;
}

void AmpEngine::prepare(double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    innerRate = rate * oversampling;
    maximumBlockSize = std::max(1, maximumBlockSize);
    oversampler.prepare(oversampling, maximumChannels, maximumBlockSize);
    innerSmoothing = 1.0 - std::exp(-1.0 / (0.02 * innerRate));
    mixSmoothing = 1.0 - std::exp(-1.0 / (0.02 * rate));
    // The supply drops in a few milliseconds and recovers over a hundred and
    // fifty, which is the bloom a sagging amp has after a hard chord.
    sagAttack = 1.0 - std::exp(-1.0 / (0.008 * innerRate));
    sagRelease = 1.0 - std::exp(-1.0 / (0.15 * innerRate));
    for (auto& line : dryLines)
        line.assign(static_cast<size_t>(std::max(1, oversampler.latencySamples())), 0.0f);
    mono.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    wet.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    for (int model = 0; model < modelCount; ++model)
        stackMakeups[static_cast<size_t>(model)] = stackMakeup(voicings[model], innerRate);
    updateCoefficients();
    reset();
}

void AmpEngine::reset()
{
    oversampler.reset();
    for (auto& circuit : circuits)
        circuit.reset();
    for (auto& line : dryLines)
        std::fill(line.begin(), line.end(), 0.0f);
    dryAt = 0;
    preGain = preGainTarget;
    powerGain = powerGainTarget;
    mix = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);
}

void AmpEngine::setSettings(const Settings& next)
{
    const auto moved = !(next == settings);
    settings = next;
    if (moved)
        updateCoefficients();
}

void AmpEngine::updateCoefficients()
{
    const auto& voicing = voicingOf(settings.model);
    const auto gain = knob(settings.gain);
    preGainTarget = gainOf(voicing.gainLowDb + (voicing.gainHighDb - voicing.gainLowDb) * gain);
    powerGainTarget = gainOf(voicing.powerDb + voicing.volumeSpanDb * (knob(settings.volume) - 0.5))
                    * stackMakeups[static_cast<size_t>(std::clamp(static_cast<int>(settings.model), 0, modelCount - 1))];

    const auto bright = eqdetail::highShelf(2.0 * pi * brightHz / innerRate, 0.7071, voicing.brightDb * (1.0 - gain));
    const auto presence = eqdetail::highShelf(2.0 * pi * presenceHz / innerRate, 0.7071,
                                              voicing.presenceDb * (knob(settings.presence) - 0.5) * 2.0);
    std::array<double, 4> b {}, a {};
    digitalStack(stackFor(voicing, knob(settings.treble), knob(settings.middle), knob(settings.bass)), innerRate, b, a);
    for (auto& circuit : circuits)
    {
        circuit.input.coefficient = highPassCoefficient(voicing.inputHz, innerRate);
        circuit.bright.coefficients = bright;
        circuit.presence.coefficients = presence;
        for (int s = 0; s < maximumStages; ++s)
        {
            circuit.coupling[static_cast<size_t>(s)].coefficient = highPassCoefficient(voicing.couplingHz, innerRate);
            circuit.miller[static_cast<size_t>(s)].coefficient = onePoleCoefficient(voicing.millerHz, innerRate);
        }
        circuit.stack.b = b;
        circuit.stack.a = a;
        circuit.transformerLow.coefficient = highPassCoefficient(voicing.transformerLowHz, innerRate);
        circuit.transformerHigh1.coefficient = onePoleCoefficient(voicing.transformerHighHz, innerRate);
        circuit.transformerHigh2.coefficient = onePoleCoefficient(voicing.transformerHighHz, innerRate);
    }
}

double AmpEngine::run(Circuit& circuit, double x)
{
    const auto& voicing = voicingOf(settings.model);
    x = circuit.bright.process(circuit.input.highPass(x)) * preGain;
    const auto stages = std::clamp(voicing.stages, 1, maximumStages);
    const auto interstage = gainOf(voicing.interstageDb);
    for (int s = 0; s < stages; ++s)
    {
        if (s > 0)
            x *= interstage;
        // Every stage inverts, as a common-cathode triode does, so which half
        // of the wave the next stage flattens first alternates down the chain.
        x = -triode(x, voicing.asymmetry);
        x = circuit.coupling[static_cast<size_t>(s)].highPass(x);
        x = circuit.miller[static_cast<size_t>(s)].lowPass(x);
    }
    x = circuit.presence.process(circuit.stack.process(x));
    x *= powerGain / (1.0 + voicing.sag * circuit.sag);
    auto y = powerStage(x, voicing.powerAsymmetry, voicing.hardness);
    const auto load = std::abs(y);
    circuit.sag += (load - circuit.sag) * (load > circuit.sag ? sagAttack : sagRelease);
    y = circuit.transformerLow.highPass(y);
    y = circuit.transformerHigh2.lowPass(circuit.transformerHigh1.lowPass(y));
    // Back the right way up after an odd number of inverting stages, so the
    // dry signal mixed back in adds to it rather than cancelling.
    return (stages % 2 == 1 ? -y : y) * gainOf(voicing.outputDb);
}

void AmpEngine::process(float* const* audio, int numChannels, int count)
{
    const auto active = std::min(numChannels, maximumChannels);
    if (active <= 0)
        return;
    const auto innerCount = count * oversampling;
    const auto mixTarget = std::clamp(static_cast<double>(settings.dryWet), 0.0, 1.0);
    const auto amps = settings.dual ? active : 1;

    // The knob gains glide per inner sample of the first amp; a second amp in
    // Dual mode takes the same glide, so the two sides never drift apart.
    const auto startPre = preGain, startPower = powerGain;
    for (int amp = 0; amp < amps; ++amp)
    {
        preGain = startPre;
        powerGain = startPower;
        const float* source = audio[amp];
        if (amps == 1 && active > 1)
        {
            for (int i = 0; i < count; ++i)
                mono[static_cast<size_t>(i)] = 0.5f * (audio[0][i] + audio[1][i]);
            source = mono.data();
        }
        auto* inner = oversampler.up(amp, source, count);
        auto& circuit = circuits[static_cast<size_t>(amp)];
        for (int i = 0; i < innerCount; ++i)
        {
            preGain += (preGainTarget - preGain) * innerSmoothing;
            powerGain += (powerGainTarget - powerGain) * innerSmoothing;
            inner[i] = static_cast<float>(run(circuit, static_cast<double>(inner[i])));
        }
        // The first amp's result is kept aside while a second one runs.
        oversampler.down(amp, amp == 0 ? wet.data() : mono.data(), count);
    }

    const auto startMix = mix;
    const auto startDryAt = dryAt;
    for (int c = 0; c < active; ++c)
    {
        mix = startMix;
        dryAt = startDryAt;
        const auto* processed = amps == 1 || c == 0 ? wet.data() : mono.data();
        auto& line = dryLines[static_cast<size_t>(c)];
        const auto length = static_cast<int>(line.size());
        auto* samples = audio[c];
        for (int i = 0; i < count; ++i)
        {
            const auto dry = line[static_cast<size_t>(dryAt)];
            line[static_cast<size_t>(dryAt)] = samples[i];
            dryAt = (dryAt + 1) % length;
            mix += (mixTarget - mix) * mixSmoothing;
            // Weighted rather than interpolated, so all wet is exactly the wet
            // signal and all dry exactly the dry one.
            samples[i] = static_cast<float>(dry * (1.0 - mix) + processed[static_cast<size_t>(i)] * mix);
        }
    }
}

double AmpEngine::toneStackDb(const Settings& settings, double sampleRate, double frequency)
{
    const auto& voicing = voicingOf(settings.model);
    const auto inner = sampleRate * oversampling;
    std::array<double, 4> b {}, a {};
    digitalStack(stackFor(voicing, knob(settings.treble), knob(settings.middle), knob(settings.bass)), inner, b, a);
    return 20.0 * std::log10(std::max(1.0e-12, magnitudeAt(b, a, frequency, inner) * stackMakeup(voicing, inner)));
}
}
