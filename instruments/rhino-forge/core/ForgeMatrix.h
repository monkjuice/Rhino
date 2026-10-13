#pragma once

#include "ForgeShapes.h"
#include "ForgeFxDsp.h"
#include <cstdint>
#include <vector>

// What can modulate what: the list of sources, the list of destinations, and
// the slots that join one to the other.
//
// Both lists are ordered, and a slot stores an index into them. The order is a
// schema: changing it bumps the preset format instead of leaving historical
// gaps in the hot-path lookup tables.
namespace rhino::forge
{
enum class ModSource
{
    off = 0,
    env1 = 1,                        // env2..env4 follow, up to 4
    lfo1 = env1 + envCount,          // 5, and lfo2..lfo6 up to 10
    velocity = lfo1 + lfoCount,      // 11
    note,                            // 12
    macro1,                          // 13, and one per macro after it
    modWheel = macro1 + macroCount,  // 21
    // A PATH LFO's second axis. Appended after everything, as the list is
    // append-only, and one per LFO whatever its type, so a slot keeps its
    // source when the type changes: outside PATH the Y output repeats the X.
    lfo1Y                            // 22, and lfo2Y..lfo6Y up to 27
};

inline constexpr int modSourceCount = static_cast<int>(ModSource::lfo1Y) + lfoCount;

inline int macroIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::macro1);
    return source >= first && source < first + macroCount ? source - first : -1;
}

// Which LFO a source names, or -1 for a source that is not one. This is the
// main output, the X of a path.
inline int lfoIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::lfo1);
    return source >= first && source < first + lfoCount ? source - first : -1;
}

// Which LFO's Y output a source names, or -1 for a source that is not one.
inline int lfoYIndexOf(int source)
{
    const auto first = static_cast<int>(ModSource::lfo1Y);
    return source >= first && source < first + lfoCount ? source - first : -1;
}

// Which LFO a source reads from, either axis, or -1 for a source that is not
// an LFO at all.
inline int lfoOfSource(int source)
{
    const auto x = lfoIndexOf(source);
    return x >= 0 ? x : lfoYIndexOf(source);
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
inline bool sourceIsBipolar(int source) { return lfoOfSource(source) >= 0; }

inline const char* modSourceName(int source)
{
    if (source == static_cast<int>(ModSource::velocity)) return "VELOCITY";
    if (source == static_cast<int>(ModSource::note)) return "NOTE";
    if (source == static_cast<int>(ModSource::modWheel)) return "MOD WHEEL";

    static const std::array<const char*, envCount> envs {
        "ENV 1", "ENV 2", "ENV 3", "ENV 4"};
    static const std::array<const char*, lfoCount> lfos {
        "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6"};
    static const std::array<const char*, lfoCount> lfoYs {
        "LFO 1 Y", "LFO 2 Y", "LFO 3 Y", "LFO 4 Y", "LFO 5 Y", "LFO 6 Y"};
    static const std::array<const char*, macroCount> macros {
        "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
        "MACRO 5", "MACRO 6", "MACRO 7", "MACRO 8"};
    const auto env = envIndexOf(source);
    if (env >= 0) return envs[static_cast<size_t>(env)];
    const auto lfo = lfoIndexOf(source);
    if (lfo >= 0) return lfos[static_cast<size_t>(lfo)];
    const auto lfoY = lfoYIndexOf(source);
    if (lfoY >= 0) return lfoYs[static_cast<size_t>(lfoY)];
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

// A compact schema: the continuously modulatable controls of every oscillator
// are contiguous, followed by the shared voice controls, rack controls, warp
// depths and the remaining source shapers. The envelopes' knobs and the
// oscillators' OCT and FINE came later and are appended after all of it.
inline const std::vector<DestinationInfo>& destinations();

inline constexpr int oscillatorDestinationBase = 1;
inline constexpr int oscillatorDestinationsPerOscillator = 5;
inline constexpr int subLevelDestination = oscillatorDestinationBase
    + oscillatorCount * oscillatorDestinationsPerOscillator;
inline constexpr int noiseLevelDestination = subLevelDestination + 1;
inline constexpr int cutoffDestination = noiseLevelDestination + 1;
inline constexpr int resonanceDestination = cutoffDestination + 1;
inline constexpr int driveDestination = resonanceDestination + 1;
inline constexpr int subPanDestination = driveDestination + 1;
inline constexpr int noisePanDestination = subPanDestination + 1;
inline constexpr int filterPanDestination = noisePanDestination + 1;
inline constexpr int filterMixDestination = filterPanDestination + 1;
inline constexpr int filterLevelDestination = filterMixDestination + 1;
inline constexpr int namedDestinationCount = filterLevelDestination + 1;

inline const std::array<DestinationInfo, namedDestinationCount>& namedDestinations()
{
    static const std::array<DestinationInfo, namedDestinationCount> table {{
        {"", "OFF"},
        {"oscAPosition", "A POS"},   {"oscALevel", "A LEVEL"}, {"oscAPan", "A PAN"},
        {"oscADetune", "A DETUNE"},  {"oscASemitone", "A PITCH"},
        {"oscBPosition", "B POS"},   {"oscBLevel", "B LEVEL"}, {"oscBPan", "B PAN"},
        {"oscBDetune", "B DETUNE"},  {"oscBSemitone", "B PITCH"},
        {"oscCPosition", "C POS"},   {"oscCLevel", "C LEVEL"}, {"oscCPan", "C PAN"},
        {"oscCDetune", "C DETUNE"},  {"oscCSemitone", "C PITCH"},
        {"subLevel", "SUB"},         {"noiseLevel", "NOISE"},
        {"cutoff", "CUTOFF"},        {"resonance", "RES"},     {"drive", "DRIVE"},
        {"subPan", "SUB PAN"},       {"noisePan", "NOISE PAN"},
        {"filterPan", "FLT PAN"},    {"filterMix", "FLT MIX"}, {"filterLevel", "FLT LEVEL"},
    }};
    return table;
}

// Where the named list stops and the racks begin.
inline constexpr int fxDestinationBase = namedDestinationCount;

// A slot's six knobs and its mix. LEVEL is left out on purpose — it is the
// slot's own trim rather than something to play, and a rack whose every stage
// could be swept in level is a rack that is hard to keep at a sane loudness.
inline constexpr int fxDestinationsPerSlot = fxKnobCount + 1;
inline constexpr int fxDestinationCount = rackCount * fxSlotCount * fxDestinationsPerSlot;

// Whether a destination is one of a rack's controls. A range, not a lower bound:
// the warp depths, the filter's second field and the noise controls were all
// appended after the racks, so "at or past the racks" takes them in too.
inline constexpr bool isFxDestination(int destination)
{
    return destination >= fxDestinationBase && destination < fxDestinationBase + fxDestinationCount;
}

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

// Where the racks stop and the oscillator warp depths begin.
inline constexpr int warpDestinationBase = fxDestinationBase + fxDestinationCount;
inline constexpr int warpDestinationCount = oscillatorCount * warpSlots;

inline const std::array<DestinationInfo, warpDestinationCount>& warpDestinations()
{
    static const std::array<DestinationInfo, warpDestinationCount> table {{
        {"oscAWarp1", "A WARP 1"}, {"oscAWarp2", "A WARP 2"},
        {"oscBWarp1", "B WARP 1"}, {"oscBWarp2", "B WARP 2"},
        {"oscCWarp1", "C WARP 1"}, {"oscCWarp2", "C WARP 2"},
    }};
    return table;
}

// The mode a warp stage is set to is deliberately absent. A source sweeping a
// list of twenty-six unrelated modes is a stutter rather than a modulation, and
// nothing about it would be continuous; the depth beside it is the thing worth
// playing, and it is here.
// The filter's second field is worth a destination because MORPH is the filter
// control a patch most wants an LFO on: sweeping LP to BP to HP is not a thing
// any of the ordinary cutoff controls can ask for.
inline constexpr int filterDestination = warpDestinationBase + warpDestinationCount;

// The noise source itself is deliberately absent, exactly as the warp modes
// are. A
// sweep through four unrelated generators is a stutter rather than a
// modulation; the two continuous controls beside it are the ones worth playing.
inline constexpr int noiseToneDestination = filterDestination + 1;
inline constexpr int noiseStereoDestination = noiseToneDestination + 1;

// Every envelope's four knobs, ENV 1 to ENV 4 in turn, each ATTACK, DECAY,
// SUSTAIN and RELEASE. The voice reads an envelope's settings before it reads
// anything else, so this bank is applied on its own and ahead of the rest; see
// Core::modulateEnvelopes.
inline constexpr int envDestinationBase = noiseStereoDestination + 1;
inline constexpr int envDestinationsPerEnvelope = 4;
inline constexpr int envDestinationCount = envCount * envDestinationsPerEnvelope;

inline constexpr bool isEnvelopeDestination(int destination)
{
    return destination >= envDestinationBase
        && destination < envDestinationBase + envDestinationCount;
}

// OCT and FINE, oscillator by oscillator. SEMI has been a destination from the
// start, as A PITCH and its neighbours, which is why it is not here. A modulated
// OCT still moves in whole octaves; see tuningRatio.
inline constexpr int tuningDestinationBase = envDestinationBase + envDestinationCount;
inline constexpr int tuningDestinationsPerOscillator = 2;
inline constexpr int tuningDestinationCount = oscillatorCount * tuningDestinationsPerOscillator;
inline constexpr int destinationCount = tuningDestinationBase + tuningDestinationCount;

inline const std::array<DestinationInfo, tuningDestinationCount>& tuningDestinations()
{
    static const std::array<DestinationInfo, tuningDestinationCount> table {{
        {"oscAOctave", "A OCT"}, {"oscAFine", "A FINE"},
        {"oscBOctave", "B OCT"}, {"oscBFine", "B FINE"},
        {"oscCOctave", "C OCT"}, {"oscCFine", "C FINE"},
    }};
    return table;
}

// Thirty slots. The panel's table shows eight of them to begin with and adds a
// row as those fill (ForgePlacement.h, the matrix); the parameters exist
// whether or not their row is on screen, so a host automates the thirtieth
// exactly as it automates the first, and a preset written with eight opens
// with the rest at rest. Eight until 2026-10-13.
inline constexpr int modSlotCount = 30;

// A parameter id belonging to one slot: modParameterId(0, "Dest") is
// "mod1Dest". One spelling of the pattern, for the same reason the rack slots
// have one.
inline juce::String modParameterId(int slot, const char* suffix)
{
    return "mod" + juce::String(slot + 1) + suffix;
}

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
    return fxDestinationBase
         + ((rack * fxSlotCount) + slot) * fxDestinationsPerSlot + control;
}

// The whole destination list: the named controls, then every rack slot's six
// knobs and its mix, then whatever was appended since. Built once, and held by
// value because the generated half owns the strings it names — a `const char*`
// here would point at a temporary.
inline const std::vector<DestinationInfo>& destinations()
{
    static const std::vector<DestinationInfo> table = []
    {
        // The ids and labels the generated half needs, kept alive for as long
        // as the table points into them.
        static std::vector<juce::String> pool;
        pool.reserve(static_cast<size_t>(fxDestinationCount + envDestinationCount) * 2);

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
            for (int slot = 0; slot < fxSlotCount; ++slot)
                appendSlot(rack, slot);

        for (const auto& named : warpDestinations()) built.push_back(named);

        built.push_back({"filterFreq", "FLT FREQ"});
        built.push_back({"noiseTone", "NOISE TONE"});
        built.push_back({"noiseStereo", "NOISE WIDTH"});

        // Spelt through envParameterId, so the bank names the parameters the
        // envelope module declares rather than a second copy of their ids.
        static constexpr std::array<const char*, envDestinationsPerEnvelope> envSuffixes {
            "Attack", "Decay", "Sustain", "Release"};
        static constexpr std::array<const char*, envDestinationsPerEnvelope> envLabels {
            "ATK", "DEC", "SUS", "REL"};
        for (int env = 0; env < envCount; ++env)
            for (int control = 0; control < envDestinationsPerEnvelope; ++control)
            {
                const auto index = static_cast<size_t>(control);
                pool.push_back(envParameterId(env, envSuffixes[index]));
                pool.push_back("ENV " + juce::String(env + 1) + " " + envLabels[index]);
                built.push_back({pool[pool.size() - 2].toRawUTF8(), pool.back().toRawUTF8()});
            }

        for (const auto& named : tuningDestinations()) built.push_back(named);
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

    // The slots worth visiting: both ends set, and a depth to apply. Settled
    // once a block by whoever fills the slots (Processor::modulation), so the
    // loops that run per voice per sample walk the handful of routings a patch
    // has rather than every one of thirty rows. A Modulation built and never
    // settled has nothing live, which is what an empty matrix is.
    std::array<std::uint8_t, modSlotCount> live {};
    int liveCount = 0;

    void settle()
    {
        liveCount = 0;
        for (int i = 0; i < modSlotCount; ++i)
        {
            const auto& slot = slots[static_cast<size_t>(i)];
            const auto destination = juce::roundToInt(slot.destination);
            if (slot.source >= 0.5f && destination > 0 && destination < destinationCount
                && slot.depth != 0.0f)
                live[static_cast<size_t>(liveCount++)] = static_cast<std::uint8_t>(i);
        }
    }

    bool anyActive() const { return liveCount > 0; }

    const ModSlot& liveSlot(int i) const { return slots[live[static_cast<size_t>(i)]]; }
};

// One mixer channel's send amounts, one per bus. Every channel that can be
// sent anywhere owns a set of these, so a channel is described by what it is
}
