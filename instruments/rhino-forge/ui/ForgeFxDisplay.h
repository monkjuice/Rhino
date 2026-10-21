#pragma once

#include "../core/ForgeFxDsp.h"
#include "ForgeFilterVisuals.h"
#include "ForgeFxVisuals.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <complex>

// What each effect draws of itself.
//
// One rule here, the same one the filter module's display was built on: a
// display is drawn from the arithmetic the engine actually runs, never from a
// picture of the effect in general. A distortion's curve is `fxShape` called
// per pixel; an equaliser's response is the magnitude of the very biquads
// `setBand` builds; a delay's repeats are placed by `fxDelaySeconds` and the
// same feedback the line is fed with. So a display cannot claim one thing while
// the slot does another — and when it disagrees with what is heard, the display
// is not the thing that is wrong.
//
// Each one is given a small, wide box and has to be legible at a glance from
// across a rack, so none of them carries axis labels. They say the shape; the
// knobs beside them and the bubble say the numbers.
namespace rhino::forge::ui
{
// The window a time-based display covers, and the floor it draws down to.
inline constexpr float fxDisplayFloorDb = -42.0f;

inline float fxAmplitudeToY(juce::Rectangle<float> box, float amplitude)
{
    const auto db = amplitude <= 1.0e-5f ? fxDisplayFloorDb
                                         : juce::jmax(fxDisplayFloorDb, 20.0f * std::log10(amplitude));
    const auto at = 1.0f - (db - fxDisplayFloorDb) / (0.0f - fxDisplayFloorDb);
    return box.getY() + juce::jlimit(0.0f, 1.0f, at) * box.getHeight();
}

// --- Reverb -------------------------------------------------------------------
//
// The tail, as an envelope over time. A comb multiplies by `decay` once per
// round trip of its own length, so the amplitude after t seconds is decay
// raised to the number of trips — which is what this plots, from the same two
// numbers renderReverb works out. The pre-delay is the flat stretch in front of
// it, and it is drawn because separating a transient from its tail is what a
// pre-delay is for and the one thing an envelope can show about it.
inline void drawFxReverb(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                         juce::Colour colour, float alpha)
{
    const auto size = fxScaled(slot.knobs[0], 0.35f, 1.0f);
    const auto decay = fxReverbDecay(slot);
    const auto preDelay = fxScaled(slot.knobs[1], 0.0f, 0.2f);

    // The mean comb length, in seconds, which is the round trip the decay is
    // applied once per.
    auto mean = 0.0f;
    for (const auto length : reverbCombLengths()) mean += static_cast<float>(length);
    mean = mean / static_cast<float>(FxSlotState::combCount) * size / 44100.0f;

    // Sixty decibels down is where a tail stops being one, so the window is
    // that long — the curve then always fills the box whatever the decay is,
    // and what is read is its shape rather than its length.
    const auto trips = std::log(0.001f) / std::log(juce::jlimit(0.05f, 0.999f, decay));
    const auto span = juce::jmax(0.25f, preDelay + mean * trips);

    juce::Path tail;
    tail.startNewSubPath(box.getX(), box.getBottom());
    const auto points = juce::jlimit(32, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto at = static_cast<float>(i) / static_cast<float>(points);
        const auto seconds = at * span;
        const auto x = box.getX() + at * box.getWidth();
        const auto amplitude = seconds < preDelay
            ? 0.0f : std::pow(decay, (seconds - preDelay) / juce::jmax(1.0e-4f, mean));
        tail.lineTo(x, fxAmplitudeToY(box, amplitude));
    }
    tail.lineTo(box.getRight(), box.getBottom());
    tail.closeSubPath();

    g.setColour(colour.withAlpha(alpha * 0.22f));
    g.fillPath(tail);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(tail, juce::PathStrokeType(1.4f));
}

// --- Delay --------------------------------------------------------------------
//
// The repeats: where each one lands and how loud it is. The two channels are
// drawn apart, above and below the centre, because the right-hand offset and
// ping-pong are both about the two arriving at different times — and a single
// row of stems could say neither.
inline void drawFxDelay(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                        double bpm, juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::delay)];
    const auto pingPong = fxModeOf(info.modeA, slot.modeA) == 1;
    const auto seconds = fxDelaySeconds(slot, bpm);
    const auto ratio = fxOffsetRatio(slot.knobs[1]);
    const auto feedback = juce::jlimit(0.0f, 0.95f, slot.knobs[2]);

    // Long enough to hold the repeats that are still audible, so a short delay
    // with a lot of feedback and a long one with none both fill the box.
    const auto quietest = 0.02f;
    const auto audible = feedback <= 0.01f ? 1
        : juce::jlimit(1, 24, juce::roundToInt(std::log(quietest) / std::log(feedback)));
    const auto span = juce::jmax(0.05f, seconds * juce::jmax(1.0f, ratio) * (audible + 0.6f));
    const auto middle = box.getCentreY();

    g.setColour(line.withAlpha(alpha * 0.5f));
    g.fillRect(box.getX(), middle - 0.5f, box.getWidth(), 1.0f);

    for (int repeat = 0; repeat <= audible; ++repeat)
    {
        const auto amplitude = std::pow(feedback, static_cast<float>(repeat));
        if (amplitude < quietest * 0.5f) break;
        // Ping-pong crosses the repeats, so each one arrives on the other side.
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto crossed = pingPong && (repeat % 2 == 1);
            const auto side = crossed ? 1 - channel : channel;
            const auto at = seconds * (static_cast<float>(repeat) + (channel == 1 ? ratio - 1.0f : 0.0f))
                          + (repeat == 0 ? 0.0f : 0.0f);
            const auto x = box.getX() + juce::jlimit(0.0f, 1.0f, at / span) * box.getWidth();
            const auto height = (middle - box.getY() - 2.0f) * amplitude;
            g.setColour(colour.withAlpha(alpha * (side == 0 ? 1.0f : 0.55f)));
            if (side == 0) g.fillRect(x - 1.0f, middle - height, 2.0f, height);
            else g.fillRect(x - 1.0f, middle, 2.0f, height);
        }
    }
}

// --- Chorus -------------------------------------------------------------------
//
// The two taps' delay times across one cycle of the sweep. The distance between
// the curves is the pair of delay times, the height they swing is the depth,
// and the width of the window is the rate — everything the module does to the
// time axis, said on the time axis.
inline void drawFxChorus(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                         juce::Colour colour, float alpha)
{
    const auto first = fxScaled(slot.knobs[1], 0.5f, 30.0f);
    const auto second = fxScaled(slot.knobs[2], 0.5f, 30.0f);
    const auto depth = fxScaled(slot.knobs[3], 0.0f, 6.0f);
    // A fixed millisecond axis, so moving a delay time moves the curve rather
    // than rescaling the picture under it.
    const auto top = 40.0f;
    const auto msToY = [box, top] (float ms)
    {
        return box.getBottom() - juce::jlimit(0.0f, 1.0f, ms / top) * box.getHeight();
    };

    for (int tap = 0; tap < 2; ++tap)
    {
        juce::Path curve;
        const auto points = juce::jlimit(24, 192, juce::roundToInt(box.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const auto at = static_cast<float>(i) / static_cast<float>(points);
            const auto phase = at * juce::MathConstants<float>::twoPi;
            // The same quadrature pair renderChorus sweeps the taps with.
            const auto sweep = tap == 0 ? std::sin(phase) : std::cos(phase);
            const auto ms = (tap == 0 ? first : second) + depth * (1.0f + sweep);
            const auto x = box.getX() + at * box.getWidth();
            if (i == 0) curve.startNewSubPath(x, msToY(ms)); else curve.lineTo(x, msToY(ms));
        }
        g.setColour(colour.withAlpha(alpha * (tap == 0 ? 1.0f : 0.55f)));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }
}

// --- Distortion ---------------------------------------------------------------
//
// The filter response beside the transfer curve. The former is the same
// low/high state-variable tap renderDistortion selects; the latter is what
// comes out for what goes in, `fxShape` called once per pixel. PRE/POST does
// not change the response itself, so its short label carries that part.
inline void drawFxDistortion(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                             juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::distortion)];
    const auto shape = fxModeOf(info.modeA, slot.modeA);
    const auto drive = juce::jlimit(0.0f, 1.0f, slot.knobs[0]);
    const auto placement = fxDistortionFilterPlacement(slot);
    const auto highPass = fxDistortionFilterHighPass(slot);

    auto filterBox = box.removeFromLeft(box.getWidth() * 0.56f).withTrimmedRight(5.0f);
    auto transferBox = box.withTrimmedLeft(5.0f);
    g.setColour(line.withAlpha(alpha * 0.45f));
    g.fillRect(filterBox.getRight() + 4.5f, filterBox.getY(), 1.0f, filterBox.getHeight());

    const auto unityY = filterDbToY(filterBox, 0.0f);
    g.setColour(line.withAlpha(alpha * 0.32f));
    g.fillRect(filterBox.getX(), unityY, filterBox.getWidth(), 1.0f);

    if (placement != 0)
    {
        const auto cutoff = fxHertz(slot.knobs[1], 40.0f, 16000.0f);
        const auto q = fxScaled(slot.knobs[2], 0.4f, 8.0f);
        juce::Path response;
        const auto points = juce::jlimit(36, 192, juce::roundToInt(filterBox.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const auto x = filterBox.getX() + filterBox.getWidth()
                * static_cast<float>(i) / static_cast<float>(points);
            const auto ratio = filterXToHz(filterBox, x) / cutoff;
            const auto real = 1.0f - ratio * ratio;
            const auto imaginary = ratio / juce::jmax(0.05f, q);
            const auto denominator = std::sqrt(real * real + imaginary * imaginary);
            const auto numerator = highPass ? ratio * ratio : 1.0f;
            const auto gain = denominator <= 1.0e-9f ? 8.0f : numerator / denominator;
            const auto db = gain <= 1.0e-6f ? filterBottomDb
                                             : 20.0f * std::log10(gain);
            const auto y = filterDbToY(filterBox, db);
            if (i == 0) response.startNewSubPath(x, y); else response.lineTo(x, y);
        }
        g.setColour(colour.withAlpha(alpha));
        g.strokePath(response, juce::PathStrokeType(1.5f));
    }

    auto badge = filterBox.toNearestInt().removeFromTop(10);
    g.setFont(panelFont(Face::emphasis, 7.5f));
    g.setColour((placement == 0 ? mutedText : colour).withAlpha(alpha * 0.9f));
    g.drawFittedText(placement == 0 ? "FILTER OFF"
                                     : juce::String(highPass ? "HP" : "LP")
                                           + (placement == 1 ? "  PRE" : "  POST"),
                     badge, juce::Justification::topLeft, 1);

    g.setColour(line.withAlpha(alpha * 0.45f));
    g.drawLine(transferBox.getX(), transferBox.getBottom(),
               transferBox.getRight(), transferBox.getY(), 1.0f);
    g.fillRect(transferBox.getX(), transferBox.getCentreY() - 0.5f,
               transferBox.getWidth(), 1.0f);

    juce::Path curve;
    const auto points = juce::jlimit(36, 192, juce::roundToInt(transferBox.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto at = static_cast<float>(i) / static_cast<float>(points);
        const auto in = at * 2.0f - 1.0f;
        // Downsampling is a rate rather than a curve, so it has no transfer
        // shape to draw: it passes what it is given between the moments it
        // holds. The diagonal is the honest answer for it.
        const auto out = shape == 7 ? in : juce::jlimit(-1.0f, 1.0f, fxShape(shape, in, drive));
        const auto x = transferBox.getX() + at * transferBox.getWidth();
        const auto y = transferBox.getCentreY() - out * transferBox.getHeight() * 0.5f;
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.6f));
}

// --- Equaliser and filter -----------------------------------------------------

// A biquad's gain at one frequency, worked out from the very coefficients
// setBand built. Evaluating the transfer function on the unit circle is the
// only way this can be guaranteed to agree with what the filter does, rather
// than with what the shape it was asked for usually looks like.
inline float biquadMagnitude(const Biquad& filter, float hz, double sampleRate)
{
    const auto w = juce::MathConstants<float>::twoPi * hz / static_cast<float>(juce::jmax(1000.0, sampleRate));
    const auto cos1 = std::cos(w), sin1 = std::sin(w);
    const auto cos2 = std::cos(2.0f * w), sin2 = std::sin(2.0f * w);
    const auto numeratorReal = filter.b0 + filter.b1 * cos1 + filter.b2 * cos2;
    const auto numeratorImaginary = -(filter.b1 * sin1 + filter.b2 * sin2);
    const auto denominatorReal = 1.0f + filter.a1 * cos1 + filter.a2 * cos2;
    const auto denominatorImaginary = -(filter.a1 * sin1 + filter.a2 * sin2);
    const auto top = std::sqrt(numeratorReal * numeratorReal + numeratorImaginary * numeratorImaginary);
    const auto bottom = std::sqrt(denominatorReal * denominatorReal
                                  + denominatorImaginary * denominatorImaginary);
    return bottom <= 1.0e-9f ? 1.0f : top / bottom;
}

inline void drawFxEqualiser(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                            double sampleRate, juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::equaliser)];
    const auto lowChoice = fxModeOf(info.modeA, slot.modeA);
    const auto highChoice = fxModeOf(info.modeB, slot.modeB);
    const auto lowShape = lowChoice == 0 ? BandShape::lowShelf
                        : lowChoice == 1 ? BandShape::peak : BandShape::highPass;
    const auto highShape = highChoice == 0 ? BandShape::highShelf
                         : highChoice == 1 ? BandShape::peak : BandShape::lowPass;

    Biquad low, high;
    setBand(low, lowShape, fxHertz(slot.knobs[0], 20.0f, 2000.0f),
            fxScaled(slot.knobs[1], 0.2f, 6.0f), fxScaled(slot.knobs[2], -18.0f, 18.0f), sampleRate);
    setBand(high, highShape, fxHertz(slot.knobs[3], 500.0f, 18000.0f),
            fxScaled(slot.knobs[4], 0.2f, 6.0f), fxScaled(slot.knobs[5], -18.0f, 18.0f), sampleRate);

    g.setColour(line.withAlpha(alpha * 0.45f));
    g.fillRect(box.getX(), box.getCentreY() - 0.5f, box.getWidth(), 1.0f);

    juce::Path curve;
    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto hz = filterXToHz(box, x);
        const auto gain = biquadMagnitude(low, hz, sampleRate) * biquadMagnitude(high, hz, sampleRate);
        const auto db = gain <= 1.0e-6f ? -24.0f : juce::jlimit(-24.0f, 24.0f, 20.0f * std::log10(gain));
        const auto y = box.getCentreY() - db / 24.0f * box.getHeight() * 0.5f;
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.6f));
}

// The rack filter's own response. It cannot borrow the filter module's, because
// the two damp differently — Core's is 1/(1+res*15) and the rack's is
// 2-res*1.96 — and a curve drawn with the wrong one is exactly the lie this
// file exists to prevent.
inline void drawFxFilter(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                         juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::filter)];
    const auto type = fxModeOf(info.modeA, slot.modeA);
    const auto cutoff = fxHertz(slot.knobs[0], 30.0f, 18000.0f);
    const auto damping = juce::jmax(0.05f, 2.0f - juce::jlimit(0.0f, 0.98f, slot.knobs[1]) * 1.96f);

    juce::Path curve;
    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto ratio = juce::jmax(1.0e-4f, filterXToHz(box, x)) / juce::jmax(1.0e-4f, cutoff);
        float gain = 1.0f;
        if (type == 1 || type == 5)
        {
            const auto bottom = std::sqrt(1.0f + ratio * ratio);
            gain = type == 1 ? 1.0f / bottom : ratio / bottom;
        }
        else
        {
            const auto real = 1.0f - ratio * ratio;
            const auto imaginary = damping * ratio;
            const auto bottom = std::sqrt(real * real + imaginary * imaginary);
            const auto top = type == 4 ? ratio * ratio
                           : type == 7 ? ratio
                           : type == 3 ? std::abs(real)
                           : type == 6 ? std::sqrt(real * real + 2.25f * ratio * ratio)
                                       : 1.0f;
            gain = bottom <= 1.0e-9f ? 8.0f : top / bottom;
            if (type == 2) gain *= gain;
        }
        const auto db = gain <= 1.0e-6f ? -36.0f : juce::jlimit(-36.0f, 18.0f, 20.0f * std::log10(gain));
        const auto y = box.getBottom() - (db + 36.0f) / 54.0f * box.getHeight();
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.6f));
}

// --- Compressor --------------------------------------------------------------
//
// A static transfer curve. It is not a gain-reduction meter: the rack publishes
// no live level yet, so drawing one would pretend to know something it does not.
inline void drawFxCompressor(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                             juce::Colour colour, float alpha)
{
    const auto threshold = fxCompressorThreshold(slot.knobs[0]);
    const auto ratio = fxCompressorRatio(slot.knobs[1]);
    const auto knee = fxCompressorKnee(slot.knobs[5]);
    g.setColour(line.withAlpha(alpha * 0.45f));
    g.drawLine(box.getX(), box.getBottom(), box.getRight(), box.getY(), 1.0f);

    juce::Path curve;
    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto at = static_cast<float>(i) / static_cast<float>(points);
        const auto inputDb = -60.0f + at * 60.0f;
        const auto outputDb = fxCompressorOutputDb(inputDb, threshold, ratio, knee);
        const auto x = box.getX() + at * box.getWidth();
        const auto y = box.getBottom() - juce::jlimit(0.0f, 1.0f, (outputDb + 60.0f) / 60.0f) * box.getHeight();
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.6f));
}

// --- Phaser ------------------------------------------------------------------
//
// The notches of dry plus the actual allpass cascade, at the two ends of the
// LFO sweep. An allpass alone is flat, so drawing only it would be a straight
// line and conceal the thing a phaser does.
inline void drawFxPhaser(juce::Graphics& g, juce::Rectangle<float> box, const FxSlot& slot,
                         double sampleRate, juce::Colour colour, float alpha)
{
    const auto stages = fxPhaserStages(slot);
    const auto centre = fxHertz(slot.knobs[2], 80.0f, 8000.0f);
    const auto depth = fxScaled(slot.knobs[1], 0.0f, 4.0f);
    g.setColour(line.withAlpha(alpha * 0.35f));
    g.fillRect(box.getX(), box.getY() + 1.0f, box.getWidth(), 1.0f);

    for (int sweepIndex = 0; sweepIndex < 2; ++sweepIndex)
    {
        const auto corner = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45),
                                         centre * std::pow(2.0f, sweepIndex == 0 ? -depth : depth));
        const auto tangent = std::tan(juce::MathConstants<float>::pi * corner / static_cast<float>(sampleRate));
        const auto coefficient = (1.0f - tangent) / (1.0f + tangent);
        juce::Path curve;
        const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
            const auto hz = filterXToHz(box, x);
            const auto omega = juce::MathConstants<float>::twoPi * hz / static_cast<float>(sampleRate);
            const std::complex<float> z(std::cos(omega), -std::sin(omega));
            const auto stage = (z - coefficient) / (1.0f - coefficient * z);
            auto allpass = std::complex<float>(1.0f, 0.0f);
            for (int stageIndex = 0; stageIndex < stages; ++stageIndex) allpass *= stage;
            const auto gain = std::abs((std::complex<float>(1.0f, 0.0f) + allpass) * 0.5f);
            const auto db = gain <= 1.0e-6f ? -36.0f : juce::jmax(-36.0f, 20.0f * std::log10(gain));
            const auto y = box.getBottom() - (db + 36.0f) / 36.0f * box.getHeight();
            if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
        }
        g.setColour(colour.withAlpha(alpha * (sweepIndex == 0 ? 0.48f : 1.0f)));
        g.strokePath(curve, juce::PathStrokeType(1.4f));
    }
}

// --- The distortion's two wells ---------------------------------------------------
//
// TONE: the filter's own response, from distortionFilterMagnitude -- the exact
// gain of the state-variable filter the slot runs -- with a node at the corner
// whose height is the gain there, which is Q. Out of the path, a flat dashed
// line says so.
inline void drawFxDistortionTone(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                                 double sampleRate, juce::Colour colour, float alpha)
{
    const auto placement = fxDistortionFilterPlacement(slot);
    const auto highPass = fxDistortionFilterHighPass(slot);
    const auto box = plot.reduced(4.0f, 4.0f);
    const auto unity = filterDbToY(box, 0.0f);
    if (placement == 0)
    {
        g.setColour(mutedText.withAlpha(alpha * 0.6f));
        const float dashes[] {3.0f, 3.0f};
        g.drawDashedLine({box.getX(), unity, box.getRight(), unity}, dashes, 2, 1.0f);
        g.setFont(panelFont(Face::reading, 7.5f));
        g.drawText("FILTER OFF", box.withTop(unity + 3.0f).withHeight(10.0f), juce::Justification::centredTop, false);
        return;
    }
    g.setColour(line.withAlpha(alpha * 0.5f));
    g.fillRect(box.getX(), unity, box.getWidth(), 1.0f);

    const auto cutoff = fxHertz(slot.knobs[1], 40.0f, 16000.0f);
    const auto q = fxScaled(slot.knobs[2], 0.4f, 8.0f);
    juce::Path curve;
    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto gain = distortionFilterMagnitude(filterXToHz(box, x), cutoff, q, highPass, sampleRate);
        const auto y = filterDbToY(box, gain <= 1.0e-6f ? filterBottomDb : 20.0f * std::log10(gain));
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    auto fill = curve;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(alpha * 0.1f));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.6f));

    const auto nodeGain = distortionFilterMagnitude(cutoff, cutoff, q, highPass, sampleRate);
    const auto node = juce::Point<float>(filterHzToX(box, cutoff),
                                         juce::jlimit(box.getY() + 3.0f, box.getBottom() - 3.0f,
                                                      filterDbToY(box, 20.0f * std::log10(nodeGain))));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node), 1.6f);

    g.setFont(panelFont(Face::reading, 7.5f));
    g.drawText(juce::String(highPass ? "HP" : "LP") + (placement == 1 ? "  PRE" : "  POST"),
               box.withHeight(10.0f), juce::Justification::topLeft, false);
}

// SHAPE: what comes out for what goes in, fxShape per pixel, filled toward the
// rails with heat and the run that sits on a rail redrawn in red. DOWNSMP is a
// rate rather than a curve, so it draws a sine held as the slot holds one.
inline void drawFxDistortionShape(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                                  juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::distortion)];
    const auto shape = fxModeOf(info.modeA, slot.modeA);
    const auto drive = juce::jlimit(0.0f, 1.0f, slot.knobs[0]);
    const auto box = plot.reduced(5.0f, 4.0f);
    const auto middle = box.getCentreY();
    const auto yOf = [box, middle] (float value) { return middle - juce::jlimit(-1.0f, 1.0f, value) * box.getHeight() * 0.5f; };

    g.setColour(line.withAlpha(alpha * 0.45f));
    g.fillRect(box.getX(), middle - 0.5f, box.getWidth(), 1.0f);
    if (shape != 7) g.drawLine(box.getX(), box.getBottom(), box.getRight(), box.getY(), 1.0f);

    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    const auto factor = fxScaled(drive, 1.0f, 48.0f);
    std::array<juce::Point<float>, 257> traced {};
    std::array<bool, 257> railed {};
    juce::Path curve;
    for (int i = 0; i <= points; ++i)
    {
        const auto at = static_cast<float>(i) / static_cast<float>(points);
        float out;
        if (shape == 7)
        {
            // One and a half cycles of a sine, sampled 96 times and held for
            // as many samples as the slot holds each one.
            const auto sample = std::floor(at * 96.0f / factor) * factor;
            out = 0.92f * std::sin(sample / 96.0f * juce::MathConstants<float>::twoPi * 1.5f);
        }
        else out = juce::jlimit(-1.0f, 1.0f, fxShape(shape, at * 2.0f - 1.0f, drive));
        const auto point = juce::Point<float>(box.getX() + at * box.getWidth(), yOf(out));
        traced[static_cast<size_t>(i)] = point;
        railed[static_cast<size_t>(i)] = std::abs(out) >= 0.965f;
        if (i == 0) curve.startNewSubPath(point); else curve.lineTo(point);
    }
    auto fill = curve;
    fill.lineTo(box.getRight(), middle);
    fill.lineTo(box.getX(), middle);
    fill.closeSubPath();
    juce::ColourGradient heat(markRed.withAlpha(alpha * 0.5f), box.getX(), box.getY(),
                              markRed.withAlpha(alpha * 0.5f), box.getX(), box.getBottom(), false);
    heat.addColour(0.5, colour.withAlpha(alpha * 0.08f));
    g.setGradientFill(heat);
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.7f));
    juce::Path hot;
    for (int i = 1; i <= points; ++i)
        if (railed[static_cast<size_t>(i)] && railed[static_cast<size_t>(i - 1)])
        {
            hot.startNewSubPath(traced[static_cast<size_t>(i - 1)]);
            hot.lineTo(traced[static_cast<size_t>(i)]);
        }
    g.setColour(markRed.withAlpha(alpha));
    g.strokePath(hot, juce::PathStrokeType(2.4f));
    if (shape == 7)
    {
        g.setColour(colour.withAlpha(alpha));
        g.setFont(panelFont(Face::reading, 7.5f));
        g.drawText("HOLD " + juce::String(factor, 1), box.withHeight(10.0f), juce::Justification::topRight, false);
    }
}

inline void drawFxDisplay(juce::Graphics& g, juce::Rectangle<int> area, const FxSlot& slot,
                          double sampleRate, double bpm, float alpha);

// --- What a running effect publishes -----------------------------------------------
//
// The readings only the effect itself knows, published by the engine once a
// block for every slot: where its LFO is, the level going in and coming out,
// and the gain a compressor is taking off. Until a slot has published, its
// pictures draw what the settings alone say and its lamps stay dark.
struct FxLive
{
    bool valid = false;
    float phase = 0.0f;
    float inputPeak = 0.0f, outputPeak = 0.0f;
    float reductionDb = 0.0f;
};

// How far a panel's lamp is lit, from 0 to 1, or -1 for a panel without one.
// A knob's lamp is the knob. An equaliser band's lights as far as its gain is
// from flat, and fully when it is a pass, which always takes something out.
// The other three say what the effect is doing rather than what it is set to,
// so they stay dark until the slot has published.
inline float fxLampLevel(const FxPanel& group, const FxSlot& slot, const FxLive& live)
{
    const auto& eq = fxTypes()[static_cast<size_t>(FxType::equaliser)];
    switch (group.lamp)
    {
        case FxLamp::none: return -1.0f;
        case FxLamp::knob:
            return group.lampKnob >= 0 && group.lampKnob < fxKnobCount
                ? juce::jlimit(0.0f, 1.0f, slot.knobs[static_cast<size_t>(group.lampKnob)]) : -1.0f;
        case FxLamp::lowBand:
            return fxModeOf(eq.modeA, slot.modeA) == 2 ? 1.0f : std::abs(slot.knobs[2] * 2.0f - 1.0f);
        case FxLamp::highBand:
            return fxModeOf(eq.modeB, slot.modeB) == 2 ? 1.0f : std::abs(slot.knobs[5] * 2.0f - 1.0f);
        case FxLamp::rate:
            return live.valid ? 0.5f + 0.5f * std::cos(live.phase * juce::MathConstants<float>::twoPi) : 0.0f;
        case FxLamp::clip:
            return live.valid && live.outputPeak >= 0.98f ? 1.0f : 0.0f;
        case FxLamp::repeats:
            return live.valid ? juce::jlimit(0.0f, 1.0f, live.outputPeak * 2.0f) : 0.0f;
    }
    return -1.0f;
}

// --- FILTER ----------------------------------------------------------------------------------
//
// The response from fxFilterMagnitude, the same coefficients the slot renders
// with, on a window from -36 to +18 dB. The true 0 dB line is drawn, the top of
// the window is marked where a resonant peak is cut off, and the peak glows as
// far as it rises above unity.
inline void drawFxFilterResponse(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                                 double sampleRate, juce::Colour colour, float alpha)
{
    static const char* const slopes[] {"12 dB/OCT", "6 dB/OCT", "24 dB/OCT", "NOTCH",
                                       "12 dB/OCT", "6 dB/OCT", "PEAK", "BAND"};
    const auto& info = fxTypes()[static_cast<size_t>(FxType::filter)];
    const auto model = fxModeOf(info.modeA, slot.modeA);
    const auto box = plot.reduced(4.0f, 3.0f);
    const auto dbY = [box] (float db)
    {
        return box.getBottom() - (juce::jlimit(-36.0f, 18.0f, db) + 36.0f) / 54.0f * box.getHeight();
    };
    g.setColour(line.withAlpha(alpha * 0.8f));
    g.fillRect(box.getX(), dbY(0.0f), box.getWidth(), 1.0f);
    g.setColour(markRed.withAlpha(alpha * 0.35f));
    const float dashes[] {2.0f, 3.0f};
    g.drawDashedLine({box.getX(), box.getY() + 0.5f, box.getRight(), box.getY() + 0.5f}, dashes, 2, 1.0f);

    juce::Path curve;
    auto peakDb = -99.0f, peakX = box.getX();
    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto gain = fxFilterMagnitude(slot, filterXToHz(box, x), sampleRate);
        const auto db = gain <= 1.0e-6f ? -36.0f : 20.0f * std::log10(gain);
        if (db > peakDb) { peakDb = db; peakX = x; }
        if (i == 0) curve.startNewSubPath(x, dbY(db)); else curve.lineTo(x, dbY(db));
    }
    if (peakDb > 0.5f)
    {
        const auto glow = juce::jlimit(0.0f, 1.0f, peakDb / 18.0f);
        for (int ring = 3; ring >= 1; --ring)
        {
            const auto size = 10.0f + 9.0f * static_cast<float>(ring) * glow;
            g.setColour(colour.withAlpha(alpha * 0.1f * glow));
            g.fillEllipse(juce::Rectangle<float>(size * 1.8f, size).withCentre({peakX, dbY(peakDb)}));
        }
    }
    auto fill = curve;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.3f), box.getX(), box.getY(),
                                           colour.withAlpha(alpha * 0.02f), box.getX(), box.getBottom(), false));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.7f));

    const auto cutoff = fxHertz(slot.knobs[0], 30.0f, 18000.0f);
    const auto atCutoff = fxFilterMagnitude(slot, cutoff, sampleRate);
    const auto node = juce::Point<float>(filterHzToX(box, cutoff),
                                         juce::jlimit(box.getY() + 3.0f, box.getBottom() - 3.0f,
                                                      dbY(atCutoff <= 1.0e-6f ? -36.0f : 20.0f * std::log10(atCutoff))));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node), 1.6f);
    g.setFont(panelFont(Face::reading, 7.5f));
    g.drawText(slopes[model], box.withHeight(10.0f), juce::Justification::topRight, false);
}

// --- DELAY ------------------------------------------------------------------------------------
//
// Every repeat, walked as the slot runs it: the left line read at TIME, the
// right at TIME times OFFSET, each repeat fed back at FEEDBACK -- into its own
// line, or crossed into the other one in PING-PONG. Left repeats stand above
// the line and right ones hang below it, each dimmer than the last by what the
// loop filter takes off at 4 kHz. In beats, a grid marks the beat.
inline void drawFxDelayTape(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                            double sampleRate, double bpm, juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::delay)];
    const auto pingPong = fxModeOf(info.modeA, slot.modeA) == 1;
    const auto synced = fxModeOf(info.modeB, slot.modeB) == 1;
    const auto box = plot.reduced(6.0f, 3.0f);
    const auto middle = box.getCentreY();
    const auto seconds = fxDelaySeconds(slot, bpm);
    const auto ratio = fxOffsetRatio(slot.knobs[1]);
    const auto feedback = juce::jlimit(0.0f, 0.95f, slot.knobs[2]);
    const auto tone = fxDelayLoopGain(slot, 4000.0f, sampleRate);
    const auto audible = feedback <= 0.01f ? 1
        : juce::jlimit(1, 24, juce::roundToInt(std::log(0.01f) / std::log(feedback)));
    const auto span = juce::jmax(0.05f, seconds * juce::jmax(1.0f, ratio) * (static_cast<float>(audible) + 0.6f));
    const auto xOf = [box, span] (float t) { return box.getX() + juce::jlimit(0.0f, 1.0f, t / span) * box.getWidth(); };

    if (synced)
    {
        const auto beat = static_cast<float>(60.0 / (bpm > 0.0 ? bpm : 120.0));
        g.setColour(line.withAlpha(alpha * 0.55f));
        for (auto t = beat; t < span && t > 0.0f; t += beat)
            g.fillRect(xOf(t), box.getY(), 1.0f, box.getHeight());
    }
    g.setColour(line.withAlpha(alpha * 0.9f));
    g.fillRect(box.getX(), middle - 0.5f, box.getWidth(), 1.0f);

    const auto right = colour.interpolatedWith(juce::Colours::white, 0.35f);
    const auto reach = box.getHeight() * 0.5f - 2.0f;
    // Heights in decibels down to -40, as the ear hears them: on a linear
    // scale a third repeat at a third of the feedback is already a pixel.
    const auto heightOf = [reach] (float gain)
    {
        return reach * juce::jlimit(0.0f, 1.0f, 1.0f + 20.0f * std::log10(juce::jmax(1.0e-4f, gain)) / 40.0f);
    };
    std::array<juce::Point<float>, 2> first {};
    std::array<bool, 2> found {};
    for (int start = 0; start < 2; ++start)
    {
        auto t = 0.0f, amplitude = 1.0f, brightness = 1.0f;
        auto side = start;
        for (int repeat = 0; repeat < 24 && amplitude > 0.01f; ++repeat)
        {
            t += side == 0 ? seconds : seconds * ratio;
            if (t > span) break;
            const auto height = heightOf(amplitude);
            const auto x = xOf(t);
            const auto ink = (side == 0 ? colour : right).interpolatedWith(mutedText, 1.0f - juce::jlimit(0.0f, 1.0f, 0.35f + 0.65f * brightness));
            g.setColour(ink.withAlpha(alpha));
            g.fillRoundedRectangle(x - 1.2f, side == 0 ? middle - height : middle, 2.4f, height, 1.0f);
            if (!found[static_cast<size_t>(side)])
            {
                found[static_cast<size_t>(side)] = true;
                first[static_cast<size_t>(side)] = {x, side == 0 ? middle - height : middle + height};
            }
            amplitude *= feedback;
            brightness *= tone;
            if (pingPong) side = 1 - side;
        }
    }
    for (int side = 0; side < 2; ++side)
        if (found[static_cast<size_t>(side)])
        {
            const auto at = first[static_cast<size_t>(side)];
            g.setColour(displayFloor);
            g.fillEllipse(juce::Rectangle<float>(7.2f, 7.2f).withCentre(at));
            g.setColour((side == 0 ? colour : right).withAlpha(alpha));
            g.drawEllipse(juce::Rectangle<float>(7.2f, 7.2f).withCentre(at), 1.5f);
        }
    g.setFont(panelFont(Face::reading, 7.0f));
    g.setColour(colour.withAlpha(alpha));
    g.drawText("L", box.withHeight(9.0f), juce::Justification::topLeft, false);
    g.setColour(right.withAlpha(alpha));
    g.drawText("R", box.withTop(box.getBottom() - 9.0f), juce::Justification::bottomLeft, false);
}

// What every repeat passes through on its way round: fxDelayLoopGain, from
// 0 to -24 dB, with a node at the corner on the curve.
inline void drawFxDelayLoop(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                            double sampleRate, juce::Colour colour, float alpha)
{
    const auto box = plot.reduced(4.0f, 4.0f);
    const auto dbY = [box] (float db) { return box.getY() + juce::jlimit(0.0f, 1.0f, -db / 24.0f) * box.getHeight(); };
    juce::Path curve;
    const auto points = juce::jlimit(32, 160, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto db = 20.0f * std::log10(juce::jmax(1.0e-6f, fxDelayLoopGain(slot, filterXToHz(box, x), sampleRate)));
        if (i == 0) curve.startNewSubPath(x, dbY(db)); else curve.lineTo(x, dbY(db));
    }
    auto fill = curve;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(alpha * 0.14f));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.5f));
    const auto corner = fxHertz(slot.knobs[3], 200.0f, 16000.0f);
    const auto node = juce::Point<float>(filterHzToX(box, corner),
                                         dbY(20.0f * std::log10(juce::jmax(1.0e-6f, fxDelayLoopGain(slot, corner, sampleRate)))));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(7.2f, 7.2f).withCentre(node));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(7.2f, 7.2f).withCentre(node), 1.5f);
}

// --- REVERB -----------------------------------------------------------------------------------
//
// The tail on a fixed axis to 6.5 seconds, square-root scaled so a short plate
// and a long hall are both readable and SIZE and TYPE visibly lengthen it. The
// pre-delay is the gap in front, the decay comes from fxReverbDecay over the
// comb's own mean trip, and the dashed line is the RT60 SIZE reads.
inline constexpr float fxReverbAxisSeconds = 6.5f;

inline void drawFxReverbTail(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                             double sampleRate, juce::Colour colour, float alpha)
{
    const auto box = plot.reduced(5.0f, 3.0f).withTrimmedBottom(9.0f);
    const auto tX = [box] (float t)
    {
        return box.getX() + std::sqrt(juce::jlimit(0.0f, 1.0f, t / fxReverbAxisSeconds)) * box.getWidth();
    };
    const auto dbY = [box] (float db) { return box.getY() + juce::jlimit(0.0f, 1.0f, -db / 60.0f) * box.getHeight(); };
    g.setFont(panelFont(Face::reading, 7.0f));
    for (const auto t : {0.1f, 0.5f, 1.0f, 2.0f, 4.0f})
    {
        g.setColour(line.withAlpha(alpha * 0.5f));
        g.fillRect(tX(t), box.getY(), 1.0f, box.getHeight());
        g.setColour(mutedText.withAlpha(alpha));
        g.drawText(t < 1.0f ? juce::String(t, 1).substring(1) : juce::String(juce::roundToInt(t)) + "s",
                   juce::Rectangle<float>(24.0f, 9.0f).withCentre({tX(t), box.getBottom() + 5.0f}),
                   juce::Justification::centred, false);
    }
    const auto preDelay = fxScaled(slot.knobs[1], 0.0f, 0.2f);
    const auto decay = fxReverbDecay(slot);
    const auto trip = juce::jmax(1.0e-4f, fxReverbMeanTrip(slot));
    const auto rt60 = fxReverbRt60(slot, 0.0f, sampleRate);
    g.setColour(colour.withAlpha(alpha * 0.1f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(tX(0.0f), box.getY(), tX(preDelay), box.getBottom()));

    juce::Path tail;
    tail.startNewSubPath(tX(preDelay), box.getBottom());
    tail.lineTo(tX(preDelay), dbY(0.0f));
    for (int i = 1; i <= 96; ++i)
    {
        const auto t = preDelay + std::pow(static_cast<float>(i) / 96.0f, 2.0f) * (fxReverbAxisSeconds - preDelay);
        tail.lineTo(tX(t), dbY(20.0f * std::log10(juce::jmax(1.0e-6f, std::pow(decay, (t - preDelay) / trip)))));
    }
    auto fill = tail;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.35f), box.getX(), box.getY(),
                                           colour.withAlpha(alpha * 0.02f), box.getX(), box.getBottom(), false));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(tail, juce::PathStrokeType(1.6f));
    const float dashes[] {2.0f, 2.0f};
    g.setColour(colour.withAlpha(alpha * 0.6f));
    g.drawDashedLine({tX(preDelay + rt60), box.getY(), tX(preDelay + rt60), box.getBottom()}, dashes, 2, 1.0f);
    for (const auto node : {juce::Point<float>(tX(preDelay), box.getY() + 3.0f),
                            juce::Point<float>(tX(preDelay + rt60), box.getBottom() - 3.0f)})
    {
        g.setColour(displayFloor);
        g.fillEllipse(juce::Rectangle<float>(6.8f, 6.8f).withCentre(node));
        g.setColour(colour.withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(6.8f, 6.8f).withCentre(node), 1.4f);
    }
}

// How long each frequency rings: fxReverbRt60 per frequency, DAMP's one-pole
// applied once per trip, on the same square-root scale, with the band outside
// LO CUT and HI CUT shaded off.
inline void drawFxReverbDecay(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                              double sampleRate, juce::Colour colour, float alpha)
{
    const auto box = plot.reduced(4.0f, 4.0f);
    const auto rtY = [box] (float t)
    {
        return box.getBottom() - std::sqrt(juce::jlimit(0.0f, 1.0f, t / fxReverbAxisSeconds)) * box.getHeight();
    };
    const auto low = fxHertz(slot.knobs[4], 20.0f, 1200.0f);
    const auto high = fxHertz(slot.knobs[5], 1200.0f, 20000.0f);
    g.setColour(wellFloor.withAlpha(alpha * 0.65f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(box.getX(), box.getY(), filterHzToX(box, low), box.getBottom()));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(filterHzToX(box, high), box.getY(), box.getRight(), box.getBottom()));
    juce::Path curve;
    const auto points = juce::jlimit(32, 160, juce::roundToInt(box.getWidth()));
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto y = rtY(fxReverbRt60(slot, filterXToHz(box, x), sampleRate));
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    auto fill = curve;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    g.setColour(colour.withAlpha(alpha * 0.12f));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.5f));
    g.setColour(colour.withAlpha(alpha * 0.75f));
    for (const auto hz : {low, high})
        g.fillRect(filterHzToX(box, hz) - 0.7f, box.getY(), 1.4f, box.getHeight());
}

// --- EQ --------------------------------------------------------------------------------------
//
// Each band's own response, faint, under their product -- the magnitudes of the
// very biquads setBand builds -- on a +/-24 dB window with a graticule at 100 Hz,
// 1 kHz, 10 kHz and +/-12 dB. A node per band stands at its frequency and gain.
inline void drawFxEqResponse(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                             double sampleRate, juce::Colour lowColour, juce::Colour highColour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::equaliser)];
    const auto lowChoice = fxModeOf(info.modeA, slot.modeA);
    const auto highChoice = fxModeOf(info.modeB, slot.modeB);
    Biquad low, high;
    setBand(low, lowChoice == 0 ? BandShape::lowShelf : lowChoice == 1 ? BandShape::peak : BandShape::highPass,
            fxHertz(slot.knobs[0], 20.0f, 2000.0f), fxScaled(slot.knobs[1], 0.2f, 6.0f),
            fxScaled(slot.knobs[2], -18.0f, 18.0f), sampleRate);
    setBand(high, highChoice == 0 ? BandShape::highShelf : highChoice == 1 ? BandShape::peak : BandShape::lowPass,
            fxHertz(slot.knobs[3], 500.0f, 18000.0f), fxScaled(slot.knobs[4], 0.2f, 6.0f),
            fxScaled(slot.knobs[5], -18.0f, 18.0f), sampleRate);
    const auto box = plot.reduced(4.0f, 4.0f);
    const auto dbY = [box] (float db) { return box.getCentreY() - juce::jlimit(-24.0f, 24.0f, db) / 24.0f * box.getHeight() * 0.5f; };
    g.setColour(line.withAlpha(alpha * 0.5f));
    for (const auto hz : {100.0f, 1000.0f, 10000.0f}) g.fillRect(filterHzToX(box, hz), box.getY(), 1.0f, box.getHeight());
    const float dashes[] {2.0f, 3.0f};
    for (const auto db : {-12.0f, 12.0f})
        g.drawDashedLine({box.getX(), dbY(db), box.getRight(), dbY(db)}, dashes, 2, 1.0f);
    g.setColour(line.withAlpha(alpha * 0.95f));
    g.fillRect(box.getX(), dbY(0.0f) - 0.5f, box.getWidth(), 1.0f);

    const auto points = juce::jlimit(48, 256, juce::roundToInt(box.getWidth()));
    juce::Path lowTrace, highTrace, sum;
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
        const auto hz = filterXToHz(box, x);
        const auto a = biquadMagnitude(low, hz, sampleRate), b = biquadMagnitude(high, hz, sampleRate);
        const auto toDb = [] (float gain) { return gain <= 1.0e-6f ? -24.0f : 20.0f * std::log10(gain); };
        const auto add = [i, x] (juce::Path& path, float y) { if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y); };
        add(lowTrace, dbY(toDb(a)));
        add(highTrace, dbY(toDb(b)));
        add(sum, dbY(toDb(a * b)));
    }
    g.setColour(lowColour.withAlpha(alpha * 0.45f));
    g.strokePath(lowTrace, juce::PathStrokeType(1.0f));
    g.setColour(highColour.withAlpha(alpha * 0.45f));
    g.strokePath(highTrace, juce::PathStrokeType(1.0f));
    auto fill = sum;
    fill.lineTo(box.getRight(), dbY(0.0f));
    fill.lineTo(box.getX(), dbY(0.0f));
    fill.closeSubPath();
    g.setColour(lowColour.withAlpha(alpha * 0.12f));
    g.fillPath(fill);
    g.setColour(lowColour.interpolatedWith(highColour, 0.5f).withAlpha(alpha));
    g.strokePath(sum, juce::PathStrokeType(1.7f));

    const auto node = [&] (float hz, float gainDb, bool live, juce::Colour ink, const char* letter)
    {
        const auto at = juce::Point<float>(filterHzToX(box, hz), dbY(live ? gainDb : 0.0f));
        g.setColour(displayFloor);
        g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(at));
        g.setColour(ink.withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre(at), 1.5f);
        g.setFont(panelFont(Face::emphasis, 7.5f));
        g.drawText(letter, juce::Rectangle<float>(10.0f, 10.0f).withCentre(at), juce::Justification::centred, false);
    };
    node(fxHertz(slot.knobs[0], 20.0f, 2000.0f), fxScaled(slot.knobs[2], -18.0f, 18.0f), lowChoice != 2, lowColour, "L");
    node(fxHertz(slot.knobs[3], 500.0f, 18000.0f), fxScaled(slot.knobs[5], -18.0f, 18.0f), highChoice != 2, highColour, "H");
}

// --- COMP ------------------------------------------------------------------------------------
//
// The static curve, lifted by the makeup actually applied and held at 0 dB
// where the slot clips, with the knee's width shaded. A dot rides it at the
// level the detector has, once the engine publishes one.
inline void drawFxCompressorCurve(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                                  juce::Colour colour, float alpha, const FxLive& live)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::compressor)];
    const auto threshold = fxCompressorThreshold(slot.knobs[0]);
    const auto ratio = fxCompressorRatio(slot.knobs[1]);
    const auto knee = fxCompressorKnee(slot.knobs[5]);
    const auto automatic = fxModeOf(info.modeB, slot.modeB) == 1;
    const auto makeup = automatic ? fxCompressorAutoMakeup(threshold, ratio) : fxCompressorMakeup(slot.knobs[4]);
    const auto box = plot.reduced(5.0f, 4.0f);
    const auto xOf = [box] (float db) { return box.getX() + (juce::jlimit(-60.0f, 0.0f, db) + 60.0f) / 60.0f * box.getWidth(); };
    const auto yOf = [box] (float db) { return box.getBottom() - juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * box.getHeight(); };
    const auto outAt = [&] (float in) { return juce::jmin(0.0f, fxCompressorOutputDb(in, threshold, ratio, knee) + makeup); };

    g.setColour(line.withAlpha(alpha * 0.9f));
    g.drawLine(xOf(-60.0f), yOf(-60.0f), xOf(0.0f), yOf(0.0f), 1.0f);
    g.setColour(markRed.withAlpha(alpha * 0.4f));
    const float dashes[] {2.0f, 3.0f};
    g.drawDashedLine({box.getX(), yOf(0.0f), box.getRight(), yOf(0.0f)}, dashes, 2, 1.0f);
    if (knee > 0.1f)
    {
        g.setColour(colour.withAlpha(alpha * 0.08f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(xOf(threshold - knee * 0.5f), box.getY(),
                                                              xOf(threshold + knee * 0.5f), box.getBottom()));
    }
    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const auto in = -60.0f + static_cast<float>(i) * 0.5f;
        if (i == 0) curve.startNewSubPath(xOf(in), yOf(outAt(in))); else curve.lineTo(xOf(in), yOf(outAt(in)));
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.7f));
    const auto knot = juce::Point<float>(xOf(threshold), yOf(outAt(threshold)));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(knot));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(knot), 1.5f);
    const auto end = juce::Rectangle<float>(7.0f, 7.0f).withCentre({xOf(0.0f) - 3.5f, yOf(outAt(0.0f))});
    g.setColour(displayFloor);
    g.fillRect(end);
    g.setColour(colour.withAlpha(alpha));
    g.drawRect(end, 1.4f);
    if (live.valid && live.inputPeak > 1.0e-4f)
    {
        const auto in = 20.0f * std::log10(live.inputPeak);
        if (in > -60.0f)
        {
            g.setColour(text.withAlpha(alpha * 0.9f));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({xOf(in), yOf(outAt(in))}));
        }
    }
}

// The gain being taken off, on a reverse-reading needle that rests at the right
// and dips left. Only the engine knows it, so until a slot has published, the
// needle rests.
inline void drawFxCompressorMeter(juce::Graphics& g, juce::Rectangle<float> area, float alpha, const FxLive& live)
{
    const auto face = area.reduced(1.0f);
    g.setColour(meterFace.withAlpha(alpha));
    g.fillRoundedRectangle(face, 3.0f);
    g.setColour(meterInk.withAlpha(alpha * 0.5f));
    g.drawRoundedRectangle(face, 3.0f, 1.0f);
    const auto pivot = juce::Point<float>(face.getCentreX(), face.getBottom() - 8.0f);
    const auto radius = juce::jmin(face.getWidth() * 0.5f - 8.0f, face.getHeight() - 20.0f);
    const auto angleFor = [] (float db)
    {
        return juce::degreesToRadians(52.0f - juce::jlimit(0.0f, 20.0f, -db) / 20.0f * 104.0f);
    };
    const auto polar = [pivot] (float r, float angle) { return pivot + juce::Point<float>(std::sin(angle) * r, -std::cos(angle) * r); };
    juce::Path scale;
    scale.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f, angleFor(-20.0f), angleFor(0.0f), true);
    g.setColour(meterInk.withAlpha(alpha));
    g.strokePath(scale, juce::PathStrokeType(0.8f));
    g.setFont(panelFont(Face::header, 7.0f));
    for (const auto db : {0.0f, -1.0f, -2.0f, -3.0f, -5.0f, -7.0f, -10.0f, -20.0f})
    {
        const auto angle = angleFor(db);
        const auto ink = (db <= -10.0f ? meterRed : meterInk).withAlpha(alpha);
        g.setColour(ink);
        g.drawLine({polar(radius, angle), polar(radius - 4.0f, angle)}, 1.0f);
        if (db == 0.0f || db == -3.0f || db == -10.0f || db == -20.0f)
            g.drawText(juce::String(juce::roundToInt(-db)),
                       juce::Rectangle<float>(16.0f, 9.0f).withCentre(polar(radius + 6.0f, angle)),
                       juce::Justification::centred, false);
    }
    const auto needle = angleFor(live.valid ? live.reductionDb : 0.0f);
    g.setColour(meterInk.withAlpha(alpha));
    g.drawLine({pivot, polar(radius + 2.0f, needle)}, 1.2f);
    g.fillEllipse(juce::Rectangle<float>(4.8f, 4.8f).withCentre(pivot));
    g.drawText("GR", juce::Rectangle<float>(face.getX() + 4.0f, face.getBottom() - 11.0f, 20.0f, 9.0f),
               juce::Justification::centredLeft, false);
    g.drawText("dB", juce::Rectangle<float>(face.getRight() - 24.0f, face.getBottom() - 11.0f, 20.0f, 9.0f),
               juce::Justification::centredRight, false);
}

// --- CHORUS ----------------------------------------------------------------------------------
//
// Both channels' taps across three seconds of the sweep, on a fixed 0-42 ms
// scale: the left solid and the right, which reads the same taps the other way
// round, dashed. RATE is how dense the waves are, the delays how high they
// sit, DEPTH how far they swing. A playhead marks where the LFO is.
inline void drawFxChorusTaps(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot, double bpm,
                             juce::Colour colour, float alpha, const FxLive& live)
{
    const auto right = juce::Colour(0xff9fe7ff);
    const auto box = plot.reduced(4.0f, 4.0f).withTrimmedLeft(14.0f);
    const auto msY = [box] (float ms) { return box.getBottom() - juce::jlimit(0.0f, 1.0f, ms / 42.0f) * box.getHeight(); };
    g.setFont(panelFont(Face::reading, 7.0f));
    for (const auto ms : {10.0f, 20.0f, 30.0f, 40.0f})
    {
        g.setColour(line.withAlpha(alpha * 0.55f));
        g.fillRect(box.getX(), msY(ms), box.getWidth(), 1.0f);
        g.setColour(mutedText.withAlpha(alpha));
        g.drawText(juce::String(juce::roundToInt(ms)), juce::Rectangle<float>(box.getX() - 15.0f, msY(ms) - 5.0f, 13.0f, 10.0f),
                   juce::Justification::centredRight, false);
    }
    const auto first = fxScaled(slot.knobs[1], 0.5f, 30.0f);
    const auto second = fxScaled(slot.knobs[2], 0.5f, 30.0f);
    const auto depth = fxScaled(slot.knobs[3], 0.0f, 6.0f);
    const auto cycles = juce::jmin(24.0f, fxChorusRate(slot, bpm) * 3.0f);
    const auto trace = [&] (float base, bool quadrature, float side)
    {
        juce::Path path;
        const auto points = juce::jlimit(48, 192, juce::roundToInt(box.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const auto at = static_cast<float>(i) / static_cast<float>(points);
            const auto phase = at * cycles * juce::MathConstants<float>::twoPi;
            const auto sweep = quadrature ? std::cos(phase) : std::sin(phase);
            const auto y = msY(base + depth * (1.0f + side * sweep));
            const auto x = box.getX() + at * box.getWidth();
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        return path;
    };
    const float dashes[] {3.0f, 2.0f};
    const auto dashed = [&] (const juce::Path& path, juce::Colour ink)
    {
        juce::Path out;
        juce::PathStrokeType(1.1f).createDashedStroke(out, path, dashes, 2);
        g.setColour(ink);
        g.fillPath(out);
    };
    dashed(trace(second, true, -1.0f), right.withAlpha(alpha * 0.45f));
    dashed(trace(first, false, -1.0f), right.withAlpha(alpha * 0.65f));
    g.setColour(colour.withAlpha(alpha * 0.6f));
    g.strokePath(trace(second, true, 1.0f), juce::PathStrokeType(1.4f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(trace(first, false, 1.0f), juce::PathStrokeType(1.6f));
    for (const auto base : {first, second})
    {
        const auto at = juce::Point<float>(box.getRight() - 4.0f, msY(base));
        g.setColour(displayFloor);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(at));
        g.setColour(colour.withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(at), 1.4f);
    }
    if (live.valid && cycles > 0.0f)
    {
        const auto x = box.getX() + juce::jlimit(0.0f, 1.0f, live.phase / juce::jmax(1.0f, cycles)) * box.getWidth();
        g.setColour(text.withAlpha(alpha * 0.35f));
        g.fillRect(x, box.getY(), 1.0f, box.getHeight());
    }
    // The legend at the top right, clear of the scale and of the delay nodes.
    const auto legend = box.withHeight(9.0f).withTrimmedRight(14.0f);
    g.setColour(colour.withAlpha(alpha));
    g.drawText("L", legend.withTrimmedRight(10.0f), juce::Justification::topRight, false);
    g.setColour(right.withAlpha(alpha));
    g.drawText("R", legend, juce::Justification::topRight, false);
}

// --- PHASER ----------------------------------------------------------------------------------
//
// The notches dry plus the allpass cascade make, feedback included, where the
// LFO has the corner now -- the left channel filled with a slick of colour that
// drifts with the sweep, the right drawn faint where SPREAD parts it from the
// left and WIDTH lets it be heard. The band the corner travels is shaded and
// its centre, FREQ, marked on top.
inline void drawFxPhaserComb(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot, double sampleRate,
                             juce::Colour colour, float alpha, const FxLive& live)
{
    using Complex = std::complex<float>;
    const auto box = plot.reduced(4.0f, 3.0f);
    const auto dbY = [box] (float db) { return box.getY() + (12.0f - juce::jlimit(-30.0f, 12.0f, db)) / 42.0f * box.getHeight(); };
    const auto stages = fxPhaserStages(slot);
    const auto centre = fxHertz(slot.knobs[2], 80.0f, 8000.0f);
    const auto depth = fxScaled(slot.knobs[1], 0.0f, 4.0f);
    const auto feedback = fxScaled(slot.knobs[3], -0.85f, 0.85f);
    const auto spread = fxScaled(slot.knobs[4], -0.5f, 0.5f);
    const auto width = juce::jlimit(0.0f, 1.0f, slot.knobs[5]);
    const auto phase = live.valid ? live.phase : 0.0f;
    const auto rate = static_cast<float>(juce::jmax(1000.0, sampleRate));

    const auto lowest = juce::jlimit(20.0f, 20000.0f, centre / std::pow(2.0f, depth));
    const auto highest = juce::jlimit(20.0f, 20000.0f, centre * std::pow(2.0f, depth));
    g.setColour(colour.withAlpha(alpha * 0.07f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(filterHzToX(box, lowest), box.getY(),
                                                          filterHzToX(box, highest), box.getBottom()));
    const auto comb = [&] (float corner)
    {
        const auto tangent = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, corner) / rate);
        const auto a = (1.0f - tangent) / (1.0f + tangent);
        juce::Path path;
        const auto points = juce::jlimit(64, 220, juce::roundToInt(box.getWidth()));
        for (int i = 0; i <= points; ++i)
        {
            const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
            const auto w = juce::MathConstants<float>::twoPi * filterXToHz(box, x) / rate;
            const Complex z(std::cos(w), -std::sin(w));
            const auto stage = (z - a) / (Complex(1.0f, 0.0f) - a * z);
            auto cascade = Complex(1.0f, 0.0f);
            for (int s = 0; s < stages; ++s) cascade *= stage;
            const auto looped = cascade / (Complex(1.0f, 0.0f) - feedback * cascade * z);
            const auto gain = std::abs((Complex(1.0f, 0.0f) + looped) * 0.5f);
            const auto y = dbY(gain <= 1.0e-6f ? -30.0f : 20.0f * std::log10(gain));
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        return path;
    };
    const auto cornerAt = [&] (float offset)
    {
        return centre * std::pow(2.0f, depth * std::sin(phase * juce::MathConstants<float>::twoPi + offset * juce::MathConstants<float>::pi));
    };
    const auto left = comb(cornerAt(-spread));
    auto fill = left;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    // The one place on the rack colour is spent freely: a slick of it under
    // the comb, drifting with the sweep.
    const auto drift = std::fmod(phase, 1.0f) * box.getWidth() * 0.6f;
    juce::ColourGradient slick(juce::Colour(0xffb58cff).withAlpha(alpha * 0.55f), box.getX() - drift, 0.0f,
                               juce::Colour(0xffb58cff).withAlpha(alpha * 0.55f), box.getRight() + box.getWidth() * 0.6f - drift, 0.0f, false);
    slick.addColour(0.3, juce::Colour(0xffff7ad9).withAlpha(alpha * 0.55f));
    slick.addColour(0.55, juce::Colour(0xff5fe3ff).withAlpha(alpha * 0.55f));
    slick.addColour(0.8, juce::Colour(0xffffd36b).withAlpha(alpha * 0.55f));
    g.setGradientFill(slick);
    g.fillPath(fill);
    if (width > 0.02f && std::abs(spread) > 0.01f)
    {
        g.setColour(juce::Colour(0xffff8fd8).withAlpha(alpha * (0.3f + 0.5f * width)));
        g.strokePath(comb(cornerAt(spread)), juce::PathStrokeType(1.2f));
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(left, juce::PathStrokeType(1.6f));
    g.setColour(line.withAlpha(alpha * 0.7f));
    const float dashes[] {2.0f, 3.0f};
    g.drawDashedLine({box.getX(), dbY(0.0f), box.getRight(), dbY(0.0f)}, dashes, 2, 1.0f);
    const auto marker = filterHzToX(box, centre);
    juce::Path diamond;
    diamond.addTriangle(marker - 4.0f, box.getY() + 5.0f, marker + 4.0f, box.getY() + 5.0f, marker, box.getY() + 10.0f);
    diamond.addTriangle(marker - 4.0f, box.getY() + 5.0f, marker + 4.0f, box.getY() + 5.0f, marker, box.getY());
    g.setColour(colour.withAlpha(alpha));
    g.fillPath(diamond);
}

// --- FLANGER ---------------------------------------------------------------------------------
//
// The comb dry plus the swept tap make -- fxFlangerMagnitude, the line's
// interpolation and the feedback included -- where the sweep has the left
// side's tap now, over faint combs at the two ends of its travel. The fill
// streaks toward white like a jet's exhaust, and the delay the tap stands at
// is printed in the corner. On the log axis every picture here uses, so the
// teeth crowd toward the top as they do to the ear.
inline void drawFxFlangerComb(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot, double sampleRate,
                              juce::Colour colour, float alpha, const FxLive& live)
{
    const auto box = plot.reduced(4.0f, 3.0f);
    const auto dbY = [box] (float db) { return box.getY() + (12.0f - juce::jlimit(-30.0f, 12.0f, db)) / 42.0f * box.getHeight(); };
    const auto base = fxFlangerDelayMs(slot.knobs[3]);
    const auto depth = fxFlangerDepthMs(slot.knobs[1]);
    const auto spread = fxScaled(slot.knobs[4], -0.5f, 0.5f);
    // Two points a pixel, because the teeth are a few pixels apart at the top.
    const auto points = juce::jlimit(96, 640, juce::roundToInt(box.getWidth() * 2.0f));
    const auto comb = [&] (float delayMs)
    {
        juce::Path path;
        for (int i = 0; i <= points; ++i)
        {
            const auto x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points);
            const auto gain = fxFlangerMagnitude(slot, filterXToHz(box, x), sampleRate, delayMs);
            const auto y = dbY(gain <= 1.0e-6f ? -30.0f : 20.0f * std::log10(gain));
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        return path;
    };
    g.setColour(colour.withAlpha(alpha * 0.18f));
    g.strokePath(comb(base), juce::PathStrokeType(1.0f));
    if (depth > 0.01f) g.strokePath(comb(base + depth), juce::PathStrokeType(1.0f));

    const auto now = base + depth * (live.valid ? fxFlangerSweep(live.phase, -spread) : 0.5f);
    const auto current = comb(now);
    auto fill = current;
    fill.lineTo(box.getRight(), box.getBottom());
    fill.lineTo(box.getX(), box.getBottom());
    fill.closeSubPath();
    juce::ColourGradient jet(colour.withAlpha(alpha * 0.04f), box.getX(), 0.0f,
                             juce::Colours::white.withAlpha(alpha * 0.4f), box.getRight(), 0.0f, false);
    jet.addColour(0.6, colour.withAlpha(alpha * 0.35f));
    g.setGradientFill(jet);
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(current, juce::PathStrokeType(1.6f));
    g.setColour(line.withAlpha(alpha * 0.7f));
    const float dashes[] {2.0f, 3.0f};
    g.drawDashedLine({box.getX(), dbY(0.0f), box.getRight(), dbY(0.0f)}, dashes, 2, 1.0f);
    g.setFont(panelFont(Face::reading, 7.5f));
    g.setColour(colour.withAlpha(alpha));
    g.drawText(juce::String(now, 2) + " ms", box.withHeight(10.0f), juce::Justification::topRight, false);
}

// One of a strip's wells: the well itself, and whichever picture its kind
// names, clipped to it so a resonant peak cannot run over the controls.
inline void drawFxWell(juce::Graphics& g, const FxWell& well, const FxSlot& slot, double sampleRate, double bpm,
                       float alpha, const FxLive& live = {})
{
    if (well.kind == FxWellKind::generic)
    {
        drawFxDisplay(g, well.area, slot, sampleRate, bpm, alpha);
        return;
    }
    if (well.area.getWidth() < 24 || well.area.getHeight() < 18) return;
    drawDisplayWell(g, well.area);
    const auto colour = fxTypeColour(static_cast<int>(fxTypeOf(slot.type)));
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(well.area));
    const auto plot = well.plot.toFloat();
    switch (well.kind)
    {
        case FxWellKind::distortionTone:  drawFxDistortionTone(g, plot, slot, sampleRate, colour, alpha); return;
        case FxWellKind::distortionShape: drawFxDistortionShape(g, plot, slot, colour, alpha); return;
        case FxWellKind::filterResponse:  drawFxFilterResponse(g, plot, slot, sampleRate, colour, alpha); return;
        case FxWellKind::delayTape:       drawFxDelayTape(g, plot, slot, sampleRate, bpm, colour, alpha); return;
        case FxWellKind::delayLoop:       drawFxDelayLoop(g, plot, slot, sampleRate, colour, alpha); return;
        case FxWellKind::reverbTail:      drawFxReverbTail(g, plot, slot, sampleRate, colour, alpha); return;
        case FxWellKind::reverbDecay:     drawFxReverbDecay(g, plot, slot, sampleRate, colour, alpha); return;
        case FxWellKind::eqResponse:      drawFxEqResponse(g, plot, slot, sampleRate, colour, fxEqHighColour, alpha); return;
        case FxWellKind::compressorCurve: drawFxCompressorCurve(g, plot, slot, colour, alpha, live); return;
        case FxWellKind::compressorMeter: drawFxCompressorMeter(g, well.area.toFloat(), alpha, live); return;
        case FxWellKind::chorusTaps:      drawFxChorusTaps(g, plot, slot, bpm, colour, alpha, live); return;
        case FxWellKind::phaserComb:      drawFxPhaserComb(g, plot, slot, sampleRate, colour, alpha, live); return;
        case FxWellKind::flangerComb:     drawFxFlangerComb(g, plot, slot, sampleRate, colour, alpha, live); return;
        case FxWellKind::generic:         break;
    }
}

// --- The well, and whichever of the above belongs in it -----------------------

inline void drawFxDisplay(juce::Graphics& g, juce::Rectangle<int> area, const FxSlot& slot,
                          double sampleRate, double bpm, float alpha)
{
    if (area.getWidth() < 24 || area.getHeight() < 18) return;
    const auto type = fxTypeOf(slot.type);
    drawDisplayWell(g, area);
    if (type == FxType::off) return;

    const auto colour = fxTypeColour(static_cast<int>(type));
    const auto box = area.toFloat().reduced(5.0f, 5.0f);
    // Clipped, because a resonant peak or a bent transfer curve runs past the
    // box it was drawn for and a curve over the knobs beside it reads as a
    // fault.
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(area));

    switch (type)
    {
        case FxType::reverb:     drawFxReverb(g, box, slot, colour, alpha); return;
        case FxType::delay:      drawFxDelay(g, box, slot, bpm, colour, alpha); return;
        case FxType::chorus:     drawFxChorus(g, box, slot, colour, alpha); return;
        case FxType::distortion: drawFxDistortion(g, box, slot, colour, alpha); return;
        case FxType::equaliser:  drawFxEqualiser(g, box, slot, sampleRate, colour, alpha); return;
        case FxType::filter:     drawFxFilter(g, box, slot, colour, alpha); return;
        case FxType::compressor: drawFxCompressor(g, box, slot, colour, alpha); return;
        case FxType::phaser:     drawFxPhaser(g, box, slot, sampleRate, colour, alpha); return;
        case FxType::flanger:    drawFxFlangerComb(g, box, slot, sampleRate, colour, alpha, {}); return;
        case FxType::off:        break;
    }
}
}
