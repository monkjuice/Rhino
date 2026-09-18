#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// What an effects rack is, before any of it is rendered or drawn.
//
// The rack is Serum's shape rather than a fixed chain: a slot holds any type,
// the same type can sit in two slots, and the order is the order the slots are
// in. That has one consequence which decides the whole of this file — a host's
// parameter list is fixed at construction, so a slot cannot declare a parameter
// per control of whichever effect it happens to hold. Thirteen types across
// twelve slots would be several hundred parameters, nearly all of them dead at
// any moment.
//
// So a slot declares a fixed set instead: a type, two mode fields, six general
// knobs, a bypass, a mix and a level. What those six knobs *mean* is a property
// of the type, declared in `fxTypeInfo` below and read by the panel to label
// them, by the readouts to format them, and by the DSP to turn them into
// seconds and Hertz. One table, three readers, so a knob cannot be labelled as
// one thing and rendered as another.
//
// The cost is that a host's automation lane says "FX 1.2 KNOB 3" rather than
// "Reverb Damp". Serum's does the same, for the same reason.
namespace rhino::forge
{
// Three racks: the main output and one on each bus. This is what the busses
// were built for — a send is only worth making if something is waiting at the
// other end of it.
inline constexpr int rackCount = 3;
inline constexpr int fxSlotCount = 4;
inline constexpr int fxKnobCount = 6;

inline const char* rackName(int rack)
{
    switch (rack)
    {
        case 1: return "BUS 1";
        case 2: return "BUS 2";
        default: break;
    }
    return "MAIN";
}

// The order is saved inside every patch, because a slot stores its type as an
// index into it. Append; never insert.
enum class FxType { off, reverb, delay, chorus, distortion, equaliser, filter };
inline constexpr int fxTypeCount = 7;

inline const char* fxTypeName(int type)
{
    switch (type)
    {
        case 1: return "REVERB";
        case 2: return "DELAY";
        case 3: return "CHORUS";
        case 4: return "DIST";
        case 5: return "EQ";
        case 6: return "FILTER";
        default: break;
    }
    return "OFF";
}

inline FxType fxTypeOf(float type)
{
    return static_cast<FxType>(juce::jlimit(0, fxTypeCount - 1, juce::roundToInt(type)));
}

// One slot of one rack. Held as floats because that is what a parameter read
// gives back, exactly as every other patch struct here is.
//
// The knobs are normalised. A knob is 0..1 whatever slot it is in and whatever
// type that slot holds; the type is what turns 0.6 into 480 milliseconds or
// into 2.4 kHz. That is what lets one parameter serve thirteen meanings without
// its range changing under a host that has already learned it.
struct FxSlot
{
    float type = 0.0f;
    // Two choices whose meaning is the type's: a reverb's plate or hall, a
    // delay's ping-pong, an equaliser's two band shapes. Stored as floats and
    // read through the type's own list, for the same reason the knobs are.
    float modeA = 0.0f, modeB = 0.0f;
    float bypass = 0.0f;
    std::array<float, fxKnobCount> knobs {};
    // 100% wet by default. Four of the six types are inserts that want all of
    // it, and a bus is fed by a send and wants all of it too — a reverb or a
    // delay placed on MAIN is the case that wants this pulled back.
    float mix = 1.0f;
    float level = 1.0f;
};

struct Rack
{
    // Bypasses the whole rack, which is the button the mixer's BUS and MAIN
    // channels carry. A slot's own bypass is the one inside it.
    float bypass = 0.0f;
    std::array<FxSlot, fxSlotCount> slots {};
};

inline bool fxOn(float value) { return value >= 0.5f; }

// --- What a type's controls are ----------------------------------------------

// A mode field's choices, or nothing when the type does not use that field.
struct FxModeInfo
{
    const char* label = nullptr;
    std::array<const char*, 8> choices {};
    int count = 0;
};

// Everything the panel and the readouts need to know about one type: what to
// call each knob, and what the two mode fields are. A null knob label is a knob
// this type does not use — the panel hides it rather than greying it, because a
// control that means nothing at all is not the same as one that is temporarily
// unavailable.
struct FxTypeInfo
{
    const char* name;
    std::array<const char*, fxKnobCount> knobs;
    FxModeInfo modeA, modeB;
    // Where the knobs and the wet/dry sit when this type is put into a slot.
    //
    // A parameter has one default and a slot's knobs serve seven types, so the
    // default cannot be right for all of them — 100% wet is what an equaliser,
    // a filter and a distortion want and exactly what a reverb on the main
    // output does not. So the panel applies these when a type is chosen, which
    // is the same thing Serum's per-module default preset does: putting a
    // reverb in a slot should give you a reverb, not a drowned one.
    std::array<float, fxKnobCount> init;
    float initMix;
};

inline const std::array<FxTypeInfo, fxTypeCount>& fxTypes()
{
    static const std::array<FxTypeInfo, fxTypeCount> table {{
        {"OFF", {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}, {}, {},
         {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, 1.0f},

        // Serum's reverb carries a high cut as well as damping. Damping is the
        // one that shapes a tail rather than trimming it, so it is the one kept
        // here; PRE-DLY earns its place instead, because separating a transient
        // from its tail is what a reverb is usually reached for.
        {"REVERB", {"SIZE", "DECAY", "DAMP", "WIDTH", "PRE-DLY", "LO CUT"},
         {"TYPE", {"PLATE", "HALL"}, 2}, {},
         // A medium plate, wide, with the bottom kept out of the tail. Barely
         // a third wet, because this is the type most often placed on the main
         // output rather than on a send.
         {0.55f, 0.5f, 0.45f, 0.8f, 0.1f, 0.25f}, 0.3f},

        // OFFSET is the right-hand delay as a share of the left, so the pair is
        // one control and a spread rather than two times to keep in step. Past
        // the centre it reads DOT and TRIP where it lands on them.
        {"DELAY", {"TIME", "OFFSET", "FEEDBACK", "FREQ", "Q", nullptr},
         {"MODE", {"NORMAL", "PING-PONG"}, 2}, {"UNIT", {"MS", "BPM"}, 2},
         // A few audible repeats, even across the two channels, darkening as
         // they go.
         {0.4f, 0.5f, 0.35f, 0.7f, 0.6f, 0.5f}, 0.3f},

        {"CHORUS", {"RATE", "DELAY 1", "DELAY 2", "DEPTH", "FEEDBACK", "FILTER"},
         {"UNIT", {"HZ", "BPM"}, 2}, {"FILTER", {"LPF", "HPF"}, 2},
         // Slow and shallow, the two taps far enough apart to be two.
         {0.25f, 0.3f, 0.5f, 0.35f, 0.2f, 0.8f}, 0.5f},

        {"DIST", {"DRIVE", "FREQ", "Q", nullptr, nullptr, nullptr},
         {"SHAPE", {"TUBE", "SOFT", "HARD", "DIODE", "FOLD", "SINE", "CRUSH", "DOWNSMP"}, 8},
         {"FILTER", {"OFF", "PRE", "POST"}, 3},
         // Enough drive to hear, all wet: an insert, not a send.
         {0.3f, 0.5f, 0.3f, 0.5f, 0.5f, 0.5f}, 1.0f},

        {"EQ", {"FREQ L", "Q L", "GAIN L", "FREQ H", "Q H", "GAIN H"},
         {"LOW", {"SHELF", "PEAK", "HI PASS"}, 3},
         {"HIGH", {"SHELF", "PEAK", "LO PASS"}, 3},
         // Flat. A gain of 0.5 is 0 dB, so an equaliser dropped into a slot
         // does nothing until it is asked to.
         {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}, 1.0f},

        {"FILTER", {"CUTOFF", "RES", "DRIVE", nullptr, nullptr, nullptr},
         {"TYPE", {"LP", "HP", "BP"}, 3}, {},
         // Open, so it is heard as a filter to close rather than as a mute.
         {0.8f, 0.2f, 0.0f, 0.5f, 0.5f, 0.5f}, 1.0f},
    }};
    return table;
}

inline const FxTypeInfo& fxTypeInfo(float type)
{
    return fxTypes()[static_cast<size_t>(fxTypeOf(type))];
}

// Which of a mode field's choices a value names, clamped. A type that does not
// use the field has no choices and always answers zero.
inline int fxModeOf(const FxModeInfo& mode, float value)
{
    return mode.count <= 0 ? 0 : juce::jlimit(0, mode.count - 1, juce::roundToInt(value * (mode.count - 1)));
}

inline const char* fxModeName(const FxModeInfo& mode, float value)
{
    return mode.count <= 0 ? "" : mode.choices[static_cast<size_t>(fxModeOf(mode, value))];
}

// --- Turning a normalised knob into what it means -----------------------------
//
// Every one of these is read twice: by the DSP that renders the effect, and by
// the readout that says what the knob is set to. Neither has a copy of the
// other's arithmetic, which is the only way a bubble saying "480 ms" is bound to
// be the delay actually being heard.

// A knob mapped onto a range, in the shape the control wants. `skew` below one
// gives the lower end of the range more of the knob, which is what a time or a
// frequency wants.
inline float fxScaled(float knob, float low, float high, float skew = 1.0f)
{
    const auto unit = juce::jlimit(0.0f, 1.0f, knob);
    return low + (high - low) * (skew == 1.0f ? unit : std::pow(unit, skew));
}

inline float fxHertz(float knob, float low, float high)
{
    return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, knob));
}

// The delay divisions a synced delay or chorus steps through, as the length of
// one cycle in beats. A beat is a quarter note, so 1/4 is one beat.
struct FxDivision
{
    const char* label;
    float beats;
};

inline const std::array<FxDivision, 9>& fxDivisions()
{
    static const std::array<FxDivision, 9> table {{
        {"1/32", 0.125f}, {"1/16", 0.25f}, {"1/8T", 1.0f / 3.0f}, {"1/8", 0.5f},
        {"1/4T", 2.0f / 3.0f}, {"1/4", 1.0f}, {"1/2", 2.0f}, {"1/1", 4.0f}, {"2/1", 8.0f},
    }};
    return table;
}

inline constexpr int fxDivisionCount = 9;

inline const FxDivision& fxDivisionAt(float knob)
{
    const auto index = juce::jlimit(0, fxDivisionCount - 1,
                                    juce::roundToInt(juce::jlimit(0.0f, 1.0f, knob) * (fxDivisionCount - 1)));
    return fxDivisions()[static_cast<size_t>(index)];
}

// A delay's right-hand offset, as a share of the left. The two ratios a delay is
// actually set to — a dotted note and a triplet — are named where the knob lands
// on them, exactly as Serum's does.
inline float fxOffsetRatio(float knob) { return fxScaled(knob, 0.5f, 1.5f); }

// How long one slot's delay line has to be, in seconds. Every slot is sized for
// this whatever type it holds, because a slot's type changes while audio is
// running and a buffer cannot be grown on that thread.
inline constexpr float fxMaxDelaySeconds = 1.6f;

// The longest a delay can actually be set to, which is short of the line by the
// margin the right-hand offset can stretch it.
inline constexpr float fxMaxDelayTime = fxMaxDelaySeconds / 1.5f;

inline float fxDelaySeconds(const FxSlot& slot, double bpm)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::delay)];
    if (fxModeOf(info.modeB, slot.modeB) == 1)
    {
        const auto tempo = bpm > 0.0 ? bpm : 120.0;
        return juce::jlimit(0.001f, fxMaxDelayTime,
                            static_cast<float>(60.0 / tempo) * fxDivisionAt(slot.knobs[0]).beats);
    }
    return fxScaled(slot.knobs[0], 0.01f, fxMaxDelayTime, 2.0f);
}

inline float fxChorusRate(const FxSlot& slot, double bpm)
{
    const auto& info = fxTypes()[static_cast<size_t>(FxType::chorus)];
    if (fxModeOf(info.modeA, slot.modeA) == 1)
    {
        const auto tempo = bpm > 0.0 ? bpm : 120.0;
        return static_cast<float>(tempo / 60.0) / juce::jmax(0.001f, fxDivisionAt(slot.knobs[0]).beats);
    }
    return fxScaled(slot.knobs[0], 0.02f, 8.0f, 2.0f);
}
}
