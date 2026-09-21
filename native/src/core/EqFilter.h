#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// One EQ band: what it is, and what it does to a frequency.
//
// This is a pure header with no JUCE and no engine, the way ScaleQuantizer.h
// is, and for the same reason -- every answer in it can be checked without a
// Session, a graph or a render.
//
// It matters that the curve on screen and the audio come from this one file.
// A response drawn from a second set of formulas is a drawing of what someone
// believed the filter does; drawn from eqBandMagnitudeDbAt, it is a drawing of
// the coefficients the samples actually go through, and a test can hold the
// two against measured audio and catch either one drifting.
namespace rhino
{

// The eight shapes, in the order the chooser lists them: low end first, high
// end last, so the list reads like the spectrum it acts on.
enum class EqFilterType
{
    LowCut48,
    LowCut12,
    LowShelf,
    Bell,
    Notch,
    HighShelf,
    HighCut12,
    HighCut48
};
inline constexpr int eqFilterTypeCount = 8;

inline const char* eqFilterTypeName(EqFilterType type)
{
    switch (type)
    {
        case EqFilterType::LowCut48:  return "Low Cut 48";
        case EqFilterType::LowCut12:  return "Low Cut 12";
        case EqFilterType::LowShelf:  return "Low Shelf";
        case EqFilterType::Bell:      return "Bell";
        case EqFilterType::Notch:     return "Notch";
        case EqFilterType::HighShelf: return "High Shelf";
        case EqFilterType::HighCut12: return "High Cut 12";
        case EqFilterType::HighCut48: return "High Cut 48";
    }
    return "Bell";
}

// Gain means nothing on a cut or a notch, and Q is fixed on the steep cuts
// because their stages are a Butterworth cascade rather than one resonance.
// The face greys those controls out rather than hiding them, so a band keeps
// its shape as the type changes under it.
inline bool eqTypeUsesGain(EqFilterType type)
{
    return type == EqFilterType::LowShelf || type == EqFilterType::Bell
        || type == EqFilterType::HighShelf;
}

inline bool eqTypeUsesQ(EqFilterType type)
{
    return type != EqFilterType::LowCut48 && type != EqFilterType::HighCut48;
}

struct EqBandSettings
{
    bool enabled = false;
    EqFilterType type = EqFilterType::Bell;
    float frequency = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.71f;
};

// Normalised so a0 is 1: a1 and a2 are already divided through.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

inline constexpr int eqMaxStagesPerBand = 4;

struct EqBandStages
{
    std::array<Biquad, eqMaxStagesPerBand> stage {};
    int count = 0;
};

namespace eqdetail
{
inline constexpr double pi = 3.14159265358979323846;

// An eighth-order Butterworth is four biquads, and these are their pole Qs:
// 1 / (2 cos(pi (2k+1) / 2N)) for N = 8. Written out because they are
// constants of the design, not something a Q control reaches.
inline constexpr double butterworthQ[eqMaxStagesPerBand]
    {0.50979558, 0.60134489, 0.89997622, 2.56291545};

inline Biquad normalise(double b0, double b1, double b2, double a0, double a1, double a2)
{
    const auto scale = a0 != 0.0 ? 1.0 / a0 : 1.0;
    return {b0 * scale, b1 * scale, b2 * scale, a1 * scale, a2 * scale};
}

inline Biquad highPass(double w0, double q)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    return normalise((1.0 + cosw) * 0.5, -(1.0 + cosw), (1.0 + cosw) * 0.5,
                     1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

inline Biquad lowPass(double w0, double q)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    return normalise((1.0 - cosw) * 0.5, 1.0 - cosw, (1.0 - cosw) * 0.5,
                     1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

inline Biquad notch(double w0, double q)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    return normalise(1.0, -2.0 * cosw, 1.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

inline Biquad bell(double w0, double q, double gainDb)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const auto a = std::pow(10.0, gainDb / 40.0);
    return normalise(1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a,
                     1.0 + alpha / a, -2.0 * cosw, 1.0 - alpha / a);
}

inline Biquad lowShelf(double w0, double q, double gainDb)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const auto a = std::pow(10.0, gainDb / 40.0), beta = 2.0 * std::sqrt(a) * alpha;
    return normalise(a * ((a + 1.0) - (a - 1.0) * cosw + beta),
                     2.0 * a * ((a - 1.0) - (a + 1.0) * cosw),
                     a * ((a + 1.0) - (a - 1.0) * cosw - beta),
                     (a + 1.0) + (a - 1.0) * cosw + beta,
                     -2.0 * ((a - 1.0) + (a + 1.0) * cosw),
                     (a + 1.0) + (a - 1.0) * cosw - beta);
}

inline Biquad highShelf(double w0, double q, double gainDb)
{
    const auto cosw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const auto a = std::pow(10.0, gainDb / 40.0), beta = 2.0 * std::sqrt(a) * alpha;
    return normalise(a * ((a + 1.0) + (a - 1.0) * cosw + beta),
                     -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw),
                     a * ((a + 1.0) + (a - 1.0) * cosw - beta),
                     (a + 1.0) - (a - 1.0) * cosw + beta,
                     2.0 * ((a - 1.0) - (a + 1.0) * cosw),
                     (a + 1.0) - (a - 1.0) * cosw - beta);
}
}

inline constexpr float eqMinimumFrequency = 10.0f;
inline constexpr float eqMaximumFrequency = 22000.0f;
inline constexpr float eqMinimumQ = 0.1f;
inline constexpr float eqMaximumQ = 18.0f;
inline constexpr float eqMaximumGainDb = 15.0f;

// A cutoff at or above Nyquist is not a filter, it is a division by zero one
// cosine away, so every path here clamps before it computes anything.
inline double eqClampedFrequency(double frequency, double sampleRate)
{
    const auto ceiling = std::min(static_cast<double>(eqMaximumFrequency), sampleRate * 0.49);
    return std::clamp(frequency, static_cast<double>(eqMinimumFrequency), ceiling);
}

inline EqBandStages eqStagesFor(const EqBandSettings& band, double sampleRate)
{
    EqBandStages stages;
    if (sampleRate <= 0.0)
        return stages;

    const auto w0 = 2.0 * eqdetail::pi * eqClampedFrequency(band.frequency, sampleRate) / sampleRate;
    const auto q = std::clamp(static_cast<double>(band.q),
                              static_cast<double>(eqMinimumQ),
                              static_cast<double>(eqMaximumQ));
    const auto gain = static_cast<double>(band.gainDb);

    switch (band.type)
    {
        case EqFilterType::LowCut48:
            for (int i = 0; i < eqMaxStagesPerBand; ++i)
                stages.stage[static_cast<size_t>(i)] = eqdetail::highPass(w0, eqdetail::butterworthQ[i]);
            stages.count = eqMaxStagesPerBand;
            break;
        case EqFilterType::LowCut12:
            stages.stage[0] = eqdetail::highPass(w0, q);
            stages.count = 1;
            break;
        case EqFilterType::LowShelf:
            stages.stage[0] = eqdetail::lowShelf(w0, q, gain);
            stages.count = 1;
            break;
        case EqFilterType::Bell:
            stages.stage[0] = eqdetail::bell(w0, q, gain);
            stages.count = 1;
            break;
        case EqFilterType::Notch:
            stages.stage[0] = eqdetail::notch(w0, q);
            stages.count = 1;
            break;
        case EqFilterType::HighShelf:
            stages.stage[0] = eqdetail::highShelf(w0, q, gain);
            stages.count = 1;
            break;
        case EqFilterType::HighCut12:
            stages.stage[0] = eqdetail::lowPass(w0, q);
            stages.count = 1;
            break;
        case EqFilterType::HighCut48:
            for (int i = 0; i < eqMaxStagesPerBand; ++i)
                stages.stage[static_cast<size_t>(i)] = eqdetail::lowPass(w0, eqdetail::butterworthQ[i]);
            stages.count = eqMaxStagesPerBand;
            break;
    }
    return stages;
}

// Where a frequency sits on the unit circle. Drawing a curve evaluates a few
// thousand points a frame and the two trigonometric calls are most of the
// cost, so a sweep hoists this out of its inner loop rather than asking for a
// frequency over and over.
inline std::complex<double> eqUnitCircle(double frequency, double sampleRate)
{
    const auto w = sampleRate > 0.0 ? 2.0 * eqdetail::pi * frequency / sampleRate : 0.0;
    return {std::cos(-w), std::sin(-w)};
}

// The magnitude of one biquad at a point on the unit circle, evaluated rather
// than approximated. This is the same transfer function the difference
// equation implements, so nothing about the drawn curve can disagree with the
// audio except by arithmetic.
inline double biquadMagnitudeAt(const Biquad& filter, std::complex<double> z)
{
    const auto z2 = z * z;
    const auto numerator = filter.b0 + filter.b1 * z + filter.b2 * z2;
    const auto denominator = 1.0 + filter.a1 * z + filter.a2 * z2;
    return std::abs(denominator) > 1.0e-12 ? std::abs(numerator) / std::abs(denominator) : 0.0;
}

inline double biquadMagnitudeAt(const Biquad& filter, double frequency, double sampleRate)
{
    if (sampleRate <= 0.0)
        return 1.0;
    return biquadMagnitudeAt(filter, eqUnitCircle(frequency, sampleRate));
}

inline double eqStagesMagnitudeDb(const EqBandStages& stages, std::complex<double> z)
{
    auto magnitude = 1.0;
    for (int i = 0; i < stages.count; ++i)
        magnitude *= biquadMagnitudeAt(stages.stage[static_cast<size_t>(i)], z);
    return 20.0 * std::log10(std::max(magnitude, 1.0e-7));
}

inline double eqBandMagnitudeDbAt(const EqBandSettings& band, double frequency, double sampleRate)
{
    if (!band.enabled)
        return 0.0;
    return eqStagesMagnitudeDb(eqStagesFor(band, sampleRate), eqUnitCircle(frequency, sampleRate));
}

}
