#include "ForgeProcessor.h"
#include "ForgeEditor.h"

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
//
// Within a version, presets are still reconciled against the parameters that
// exist when they are opened — see Processor::migrated. That is what keeps a
// preset saved at one milestone working at the next, while Forge's controls
// are still moving.
constexpr int presetFormatVersion = 2;

// A table travels as one child node holding every frame end to end. The
// samples are deflated and then base64'd, because a ValueTree is written out as
// XML and eighty kilobytes of raw float is not text.
constexpr const char* tableNodeType = "TABLE";

juce::ValueTree tableNode(const WavetableEdit& edit, int oscillator)
{
    juce::ValueTree node(tableNodeType);
    node.setProperty("osc", oscillator, nullptr);
    node.setProperty("frames", edit.frameCount(), nullptr);
    node.setProperty("name", edit.title(), nullptr);

    // Written a float at a time rather than as a block of memory: JUCE's streams
    // are explicitly little-endian, so a preset written on one machine reads the
    // same on another whatever the native order is.
    juce::MemoryOutputStream raw;
    for (const auto sample : edit.samples()) raw.writeFloat(sample);
    juce::MemoryOutputStream packed;
    {
        juce::GZIPCompressorOutputStream deflate(packed, 9);
        deflate.write(raw.getData(), raw.getDataSize());
    }
    node.setProperty("data", packed.getMemoryBlock().toBase64Encoding(), nullptr);
    return node;
}

// Refuses anything it cannot account for exactly. A table whose data does not
// match the frame count it declares is a damaged preset, and loading the part
// of it that parsed would leave an oscillator on a table nobody authored.
bool readTableNode(const juce::ValueTree& node, WavetableEdit& edit)
{
    const auto frames = static_cast<int>(node.getProperty("frames", 0));
    if (frames < 1 || frames > maxEditableFrames) return false;

    juce::MemoryBlock packed;
    if (!packed.fromBase64Encoding(node.getProperty("data").toString())) return false;
    juce::MemoryInputStream source(packed, false);
    juce::GZIPDecompressorInputStream inflate(source);
    juce::MemoryOutputStream raw;
    raw.writeFromInputStream(inflate, -1);

    const auto count = static_cast<size_t>(frames) * wavetableFrameSize;
    if (raw.getDataSize() != count * sizeof(float)) return false;
    std::vector<float> samples(count);
    juce::MemoryInputStream floats(raw.getData(), raw.getDataSize(), false);
    for (auto& sample : samples) sample = floats.readFloat();
    return edit.setFrames(samples.data(), frames, node.getProperty("name", "CUSTOM").toString());
}

// Serum names a wavetable file's frame size in a `clm ` chunk, which JUCE's wav
// reader does not surface, so the RIFF is walked for it directly. Zero means the
// file did not say, and 2048 is then assumed — which is the convention every
// wavetable file that carries no chunk is written to anyway.
int declaredFrameSize(const juce::File& file)
{
    juce::FileInputStream stream(file);
    char header[12] = {};
    if (!stream.openedOk() || stream.read(header, 12) != 12) return 0;
    if (juce::String(header, 4) != "RIFF" || juce::String(header + 8, 4) != "WAVE") return 0;

    while (!stream.isExhausted())
    {
        char id[4] = {};
        if (stream.read(id, 4) != 4) return 0;
        const auto size = stream.readInt();
        if (size < 0) return 0;
        if (juce::String(id, 4) == "clm ")
        {
            juce::MemoryBlock block;
            stream.readIntoMemoryBlock(block, size);
            // "<!>2048 00000000 wavetable" — the size is the digits after the
            // marker, and getIntValue stops at the first character that is not
            // one of them.
            return block.toString().fromFirstOccurrenceOf("<!>", false, false).getIntValue();
        }
        // Chunks are padded to an even length, and the pad byte is not counted
        // in the size, so skipping by the size alone drifts one byte per odd
        // chunk and every chunk after it is read as garbage.
        stream.setPosition(stream.getPosition() + size + (size & 1));
    }
    return 0;
}

juce::ValueTree parameterEntry(const juce::ValueTree& tree, const juce::String& id)
{
    for (const auto child : tree)
        if (child.getProperty("id").toString() == id)
            return child;
    return {};
}

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
    // Three racks of four slots, and every slot declares the same twelve
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
    }
    return {result.begin(), result.end()};
}

Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "RhinoForgeState", parameterLayout())
{
}

void Processor::prepareToPlay(double sampleRate, int)
{
    core.initialise(sampleRate);
    // Hand the engine each destination's range so modulation happens in the
    // same normalised space the knob moves in. Taken from the parameters
    // themselves, so there is only ever one definition of a range.
    for (int i = 1; i < destinationCount; ++i)
        if (const auto* parameter = state.getParameter(destinations()[static_cast<size_t>(i)].id))
            core.setDestinationRange(i, parameter->getNormalisableRange());
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

// The rate an LFO actually runs at: the rate knob when it is set in Hertz, and a
// division of the host's tempo when it is set in beats. Worked out rather than
// published, because it depends only on parameters and the tempo, both of which
// the message thread can read for itself — so the panel and the voice cannot end
// up quoting different rates.
float Processor::lfoRateHz(int lfo) const
{
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    const auto id = [lfo] (const char* suffix) { return lfoParameterId(lfo, suffix); };
    if (value(id("RateUnit")) < 0.5f) return value(id("Rate"));

    const auto beats = lfoDivisions()[static_cast<size_t>(
        juce::jlimit(0, lfoDivisionCount - 1, juce::roundToInt(value(id("Division")))))].beats;
    const auto bpm = hostBpm.load(std::memory_order_relaxed);
    // Standing in for a host that reports no tempo, so a synced LFO still runs
    // at a musical rate in a standalone rather than stopping dead.
    const auto tempo = bpm > 0.0 ? bpm : 120.0;
    return static_cast<float>(tempo / 60.0) / juce::jmax(0.0001f, beats);
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // Everything that reads a published table happens inside this bracket, which
    // is what lets the message thread tell when a replaced table has stopped
    // being read. See ForgeTableStore.h.
    const WavetableStore::ScopedBlock block(tables);
    buffer.clear();
    // Read before the patch is built, because the patch resolves a synced LFO
    // against it.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                hostBpm.store(*bpm, std::memory_order_relaxed);

    // Anything played on the editor's keyboard joins the host's own notes
    // before a single sample is rendered.
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    // The racks divide the tempo themselves: a synced LFO's rate is resolved
    // into Hertz before the patch is built, but a delay's division has to be
    // read against the tempo at the moment it is rendered.
    core.setTempo(hostBpm.load(std::memory_order_relaxed));
    const auto values = patch();
    const auto mods = modulation();
    auto event = midi.cbegin();
    const auto end = midi.cend();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (event != end)
        {
            const auto metadata = *event;
            if (metadata.samplePosition > i) break;
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) core.noteOn(message.getNoteNumber(), message.getFloatVelocity(), values);
            else if (message.isNoteOff()) core.noteOff(message.getNoteNumber());
            else if (message.isAllNotesOff()) core.allNotesOff();
            ++event;
        }
        float left, right; core.renderSample(values, mods, left, right);
        buffer.addSample(0, i, left);
        if (buffer.getNumChannels() > 1) buffer.addSample(1, i, right);
    }

    // Published once per block rather than per sample: the display redraws at
    // 24 Hz, so a per-sample store would be pure contention for no extra detail.
    for (int env = 0; env < envCount; ++env)
    {
        meterLevel[static_cast<size_t>(env)].store(core.envelopeLevel(env), std::memory_order_relaxed);
        meterStage[static_cast<size_t>(env)].store(core.envelopeStage(env), std::memory_order_relaxed);
    }
    for (int lfo = 0; lfo < lfoCount; ++lfo)
    {
        meterLfoPhase[static_cast<size_t>(lfo)].store(core.lfoPosition(lfo), std::memory_order_relaxed);
        meterLfoValue[static_cast<size_t>(lfo)].store(core.lfoOutput(lfo), std::memory_order_relaxed);
    }
    for (int destination = 1; destination < destinationCount; ++destination)
        meterOffsets[static_cast<size_t>(destination)]
            .store(core.modulationOffset(destination), std::memory_order_relaxed);
}

const FxTypeInfo& Processor::fxSlotType(int rack, int slot) const
{
    return fxTypeInfo(state.getRawParameterValue(fxParameterId(rack, slot, "Type"))->load());
}

// A slot's knobs are plain 0..1 and mean whatever the type in that slot says
// they mean, so this is where a number becomes a reading. It is the same
// arithmetic the DSP runs — taken from the same helpers in ForgeFx.h rather
// than copied — which is what binds "480 ms" in the bubble to the delay that is
// actually sounding.
juce::String Processor::fxKnobText(int rack, int slot, int knob, float value) const
{
    const auto asMilliseconds = [] (float seconds)
    {
        return seconds < 1.0f ? juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms"
                              : juce::String(seconds, 2) + " s";
    };
    const auto hertz = [] (float hz)
    {
        return hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz"
                             : juce::String(juce::roundToInt(hz)) + " Hz";
    };
    const auto percent = [value] { return juce::String(juce::roundToInt(value * 100.0f)) + " %"; };

    const auto* id = fxSlotType(rack, slot).knobs[static_cast<size_t>(knob)];
    if (id == nullptr) return "-";

    const auto read = [this, rack, slot] (int which)
    {
        return state.getRawParameterValue(fxParameterId(rack, slot, "Knob")
                                          + juce::String(which + 1))->load();
    };
    juce::ignoreUnused(read);

    switch (fxTypeOf(state.getRawParameterValue(fxParameterId(rack, slot, "Type"))->load()))
    {
        case FxType::reverb:
            if (knob == 4) return asMilliseconds(fxScaled(value, 0.0f, 0.2f));
            if (knob == 5) return hertz(fxHertz(value, 20.0f, 1200.0f));
            return percent();

        case FxType::delay:
        {
            if (knob == 0)
            {
                const auto synced = fxModeOf(fxTypes()[static_cast<size_t>(FxType::delay)].modeB,
                                             state.getRawParameterValue(
                                                 fxParameterId(rack, slot, "ModeB"))->load()) == 1;
                if (synced) return juce::String(fxDivisionAt(value).label);
                return asMilliseconds(fxScaled(value, 0.01f, fxMaxDelayTime, 2.0f));
            }
            if (knob == 1)
            {
                // The two ratios a delay is actually set to are named where the
                // knob lands on them, as Serum's own offset does.
                const auto ratio = fxOffsetRatio(value);
                if (std::abs(ratio - 1.5f) < 0.02f) return juce::String("DOT");
                if (std::abs(ratio - 4.0f / 3.0f) < 0.02f) return juce::String("TRIP");
                return juce::String(ratio, 2) + " x";
            }
            if (knob == 3) return hertz(fxHertz(value, 200.0f, 16000.0f));
            return percent();
        }

        case FxType::chorus:
        {
            if (knob == 0)
            {
                const auto synced = fxModeOf(fxTypes()[static_cast<size_t>(FxType::chorus)].modeA,
                                             state.getRawParameterValue(
                                                 fxParameterId(rack, slot, "ModeA"))->load()) == 1;
                if (synced) return juce::String(fxDivisionAt(value).label);
                return juce::String(fxScaled(value, 0.02f, 8.0f, 2.0f), 2) + " Hz";
            }
            if (knob == 1 || knob == 2) return asMilliseconds(fxScaled(value, 0.5f, 30.0f) * 0.001f);
            if (knob == 3) return asMilliseconds(fxScaled(value, 0.0f, 6.0f) * 0.001f);
            if (knob == 5) return hertz(fxHertz(value, 120.0f, 16000.0f));
            return percent();
        }

        case FxType::distortion:
            if (knob == 1) return hertz(fxHertz(value, 40.0f, 16000.0f));
            if (knob == 2) return juce::String(fxScaled(value, 0.4f, 8.0f), 2);
            return percent();

        case FxType::equaliser:
            if (knob == 0) return hertz(fxHertz(value, 20.0f, 2000.0f));
            if (knob == 3) return hertz(fxHertz(value, 500.0f, 18000.0f));
            if (knob == 1 || knob == 4) return juce::String(fxScaled(value, 0.2f, 6.0f), 2);
            {
                const auto gain = fxScaled(value, -18.0f, 18.0f);
                return (gain > 0.0f ? "+" : "") + juce::String(gain, 1) + " dB";
            }

        case FxType::filter:
            if (knob == 0) return hertz(fxHertz(value, 30.0f, 18000.0f));
            return percent();

        case FxType::off:
            break;
    }
    return percent();
}

Patch Processor::patch() const
{
    // Assigned by name, not positionally: the parameter list and the Patch
    // layout no longer have to be kept in the same order to stay correct.
    // Nothing is remapped on the way through — the macros that used to bend
    // these values were hardwired offsets, and return as real assignable
    // sources with the modulation matrix.
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    const auto readOscillator = [&value] (const char* prefix)
    {
        const auto id = [prefix] (const char* suffix) { return juce::String(prefix) + suffix; };
        Oscillator osc;
        osc.enable = value(id("Enable"));
        osc.position = value(id("Position"));
        osc.octave = value(id("Octave"));
        osc.semitone = value(id("Semitone"));
        osc.fine = value(id("Fine"));
        osc.unison = value(id("Unison"));
        osc.detune = value(id("Detune"));
        osc.blend = value(id("Blend"));
        osc.pan = value(id("Pan"));
        osc.level = value(id("Level"));
        for (int slot = 0; slot < warpSlots; ++slot)
        {
            const auto stage = juce::String(prefix) + "Warp" + juce::String(slot + 1);
            osc.warpMode[static_cast<size_t>(slot)] = value(stage + "Mode");
            osc.warpAmount[static_cast<size_t>(slot)] = value(stage);
        }
        return osc;
    };

    Patch result;
    result.a = readOscillator("oscA");
    result.b = readOscillator("oscB");
    // Picked up once per block and used for the whole of it, never re-read
    // mid-block: that is the property the hand-over rule depends on.
    result.a.table = tables.table(0);
    result.b.table = tables.table(1);
    result.subEnable = value("subEnable");
    result.subLevel = value("subLevel");
    result.subPan = value("subPan");
    result.noiseEnable = value("noiseEnable");
    result.noiseLevel = value("noiseLevel");
    result.noisePan = value("noisePan");
    result.filterEnable = value("filterEnable");
    result.filterType = value("filterType");
    result.routeA = value("routeA");
    result.routeB = value("routeB");
    result.routeSub = value("routeSub");
    result.routeNoise = value("routeNoise");
    result.cutoff = value("cutoff");
    result.resonance = value("resonance");
    result.drive = value("drive");
    result.filterPan = value("filterPan");
    result.filterMix = value("filterMix");
    result.filterLevel = value("filterLevel");
    const auto readSends = [&value] (const char* prefix)
    {
        Sends sends;
        for (int bus = 0; bus < busCount; ++bus)
            sends.amount[static_cast<size_t>(bus)] =
                value(juce::String(prefix) + "Send" + juce::String(bus + 1));
        return sends;
    };
    result.sendA = readSends("oscA");
    result.sendB = readSends("oscB");
    result.sendSub = readSends("sub");
    result.sendNoise = readSends("noise");
    result.sendFilter = readSends("filter");
    for (int bus = 0; bus < busCount; ++bus)
    {
        const auto id = [bus] (const char* suffix)
        {
            return "bus" + juce::String(bus + 1) + suffix;
        };
        auto& settings = result.buses[static_cast<size_t>(bus)];
        settings.enable = value(id("Enable"));
        settings.dest = value(id("Dest"));
        settings.pan = value(id("Pan"));
        settings.level = value(id("Level"));
    }
    for (int env = 0; env < envCount; ++env)
    {
        auto& shape = result.envs[static_cast<size_t>(env)];
        shape.attack = value(envParameterId(env, "Attack"));
        shape.decay = value(envParameterId(env, "Decay"));
        shape.sustain = value(envParameterId(env, "Sustain"));
        shape.release = value(envParameterId(env, "Release"));
    }
    for (int lfo = 0; lfo < lfoCount; ++lfo)
    {
        auto& setting = result.lfos[static_cast<size_t>(lfo)];
        setting.rate = lfoRateHz(lfo);
        setting.shape = value(lfoParameterId(lfo, "Shape"));
        setting.mode = value(lfoParameterId(lfo, "Mode"));
    }
    for (int rack = 0; rack < rackCount; ++rack)
    {
        auto& held = result.racks[static_cast<size_t>(rack)];
        held.bypass = value(fxRackParameterId(rack, "Bypass"));
        for (int slot = 0; slot < fxSlotCount; ++slot)
        {
            const auto id = [rack, slot] (const char* suffix)
            {
                return fxParameterId(rack, slot, suffix);
            };
            auto& settings = held.slots[static_cast<size_t>(slot)];
            settings.type = value(id("Type"));
            settings.modeA = value(id("ModeA"));
            settings.modeB = value(id("ModeB"));
            settings.bypass = value(id("Bypass"));
            for (int knob = 0; knob < fxKnobCount; ++knob)
                settings.knobs[static_cast<size_t>(knob)] =
                    value(id("Knob") + juce::String(knob + 1));
            settings.mix = value(id("Mix"));
            settings.level = value(id("Level"));
        }
    }
    result.polyphony = value("polyphony");
    result.mono = value("mono");
    result.legato = value("legato");
    result.glide = value("glide");
    result.output = value("output");
    for (int macro = 0; macro < macroCount; ++macro)
        result.macros[static_cast<size_t>(macro)] = value("macro" + juce::String(macro + 1));
    return result;
}

Modulation Processor::modulation() const
{
    const auto value = [this] (const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    Modulation result;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto id = [slot] (const char* suffix) { return "mod" + juce::String(slot + 1) + suffix; };
        result.slots[static_cast<size_t>(slot)] = {value(id("Source")), value(id("Dest")), value(id("Depth"))};
    }
    return result;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree = state.copyState();
    appendTables(tree);
    if (const auto xml = tree.createXml())
        copyXmlToBinary(*xml, destination);
}

void Processor::setStateInformation(const void* data, int size)
{
    // Host state gets the same reconciliation as a preset file, so a project
    // saved against an older Forge opens without carrying retired parameters
    // or leaving newer ones unset.
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
        {
            const auto saved = juce::ValueTree::fromXml(*xml);
            // Read before migrated() strips the table nodes back out, so the
            // live parameter state stays parameters only.
            applyTables(saved);
            state.replaceState(migrated(saved));
        }
}

juce::Result Processor::savePreset(const juce::File& destination, const juce::String& name)
{
    if (destination == juce::File {}) return juce::Result::fail("Choose a preset file first.");
    juce::ValueTree preset("RhinoForgePreset");
    preset.setProperty("formatVersion", presetFormatVersion, nullptr);
    preset.setProperty("name", name.trim().isNotEmpty() ? name.trim()
                                                        : destination.getFileNameWithoutExtension(), nullptr);
    auto tree = state.copyState();
    appendTables(tree);
    preset.addChild(tree, -1, nullptr);
    const auto xml = preset.createXml();
    if (xml == nullptr) return juce::Result::fail("Forge could not create the preset data.");

    juce::TemporaryFile temporary(destination);
    if (!xml->writeTo(temporary.getFile(), {}))
        return juce::Result::fail("Forge could not write the preset file.");
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Forge could not replace the preset file.");
    return juce::Result::ok();
}

juce::Result Processor::loadPreset(const juce::File& source)
{
    const auto xml = juce::XmlDocument::parse(source);
    if (xml == nullptr) return juce::Result::fail("That file is not readable XML.");
    const auto preset = juce::ValueTree::fromXml(*xml);
    if (!preset.hasType("RhinoForgePreset"))
        return juce::Result::fail("That file is not a Rhino Forge preset.");
    const auto version = static_cast<int>(preset.getProperty("formatVersion", 0));
    if (version != presetFormatVersion)
        return juce::Result::fail("This preset was saved by a different version of Forge.");
    const auto savedState = preset.getChildWithName(state.state.getType());
    if (!savedState.isValid())
        return juce::Result::fail("The preset does not contain Forge parameter state.");
    applyTables(savedState);
    state.replaceState(migrated(savedState));
    return juce::Result::ok();
}

// Reconcile a preset of any supported version with the parameters that exist
// now: drop entries Forge has retired, and fill in entries it has since gained
// with their defaults. replaceState only touches parameters the tree mentions,
// so without the second half an old preset would leave newer controls holding
// the previous patch's values.
juce::ValueTree Processor::migrated(const juce::ValueTree& savedState) const
{
    auto result = savedState.createCopy();

    // Each modulator that arrived as a bank did the same two things to a state
    // written before it: the one that already existed was renamed for its place
    // in the bank, and the rest were inserted into the middle of the source
    // list, moving everything past them. A dropped parameter loads at its
    // default, which is harmless; a parameter or a source index that quietly
    // means something different is not, so both are put right here rather than
    // left to the reconciliation below.
    //
    // Everything a slot's Source sat past a run moved up by the rest of that
    // run. `inserted` is that shift, in the numbering the state is in when it is
    // applied — so these run oldest first, each handing the next a state written
    // as if its own change had always been there.
    const auto inserted = [&result] (int after, int howMany)
    {
        for (int slot = 1; slot <= modSlotCount; ++slot)
        {
            auto entry = parameterEntry(result, "mod" + juce::String(slot) + "Source");
            if (!entry.isValid()) continue;
            const auto was = juce::roundToInt(static_cast<float>(entry.getProperty("value")));
            if (was > after) entry.setProperty("value", static_cast<float>(was + howMany), nullptr);
        }
    };
    const auto renamed = [&result] (const std::vector<std::pair<const char*, const char*>>& pairs)
    {
        for (auto child : result)
        {
            const auto id = child.getProperty("id").toString();
            for (const auto& [was, now] : pairs)
                if (id == was) child.setProperty("id", now, nullptr);
        }
    };

    // What a state is old enough to predate, read before anything is renamed,
    // because renaming is what clears these marks — which is also what makes
    // the whole of this safe to run twice.
    //
    // The one LFO's parameters were named for the only LFO there was, so
    // `lfoShape` says a state predates the other five; the amp envelope's
    // carried no number for the same reason, so `attack` says one predates
    // ENV 2-4. Either mark is enough on its own, and the older one implies the
    // newer: a state from before the LFOs came in banks is from before the
    // envelopes did too, whether or not it happens to name an envelope at all.
    const auto predatesLfoBanks = parameterEntry(result, "lfoShape").isValid();
    const auto predatesEnvBanks = predatesLfoBanks || parameterEntry(result, "attack").isValid();
    // The mixer gave SUB and NOISE a pan, so a state that names neither is one
    // written before those two were panned — and therefore one whose levels are
    // in the old, 3 dB hotter scale. `subPan` is added below like any other
    // missing parameter, which is what clears this mark and makes running the
    // whole of this twice safe.
    const auto predatesMixer = !parameterEntry(result, "subPan").isValid();

    // LFO 2-6.
    if (predatesLfoBanks)
    {
        renamed({{"lfoShape", "lfo1Shape"}, {"lfoMode", "lfo1Mode"}, {"lfoRate", "lfo1Rate"},
                 {"lfoRateUnit", "lfo1RateUnit"}, {"lfoDivision", "lfo1Division"}});
        // LFO 1 sat at 2 in the list as it was numbered then, straight after
        // ENV 1, and five LFOs went in behind it. Written out rather than taken
        // from ModSource::lfo1, because that constant says where LFO 1 is now —
        // it has itself moved since, and reading it here would silently shift
        // this by however far the list has travelled afterwards.
        inserted(2, lfoCount - 1);
    }

    // ENV 2-4, which moved everything past ENV 1 again.
    if (predatesEnvBanks)
    {
        renamed({{"attack", "env1Attack"}, {"decay", "env1Decay"},
                 {"sustain", "env1Sustain"}, {"release", "env1Release"}});
        // ENV 1 has always been 1 and does not move; the three behind it push
        // everything else along. Anything already remapped by the LFO step is in
        // the numbering this step expects, which is why that one runs first.
        inserted(static_cast<int>(ModSource::env1), envCount - 1);
    }

    // SUB and NOISE used to be summed into both channels at full amplitude and
    // are now panned at equal power, which costs them 3 dB at centre. Their
    // saved levels are raised by exactly that, so a patch written before the
    // mixer sounds as it did. A level already at the top of its range cannot be
    // raised and is left there — the only patches this changes are ones whose
    // sub or noise was already at maximum.
    if (predatesMixer)
        for (const auto* id : {"subLevel", "noiseLevel"})
        {
            auto entry = parameterEntry(result, id);
            if (!entry.isValid()) continue;
            const auto was = static_cast<float>(entry.getProperty("value"));
            entry.setProperty("value", juce::jlimit(0.0f, 1.0f, was * juce::MathConstants<float>::sqrt2),
                              nullptr);
        }

    for (int i = result.getNumChildren(); --i >= 0;)
    {
        // A table is data rather than a parameter and is applied separately, so
        // it is taken out here and never reaches the parameter state.
        if (result.getChild(i).hasType(tableNodeType)) { result.removeChild(i, nullptr); continue; }
        const auto id = result.getChild(i).getProperty("id").toString();
        if (id.isNotEmpty() && state.getParameter(id) == nullptr)
            result.removeChild(i, nullptr);
    }
    for (auto* raw : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(raw);
        if (ranged == nullptr || parameterEntry(result, ranged->paramID).isValid()) continue;
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", ranged->paramID, nullptr);
        entry.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
        result.addChild(entry, -1, nullptr);
    }
    return result;
}

void Processor::appendTables(juce::ValueTree& tree) const
{
    for (int osc = 0; osc < oscillatorCount; ++osc)
    {
        // A table nobody has touched is the built-in ten, which every copy of
        // Forge already has. Writing it out would put eighty kilobytes of
        // base64 into a preset to say "unchanged".
        if (tables.edit(osc).isUntouched()) continue;
        tree.addChild(tableNode(tables.edit(osc), osc), -1, nullptr);
    }
}

void Processor::applyTables(const juce::ValueTree& tree)
{
    for (int osc = 0; osc < oscillatorCount; ++osc)
    {
        juce::ValueTree found;
        for (const auto child : tree)
            if (child.hasType(tableNodeType) && static_cast<int>(child.getProperty("osc", -1)) == osc)
                found = child;
        if (found.isValid() && readTableNode(found, tables.edit(osc))) tables.publish(osc);
        else tables.resetToBuiltIn(osc);
    }
}

// A wavetable file is an ordinary audio file holding single-cycle frames end to
// end. Nothing here tries to find cycles in arbitrary recorded audio: that is a
// guess, it is wrong often, and it is its own problem.
juce::Result Processor::importTable(int oscillator, const juce::File& file)
{
    if (!file.existsAsFile()) return juce::Result::fail("that file is not there.");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return juce::Result::fail("Forge cannot read that kind of file.");

    auto sourceFrameSize = declaredFrameSize(file);
    if (sourceFrameSize < 4) sourceFrameSize = wavetableFrameSize;
    const auto available = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, 1 << 22));
    const auto sourceFrames = available / sourceFrameSize;
    if (sourceFrames < 1)
        return juce::Result::fail("that file is shorter than one " + juce::String(sourceFrameSize)
                                  + "-sample frame.");

    // Channel 0 only. A wavetable file is mono by convention, and averaging the
    // channels of one that is not would blur two different tables together.
    juce::AudioBuffer<float> source(1, sourceFrames * sourceFrameSize);
    source.clear();
    if (!reader->read(&source, 0, source.getNumSamples(), 0, true, false))
        return juce::Result::fail("Forge could not read that file's samples.");

    // A table larger than the ceiling is thinned evenly rather than cut short,
    // so a 256-frame table still sweeps from its first shape to its last.
    const auto frames = juce::jmin(sourceFrames, maxEditableFrames);
    std::vector<float> samples(static_cast<size_t>(frames) * wavetableFrameSize);
    const auto* read = source.getReadPointer(0);
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto pick = frames == 1 ? 0
            : juce::jlimit(0, sourceFrames - 1,
                           juce::roundToInt(static_cast<double>(frame) * (sourceFrames - 1)
                                            / static_cast<double>(frames - 1)));
        const auto* from = read + static_cast<size_t>(pick) * sourceFrameSize;
        auto* into = samples.data() + static_cast<size_t>(frame) * wavetableFrameSize;
        if (sourceFrameSize == wavetableFrameSize)
        {
            std::copy_n(from, wavetableFrameSize, into);
            continue;
        }
        // A frame written at some other size is resampled onto Forge's, with the
        // same spline the oscillator reads a frame with, so a table stored at
        // 256 points sits beside a built-in one without a step in it.
        for (int i = 0; i < wavetableFrameSize; ++i)
            into[i] = wavetableInterpolate(from, sourceFrameSize,
                                           static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
    }

    if (!tables.edit(oscillator).setFrames(samples.data(), frames,
                                           file.getFileNameWithoutExtension().toUpperCase()))
        return juce::Result::fail("Forge could not build a table from that file.");
    tables.publish(oscillator);
    return juce::Result::ok();
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rhino::forge::Processor();
}
