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
// Each one is drawn into a well of its type's strip, beside the knobs it
// pictures (ForgeFxStrips.h says where), and carries only the marks a reading
// needs: a scale, a node, the side a repeat is on. The knobs say the numbers.
namespace rhino::forge::ui
{
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

// Where each picture draws inside its well: the box its axes span. Shared with
// the gestures in ForgeFxGestures.h, so a press lands on the value drawn under
// it.
// A fill under a curve, strongest at the curve's own highest point and fading
// to the box's foot, so a curve that sits low in its box is filled as boldly
// as one at the top.
inline void fillUnderCurve(juce::Graphics& g, const juce::Path& fill, const juce::Path& curve,
                           juce::Rectangle<float> box, juce::Colour colour, float alpha, float strength)
{
    const auto top = juce::jlimit(box.getY(), box.getBottom() - 1.0f, curve.getBounds().getY());
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * strength), box.getX(), top,
                                           colour.withAlpha(alpha * 0.04f), box.getX(), box.getBottom(), false));
    g.fillPath(fill);
}

// The size of the words inside a picture: a scale's figures, a slope's name,
// the side a repeat is on.
inline constexpr float fxPictureTextSize = 8.5f;
inline constexpr float fxPictureTextHeight = 11.0f;

inline juce::Rectangle<float> fxPictureBox(FxWellKind kind, juce::Rectangle<float> plot)
{
    switch (kind)
    {
        case FxWellKind::distortionTone:  return plot.reduced(4.0f, 4.0f);
        case FxWellKind::distortionShape: return plot.reduced(5.0f, 4.0f);
        case FxWellKind::filterResponse:  return plot.reduced(4.0f, 3.0f);
        case FxWellKind::delayTape:       return plot.reduced(6.0f, 3.0f);
        case FxWellKind::delayLoop:       return plot.reduced(4.0f, 4.0f);
        case FxWellKind::reverbTail:      return plot.reduced(5.0f, 3.0f).withTrimmedBottom(fxPictureTextHeight);
        case FxWellKind::reverbDecay:     return plot.reduced(4.0f, 4.0f);
        case FxWellKind::eqResponse:      return plot.reduced(4.0f, 4.0f);
        case FxWellKind::compressorCurve: return plot.reduced(5.0f, 4.0f);
        case FxWellKind::chorusTaps:      return plot.reduced(4.0f, 4.0f).withTrimmedLeft(17.0f);
        case FxWellKind::phaserComb:      return plot.reduced(4.0f, 3.0f);
        case FxWellKind::flangerComb:     return plot.reduced(4.0f, 3.0f);
        case FxWellKind::compressorMeter:
        case FxWellKind::generic:         break;
    }
    return plot;
}

// How much time the delay's picture spans: every repeat still above -40 dB and
// a little after, so the axis follows TIME and FEEDBACK. A drag holds the span
// it was pressed on, or the axis would move under the pointer.
inline float fxDelayTapeSpan(const FxSlot& slot, double bpm)
{
    const auto seconds = fxDelaySeconds(slot, bpm);
    const auto feedback = juce::jlimit(0.0f, 0.95f, slot.knobs[2]);
    const auto audible = feedback <= 0.01f ? 1
        : juce::jlimit(1, 24, juce::roundToInt(std::log(0.01f) / std::log(feedback)));
    return juce::jmax(0.05f, seconds * juce::jmax(1.0f, fxOffsetRatio(slot.knobs[1])) * (static_cast<float>(audible) + 0.6f));
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
    const auto box = fxPictureBox(FxWellKind::distortionTone, plot);
    const auto unity = filterDbToY(box, 0.0f);
    if (placement == 0)
    {
        g.setColour(mutedText.withAlpha(alpha * 0.6f));
        const float dashes[] {3.0f, 3.0f};
        g.drawDashedLine({box.getX(), unity, box.getRight(), unity}, dashes, 2, 1.0f);
        g.setFont(panelFont(Face::reading, fxPictureTextSize));
        g.drawText("FILTER OFF", box.withTop(unity + 3.0f).withHeight(fxPictureTextHeight),
                   juce::Justification::centredTop, false);
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
    fillUnderCurve(g, fill, curve, box, colour, alpha, 0.3f);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.8f));

    const auto nodeGain = distortionFilterMagnitude(cutoff, cutoff, q, highPass, sampleRate);
    const auto node = juce::Point<float>(filterHzToX(box, cutoff),
                                         juce::jlimit(box.getY() + 3.0f, box.getBottom() - 3.0f,
                                                      filterDbToY(box, 20.0f * std::log10(nodeGain))));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(8.4f, 8.4f).withCentre(node), 1.6f);

    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    g.drawText(juce::String(highPass ? "HP" : "LP") + (placement == 1 ? "  PRE" : "  POST"),
               box.withHeight(fxPictureTextHeight), juce::Justification::topLeft, false);
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
    const auto box = fxPictureBox(FxWellKind::distortionShape, plot);
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
        g.setFont(panelFont(Face::reading, fxPictureTextSize));
        g.drawText("HOLD " + juce::String(factor, 1), box.withHeight(fxPictureTextHeight),
                   juce::Justification::topRight, false);
    }
}

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
    const auto box = fxPictureBox(FxWellKind::filterResponse, plot);
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
    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    g.drawText(slopes[model], box.withHeight(fxPictureTextHeight), juce::Justification::topRight, false);
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
    const auto box = fxPictureBox(FxWellKind::delayTape, plot);
    const auto middle = box.getCentreY();
    const auto seconds = fxDelaySeconds(slot, bpm);
    const auto ratio = fxOffsetRatio(slot.knobs[1]);
    const auto feedback = juce::jlimit(0.0f, 0.95f, slot.knobs[2]);
    const auto tone = fxDelayLoopGain(slot, 4000.0f, sampleRate);
    const auto span = fxDelayTapeSpan(slot, bpm);
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
    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    g.setColour(colour.withAlpha(alpha));
    g.drawText("L", box.withHeight(fxPictureTextHeight), juce::Justification::topLeft, false);
    g.setColour(right.withAlpha(alpha));
    g.drawText("R", box.withTop(box.getBottom() - fxPictureTextHeight), juce::Justification::bottomLeft, false);
}

// What every repeat passes through on its way round: fxDelayLoopGain, from
// 0 to -24 dB, with a node at the corner on the curve.
inline void drawFxDelayLoop(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                            double sampleRate, juce::Colour colour, float alpha)
{
    const auto box = fxPictureBox(FxWellKind::delayLoop, plot);
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
    fillUnderCurve(g, fill, curve, box, colour, alpha, 0.34f);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.8f));
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
    const auto box = fxPictureBox(FxWellKind::reverbTail, plot);
    const auto tX = [box] (float t)
    {
        return box.getX() + std::sqrt(juce::jlimit(0.0f, 1.0f, t / fxReverbAxisSeconds)) * box.getWidth();
    };
    const auto dbY = [box] (float db) { return box.getY() + juce::jlimit(0.0f, 1.0f, -db / 60.0f) * box.getHeight(); };
    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    for (const auto t : {0.1f, 0.5f, 1.0f, 2.0f, 4.0f})
    {
        g.setColour(line.withAlpha(alpha * 0.6f));
        g.fillRect(tX(t), box.getY(), 1.0f, box.getHeight());
        g.setColour(mutedText.interpolatedWith(text, 0.35f).withAlpha(alpha));
        g.drawText(t < 1.0f ? juce::String(t, 1).substring(1) : juce::String(juce::roundToInt(t)) + "s",
                   juce::Rectangle<float>(26.0f, fxPictureTextHeight)
                       .withCentre({tX(t), box.getBottom() + fxPictureTextHeight * 0.5f + 1.0f}),
                   juce::Justification::centred, false);
    }
    const auto preDelay = fxScaled(slot.knobs[1], 0.0f, 0.2f);
    const auto decay = fxReverbDecay(slot);
    const auto trip = juce::jmax(1.0e-4f, fxReverbMeanTrip(slot));
    const auto rt60 = fxReverbRt60(slot, 0.0f, sampleRate);
    g.setColour(colour.withAlpha(alpha * 0.14f));
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
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.45f), box.getX(), box.getY(),
                                           colour.withAlpha(alpha * 0.04f), box.getX(), box.getBottom(), false));
    g.fillPath(fill);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(tail, juce::PathStrokeType(1.9f));
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
    const auto box = fxPictureBox(FxWellKind::reverbDecay, plot);
    const auto rtY = [box] (float t)
    {
        return box.getBottom() - std::sqrt(juce::jlimit(0.0f, 1.0f, t / fxReverbAxisSeconds)) * box.getHeight();
    };
    const auto low = fxHertz(slot.knobs[4], 20.0f, 1200.0f);
    const auto high = fxHertz(slot.knobs[5], 1200.0f, 20000.0f);
    const auto lowX = filterHzToX(box, low), highX = filterHzToX(box, high);
    g.setColour(colour.withAlpha(alpha * 0.07f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(lowX, box.getY(), highX, box.getBottom()));
    g.setColour(line.withAlpha(alpha * 0.5f));
    for (const auto seconds : {1.0f, 2.0f, 4.0f}) g.fillRect(box.getX(), rtY(seconds), box.getWidth(), 1.0f);
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
    fillUnderCurve(g, fill, curve, box, colour, alpha, 0.45f);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(1.9f));
    // What the cuts take away is the well's own dark, not a grey over it.
    g.setColour(displayFloor.withAlpha(alpha * 0.62f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(box.getX(), box.getY(), lowX, box.getBottom()));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(highX, box.getY(), box.getRight(), box.getBottom()));
    g.setColour(colour.withAlpha(alpha * 0.9f));
    for (const auto x : {lowX, highX})
    {
        g.fillRect(x - 0.75f, box.getY(), 1.5f, box.getHeight());
        juce::Path tab;
        tab.addTriangle(x - 4.0f, box.getY(), x + 4.0f, box.getY(), x, box.getY() + 5.0f);
        g.fillPath(tab);
    }
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
    const auto box = fxPictureBox(FxWellKind::eqResponse, plot);
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
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(at));
        g.setColour(ink.withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(at), 1.6f);
        g.setFont(panelFont(Face::emphasis, fxPictureTextSize));
        g.drawText(letter, juce::Rectangle<float>(12.0f, 12.0f).withCentre(at), juce::Justification::centred, false);
    };
    node(fxHertz(slot.knobs[0], 20.0f, 2000.0f), fxScaled(slot.knobs[2], -18.0f, 18.0f), lowChoice != 2, lowColour, "L");
    node(fxHertz(slot.knobs[3], 500.0f, 18000.0f), fxScaled(slot.knobs[5], -18.0f, 18.0f), highChoice != 2, highColour, "H");
}

// --- COMP ------------------------------------------------------------------------------------
//
// The static curve, lifted by the makeup actually applied and held at 0 dB
// where the slot clips, with the knee's width shaded. A dot rides it at the
// level the detector has, once the engine publishes one (drawFxWellLive).

// Where the curve passes for an input level, in its picture's box.
inline juce::Point<float> fxCompressorCurveAt(juce::Rectangle<float> box, const FxSlot& slot, float inputDb)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::compressor)];
    const auto threshold = fxCompressorThreshold(slot.knobs[0]);
    const auto ratio = fxCompressorRatio(slot.knobs[1]);
    const auto makeup = fxModeOf(info.modeB, slot.modeB) == 1 ? fxCompressorAutoMakeup(threshold, ratio)
                                                               : fxCompressorMakeup(slot.knobs[4]);
    const auto out = juce::jmin(0.0f, fxCompressorOutputDb(inputDb, threshold, ratio, fxCompressorKnee(slot.knobs[5])) + makeup);
    return {box.getX() + (juce::jlimit(-60.0f, 0.0f, inputDb) + 60.0f) / 60.0f * box.getWidth(),
            box.getBottom() - juce::jlimit(0.0f, 1.0f, (out + 60.0f) / 66.0f) * box.getHeight()};
}

inline void drawFxCompressorCurve(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot,
                                  juce::Colour colour, float alpha)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::compressor)];
    const auto threshold = fxCompressorThreshold(slot.knobs[0]);
    const auto ratio = fxCompressorRatio(slot.knobs[1]);
    const auto knee = fxCompressorKnee(slot.knobs[5]);
    const auto automatic = fxModeOf(info.modeB, slot.modeB) == 1;
    const auto makeup = automatic ? fxCompressorAutoMakeup(threshold, ratio) : fxCompressorMakeup(slot.knobs[4]);
    const auto box = fxPictureBox(FxWellKind::compressorCurve, plot);
    const auto xOf = [box] (float db) { return box.getX() + (juce::jlimit(-60.0f, 0.0f, db) + 60.0f) / 60.0f * box.getWidth(); };
    const auto yOf = [box] (float db) { return box.getBottom() - juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f) * box.getHeight(); };
    const auto outAt = [&] (float in) { return juce::jmin(0.0f, fxCompressorOutputDb(in, threshold, ratio, knee) + makeup); };

    g.setColour(line.withAlpha(alpha * 0.45f));
    for (const auto db : {-48.0f, -36.0f, -24.0f, -12.0f})
    {
        g.fillRect(xOf(db), box.getY(), 1.0f, box.getHeight());
        g.fillRect(box.getX(), yOf(db), box.getWidth(), 1.0f);
    }
    g.setColour(line.withAlpha(alpha * 0.95f));
    g.drawLine(xOf(-60.0f), yOf(-60.0f), xOf(0.0f), yOf(0.0f), 1.0f);
    g.setColour(markRed.withAlpha(alpha * 0.55f));
    const float dashes[] {2.0f, 3.0f};
    g.drawDashedLine({box.getX(), yOf(0.0f), box.getRight(), yOf(0.0f)}, dashes, 2, 1.0f);
    if (knee > 0.1f)
    {
        g.setColour(colour.withAlpha(alpha * 0.06f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(xOf(threshold - knee * 0.5f), box.getY(),
                                                              xOf(threshold + knee * 0.5f), box.getBottom()));
    }
    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const auto in = -60.0f + static_cast<float>(i) * 0.5f;
        if (i == 0) curve.startNewSubPath(xOf(in), yOf(outAt(in))); else curve.lineTo(xOf(in), yOf(outAt(in)));
    }
    // The gain the compressor changes, between its curve and unity: the
    // wedge above the threshold is what it takes off.
    auto change = curve;
    for (int i = 120; i >= 0; --i)
    {
        const auto in = -60.0f + static_cast<float>(i) * 0.5f;
        change.lineTo(xOf(in), yOf(in));
    }
    change.closeSubPath();
    auto under = curve;
    under.lineTo(xOf(0.0f), box.getBottom());
    under.lineTo(xOf(-60.0f), box.getBottom());
    under.closeSubPath();
    fillUnderCurve(g, under, curve, box, colour, alpha, 0.3f);
    g.setColour(colour.withAlpha(alpha * 0.38f));
    g.fillPath(change);
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(curve, juce::PathStrokeType(2.0f));
    const auto knot = juce::Point<float>(xOf(threshold), yOf(outAt(threshold)));
    g.setColour(displayFloor);
    g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(knot));
    g.setColour(colour.withAlpha(alpha));
    g.drawEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(knot), 1.6f);
    const auto end = juce::Rectangle<float>(7.0f, 7.0f).withCentre({xOf(0.0f) - 3.5f, yOf(outAt(0.0f))});
    g.setColour(displayFloor);
    g.fillRect(end);
    g.setColour(colour.withAlpha(alpha));
    g.drawRect(end, 1.4f);
}

// The gain being taken off, on a reverse-reading needle that rests at the right
// and dips left. The face is drawn here; the needle, which only the engine
// knows, is drawn over it every frame (drawFxWellLive) and rests until a slot
// has published.
struct FxMeterDial
{
    juce::Point<float> pivot;
    float radius = 0.0f;

    static float angleFor(float db)
    {
        return juce::degreesToRadians(52.0f - juce::jlimit(0.0f, 20.0f, -db) / 20.0f * 104.0f);
    }
    juce::Point<float> at(float r, float angle) const
    {
        return pivot + juce::Point<float>(std::sin(angle) * r, -std::cos(angle) * r);
    }
};

inline FxMeterDial fxMeterDial(juce::Rectangle<float> area)
{
    const auto face = area.reduced(1.0f);
    return {{face.getCentreX(), face.getBottom() - 8.0f},
            juce::jmin(face.getWidth() * 0.5f - 8.0f, face.getHeight() - 20.0f)};
}

// The needle, the reduction it points at as a figure, and a lamp that lights
// as the compressor works: what moves with the sound, drawn over the face.
inline void drawFxCompressorNeedle(juce::Graphics& g, juce::Rectangle<float> area, float alpha, float reductionDb,
                                   bool live)
{
    const auto face = area.reduced(1.0f);
    const auto dial = fxMeterDial(area);
    const auto tip = dial.at(dial.radius + 2.0f, FxMeterDial::angleFor(reductionDb));
    g.setColour(meterInk.withAlpha(alpha * 0.22f));
    g.drawLine({dial.pivot.translated(1.0f, 1.0f), tip.translated(1.0f, 1.0f)}, 1.6f);
    g.setColour(meterInk.withAlpha(alpha));
    g.drawLine({dial.pivot, tip}, 1.6f);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(dial.pivot));

    const auto working = live ? juce::jlimit(0.0f, 1.0f, -reductionDb / 12.0f) : 0.0f;
    const auto lamp = juce::Rectangle<float>(5.0f, 5.0f).withCentre({face.getX() + 8.0f, face.getY() + 8.0f});
    if (working > 0.02f)
    {
        g.setColour(meterRed.withAlpha(alpha * 0.35f * working));
        g.fillEllipse(lamp.expanded(3.5f));
    }
    g.setColour(meterInk.withAlpha(alpha * 0.35f).interpolatedWith(meterRed.withAlpha(alpha), working));
    g.fillEllipse(lamp);
    if (live)
    {
        g.setColour(meterInk.withAlpha(alpha));
        g.setFont(panelFont(Face::reading, fxPictureTextSize));
        g.drawText(juce::String(juce::jmin(0.0f, reductionDb), 1),
                   juce::Rectangle<float>(face.getRight() - 34.0f, face.getBottom() - fxPictureTextHeight - 2.0f,
                                          30.0f, fxPictureTextHeight),
                   juce::Justification::centredRight, false);
    }
}

inline void drawFxCompressorMeter(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float alpha)
{
    const auto face = area.reduced(1.0f);
    // Lit from behind, warm where the lamp is, like the meter on a channel
    // strip: the type's colour glows through the card from under the scale.
    g.setColour(meterFace.withAlpha(alpha));
    g.fillRoundedRectangle(face, 3.0f);
    g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha * 0.34f), face.getCentreX(), face.getBottom() - 6.0f,
                                           colour.withAlpha(0.0f), face.getCentreX(), face.getY() - face.getHeight() * 0.2f,
                                           true));
    g.fillRoundedRectangle(face, 3.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(alpha * 0.22f), face.getX(), face.getY(),
                                           juce::Colours::white.withAlpha(0.0f), face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle(face.reduced(1.0f), 2.5f);
    g.setColour(meterInk.withAlpha(alpha * 0.85f));
    g.drawRoundedRectangle(face, 3.0f, 1.4f);
    const auto dial = fxMeterDial(area);
    const auto pivot = dial.pivot;
    const auto radius = dial.radius;
    const auto angleFor = [] (float db) { return FxMeterDial::angleFor(db); };
    const auto polar = [&dial] (float r, float angle) { return dial.at(r, angle); };
    juce::Path scale;
    scale.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f, angleFor(-20.0f), angleFor(0.0f), true);
    g.setColour(meterInk.withAlpha(alpha));
    g.strokePath(scale, juce::PathStrokeType(1.4f));
    juce::Path hot;
    hot.addCentredArc(pivot.x, pivot.y, radius - 2.5f, radius - 2.5f, 0.0f, angleFor(-20.0f), angleFor(-10.0f), true);
    g.setColour(meterRed.withAlpha(alpha));
    g.strokePath(hot, juce::PathStrokeType(3.0f));
    g.setFont(panelFont(Face::header, fxPictureTextSize));
    for (const auto db : {0.0f, -1.0f, -2.0f, -3.0f, -5.0f, -7.0f, -10.0f, -20.0f})
    {
        const auto angle = angleFor(db);
        const auto major = db == 0.0f || db == -3.0f || db == -10.0f || db == -20.0f;
        const auto ink = (db <= -10.0f ? meterRed : meterInk).withAlpha(alpha);
        g.setColour(ink);
        g.drawLine({polar(radius + 1.0f, angle), polar(radius - (major ? 7.0f : 5.0f), angle)}, major ? 1.6f : 1.2f);
        if (major)
            g.drawText(juce::String(juce::roundToInt(-db)),
                       juce::Rectangle<float>(18.0f, fxPictureTextHeight).withCentre(polar(radius + 7.0f, angle)),
                       juce::Justification::centred, false);
    }
    g.setColour(meterInk.withAlpha(alpha));
    g.drawText("GR", juce::Rectangle<float>(face.getX() + 4.0f, face.getBottom() - fxPictureTextHeight - 2.0f,
                                            22.0f, fxPictureTextHeight),
               juce::Justification::centredLeft, false);
}

// --- CHORUS ----------------------------------------------------------------------------------
//
// Both channels' taps across three seconds of the sweep, on a fixed 0-42 ms
// scale: the left solid and the right, which reads the same taps the other way
// round, dashed. RATE is how dense the waves are, the delays how high they
// sit, DEPTH how far they swing. A playhead marks where the LFO is.
inline void drawFxChorusTaps(juce::Graphics& g, juce::Rectangle<float> plot, const FxSlot& slot, double bpm,
                             juce::Colour colour, float alpha)
{
    const auto right = juce::Colour(0xff9fe7ff);
    const auto box = fxPictureBox(FxWellKind::chorusTaps, plot);
    const auto msY = [box] (float ms) { return box.getBottom() - juce::jlimit(0.0f, 1.0f, ms / 42.0f) * box.getHeight(); };
    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    for (const auto ms : {10.0f, 20.0f, 30.0f, 40.0f})
    {
        g.setColour(line.withAlpha(alpha * 0.55f));
        g.fillRect(box.getX(), msY(ms), box.getWidth(), 1.0f);
        g.setColour(mutedText.interpolatedWith(text, 0.35f).withAlpha(alpha));
        g.drawText(juce::String(juce::roundToInt(ms)),
                   juce::Rectangle<float>(box.getX() - 18.0f, msY(ms) - fxPictureTextHeight * 0.5f, 15.0f, fxPictureTextHeight),
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
    // The legend at the top right, clear of the scale and of the delay nodes.
    const auto legend = box.withHeight(fxPictureTextHeight).withTrimmedRight(14.0f);
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
    const auto box = fxPictureBox(FxWellKind::phaserComb, plot);
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
    const auto box = fxPictureBox(FxWellKind::flangerComb, plot);
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
    g.setFont(panelFont(Face::reading, fxPictureTextSize));
    g.setColour(colour.withAlpha(alpha));
    g.drawText(juce::String(now, 2) + " ms", box.withHeight(fxPictureTextHeight), juce::Justification::topRight, false);
}

// One of a strip's wells: the well itself, and whichever picture its kind
// names, clipped to it so a resonant peak cannot run over the controls. What
// moves with the sound rather than with the settings is left to
// drawFxWellLive, so the editor can keep this as an image between frames.
inline void drawFxWellPicture(juce::Graphics& g, const FxWell& well, const FxSlot& slot, double sampleRate,
                              double bpm, float alpha, const FxLive& live)
{
    if (well.area.getWidth() < 24 || well.area.getHeight() < 18) return;
    drawDisplayWell(g, well.area);
    const auto colour = fxTypeColour(static_cast<int>(fxTypeOf(slot.type)));
    // A type without a strip of its own yet: its well, with its mark in it.
    if (well.kind == FxWellKind::generic)
    {
        drawFxMark(g, well.area.toFloat().withSizeKeepingCentre(36.0f, 24.0f),
                   static_cast<int>(fxTypeOf(slot.type)), colour, alpha * 0.6f);
        return;
    }
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
        case FxWellKind::compressorCurve: drawFxCompressorCurve(g, plot, slot, colour, alpha); return;
        case FxWellKind::compressorMeter: drawFxCompressorMeter(g, well.area.toFloat(), colour, alpha); return;
        case FxWellKind::chorusTaps:      drawFxChorusTaps(g, plot, slot, bpm, colour, alpha); return;
        case FxWellKind::phaserComb:      drawFxPhaserComb(g, plot, slot, sampleRate, colour, alpha, live); return;
        case FxWellKind::flangerComb:     drawFxFlangerComb(g, plot, slot, sampleRate, colour, alpha, live); return;
        case FxWellKind::generic:         break;
    }
}

// What of a picture moves with the sound: the compressor's needle and the dot
// on its curve, and the chorus's playhead. Drawn over the picture every frame.
inline void drawFxWellLive(juce::Graphics& g, const FxWell& well, const FxSlot& slot, double bpm, float alpha,
                           const FxLive& live)
{
    if (well.area.getWidth() < 24 || well.area.getHeight() < 18) return;
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(displayClip(well.area));
    const auto box = fxPictureBox(well.kind, well.plot.toFloat());
    switch (well.kind)
    {
        case FxWellKind::compressorMeter:
            drawFxCompressorNeedle(g, well.area.toFloat(), alpha, live.valid ? live.reductionDb : 0.0f, live.valid);
            return;
        case FxWellKind::compressorCurve:
            if (live.valid && live.inputPeak > 1.0e-3f)
            {
                g.setColour(text.withAlpha(alpha * 0.9f));
                g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f)
                                  .withCentre(fxCompressorCurveAt(box, slot, 20.0f * std::log10(live.inputPeak))));
            }
            return;
        case FxWellKind::chorusTaps:
        {
            const auto cycles = juce::jmin(24.0f, fxChorusRate(slot, bpm) * 3.0f);
            if (!live.valid || cycles <= 0.0f) return;
            const auto x = box.getX() + juce::jlimit(0.0f, 1.0f, live.phase / juce::jmax(1.0f, cycles)) * box.getWidth();
            g.setColour(text.withAlpha(alpha * 0.35f));
            g.fillRect(x, box.getY(), 1.0f, box.getHeight());
            return;
        }
        default: return;
    }
}

// A well whole: its picture and what moves over it.
inline void drawFxWell(juce::Graphics& g, const FxWell& well, const FxSlot& slot, double sampleRate, double bpm,
                       float alpha, const FxLive& live = {})
{
    drawFxWellPicture(g, well, slot, sampleRate, bpm, alpha, live);
    drawFxWellLive(g, well, slot, bpm, alpha, live);
}

// The live reading a well's picture itself is drawn with. The phaser's and
// the flanger's combs move whole with their LFO, so their picture is drawn
// per phase, to a step finer than a frame shows; every other picture is drawn
// from its settings alone and has its live marks drawn over it.
inline FxLive fxWellPictureLive(FxWellKind kind, const FxLive& live)
{
    FxLive kept;
    if ((kind == FxWellKind::phaserComb || kind == FxWellKind::flangerComb) && live.valid)
    {
        kept.valid = true;
        kept.phase = std::round(live.phase * 512.0f) / 512.0f;
    }
    return kept;
}

// Everything a well's picture is drawn from, so a copy of it can be kept until
// one of them moves. Positions are the well's own, so scrolling the rack moves
// the copy rather than throwing it away.
struct FxPictureKey
{
    FxWellKind kind = FxWellKind::generic;
    juce::Rectangle<int> area, plot;
    float type = 0.0f, modeA = 0.0f, modeB = 0.0f;
    std::array<float, fxKnobCount> knobs {};
    double sampleRate = 0.0, bpm = 0.0;
    float alpha = 0.0f, scale = 0.0f;
    bool live = false;
    float phase = 0.0f;

    bool operator==(const FxPictureKey& other) const
    {
        return kind == other.kind && area == other.area && plot == other.plot && type == other.type
            && modeA == other.modeA && modeB == other.modeB && knobs == other.knobs
            && sampleRate == other.sampleRate && bpm == other.bpm && alpha == other.alpha
            && scale == other.scale && live == other.live && phase == other.phase;
    }
};
}
