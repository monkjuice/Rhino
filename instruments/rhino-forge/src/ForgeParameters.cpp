#include "ForgeProcessor.h"

// Every parameter Forge has, and what each one reads as.
//
// Value formatting belongs to the parameter rather than to the editor: a
// SliderAttachment overwrites any textFromValueFunction the editor sets, so a
// formatter installed there never runs and every knob reads "0.5500000".
// Declaring it here means the host's automation lane and Forge's own knobs
// show the same text.
//
// Adding a control is a line here and a line in a module in ForgeModules.h.
// The layout test fails if the two disagree in either direction.
namespace rhino::forge
{
namespace
{
// Value formatting belongs to the parameter, not the editor. A
// SliderAttachment overwrites any textFromValueFunction the editor sets, so a
// formatter installed there never runs and every knob reads "0.5500000".
// Declaring it here means the host's automation lane and Forge's own knobs
// show the same text.
using Format = std::function<juce::String (float)>;

juce::String asPercent(float value) { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; }

juce::String asSignedPercent(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

juce::String asSemitones(float value)
{
    return (value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " st";
}

juce::String asHertz(float value)
{
    return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz"
                            : juce::String(juce::roundToInt(value)) + " Hz";
}

juce::String asRate(float value) { return juce::String(value, 2) + " Hz"; }

juce::String asSeconds(float value)
{
    return value < 1.0f ? juce::String(juce::roundToInt(value * 1000.0f)) + " ms"
                        : juce::String(value, 2) + " s";
}

juce::String asCount(float value) { return juce::String(juce::roundToInt(value)); }

juce::String asGain(float value) { return juce::String(value, 2); }

// A fader reads in decibels, because that is the unit a level is balanced in.
// The value behind it is still the linear gain every other level in Forge is,
// so nothing about the range or the saved patch changes — only what the bubble
// says while the fader is moving.
juce::String asDecibels(float value)
{
    if (value <= 0.0001f) return "-inf dB";
    const auto db = 20.0f * std::log10(value);
    return (db > 0.0f ? "+" : "") + juce::String(db, 1) + " dB";
}

juce::String asOctaves(float value)
{
    const auto octaves = juce::roundToInt(value);
    return octaves == 0 ? juce::String("0") : (octaves > 0 ? "+" : "") + juce::String(octaves) + " oct";
}

juce::String asCents(float value)
{
    const auto cents = juce::roundToInt(value);
    return cents == 0 ? juce::String("0") : (cents > 0 ? "+" : "") + juce::String(cents) + " ct";
}

juce::String asPan(float value)
{
    const auto amount = juce::roundToInt(std::abs(value) * 100.0f);
    if (amount == 0) return "C";
    return (value < 0.0f ? "L" : "R") + juce::String(amount);
}

// Format 2 dropped the effects, the macros and the filter envelope. Format 1
// files are not accepted; they described a synth that no longer exists.

std::unique_ptr<juce::RangedAudioParameter> parameter(const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float initial, Format format)
{
    // Explicit version hints make VST3 parameter IDs stable from the first
    // release, rather than relying on JUCE's legacy VST2-compatible mapping.
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID {id, 1}, name, range, initial,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [format] (float value, int) { return format(value); }));
}

// Module enables are genuine switches, so they are declared as bools and show
// up in a host's automation lane as on/off rather than as a float that happens
// to be stepped.
//
// A switch whose two states have names of their own says them: the mixer draws
// the filter routing as a field rather than as a chip, and "FILTER" or "MAIN"
// is what that field has to read. The chips on the FILTER module are unaffected
// — they carry their own caption and never print a value.
std::unique_ptr<juce::RangedAudioParameter> toggle(const juce::String& id, const juce::String& name,
                                                   bool initial, const char* whenOff = nullptr,
                                                   const char* whenOn = nullptr)
{
    auto attributes = juce::AudioParameterBoolAttributes();
    if (whenOff != nullptr && whenOn != nullptr)
        attributes = attributes.withStringFromValueFunction(
            [whenOff, whenOn] (bool value, int) { return juce::String(value ? whenOn : whenOff); });
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID {id, 1}, name, initial,
                                                      attributes);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Processor::parameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;

    // The warp modes, in the order ForgeWarp.h declares them, so a host's
    // automation lane names the very mode the engine will run.
    juce::StringArray warpModeNames;
    for (int i = 0; i < warpModeCount; ++i) warpModeNames.add(warpModeName(i));

    // The two oscillators are declared identically. Neither is expressed in
    // terms of the other, so each owns its tuning, its stack, its pan and its
    // level outright.
    const auto oscillator = [this, &result, &warpModeNames] (int which, const char* prefix,
                                                             const char* label, bool enabled,
                                                             float position, float semitone, float level)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        const auto name = [label] (const char* suffix) { return juce::String(label) + " " + suffix; };
        result.push_back(toggle(id("Enable"), name("Enable"), enabled));
        // POSITION reads as the shape it is on, or as the two it sits between —
        // "where is the saw?" is the question a wavetable knob is asked, and a
        // percentage answers none of it. On a table somebody drew there are no
        // shape names left to give, so it counts frames instead. Either way the
        // answer depends on the table this oscillator is reading, which is why
        // this one formatter closes over the processor and the rest do not.
        //
        // A host can call this from its own thread while the message thread is
        // publishing a new table; what it reads is published as atomics for
        // exactly that reason.
        result.push_back(parameter(id("Position"), name("Position"), {0.0f, 1.0f}, position,
                                   [this, which] (float value)
                                   {
                                       return positionLabel(tables.frameCount(which),
                                                            tables.isBuiltIn(which), value);
                                   }));
        result.push_back(parameter(id("Octave"), name("Octave"), {-4.0f, 4.0f, 1.0f}, 0.0f, asOctaves));
        result.push_back(parameter(id("Semitone"), name("Semitone"), {-12.0f, 12.0f}, semitone, asSemitones));
        result.push_back(parameter(id("Fine"), name("Fine"), {-100.0f, 100.0f, 1.0f}, 0.0f, asCents));
        result.push_back(parameter(id("Unison"), name("Unison"), {1.0f, static_cast<float>(unisonMax), 1.0f}, 2.0f, asCount));
        result.push_back(parameter(id("Detune"), name("Detune"), {0.0f, 1.0f}, 0.18f, asPercent));
        result.push_back(parameter(id("Blend"), name("Blend"), {0.0f, 1.0f}, 0.5f, asPercent));
        result.push_back(parameter(id("Pan"), name("Pan"), {-1.0f, 1.0f}, 0.0f, asPan));
        result.push_back(parameter(id("Level"), name("Level"), {0.0f, 1.0f}, level, asDecibels));
        // Two warp stages, applied in the order they are declared. The mode is
        // a choice rather than a stepped float, so a host's lane reads "BEND +"
        // instead of 0.16; the depth beside it is a plain 0..1, because what it
        // means is the mode's business and every mode uses the whole of it.
        //
        // Both open at nothing: a fresh patch has no warp on it, exactly as the
        // manual says Serum's does, so every preset written before warp existed
        // still sounds as it did.
        for (int slot = 1; slot <= warpSlots; ++slot)
        {
            const auto suffix = "Warp" + juce::String(slot);
            result.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID {juce::String(prefix) + suffix + "Mode", 1},
                juce::String(label) + " " + suffix + " Mode", warpModeNames, 0));
            result.push_back(parameter(juce::String(prefix) + suffix,
                                       juce::String(label) + " " + suffix,
                                       {0.0f, 1.0f}, 0.0f, asPercent));
        }
    };
    // 6/9 is SAW and 1/9 is TRI: a fresh patch starts on shapes with names
    // rather than part-way between two of them.
    oscillator(0, "oscA", "Osc A", true, 6.0f / 9.0f, 0.0f, 0.75f);
    oscillator(1, "oscB", "Osc B", true, 1.0f / 9.0f, 7.0f, 0.25f);

    result.push_back(toggle("subEnable", "Sub Enable", true));
    // The shape, as a choice rather than a stepped float, so a host's lane
    // reads SAW instead of 0.6 — the same treatment the filter type and the
    // warp modes get. The names are the engine's own, in the engine's order, so
    // the index a host writes is the frame the sub reads.
    juce::StringArray subWaveNames;
    for (int shape = 0; shape < subShapeCount; ++shape) subWaveNames.add(subShapeName(shape));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"subWave", 1}, "Sub Wave", subWaveNames, 0));
    // Two octaves either way, against an oscillator's four. Zero is already an
    // octave below the note, so the bottom of this range is three octaves down
    // — far enough that the fundamental of a low note has left the range a
    // speaker reproduces, and further would only be further inaudible.
    result.push_back(parameter("subOctave", "Sub Octave", {-2.0f, 2.0f, 1.0f}, 0.0f, asOctaves));
    result.push_back(parameter("subLevel", "Sub Level", {0.0f, 1.0f}, 0.17f, asDecibels));
    result.push_back(toggle("noiseEnable", "Noise Enable", false));
    result.push_back(parameter("noiseLevel", "Noise Level", {0.0f, 1.0f}, 0.35f, asDecibels));
    result.push_back(toggle("filterEnable", "Filter Enable", true));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"filterType", 1}, "Filter Type",
        juce::StringArray {"LP", "HP", "BP"}, 0));
    // Where a source goes. One switch, two readings: the FILTER module draws it
    // as a lettered chip, the mixer as a field that says the destination out.
    result.push_back(toggle("routeA", "Filter Route Osc A", true, "MAIN", "FILTER"));
    result.push_back(toggle("routeB", "Filter Route Osc B", true, "MAIN", "FILTER"));
    result.push_back(toggle("routeSub", "Filter Route Sub", true, "MAIN", "FILTER"));
    result.push_back(toggle("routeNoise", "Filter Route Noise", true, "MAIN", "FILTER"));
    result.push_back(parameter("cutoff", "Cutoff", {30.0f, 18000.0f, 0.0f, 0.25f}, 7800.0f, asHertz));
    result.push_back(parameter("resonance", "Resonance", {0.0f, 1.0f}, 0.12f, asPercent));
    result.push_back(parameter("drive", "Drive", {0.0f, 1.0f}, 0.08f, asPercent));

    // --- The mixer ------------------------------------------------------------
    //
    // Everything below exists so the MIX tab can be a mixer rather than a
    // second set of level knobs: a pan for the two sources that had none, a
    // channel of the filter's own, two sends per channel, and the two busses
    // those sends arrive at.
    //
    // The oscillators are not repeated here. Their pan and level already exist
    // and the mixer shows those very parameters — one setting, two places to
    // reach it, exactly as Serum's mixer shows the oscillator levels.
    //
    // SUB and NOISE are summed into both channels at full amplitude today, and
    // every oscillator is summed at equal power, which leaves those two 3 dB
    // hot against an oscillator reading the same level. Giving them a pan law
    // settles that, and their defaults above rise by the same 3 dB so a fresh
    // patch is unchanged. A patch saved before the mixer is corrected on the
    // way in — see Processor::migrated.
    result.push_back(parameter("subPan", "Sub Pan", {-1.0f, 1.0f}, 0.0f, asPan));
    result.push_back(parameter("noisePan", "Noise Pan", {-1.0f, 1.0f}, 0.0f, asPan));

    // The filter is a channel of the mixer as well as a module: what comes out
    // of it has a place in the image, a blend against what went in, and a level
    // of its own. All three default to leaving the filter exactly as it was.
    result.push_back(parameter("filterPan", "Filter Pan", {-1.0f, 1.0f}, 0.0f, asPan));
    result.push_back(parameter("filterMix", "Filter Mix", {0.0f, 1.0f}, 1.0f, asPercent));
    result.push_back(parameter("filterLevel", "Filter Level", {0.0f, 1.0f}, 1.0f, asDecibels));

    // Two sends per channel. A send is parallel to wherever the channel is
    // already going, which is what makes it a send rather than a second
    // destination: a source can reach the filter and both busses at once.
    const auto sends = [&result] (const char* prefix, const char* label)
    {
        for (int bus = 1; bus <= busCount; ++bus)
            result.push_back(parameter(juce::String(prefix) + "Send" + juce::String(bus),
                                       juce::String(label) + " Send " + juce::String(bus),
                                       {0.0f, 1.0f}, 0.0f, asPercent));
    };
    sends("oscA", "Osc A");
    sends("oscB", "Osc B");
    sends("sub", "Sub");
    sends("noise", "Noise");
    sends("filter", "Filter");

    // The two busses. Each is a summing point with a level and a place in the
    // image, and it goes to the main output or across to the other one. They
    // carry no effects yet — the FX racks that make a bus worth sending to
    // arrive with M11, and land on these channels without moving them.
    for (int bus = 1; bus <= busCount; ++bus)
    {
        const auto id = [bus] (const char* suffix) { return "bus" + juce::String(bus) + suffix; };
        const auto name = [bus] (const char* suffix)
        {
            return "Bus " + juce::String(bus) + " " + suffix;
        };
        result.push_back(toggle(id("Enable"), name("Enable"), true));
        // A bus goes to the main output, or across to the other bus. Two
        // busses pointed at each other would be a loop; the engine breaks it
        // rather than the parameter forbidding it, so neither setting is one
        // the panel has to refuse. See Core::renderSample.
        result.push_back(toggle(id("Dest"), name("Destination"), false,
                                "MAIN", bus == 1 ? "BUS 2" : "BUS 1"));
        result.push_back(parameter(id("Pan"), name("Pan"), {-1.0f, 1.0f}, 0.0f, asPan));
        result.push_back(parameter(id("Level"), name("Level"), {0.0f, 1.0f}, 0.75f, asDecibels));
    }
    // Four envelopes, declared identically and with the same defaults. ENV 1 is
    // the amplitude and ENV 2-4 are sources, but that is a matter of what reads
    // them, not of what they are: the LFOs were given a rate each so six of them
    // would not move as one, and there is no such thing to avoid here — four
    // envelopes are started by the same note whatever their times, and a source
    // that opens exactly as the amplitude does is the useful place to start
    // from rather than one to be nudged off.
    for (int env = 1; env <= envCount; ++env)
    {
        const auto id = [env] (const char* suffix) { return envParameterId(env - 1, suffix); };
        const auto name = [env] (const char* suffix)
        {
            return "Env " + juce::String(env) + " " + suffix;
        };
        result.push_back(parameter(id("Attack"), name("Attack"), {0.001f, 4.0f, 0.0f, 0.35f}, 0.01f, asSeconds));
        result.push_back(parameter(id("Decay"), name("Decay"), {0.001f, 4.0f, 0.0f, 0.35f}, 0.24f, asSeconds));
        result.push_back(parameter(id("Sustain"), name("Sustain"), {0.0f, 1.0f}, 0.75f, asPercent));
        result.push_back(parameter(id("Release"), name("Release"), {0.001f, 8.0f, 0.0f, 0.35f}, 0.35f, asSeconds));
    }
    // Six LFOs, declared identically. None is expressed in terms of another:
    // each owns its shape, its mode and its rate outright, exactly as the two
    // oscillators do, and the panel shows one at a time.
    juce::StringArray lfoShapeNames;
    for (int i = 0; i < lfoShapeCount; ++i) lfoShapeNames.add(lfoShapeName(i));
    juce::StringArray lfoModeNames;
    for (int i = 0; i < lfoModeCount; ++i) lfoModeNames.add(lfoModeName(i));
    juce::StringArray lfoRateUnitNames;
    for (int i = 0; i < lfoRateUnitCount; ++i) lfoRateUnitNames.add(lfoRateUnitName(i));
    juce::StringArray lfoDivisionNames;
    for (const auto& division : lfoDivisions()) lfoDivisionNames.add(division.label);

    for (int lfo = 1; lfo <= lfoCount; ++lfo)
    {
        const auto id = [lfo] (const char* suffix) { return lfoParameterId(lfo - 1, suffix); };
        const auto name = [lfo] (const char* suffix)
        {
            return "LFO " + juce::String(lfo) + " " + suffix;
        };
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Shape"), 1}, name("Shape"), lfoShapeNames, 0));
        // TRIG by default: an LFO that answers the keyboard is what a player
        // expects of one, and it is the mode the other two are heard against.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Mode"), 1}, name("Mode"), lfoModeNames, 0));
        // Each LFO starts at a rate of its own, so six of them pointed at six
        // destinations do not all move as one until they are set apart by hand.
        const auto defaultRate = 0.5f * static_cast<float>(lfo);
        result.push_back(parameter(id("Rate"), name("Rate"), {0.05f, 20.0f, 0.0f, 0.35f},
                                   defaultRate, asRate));
        // The unit the rate is set in, rather than a sync switch with an implied
        // "off". HZ and BPM are two readings of one setting, and the panel puts
        // whichever is in charge in the same place on the row.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("RateUnit"), 1}, name("Rate Unit"), lfoRateUnitNames, 0));
        // 1/4 by default: one cycle per beat is the rate a sync is usually
        // reached for in the first place.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Division"), 1}, name("Division"), lfoDivisionNames, 2));
    }
    // --- The effects racks ----------------------------------------------------
    //
    // Three racks of eight slots, and every slot declares the same twelve
    // parameters whatever type it holds. A slot's knobs are plain 0..1: what
    // 0.6 means is the type's business, declared once in ForgeFx.h and read by
    // the DSP, by the panel's labels and by the readout below — so a bubble
    // saying "480 ms" is bound to be the delay actually being heard.
    //
    // This is what a host's automation lane pays for a rack that holds any type
    // in any slot: it reads "FX 1.2 KNOB 3" rather than "Reverb Damp". Naming
    // them properly would mean a parameter per control per type per slot, which
    // is several hundred of them, nearly all dead at any moment.
    juce::StringArray fxTypeNames;
    for (int i = 0; i < fxTypeCount; ++i) fxTypeNames.add(fxTypeName(i));

    for (int rack = 0; rack < rackCount; ++rack)
    {
        const auto rackLabel = juce::String(rackName(rack));
        result.push_back(toggle(fxRackParameterId(rack, "Bypass"), rackLabel + " FX Bypass", false));

        for (int slot = 0; slot < fxSlotCount; ++slot)
        {
            const auto id = [rack, slot] (const char* suffix)
            {
                return fxParameterId(rack, slot, suffix);
            };
            const auto name = [&rackLabel, slot] (const juce::String& suffix)
            {
                return rackLabel + " " + juce::String(slot + 1) + " " + suffix;
            };
            result.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID {id("Type"), 1}, name("Type"), fxTypeNames, 0));
            // The two mode fields are floats rather than choices because what
            // they step through changes with the type, and a host's choice list
            // is fixed when the parameter is made. The formatter reads the
            // slot's current type, exactly as POSITION's reads the table an
            // oscillator is on.
            for (const auto* suffix : {"ModeA", "ModeB"})
            {
                const auto whichB = juce::String(suffix) == "ModeB";
                result.push_back(parameter(id(suffix), name(whichB ? "Mode B" : "Mode A"), {0.0f, 1.0f}, 0.0f,
                                           [this, rack, slot, whichB] (float value)
                                           {
                                               const auto& info = fxTypeInfo(
                                                   state.getRawParameterValue(fxParameterId(rack, slot, "Type"))->load());
                                               const auto& mode = whichB ? info.modeB : info.modeA;
                                               const auto* named = fxModeName(mode, value);
                                               return juce::String(named).isEmpty() ? juce::String("-")
                                                                                    : juce::String(named);
                                           }));
            }
            result.push_back(toggle(id("Bypass"), name("Bypass"), false));
            for (int knob = 0; knob < fxKnobCount; ++knob)
                result.push_back(parameter(id("Knob") + juce::String(knob + 1),
                                           name("Knob " + juce::String(knob + 1)), {0.0f, 1.0f}, 0.5f,
                                           [this, rack, slot, knob] (float value)
                                           {
                                               return fxKnobText(rack, slot, knob, value);
                                           }));
            result.push_back(parameter(id("Mix"), name("Mix"), {0.0f, 1.0f}, 1.0f, asPercent));
            result.push_back(parameter(id("Level"), name("Level"), {0.0f, 2.0f}, 1.0f, asDecibels));
        }
    }

    result.push_back(parameter("polyphony", "Polyphony", {1.0f, 16.0f, 1.0f}, 8.0f, asCount));
    result.push_back(toggle("mono", "Mono", false));
    result.push_back(toggle("legato", "Legato", true));
    result.push_back(parameter("glide", "Glide", {0.0f, 2.0f, 0.0f, 0.35f}, 0.08f, asSeconds));
    result.push_back(parameter("output", "Output", {0.0f, 1.25f}, 0.75f, asDecibels));

    for (int macro = 1; macro <= macroCount; ++macro)
        result.push_back(parameter("macro" + juce::String(macro), "Macro " + juce::String(macro),
                                   {0.0f, 1.0f}, 0.0f, asPercent));

    // Eight modulation slots. Each is three parameters so a host can automate a
    // routing as readily as a knob, and so the whole matrix saves with a preset
    // without a separate serialisation path.
    juce::StringArray sourceNames;
    for (int i = 0; i < modSourceCount; ++i) sourceNames.add(modSourceName(i));
    juce::StringArray destinationNames;
    for (const auto& destination : destinations()) destinationNames.add(destination.label);

    for (int slot = 1; slot <= modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot) + suffix; };
        const auto name = [slot] (const char* suffix) { return "Mod " + juce::String(slot) + " " + suffix; };
        // Slot 1 is pre-wired to LFO 1 into the cutoff at zero depth, so the
        // most common first move is one knob rather than three.
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Source"), 1}, name("Source"), sourceNames,
            slot == 1 ? static_cast<int>(ModSource::lfo1) : 0));
        result.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID {id("Dest"), 1}, name("Destination"), destinationNames,
            slot == 1 ? 13 : 0));
        result.push_back(parameter(id("Depth"), name("Depth"), {-1.0f, 1.0f}, 0.0f, asSignedPercent));
        // Whether the destination's own setting is the start of the reach or
        // the middle of it. Off by default, so every preset written before this
        // existed still modulates exactly as it did, and so a source at rest
        // still means "nothing happening" until the switch says otherwise.
        result.push_back(toggle(id("Bipolar"), name("Polarity"), false, "UNI", "BI"));
    }

    // --- The arpeggiator ------------------------------------------------------
    //
    // Six panes, grouped exactly as the manual groups them, because that is the
    // order the settings are reached for: what the whole arp does, the pattern,
    // how far it is transposed, how it is played back, what restarts it, and
    // what happens to the velocity as it runs.
    //
    // Everything here is a plain parameter with a name, unlike a rack slot's
    // twelve anonymous knobs: there is one arpeggiator rather than a slot that
    // could hold any of seven types, so a host's lane can say ARP GATE and
    // mean it.
    juce::StringArray arpShapeNames;
    for (int i = 0; i < arpShapeCount; ++i) arpShapeNames.add(arpShapeName(i));
    juce::StringArray arpDivisionNames;
    for (const auto& division : arpDivisions()) arpDivisionNames.add(division.label);
    juce::StringArray arpQuantNames {"OFF"};
    for (const auto& division : arpDivisions()) arpQuantNames.add(division.label);

    result.push_back(toggle("arpEnable", "Arp Enable", false));
    // GLOBAL. The bank field and EDIT ALL belong to the twelve launchable slots
    // and arrive with them; what is real without slots is the interval the arp
    // waits for before it starts, which is what LAUNCH QUANT means.
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpLaunchQuant", 1}, "Arp Launch Quant", arpQuantNames, 0));

    // PATTERN. RATE is one knob in one place and UNIT decides what it counts
    // in, the pair sharing a cell exactly as an LFO's rate does. TRIP and DOT
    // scale whichever division is chosen rather than being entries in it, which
    // is what keeps the list seven long instead of twenty-one.
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpShape", 1}, "Arp Shape", arpShapeNames, 0));
    result.push_back(parameter("arpRate", "Arp Rate", {0.1f, 50.0f, 0.0f, 0.35f}, 8.0f, asRate));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpRateUnit", 1}, "Arp Rate Unit",
        juce::StringArray {lfoRateUnitName(0), lfoRateUnitName(1)}, 1));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpDivision", 1}, "Arp Division", arpDivisionNames, arpDefaultDivision));
    result.push_back(toggle("arpTriplet", "Arp Triplet", false));
    result.push_back(toggle("arpDotted", "Arp Dotted", false));

    // TRANSPOSE. SHIFT is how far each repetition moves and RANGE is how many
    // repetitions there are; the shape of the range is the same vocabulary the
    // pattern uses, because ordering four transpositions and ordering four keys
    // are the same question asked twice.
    result.push_back(parameter("arpShift", "Arp Shift", {-24.0f, 24.0f, 1.0f}, 0.0f, asSemitones));
    result.push_back(parameter("arpRange", "Arp Range",
                               {1.0f, static_cast<float>(arpMaxRange), 1.0f}, 1.0f, asCount));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpRangeShape", 1}, "Arp Range Shape", arpShapeNames, 0));

    // PLAYBACK.
    result.push_back(toggle("arpLatch", "Arp Latch", false));
    result.push_back(toggle("arpThru", "Arp Thru", false));
    result.push_back(parameter("arpOffset", "Arp Offset",
                               {0.0f, static_cast<float>(arpMaxHeld - 1), 1.0f}, 0.0f, asCount));
    // Zero is forever, and forever is what an arpeggio nearly always wants, so
    // that is where the knob opens and what it reads there.
    result.push_back(parameter("arpRepeats", "Arp Repeats", {0.0f, 64.0f, 1.0f}, 0.0f,
                               [] (float value)
                               {
                                   const auto count = juce::roundToInt(value);
                                   return count <= 0 ? juce::String("inf") : juce::String(count);
                               }));
    result.push_back(parameter("arpGate", "Arp Gate", {0.01f, 2.0f}, 1.0f, asPercent));
    result.push_back(parameter("arpChance", "Arp Chance", {0.0f, 1.0f}, 1.0f, asPercent));
    result.push_back(toggle("arpChancePre", "Arp Chance Timing", false, "POST", "PRE"));

    // RETRIGGER.
    result.push_back(toggle("arpRetrigLaunch", "Arp Retrigger On Launch", true));
    result.push_back(toggle("arpRetrigRateOn", "Arp Retrigger On Rate", false));
    result.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"arpRetrigRate", 1}, "Arp Retrigger Rate", arpDivisionNames, 0));
    result.push_back(toggle("arpRetrigNote", "Arp Retrigger On Note", false));
    result.push_back(toggle("arpRetrigFirst", "Arp Retrigger First Only", false));

    // VELOCITY. Off by default, which is the arp playing every note at the
    // velocity its key was struck at — the behaviour anything written before
    // this existed would expect.
    result.push_back(toggle("arpVelEnable", "Arp Velocity Enable", false));
    result.push_back(toggle("arpVelRetrig", "Arp Velocity Retrigger", false));
    result.push_back(parameter("arpVelDecay", "Arp Velocity Decay", {0.0f, 1.0f}, 0.15f, asPercent));
    result.push_back(parameter("arpVelTarget", "Arp Velocity Target", {0.0f, 1.0f}, 0.0f, asPercent));

    return {result.begin(), result.end()};
}
}
