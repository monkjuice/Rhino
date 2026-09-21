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
        case FxType::off:        break;
    }
}
}
