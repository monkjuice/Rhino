#pragma once

// Taking hold of a strip's pictures. A picture with a node can be dragged by
// it: a press anywhere on the picture picks up whatever is nearest there, and
// dragging sets the knobs that thing stands for, through the same axes the
// picture is drawn on, so the node arrives under the pointer.
//
// A gesture reads its axes from the slot as it was pressed. Some pictures
// rescale with what is dragged -- the delay's time axis follows TIME -- and an
// axis that moved under the pointer would chase it. The picture redraws to its
// new scale once the drag lets go.
//
// Pure functions of a well, a slot and a point, so the tests can drive every
// gesture without a window; the editor only finds the well and sends the
// values to the host.
#include "ForgeFxDisplay.h"

namespace rhino::forge::ui
{
// The inverse of fxHertz and of fxScaled: the knob that gives a value.
inline float fxKnobForHertz(float hz, float low, float high)
{
    return juce::jlimit(0.0f, 1.0f, std::log(juce::jmax(1.0e-3f, hz) / low) / std::log(high / low));
}

inline float fxKnobForScaled(float value, float low, float high, float skew = 1.0f)
{
    const auto unit = juce::jlimit(0.0f, 1.0f, (value - low) / (high - low));
    return skew == 1.0f ? unit : std::pow(unit, 1.0f / skew);
}

// What a press picked up: which part of the picture, the slot as it was, and
// where the press was.
struct FxWellGrab
{
    FxWellKind kind = FxWellKind::generic;
    int part = 0;
    FxSlot pressed;
    juce::Point<float> start;
};

// The knobs a drag sets, each as a knob index and its new 0..1 value.
struct FxWellEdit
{
    std::array<int, 2> knobs {-1, -1};
    std::array<float, 2> values {};
    int count = 0;

    void set(int knob, float value)
    {
        if (count >= 2) return;
        knobs[static_cast<size_t>(count)] = knob;
        values[static_cast<size_t>(count)] = juce::jlimit(0.0f, 1.0f, value);
        ++count;
    }
};

// The parts of the pictures that hold more than one thing. A part is chosen
// at the press and kept for the whole drag.
namespace fxPart
{
inline constexpr int time = 0, offset = 1;          // the delay's repeats
inline constexpr int preDelay = 0, size = 1;        // the reverb's tail
inline constexpr int lowCut = 0, highCut = 1;       // the reverb's tone
inline constexpr int lowBand = 0, highBand = 1;     // the equaliser
inline constexpr int threshold = 0, ratio = 1;      // the compressor's curve
inline constexpr int firstTap = 0, secondTap = 1;   // the chorus
}

// Where the equaliser's two nodes are drawn, low first.
inline std::array<juce::Point<float>, 2> fxEqNodes(juce::Rectangle<float> box, const FxSlot& slot)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::equaliser)];
    const auto dbY = [box] (float db) { return box.getCentreY() - juce::jlimit(-24.0f, 24.0f, db) / 24.0f * box.getHeight() * 0.5f; };
    const auto lowLive = fxModeOf(info.modeA, slot.modeA) != 2;
    const auto highLive = fxModeOf(info.modeB, slot.modeB) != 2;
    return {juce::Point<float>(filterHzToX(box, fxHertz(slot.knobs[0], 20.0f, 2000.0f)),
                               dbY(lowLive ? fxScaled(slot.knobs[2], -18.0f, 18.0f) : 0.0f)),
            juce::Point<float>(filterHzToX(box, fxHertz(slot.knobs[3], 500.0f, 18000.0f)),
                               dbY(highLive ? fxScaled(slot.knobs[5], -18.0f, 18.0f) : 0.0f))};
}

// Whether a press at `at` takes hold of this well's picture, and of which part
// of it. The gain-reduction meter and the flanger's comb only show what is
// happening and take nothing; the distortion's tone takes nothing while its
// filter is switched out.
inline bool fxWellGrabAt(const FxWell& well, const FxSlot& slot, juce::Point<float> at, FxWellGrab& grab)
{
    // The picture itself, not the field seated above it, which is a control
    // of its own and takes its own presses.
    if (!well.plot.toFloat().contains(at)) return false;
    const auto box = fxPictureBox(well.kind, well.plot.toFloat());
    grab = {well.kind, 0, slot, at};
    switch (well.kind)
    {
        case FxWellKind::distortionTone:  return fxDistortionFilterPlacement(slot) != 0;
        case FxWellKind::distortionShape:
        case FxWellKind::filterResponse:
        case FxWellKind::delayLoop:
        case FxWellKind::phaserComb:      return true;
        case FxWellKind::delayTape:
            grab.part = at.y < box.getCentreY() ? fxPart::time : fxPart::offset;
            return true;
        case FxWellKind::reverbTail:
            grab.part = at.y < box.getCentreY() ? fxPart::preDelay : fxPart::size;
            return true;
        case FxWellKind::reverbDecay:
        {
            const auto low = filterHzToX(box, fxHertz(slot.knobs[4], 20.0f, 1200.0f));
            const auto high = filterHzToX(box, fxHertz(slot.knobs[5], 1200.0f, 20000.0f));
            grab.part = std::abs(at.x - low) <= std::abs(at.x - high) ? fxPart::lowCut : fxPart::highCut;
            return true;
        }
        case FxWellKind::eqResponse:
        {
            const auto nodes = fxEqNodes(box, slot);
            grab.part = at.getDistanceFrom(nodes[0]) <= at.getDistanceFrom(nodes[1]) ? fxPart::lowBand : fxPart::highBand;
            return true;
        }
        case FxWellKind::compressorCurve:
            grab.part = at.x > box.getRight() - 12.0f ? fxPart::ratio : fxPart::threshold;
            return true;
        case FxWellKind::chorusTaps:
        {
            // The taps are taken at the right, where their nodes stand; the
            // left half is the sweep's history and holds nothing.
            if (at.x < box.getCentreX()) return false;
            const auto msY = [box] (float ms) { return box.getBottom() - juce::jlimit(0.0f, 1.0f, ms / 42.0f) * box.getHeight(); };
            const auto first = msY(fxScaled(slot.knobs[1], 0.5f, 30.0f));
            const auto second = msY(fxScaled(slot.knobs[2], 0.5f, 30.0f));
            grab.part = std::abs(at.y - first) <= std::abs(at.y - second) ? fxPart::firstTap : fxPart::secondTap;
            return true;
        }
        case FxWellKind::compressorMeter:
        case FxWellKind::flangerComb:
        case FxWellKind::generic:         break;
    }
    return false;
}

// What dragging the part a press took hold of to `at` sets.
inline FxWellEdit fxWellDragTo(const FxWell& well, const FxWellGrab& grab, juce::Point<float> at,
                               double sampleRate, double bpm)
{
    FxWellEdit edit;
    const auto& p = grab.pressed;
    const auto box = fxPictureBox(grab.kind, well.plot.toFloat());
    const auto across = juce::jlimit(0.0f, 1.0f, (at.x - box.getX()) / juce::jmax(1.0f, box.getWidth()));
    const auto down = juce::jlimit(0.0f, 1.0f, (at.y - box.getY()) / juce::jmax(1.0f, box.getHeight()));
    const auto hz = filterXToHz(box, juce::jlimit(box.getX(), box.getRight(), at.x));
    switch (grab.kind)
    {
        // FREQ across; Q up the node's height, which is the gain the filter
        // has at its own corner: Q itself, in decibels.
        case FxWellKind::distortionTone:
        {
            edit.set(1, fxKnobForHertz(hz, 40.0f, 16000.0f));
            const auto db = filterTopDb - down * (filterTopDb - filterBottomDb);
            edit.set(2, fxKnobForScaled(std::pow(10.0f, db / 20.0f), 0.4f, 8.0f));
            break;
        }
        // DRIVE, by how far the pointer has moved up since the press.
        case FxWellKind::distortionShape:
            edit.set(0, p.knobs[0] - (at.y - grab.start.y) / 120.0f);
            break;
        // CUTOFF across; RES as the peak it raises at the corner, where a
        // two-pole low pass gains one over its damping. The one-pole models
        // have no resonance, so their node only moves across.
        case FxWellKind::filterResponse:
        {
            edit.set(0, fxKnobForHertz(hz, 30.0f, 18000.0f));
            const auto model = fxModeOf(fxTypes()[static_cast<size_t>(FxType::filter)].modeA, p.modeA);
            if (model != 1 && model != 5)
            {
                const auto gain = juce::jmax(0.5f, std::pow(10.0f, (18.0f - down * 54.0f) / 20.0f));
                edit.set(1, juce::jlimit(0.0f, 0.98f, (2.0f - 1.0f / gain) / 1.96f));
            }
            break;
        }
        // TIME is where the left side's first repeat lands, OFFSET where the
        // right side's does, on the axis the press saw. A synced delay takes
        // the division nearest the time.
        case FxWellKind::delayTape:
        {
            const auto span = fxDelayTapeSpan(p, bpm);
            const auto seconds = juce::jmax(0.001f, across * span);
            if (grab.part == fxPart::time)
            {
                const auto& info = fxTypes()[static_cast<size_t>(FxType::delay)];
                if (fxModeOf(info.modeB, p.modeB) == 1)
                {
                    const auto beat = static_cast<float>(60.0 / (bpm > 0.0 ? bpm : 120.0));
                    auto best = 0;
                    for (int i = 1; i < fxDivisionCount; ++i)
                        if (std::abs(std::log(beat * fxDivisions()[static_cast<size_t>(i)].beats / seconds))
                            < std::abs(std::log(beat * fxDivisions()[static_cast<size_t>(best)].beats / seconds)))
                            best = i;
                    edit.set(0, static_cast<float>(best) / static_cast<float>(fxDivisionCount - 1));
                }
                else
                    edit.set(0, fxKnobForScaled(seconds, 0.01f, fxMaxDelayTime, 2.0f));
            }
            else
                edit.set(1, fxKnobForScaled(seconds / juce::jmax(0.001f, fxDelaySeconds(p, bpm)), 0.5f, 1.5f));
            break;
        }
        // FREQ across; Q as what the loop keeps at that corner, the height the
        // node is drawn at. Keeping more of the dry signal only ever raises
        // it, so it is found by halving.
        case FxWellKind::delayLoop:
        {
            auto slot = p;
            slot.knobs[3] = fxKnobForHertz(hz, 200.0f, 16000.0f);
            const auto corner = fxHertz(slot.knobs[3], 200.0f, 16000.0f);
            const auto wanted = std::pow(10.0f, -down * 24.0f / 20.0f);
            auto low = 0.0f, high = 1.0f;
            for (int i = 0; i < 24; ++i)
            {
                slot.knobs[4] = (low + high) * 0.5f;
                if (fxDelayLoopGain(slot, corner, sampleRate) < wanted) low = slot.knobs[4]; else high = slot.knobs[4];
            }
            edit.set(3, slot.knobs[3]);
            edit.set(4, (low + high) * 0.5f);
            break;
        }
        // PRE-DLY is where the tail starts; SIZE is where it has fallen sixty
        // decibels, found by halving since a bigger room only rings longer.
        case FxWellKind::reverbTail:
        {
            const auto seconds = across * across * fxReverbAxisSeconds;
            if (grab.part == fxPart::preDelay)
            {
                edit.set(1, fxKnobForScaled(seconds, 0.0f, 0.2f));
                break;
            }
            auto slot = p;
            const auto wanted = seconds - fxScaled(p.knobs[1], 0.0f, 0.2f);
            auto low = 0.0f, high = 1.0f;
            for (int i = 0; i < 24; ++i)
            {
                slot.knobs[0] = (low + high) * 0.5f;
                if (fxReverbRt60(slot, 0.0f, sampleRate) < wanted) low = slot.knobs[0]; else high = slot.knobs[0];
            }
            edit.set(0, (low + high) * 0.5f);
            break;
        }
        case FxWellKind::reverbDecay:
            if (grab.part == fxPart::lowCut) edit.set(4, fxKnobForHertz(hz, 20.0f, 1200.0f));
            else edit.set(5, fxKnobForHertz(hz, 1200.0f, 20000.0f));
            break;
        // A band's frequency across and its gain up, unless it is a pass,
        // which has no gain to give.
        case FxWellKind::eqResponse:
        {
            const auto& info = fxTypes()[static_cast<size_t>(FxType::equaliser)];
            const auto db = juce::jlimit(-18.0f, 18.0f, (0.5f - down) * 48.0f);
            if (grab.part == fxPart::lowBand)
            {
                edit.set(0, fxKnobForHertz(hz, 20.0f, 2000.0f));
                if (fxModeOf(info.modeA, p.modeA) != 2) edit.set(2, fxKnobForScaled(db, -18.0f, 18.0f));
            }
            else
            {
                edit.set(3, fxKnobForHertz(hz, 500.0f, 18000.0f));
                if (fxModeOf(info.modeB, p.modeB) != 2) edit.set(5, fxKnobForScaled(db, -18.0f, 18.0f));
            }
            break;
        }
        // THRESH is where the curve bends; RATIO is how high its far end is,
        // the level a full-scale input comes out at.
        case FxWellKind::compressorCurve:
        {
            if (grab.part == fxPart::threshold)
            {
                edit.set(0, fxKnobForScaled(across * 60.0f - 60.0f, -60.0f, 0.0f));
                break;
            }
            const auto threshold = fxCompressorThreshold(p.knobs[0]);
            const auto ratioOf = fxCompressorRatio(p.knobs[1]);
            const auto automatic = fxModeOf(fxTypes()[static_cast<size_t>(FxType::compressor)].modeB, p.modeB) == 1;
            const auto makeup = automatic ? fxCompressorAutoMakeup(threshold, ratioOf) : fxCompressorMakeup(p.knobs[4]);
            const auto out = (1.0f - down) * 66.0f - 60.0f - makeup;
            const auto ratio = out <= threshold + 0.05f ? 20.0f : -threshold / (out - threshold);
            edit.set(1, fxKnobForScaled(juce::jlimit(1.0f, 20.0f, ratio), 1.0f, 20.0f, 1.7f));
            break;
        }
        case FxWellKind::chorusTaps:
            edit.set(grab.part == fxPart::firstTap ? 1 : 2, fxKnobForScaled((1.0f - down) * 42.0f, 0.5f, 30.0f));
            break;
        case FxWellKind::phaserComb:
            edit.set(2, fxKnobForHertz(hz, 80.0f, 8000.0f));
            break;
        case FxWellKind::compressorMeter:
        case FxWellKind::flangerComb:
        case FxWellKind::generic:         break;
    }
    return edit;
}

// Whether a drag on this picture is a node following the pointer, which a
// press moves at once, or a handle dragged by how far it travels.
inline bool fxWellFollowsPointer(FxWellKind kind) { return kind != FxWellKind::distortionShape; }
}
