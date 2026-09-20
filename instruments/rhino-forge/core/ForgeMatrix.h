#pragma once

#include "ForgeShapes.h"
#include "ForgeFxDsp.h"
#include <vector>

// What can modulate what: the list of sources, the list of destinations, and
// the eight slots that join one to the other.
//
// Both lists are ordered, and a slot stores an index into them, so the order
// is saved inside every preset — see Processor::migrated for what happens when
// something is inserted into the middle of one.
namespace rhino::forge
{
// either is remapped on the way in; see Processor::migrated.
enum class ModSource
{
    off = 0,
    env1 = 1,                        // env2..env4 follow, up to 4
    lfo1 = env1 + envCount,          // 5, and lfo2..lfo6 up to 10
    velocity = lfo1 + lfoCount,      // 11
    note,                            // 12
    macro1                           // 13, and one per macro after it
};

inline constexpr int modSourceCount = static_cast<int>(ModSource::macro1) + macroCount;

inline int macroIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::macro1);
    return source >= first && source < first + macroCount ? source - first : -1;
}

// Which LFO a source names, or -1 for a source that is not one.
inline int lfoIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::lfo1);
    return source >= first && source < first + lfoCount ? source - first : -1;
}

// Which envelope a source names, or -1 for a source that is not one.
inline int envIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::env1);
    return source >= first && source < first + envCount ? source - first : -1;
}

// Whether a source already swings both ways of its own accord. The LFOs do —
// lfoWave runs -1 to 1 — and nothing else does: an envelope, a velocity, a note
// number and a macro all start at nothing and only rise.
//
// This is what a slot's BI switch acts on. Centring a source that only rises
// turns it into a swing; centring one that already swings would only double its
// reach, which is the depth control's job, so BI leaves the LFOs alone.
inline bool sourceIsBipolar(int source) { return lfoIndexOf(source) >= 0; }

inline const char* modSourceName(int source)
{
    if (source == static_cast<int>(ModSource::velocity)) return "VELOCITY";
    if (source == static_cast<int>(ModSource::note)) return "NOTE";

    static const std::array<const char*, envCount> envs {
        "ENV 1", "ENV 2", "ENV 3", "ENV 4"};
    static const std::array<const char*, lfoCount> lfos {
        "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6"};
    static const std::array<const char*, macroCount> macros {
        "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
        "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"};
    const auto env = envIndexOf(source);
    if (env >= 0) return envs[static_cast<size_t>(env)];
    const auto lfo = lfoIndexOf(source);
    if (lfo >= 0) return lfos[static_cast<size_t>(lfo)];
    const auto macro = macroIndexOf(source);
    return macro >= 0 ? macros[static_cast<size_t>(macro)] : "OFF";
}

// Everything a slot may be pointed at. Index 0 is "nothing". The id is the
// parameter the destination corresponds to; the Processor uses it to hand the
// Core that parameter's range, so modulation happens in the same normalised
// space the knob moves in and nothing here has to duplicate a range.
struct DestinationInfo
{
    const char* id;
    const char* label;
};

// Appended to, never inserted into: a slot stores its destination as an index
// into this list, so every index already written into a preset has to keep
// meaning what it meant. The five the mixer added therefore sit at the end
// rather than beside the controls they belong with, and the racks' come after
// those.
inline const std::vector<DestinationInfo>& destinations();

inline const std::array<DestinationInfo, 21>& namedDestinations()
{
    static const std::array<DestinationInfo, 21> table {{
        {"", "OFF"},
        {"oscAPosition", "A POS"},   {"oscALevel", "A LEVEL"}, {"oscAPan", "A PAN"},
        {"oscADetune", "A DETUNE"},  {"oscASemitone", "A PITCH"},
        {"oscBPosition", "B POS"},   {"oscBLevel", "B LEVEL"}, {"oscBPan", "B PAN"},
        {"oscBDetune", "B DETUNE"},  {"oscBSemitone", "B PITCH"},
        {"subLevel", "SUB"},         {"noiseLevel", "NOISE"},
        {"cutoff", "CUTOFF"},        {"resonance", "RES"},     {"drive", "DRIVE"},
        {"subPan", "SUB PAN"},       {"noisePan", "NOISE PAN"},
        {"filterPan", "FLT PAN"},    {"filterMix", "FLT MIX"}, {"filterLevel", "FLT LEVEL"},
    }};
    return table;
}

// Where the named list stops and the racks begin. The original four slots per
// rack remain in their historic contiguous block so every preset keeps naming
// the same destination. New slots are appended after the warp destinations
// below rather than inserted into that block.
inline constexpr int fxDestinationBase = 21;

// A slot's six knobs and its mix. LEVEL is left out on purpose — it is the
// slot's own trim rather than something to play, and a rack whose every stage
// could be swept in level is a rack that is hard to keep at a sane loudness.
inline constexpr int fxDestinationsPerSlot = fxKnobCount + 1;
inline constexpr int legacyFxDestinationCount =
    rackCount * legacyFxSlotCount * fxDestinationsPerSlot;

// Output is deliberately absent, and so are the bus levels and the sends: all
// of them are applied once the voices are summed, so a per-voice modulation of
// one would not mean anything.
//
// The rack's own controls are applied after the voices too, and they are here
// anyway. A rack is one process fed by every note, so a per-voice source
// reaching it has to resolve to a single value; Core takes the loudest voice's,
// which is the voice every other display already follows. Serum allows the same
// thing and warns about the same consequence: a per-voice envelope on an FX
// knob retriggers on every note.

// Where the racks stop and the warp depths begin. They sit past eighty-four
// generated entries rather than beside the oscillator controls they belong
// with, because this list is appended to and never inserted into: a slot stores
// its destination as an index, and moving one is moving it inside every preset
// already saved.
inline constexpr int warpDestinationBase = fxDestinationBase + legacyFxDestinationCount;
inline constexpr int warpDestinationCount = oscillatorCount * warpSlots;

inline const std::array<DestinationInfo, warpDestinationCount>& warpDestinations()
{
    static const std::array<DestinationInfo, warpDestinationCount> table {{
        {"oscAWarp1", "A WARP 1"}, {"oscAWarp2", "A WARP 2"},
        {"oscBWarp1", "B WARP 1"}, {"oscBWarp2", "B WARP 2"},
    }};
    return table;
}

// The mode a warp stage is set to is deliberately absent. A source sweeping a
// list of twenty-six unrelated modes is a stutter rather than a modulation, and
// nothing about it would be continuous; the depth beside it is the thing worth
// playing, and it is here.
inline constexpr int extendedFxDestinationBase = warpDestinationBase + warpDestinationCount;
inline constexpr int extendedFxSlotCount = fxSlotCount - legacyFxSlotCount;
inline constexpr int extendedFxDestinationCount =
    rackCount * extendedFxSlotCount * fxDestinationsPerSlot;
inline constexpr int fxDestinationCount = legacyFxDestinationCount + extendedFxDestinationCount;
inline constexpr int destinationCount = extendedFxDestinationBase + extendedFxDestinationCount;
inline constexpr int modSlotCount = 8;

// A parameter id belonging to one rack slot: fxParameterId(0, 1, "Mix") is
// "fx1s2Mix". One spelling of the pattern, shared by the parameters, the panel,
// the destination list and the tests, so rack slots cannot drift apart from
// the ids they name — the same reason the envelopes and the LFOs have one.
inline juce::String fxParameterId(int rack, int slot, const char* suffix)
{
    return "fx" + juce::String(rack + 1) + "s" + juce::String(slot + 1) + suffix;
}

// A rack's own bypass, which is the button the mixer's BUS and MAIN channels
// carry: fxRackParameterId(0, "Bypass") is "fx1Bypass".
inline juce::String fxRackParameterId(int rack, const char* suffix)
{
    return "fx" + juce::String(rack + 1) + suffix;
}

// Where one slot's run of destinations starts.
inline int fxDestinationOf(int rack, int slot, int control)
{
    if (slot < legacyFxSlotCount)
        return fxDestinationBase
             + ((rack * legacyFxSlotCount) + slot) * fxDestinationsPerSlot + control;
    return extendedFxDestinationBase
         + ((rack * extendedFxSlotCount) + slot - legacyFxSlotCount)
             * fxDestinationsPerSlot + control;
}

// The whole destination list: the named controls, then every rack slot's six
// knobs and its mix. Built once, and held by value because the generated half
// owns the strings it names — a `const char*` here would point at a temporary.
inline const std::vector<DestinationInfo>& destinations()
{
    static const std::vector<DestinationInfo> table = []
    {
        // The ids and labels the generated half needs, kept alive for as long
        // as the table points into them.
        static std::vector<juce::String> pool;
        pool.reserve(static_cast<size_t>(fxDestinationCount) * 2);

        std::vector<DestinationInfo> built;
        built.reserve(static_cast<size_t>(destinationCount));
        for (const auto& named : namedDestinations()) built.push_back(named);

        const auto appendSlot = [&built] (int rack, int slot)
        {
            for (int control = 0; control < fxDestinationsPerSlot; ++control)
            {
                const auto knob = control < fxKnobCount;
                pool.push_back(knob ? fxParameterId(rack, slot, "Knob") + juce::String(control + 1)
                                    : fxParameterId(rack, slot, "Mix"));
                // Named for where it is rather than for what it does: a
                // slot's knob 3 is a different control in a reverb and in a
                // delay, and the matrix cannot know which is in there.
                pool.push_back(juce::String(rackName(rack)) + " " + juce::String(slot + 1) + " "
                               + (knob ? "K" + juce::String(control + 1) : juce::String("MIX")));
                built.push_back({pool[pool.size() - 2].toRawUTF8(), pool.back().toRawUTF8()});
            }
        };

        for (int rack = 0; rack < rackCount; ++rack)
            for (int slot = 0; slot < legacyFxSlotCount; ++slot)
                appendSlot(rack, slot);

        for (const auto& named : warpDestinations()) built.push_back(named);

        for (int rack = 0; rack < rackCount; ++rack)
            for (int slot = legacyFxSlotCount; slot < fxSlotCount; ++slot)
                appendSlot(rack, slot);
        return built;
    }();
    return table;
}

// Held as floats because that is what a parameter read gives back, and it
// keeps the slot a plain value the processor can fill without conversion.
struct ModSlot
{
    float source = 0.0f;
    float destination = 0.0f;
    float depth = 0.0f;
    // Whether the source is centred before the depth is applied — see
    // applyModulation. Held as a float for the same reason the rest are.
    float bipolar = 0.0f;
};

struct Modulation
{
    std::array<ModSlot, modSlotCount> slots {};

    bool anyActive() const
    {
        for (const auto& slot : slots)
            if (slot.source >= 0.5f && slot.destination >= 0.5f && slot.depth != 0.0f)
                return true;
        return false;
    }
};

// One mixer channel's send amounts, one per bus. Every channel that can be
// sent anywhere owns a set of these, so a channel is described by what it is
}
