#pragma once

#include "ForgeMatrix.h"

// Everything the engine is told, in one struct, plus the arithmetic that reads
// it. A Patch is a snapshot of the parameters as of one block: the processor
// fills it, the voice renders from it, and nothing in here holds state between
// samples.
//
// destinationField is the one place the matrix and the patch meet: it turns a
// destination index into the field it moves, so a new destination is a line
// there and a line in ForgeMatrix.h's list rather than a case in the voice.
namespace rhino::forge
{
// rather than by which of several parallel arrays its index falls in.
struct Sends
{
    std::array<float, busCount> amount {};
};

// A bus: where the sends arrive. It has a level and a place in the image like
// any channel, and it goes to the main output or across to the other bus.
//
// It carries no effects yet. That is the whole reason a bus exists in Serum,
// and the reason it exists here is that the routing has to be in place before
// a rack can be hung on it — so the topology lands first and M11 fills it in.
// Until then a bus is a summing point: audibly a gain, but one several sources
// share and one that can be panned and re-routed as a group.
struct Bus
{
    float enable = 1.0f;
    // False for the main output, true for the other bus.
    float dest = 0.0f;
    float pan = 0.0f;
    float level = 0.75f;
};

struct Patch
{
    Oscillator a, b;
    float subEnable = 1.0f, subLevel = 0.17f;
    // Which of the six shapes the sub is reading, and how far below the note it
    // is reading it. Both open where the sub has always stood -- a sine, one
    // octave down -- so a patch written before either existed is unchanged.
    float subWave = 0.0f, subOctave = 0.0f;
    float noiseEnable = 0.0f, noiseLevel = 0.35f;
    // SUB and NOISE are panned with the same equal-power law the oscillators
    // use, so a source reading a given level is that loud whichever source it
    // is. Their levels above carry the 3 dB that law costs at centre, which is
    // why they are not the 0.12 and 0.25 they were before the mixer.
    float subPan = 0.0f, noisePan = 0.0f;
    float filterEnable = 1.0f, filterType = 0.0f;
    // Each source either passes through the filter or bypasses it straight to
    // the voice sum, exactly as Serum's per-source routing buttons work.
    float routeA = 1.0f, routeB = 1.0f, routeSub = 1.0f, routeNoise = 1.0f;
    float cutoff = 7800.0f, resonance = 0.12f, drive = 0.08f;
    // The filter's own channel in the mixer: where its output sits in the
    // image, how much of it is the filtered signal rather than what went in,
    // and how loud the whole channel is. The defaults leave the filter exactly
    // as it behaved before it had a channel.
    float filterPan = 0.0f, filterMix = 1.0f, filterLevel = 1.0f;
    // What each channel sends to each bus, parallel to wherever it is already
    // going.
    Sends sendA, sendB, sendSub, sendNoise, sendFilter;
    std::array<Bus, busCount> buses {};
    // One effects rack on the main output and one on each bus, in that order.
    // They run on the summed voices rather than inside them, which is what an
    // insert after the synth is — see ForgeFxDsp.h.
    std::array<Rack, rackCount> racks {};
    // The four envelopes. ENV 1 is the voice's amplitude and every voice is
    // rendered through it; ENV 2-4 reach anything at all through the matrix.
    std::array<EnvSetting, envCount> envs {};
    // The six LFOs. They are sources, not routers: where one goes is a matter
    // for the modulation slots.
    std::array<LfoSetting, lfoCount> lfos {};
    float polyphony = 8.0f, mono = 0.0f, legato = 1.0f, glide = 0.08f;
    float output = 0.75f;
    // Performance macros. Sources only: a macro is a hand on a knob, and what
    // it reaches is a matter for the matrix.
    std::array<float, macroCount> macros {};
};

inline bool on(float enable) { return enable >= 0.5f; }

// How long a finished voice takes to fade out. The amp envelope reaching zero
// is not the end of a voice: everything it feeds goes through the filter, and a
// filter holds energy, so at a low cutoff the voice is still sounding
// milliseconds after the envelope that fed it stopped. Long enough to cover
// half a cycle of the lowest cutoff the filter offers, so cutting the ring off
// is never a step.
inline constexpr float voiceTailSeconds = 0.015f;


// The field a destination index names, inside a patch that is about to be
// modulated. Null for "nothing", which is also what an out-of-range index gets.
inline float* destinationField(Patch& patch, int destination)
{
    switch (destination)
    {
        case 1:  return &patch.a.position;
        case 2:  return &patch.a.level;
        case 3:  return &patch.a.pan;
        case 4:  return &patch.a.detune;
        case 5:  return &patch.a.semitone;
        case 6:  return &patch.b.position;
        case 7:  return &patch.b.level;
        case 8:  return &patch.b.pan;
        case 9:  return &patch.b.detune;
        case 10: return &patch.b.semitone;
        case 11: return &patch.subLevel;
        case 12: return &patch.noiseLevel;
        case 13: return &patch.cutoff;
        case 14: return &patch.resonance;
        case 15: return &patch.drive;
        case 16: return &patch.subPan;
        case 17: return &patch.noisePan;
        case 18: return &patch.filterPan;
        case 19: return &patch.filterMix;
        case 20: return &patch.filterLevel;
        default: break;
    }
    // Past the racks are the four warp depths, which are named rather than
    // generated and so are read back the same way.
    const auto warp = destination - warpDestinationBase;
    if (warp >= 0 && warp < warpDestinationCount)
    {
        auto& osc = warp < warpSlots ? patch.a : patch.b;
        return &osc.warpAmount[static_cast<size_t>(warp % warpSlots)];
    }

    // The first four rack slots occupy their historic range. Slots five to
    // eight live after the warp block so adding them cannot move an existing
    // destination stored by index in an older preset.
    auto fx = destination - fxDestinationBase;
    auto slotsPerRack = legacyFxSlotCount;
    auto slotOffset = 0;
    if (fx < 0 || fx >= legacyFxDestinationCount)
    {
        fx = destination - extendedFxDestinationBase;
        slotsPerRack = extendedFxSlotCount;
        slotOffset = legacyFxSlotCount;
        if (fx < 0 || fx >= extendedFxDestinationCount) return nullptr;
    }
    const auto control = fx % fxDestinationsPerSlot;
    const auto slot = (fx / fxDestinationsPerSlot) % slotsPerRack + slotOffset;
    const auto rack = fx / (fxDestinationsPerSlot * slotsPerRack);
    auto& held = patch.racks[static_cast<size_t>(rack)].slots[static_cast<size_t>(slot)];
    return control < fxKnobCount ? &held.knobs[static_cast<size_t>(control)] : &held.mix;
}

// Taken as a bare value as well as from a patch, because the panel draws the
// filter's response from the parameter and has to land on the same tap the
// engine will run.
inline FilterType filterTypeOf(float filterType)
{
    return static_cast<FilterType>(juce::jlimit(0, 2, juce::roundToInt(filterType)));
}

inline FilterType filterTypeOf(const Patch& patch) { return filterTypeOf(patch.filterType); }

// Drive at zero is genuinely clean: the saturation is skipped rather than run
// at unity, which would still compress the peaks.
inline float saturate(float x, float drive)
{
    const auto amount = juce::jlimit(0.0f, 1.0f, drive);
    if (amount <= 0.0f) return x;
    const auto gain = 1.0f + amount * 12.0f;
    return std::tanh(x * gain) / std::tanh(gain);
}

// Exactly linear below the knee, asymptotic to full scale above it. A quiet
// patch passes through untouched — which a plain tanh does not do — while a
// loud one still cannot leave full scale.
inline float softClip(float x)
{
    constexpr float knee = 0.8f;
    const auto magnitude = std::abs(x);
    if (magnitude <= knee) return x;
    const auto limited = knee + (1.0f - knee) * std::tanh((magnitude - knee) / (1.0f - knee));
    return x < 0.0f ? -limited : limited;
}

// --- Panning ------------------------------------------------------------------
//
// Two laws, because a pan means two different things in a mixer.
//
// A *source* pan spreads one source among the others. It is the law the unison
// stack already spreads across, and it holds the source's power constant as it
// moves — at the cost of 3 dB against the mono sum, which is why a centred
// source reads 0.707 rather than 1.
//
// A *channel* pan moves a sum that is already balanced: the filter's output, or
// a bus. Costing that 3 dB would mean placing a channel in the centre quietly
// turned it down, so the channel law is the same curve normalised to unity at
// centre instead. Both hold left-squared plus right-squared constant; they
// differ only in where that constant sits.
inline float sourcePanLeft(float pan)
{
    return std::sqrt(0.5f * (1.0f - juce::jlimit(-1.0f, 1.0f, pan)));
}

inline float sourcePanRight(float pan)
{
    return std::sqrt(0.5f * (1.0f + juce::jlimit(-1.0f, 1.0f, pan)));
}

inline float channelPanLeft(float pan)
{
    return std::sqrt(1.0f - juce::jlimit(-1.0f, 1.0f, pan));
}

inline float channelPanRight(float pan)
{
    return std::sqrt(1.0f + juce::jlimit(-1.0f, 1.0f, pan));
}

// Octave, semitone and fine are one frequency multiplier. Fine is in cents.
inline float tuningRatio(const Oscillator& osc)
{
    return std::pow(2.0f, osc.octave + osc.semitone / 12.0f + osc.fine / 1200.0f);
}
}
