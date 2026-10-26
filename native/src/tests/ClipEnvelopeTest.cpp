#include "ClipEnvelopeTest.h"
#include "dsp/ClipEnvelope.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace rhino
{
namespace
{
void require(bool condition, const std::string& what)
{
    if (!condition)
        throw std::runtime_error("Clip envelopes: " + what);
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1.0e-5;
}

struct Point
{
    double timeSeconds = 0.0;
    float value = 0.0f;
};
}

void checkClipEnvelopeRules()
{
    // Each envelope rests where it does nothing, inside its own range.
    for (int kind = 0; kind < clipEnvelopeKindCount; ++kind)
    {
        const auto range = clipEnvelopeRange(static_cast<ClipEnvelopeKind>(kind));
        const auto name = std::string(clipEnvelopeName(static_cast<ClipEnvelopeKind>(kind)));
        require(range.minimum < range.maximum, name + " has a range");
        require(range.neutral >= range.minimum && range.neutral <= range.maximum, name + " rests inside it");
        require(!std::string(clipEnvelopeId(static_cast<ClipEnvelopeKind>(kind))).empty(), name + " has an id");
    }
    require(clipEnvelopeRange(ClipEnvelopeKind::gain).neutral == 1.0f, "Gain rests at the knob's own level");
    require(clipEnvelopeRange(ClipEnvelopeKind::pan).neutral == 0.0f, "Pan rests at centre");
    require(clipEnvelopeRange(ClipEnvelopeKind::pitch).neutral == 0.0f, "Pitch rests at no shift");
    require(clampClipEnvelopeValue(ClipEnvelopeKind::gain, 1.5f) == 1.0f,
            "Gain can only take the clip down from its knob");
    require(clampClipEnvelopeValue(ClipEnvelopeKind::pitch, -99.0f) == -clipEnvelopeMaximumSemitones,
            "Pitch stops where the knob does");
    require(clampClipEnvelopeValue(ClipEnvelopeKind::pan, std::nanf("")) == 0.0f, "A value that is no number rests");

    // Pitch lands on semitones unless asked to be fine.
    require(snapClipEnvelopeValue(ClipEnvelopeKind::pitch, 2.6f, false) == 3.0f, "Pitch snaps to a semitone");
    require(near(snapClipEnvelopeValue(ClipEnvelopeKind::pitch, 2.6f, true), 2.6), "Fine pitch does not snap");
    require(near(snapClipEnvelopeValue(ClipEnvelopeKind::gain, 0.37f, false), 0.37), "Gain never snaps");

    // The line between points, level outside them, neutral with none.
    const std::vector<Point> none;
    require(clipEnvelopeValueAt(none, 1.0, 0.5f) == 0.5f, "No points is the neutral value");
    const std::vector<Point> ramp {{1.0, 0.0f}, {3.0, 1.0f}};
    require(clipEnvelopeValueAt(ramp, 0.0, 9.0f) == 0.0f, "Before the first point it holds the first");
    require(near(clipEnvelopeValueAt(ramp, 2.0, 9.0f), 0.5), "Halfway along it is halfway");
    require(clipEnvelopeValueAt(ramp, 5.0, 9.0f) == 1.0f, "After the last point it holds the last");
    const std::vector<Point> one {{2.0, 0.25f}};
    require(clipEnvelopeValueAt(one, 0.0, 1.0f) == 0.25f && clipEnvelopeValueAt(one, 9.0, 1.0f) == 0.25f,
            "One point holds its value across the whole clip");
    const std::vector<Point> step {{0.0, 1.0f}, {1.0, 1.0f}, {1.0, 0.0f}, {2.0, 0.0f}};
    require(clipEnvelopeValueAt(step, 0.999, 9.0f) == 1.0f, "Before a step it is the first side");
    require(clipEnvelopeValueAt(step, 1.0, 9.0f) == 0.0f, "From a step on it is the second");
    require(clipEnvelopeValueBefore(step, 1.0, 9.0f) == 1.0f, "Just before a step it is still the first side");
    require(near(clipEnvelopeValueBefore(ramp, 2.0, 9.0f), 0.5), "Just before a moment on a ramp is the ramp");
    require(clipEnvelopeValueBefore(ramp, 1.0, 9.0f) == 0.0f && clipEnvelopeValueBefore(ramp, 9.0, 9.0f) == 1.0f,
            "and level outside the points");
    require(clipEnvelopeValueBefore(none, 1.0, 0.5f) == 0.5f, "and neutral with none");

    // The balance: centre leaves both sides, one way fades the other side.
    auto left = 0.0f, right = 0.0f;
    clipEnvelopeBalance(0.0f, left, right);
    require(left == 1.0f && right == 1.0f, "Centre leaves both sides alone");
    clipEnvelopeBalance(1.0f, left, right);
    require(left == 0.0f && right == 1.0f, "Hard right silences the left");
    clipEnvelopeBalance(-0.5f, left, right);
    require(left == 1.0f && near(right, 0.5), "Half left halves the right");

    require(clipEnvelopeGainDecibels(1.0f) == 0.0f, "All of the knob's gain is 0 dB");
    require(near(clipEnvelopeGainDecibels(0.5f), -6.0206), "Half of it is -6 dB");
    require(std::isinf(clipEnvelopeGainDecibels(0.0f)), "None of it is silence");
}
}
