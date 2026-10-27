#pragma once
#include <algorithm>
#include <cmath>

// Saturator's shaping curves, Live's eight: the one place both the audio and
// the curve drawn on the face read, so the picture cannot disagree with the
// sound (wiki: displays draw from the DSP).
//
// Every curve is odd, so none of them makes a DC offset, and every one has a
// slope of one through zero, so a quiet signal passes at the level it came in
// at whichever curve is chosen. They differ in how they approach their
// ceiling: Analog Clip and Bass Shaper stay straight to a knee and bend
// there, Digital Clip stays straight to the ceiling and stops, the three
// curves bend all the way, and Sinoid Fold turns back on itself.
//
// Pure functions on doubles, with no JUCE and no state.
namespace rhino::saturator
{
enum class Curve
{
    analogClip,
    softSine,
    bassShaper,
    mediumCurve,
    hardCurve,
    sinoidFold,
    digitalClip,
    waveshaper
};
// The chooser's list; saved documents store a position in it, so append only.
inline constexpr const char* curveNames[] { "Analog Clip", "Soft Sine", "Bass Shaper", "Medium Curve",
                                            "Hard Curve", "Sinoid Fold", "Digital Clip", "Waveshaper" };
inline constexpr int curveCount = 8;

// The Waveshaper curve's own six controls, each 0 to 1.
struct WaveshaperShape
{
    double drive = 0.5;     // how much of the shaped curve replaces the straight line
    double curve = 0.0;     // a cubic bend: mostly third harmonic
    double depth = 0.0;     // a sine ripple laid over the curve
    double linear = 0.5;    // how much of the curve stays the straight line
    double damp = 0.0;      // flattens what is near zero, an ultra-fast gate
    double period = 0.0;    // how densely the ripple folds

    bool operator==(const WaveshaperShape&) const = default;
};

constexpr double halfPi = 1.57079632679489661923;
constexpr double pi = 3.14159265358979323846;

inline double sign(double x) { return x < 0.0 ? -1.0 : 1.0; }

// Straight to `knee`, then bending smoothly into a ceiling of one: the curve
// and its slope are both continuous at the knee, so it never kinks.
inline double kneeClip(double x, double knee)
{
    const auto magnitude = std::abs(x);
    if (magnitude <= knee)
        return x;
    if (knee >= 1.0)
        return sign(x);
    const auto room = 1.0 - knee;
    return sign(x) * (knee + room * std::tanh((magnitude - knee) / room));
}

// Bass Shaper's knee: the same idea as Analog Clip, but bent by an algebraic
// sigmoid, whose harmonics fall away more gently than tanh's as the drive
// climbs. `threshold` is linear, from Live's 0 to -50 dB: high is close to a
// hard clip, low is a soft one from almost nothing.
inline double bassShaper(double x, double threshold)
{
    const auto magnitude = std::abs(x);
    if (magnitude <= threshold)
        return x;
    if (threshold >= 1.0)
        return sign(x);
    const auto room = 1.0 - threshold;
    const auto u = (magnitude - threshold) / room;
    return sign(x) * (threshold + room * u / std::sqrt(1.0 + u * u));
}

inline double waveshaper(double x, const WaveshaperShape& shape)
{
    const auto t = std::clamp(x, -1.0, 1.0);
    // A cubic that reaches one with zero slope: 1.5t - 0.5t^3.
    const auto bent = (1.0 - shape.curve) * t + shape.curve * (1.5 * t - 0.5 * t * t * t);
    const auto straight = shape.linear * t + (1.0 - shape.linear) * bent;
    const auto ripple = 0.5 * shape.depth * std::sin(pi * t * (1.0 + 15.0 * shape.period));
    auto shaped = straight + ripple;
    // An even gain under an odd curve keeps it odd.
    const auto floor = 0.1 * shape.damp;
    if (floor > 0.0)
        shaped *= t * t / (t * t + floor * floor);
    return (1.0 - shape.drive) * t + shape.drive * shaped;
}

// The chosen curve at one input value. `bassThreshold` is linear.
inline double shape(Curve curve, double x, double bassThreshold, const WaveshaperShape& waves)
{
    switch (curve)
    {
        case Curve::analogClip:  return kneeClip(x, 0.6);
        case Curve::softSine:    return std::abs(x) >= halfPi ? sign(x) : std::sin(x);
        case Curve::bassShaper:  return bassShaper(x, bassThreshold);
        case Curve::mediumCurve: return std::tanh(x);
        case Curve::hardCurve:   return x / std::sqrt(std::sqrt(1.0 + x * x * x * x));
        case Curve::sinoidFold:  return std::sin(x);
        case Curve::digitalClip: return std::clamp(x, -1.0, 1.0);
        case Curve::waveshaper:  return waveshaper(x, waves);
    }
    return x;
}

// Post Clip: Off, Soft (Analog Clip's curve again) or Hard, applied before
// the Output control so the output never passes the level Output sets.
enum class PostClip { off, soft, hard };
inline constexpr const char* postClipNames[] { "Off", "Soft", "Hard" };

inline double postClip(PostClip mode, double x)
{
    return mode == PostClip::soft ? kneeClip(x, 0.6)
         : mode == PostClip::hard ? std::clamp(x, -1.0, 1.0)
         : x;
}
}
