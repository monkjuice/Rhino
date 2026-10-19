// Opening another synth's preset: a Vital preset written here from scratch,
// converted, and checked setting by setting against what it says in Vital's own
// units. Nothing reads a preset installed on the machine, so the suite means
// the same thing everywhere.
#include "ForgeSuites.h"
#include "ForgeTestSupport.h"
#include "../src/ForgePresetImport.h"
#include <cstring>

namespace rhino::forge::tests
{
namespace
{
juce::String base64Floats(const std::vector<float>& values)
{
    juce::MemoryOutputStream raw;
    for (const auto value : values) raw.writeFloat(value);
    return juce::Base64::toBase64(raw.getData(), raw.getDataSize());
}

std::vector<float> cycle(bool saw)
{
    std::vector<float> values(static_cast<size_t>(wavetableFrameSize));
    for (int i = 0; i < wavetableFrameSize; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(wavetableFrameSize);
        values[static_cast<size_t>(i)] = saw ? 2.0f * phase - 1.0f
                                             : std::sin(phase * juce::MathConstants<float>::twoPi);
    }
    return values;
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> members)
{
    auto* made = new juce::DynamicObject();
    for (const auto& [name, value] : members) made->setProperty(name, value);
    return juce::var(made);
}

juce::var keyframe(int position, bool saw)
{
    return object({{"position", position}, {"wave_data", base64Floats(cycle(saw))}});
}

juce::var waveSource(juce::Array<juce::var> keyframes)
{
    return object({{"type", "Wave Source"}, {"interpolation", 0}, {"interpolation_style", 1},
                   {"keyframes", keyframes}});
}

juce::var table(const char* name, juce::Array<juce::var> components)
{
    juce::Array<juce::var> groups {object({{"components", components}})};
    return object({{"name", name}, {"remove_all_dc", false}, {"full_normalize", false}, {"groups", groups}});
}

juce::var route(const char* source, const char* destination)
{
    return object({{"source", source}, {"destination", destination}});
}

// A Vital preset with one of everything the conversion has a rule for, and a
// few things it has to refuse: a second filter, a random source, a flanger.
juce::var testPreset()
{
    auto* settings = new juce::DynamicObject();
    const auto set = [settings] (const char* key, juce::var value) { settings->setProperty(key, value); };
    set("osc_1_on", 1.0);
    set("osc_1_level", 0.8);
    set("osc_1_wave_frame", 128.0);
    set("osc_1_transpose", -12.0);
    set("osc_1_tune", 0.25);
    set("osc_1_unison_voices", 3.0);
    set("osc_1_unison_detune", 5.0);
    set("osc_1_detune_range", 2.0);
    set("osc_1_distortion_type", 1.0);
    set("osc_1_distortion_amount", 0.75);
    set("osc_1_destination", 0.0);
    set("osc_2_on", 0.0);
    set("osc_3_on", 0.0);
    set("filter_1_on", 1.0);
    set("filter_1_model", 0.0);
    set("filter_1_style", 0.0);
    set("filter_1_blend", 0.0);
    set("filter_1_cutoff", 60.0);
    set("filter_1_resonance", 0.3);
    set("filter_2_on", 1.0);
    set("filter_2_model", 6.0);
    set("env_1_attack", 0.5);
    set("env_1_decay", 1.0);
    set("env_1_sustain", 0.5);
    set("env_1_release", 1.0);
    set("env_2_attack", 0.5);
    set("env_2_release", 2.0);
    set("lfo_1_sync", 0.0);
    set("lfo_1_frequency", 1.0);
    set("lfo_1_sync_type", 0.0);
    set("macro_control_1", 0.25);
    set("polyphony", 4.0);
    set("volume", 5473.04);
    set("flanger_on", 1.0);
    set("reverb_on", 1.0);
    set("reverb_dry_wet", 0.4);
    set("effect_chain_order", 0.0);
    // LFO 1 opens the cutoff by half its range, unipolar; a random source
    // reaches the level; ENV 2's reach into the pan is nothing until
    // velocity scales it, Vital's way of making a level velocity sensitive.
    set("modulation_1_amount", 0.5);
    set("modulation_2_amount", 0.4);
    set("modulation_3_amount", 0.0);
    set("modulation_4_amount", 0.5);
    juce::Array<juce::var> routes {route("lfo_1", "filter_1_cutoff"), route("random_1", "osc_1_level"),
                                   route("env_2", "osc_1_pan"), route("velocity", "modulation_3_amount")};
    for (int i = routes.size(); i < 64; ++i) routes.add(route("", ""));
    set("modulations", routes);
    set("wavetables", juce::Array<juce::var> {table("TEST TABLE", {waveSource({keyframe(0, false), keyframe(256, true)})}),
                                              table("B", {waveSource({keyframe(0, false)})}),
                                              table("C", {waveSource({keyframe(0, false)})})});
    juce::Array<juce::var> lfos;
    for (int i = 0; i < 8; ++i)
        lfos.add(object({{"name", "Triangle"}, {"num_points", 3},
                         {"points", juce::Array<juce::var> {0.0, 1.0, 0.5, 0.0, 1.0, 1.0}},
                         {"powers", juce::Array<juce::var> {0.0, 0.0, 0.0}}, {"smooth", false}}));
    set("lfos", lfos);

    return object({{"author", "Tester"}, {"comments", "A note about it"}, {"preset_style", "Bass"},
                   {"macro1", "Wobble"}, {"macro2", "MACRO 2"}, {"synth_version", "1.0.0"},
                   {"settings", juce::var(settings)}});
}

int slotFor(const Processor& processor, int source, const char* destination)
{
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto at = juce::roundToInt(value(processor, modParameterId(slot, "Source").toRawUTF8()));
        const auto to = juce::roundToInt(value(processor, modParameterId(slot, "Dest").toRawUTF8()));
        if (at == source && juce::String(destinations()[static_cast<size_t>(to)].id) == destination) return slot;
    }
    return -1;
}

bool warned(const PresetInfo& info, const char* about)
{
    for (const auto& line : info.warnings)
        if (line.contains(about)) return true;
    return false;
}

void vitalSuite()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-import-test", {}, true);
    require(folder.createDirectory(), "a scratch folder for the converted presets");
    const auto file = folder.getChildFile("Test Patch.vital");
    require(file.replaceWithText(juce::JSON::toString(testPreset())), "the Vital preset is written");

    const auto entry = readPresetEntry(file);
    requireText(entry.author, "Tester", "the browser reads a Vital preset's author from the head of the file");
    requireText(entry.style, "Bass", "and its style");

    Processor processor;
    require(processor.loadPreset(file).wasOk(), "a Vital preset opens");
    const auto info = processor.presetInfo();
    requireText(info.name, "Test Patch", "a converted preset is named after its file");
    requireText(info.source, "Vital", "a converted preset says where it came from");
    requireText(info.author, "Tester", "Vital's author survives");
    requireText(info.style, "Bass", "Vital's style is Forge's style");
    requireText(info.notes, "A note about it", "Vital's comments are the notes");
    requireText(processor.macroName(0), "WOBBLE", "a macro's name crosses");
    require(processor.macroName(1).isEmpty(), "a macro still called MACRO 2 stays unnamed");

    require(value(processor, "oscAEnable") > 0.5f && value(processor, "oscBEnable") < 0.5f,
            "the oscillators Vital plays are the ones Forge plays");
    require(value(processor, "subEnable") < 0.5f, "Vital has no sub, so Forge's is off");
    requireClose(value(processor, "oscALevel"), 0.96f, 0.001f, "a level is Vital's amplitude, kept to Forge's scale");
    requireClose(value(processor, "oscAOctave"), -1.0f, 0.001f, "twelve semitones down is an octave");
    requireClose(value(processor, "oscASemitone"), 0.0f, 0.001f, "and no semitones over it");
    requireClose(value(processor, "oscAFine"), 25.0f, 0.5f, "a quarter semitone of tune is 25 cents");
    requireClose(value(processor, "oscAUnison"), 3.0f, 0.001f, "the unison voices cross");
    requireClose(value(processor, "oscADetune"), 2.0f * 25.0f / 100.0f / 0.7f, 0.01f,
                 "the outer voices are detuned as far as Vital's");
    requireClose(value(processor, "oscAPosition"), 0.5f, 0.001f, "frame 128 of 256 is halfway through the table");
    require(juce::roundToInt(value(processor, "oscAWarp1Mode")) == static_cast<int>(WarpMode::sync),
            "Vital's sync is Forge's sync");
    requireClose(value(processor, "oscAWarp1"), 3.0f / 7.0f, 0.01f, "a sync to four times the note");
    require(processor.tableStore().frameCount(0) == maxEditableFrames,
            "a 257-position table is sampled to the most frames Forge holds");

    require(juce::roundToInt(value(processor, "filterType")) == static_cast<int>(FilterType::lowPass),
            "an analog 12 dB low pass is Forge's LOW");
    // LFO 1 rises from the cutoff by half the range; Forge's LFOs swing both
    // ways, so the cutoff moves up by a quarter of it and the slot swings a
    // quarter either side: note 60 + 32 is about 1.66 kHz.
    requireClose(value(processor, "cutoff"), 8.1757989f * std::pow(2.0f, 92.0f / 12.0f), 5.0f,
                 "the cutoff sits in the middle of what the LFO reaches");
    const auto lfoSlot = slotFor(processor, srcLfo1, "cutoff");
    require(lfoSlot >= 0, "LFO 1 reaches the cutoff");
    if (lfoSlot >= 0)
        require(value(processor, modParameterId(lfoSlot, "Depth").toRawUTF8()) > 0.05f, "and opens it");
    requireClose(value(processor, "env1Attack"), 0.0625f, 0.001f, "an envelope time is the fourth power");
    requireClose(value(processor, "env1Sustain"), 0.25f, 0.001f,
                 "Vital squares its amplitude envelope, so its sustain is squared here");
    requireClose(value(processor, "lfo1Rate"), 2.0f, 0.01f, "a rate stored as 1 is 2 Hz");
    require(juce::roundToInt(value(processor, "lfo1Shape")) == static_cast<int>(LfoShape::triangle),
            "Vital's triangle is Forge's TRI");
    requireClose(value(processor, "polyphony"), 4.0f, 0.001f, "the polyphony crosses");
    // Vital's 16-second release is twice Forge's longest.
    requireClose(value(processor, envParameterId(1, "Release").toRawUTF8()), 8.0f, 0.001f,
                 "a release past Forge's longest is held at it");
    require(warned(info, "ENV 2 RELEASE: past Forge's maximum, set to 8"),
            "and the warning says which end it is held at");

    const auto envSlot = slotFor(processor, srcEnv1 + 1, "oscAPan");
    require(envSlot >= 0, "an envelope whose reach velocity scales still reaches the pan");
    if (envSlot >= 0)
        require(value(processor, modParameterId(envSlot, "Depth").toRawUTF8()) > 0.05f,
                "at the depth a firm hit gives it");
    require(warned(info, "VELOCITY"), "and the scaling is said to be fixed");

    require(juce::roundToInt(value(processor, fxParameterId(0, 0, "Type").toRawUTF8()))
                == static_cast<int>(FxType::reverb),
            "the reverb takes the first slot; the flanger has none");
    requireClose(value(processor, fxParameterId(0, 0, "Mix").toRawUTF8()), 0.4f, 0.001f, "at its own wet/dry");

    require(warned(info, "FILTER 2"), "the second filter is said to be left out");
    require(warned(info, "RANDOM 1"), "a random source is said to be left out");
    require(warned(info, "FLANGER"), "the flanger is said to be left out");

    // A conversion copied into the bank keeps what it lost.
    const auto copy = folder.getChildFile("Copied.forgepreset");
    require(processor.convertPreset(file, copy).wasOk(), "a Vital preset converts into a Forge preset file");
    Processor reopened;
    require(reopened.loadPreset(copy).wasOk(), "and that file opens as a Forge preset");
    require(warned(reopened.presetInfo(), "FLANGER"), "still saying what the conversion lost");
    requireText(reopened.presetInfo().source, "Vital", "and where it came from");

    // A file that is not a preset changes nothing.
    const auto broken = folder.getChildFile("Broken.vital");
    broken.replaceWithText("{ not json");
    setValue(processor, "cutoff", 1234.0f);
    require(processor.loadPreset(broken).failed(), "a damaged Vital preset is refused");
    requireClose(value(processor, "cutoff"), 1234.0f, 0.5f, "and the patch playing is left alone");
    folder.deleteRecursively();
}
}

namespace
{
// --- Serum 2 ------------------------------------------------------------------
//
// A .SerumPreset is a JSON header over a zstd frame of CBOR. The suite writes
// one itself: the CBOR by hand, and the frame as zstd's stored blocks, which
// every decoder reads and which need no compressor to make.

class Cbor
{
public:
    juce::MemoryOutputStream bytes;

    void head(int major, juce::uint64 value)
    {
        const auto type = static_cast<juce::uint8>(major << 5);
        if (value < 24) bytes.writeByte(static_cast<char>(type | value));
        else if (value < 256) { bytes.writeByte(static_cast<char>(type | 24)); bytes.writeByte(static_cast<char>(value)); }
        else if (value < 65536) { bytes.writeByte(static_cast<char>(type | 25)); bytes.writeShortBigEndian(static_cast<short>(value)); }
        else { bytes.writeByte(static_cast<char>(type | 26)); bytes.writeIntBigEndian(static_cast<int>(value)); }
    }

    void write(const juce::var& value)
    {
        if (value.isString())
        {
            const auto text = value.toString();
            head(3, static_cast<juce::uint64>(text.getNumBytesAsUTF8()));
            bytes.write(text.toRawUTF8(), text.getNumBytesAsUTF8());
        }
        else if (value.isArray())
        {
            head(4, static_cast<juce::uint64>(value.getArray()->size()));
            for (const auto& item : *value.getArray()) write(item);
        }
        else if (auto* object = value.getDynamicObject())
        {
            head(5, static_cast<juce::uint64>(object->getProperties().size()));
            for (const auto& property : object->getProperties())
            {
                write(property.name.toString());
                write(property.value);
            }
        }
        else if (value.isInt() || value.isInt64())
        {
            const auto number = static_cast<juce::int64>(value);
            if (number >= 0) head(0, static_cast<juce::uint64>(number));
            else head(1, static_cast<juce::uint64>(-1 - number));
        }
        else
        {
            // Every other number as a float32, as Serum writes most of its own.
            bytes.writeByte(static_cast<char>(0xfa));
            const auto single = static_cast<float>(static_cast<double>(value));
            juce::uint32 bits;
            std::memcpy(&bits, &single, sizeof(bits));
            bytes.writeIntBigEndian(static_cast<int>(bits));
        }
    }
};

juce::MemoryBlock storedZstdFrame(const void* data, size_t size)
{
    juce::MemoryOutputStream frame;
    frame.writeInt(static_cast<int>(0xFD2FB528u));
    frame.writeByte(static_cast<char>(0xA0));  // one segment, a four-byte content size
    frame.writeInt(static_cast<int>(size));
    const auto* at = static_cast<const char*>(data);
    auto left = size;
    do
    {
        const auto block = juce::jmin<size_t>(left, 100000);
        const auto last = block == left ? 1u : 0u;
        const auto header = static_cast<juce::uint32>(last | (block << 3));
        frame.writeByte(static_cast<char>(header & 0xff));
        frame.writeByte(static_cast<char>((header >> 8) & 0xff));
        frame.writeByte(static_cast<char>((header >> 16) & 0xff));
        frame.write(at, block);
        at += block;
        left -= block;
    } while (left > 0);
    return frame.getMemoryBlock();
}

juce::var params(std::initializer_list<std::pair<const char*, juce::var>> members)
{
    return object({{"plainParams", object(members)}});
}

juce::var serumPatch()
{
    juce::Array<juce::var> frames;
    for (const auto saw : {false, true})
        for (const auto sample : cycle(saw)) frames.add(static_cast<double>(sample));
    auto oscillator = params({{"kParamOctave", -1.0}, {"kParamVolume", 0.5}, {"kParamUnison", 3.0}, {"kParamDetune", 0.25}});
    oscillator.getDynamicObject()->setProperty("WTOsc0", object({
        {"plainParams", object({{"kParamTablePos", 128.5}, {"kParamWarpMenu", "kSync"}, {"kParamWarp", 0.4}})},
        {"embeddedWTData", frames}, {"numFrames", 4096}, {"tableDisplayName", "Custom"}}));
    const auto slot = [] (int source, int aux, const char* type, const char* param, double amount)
    {
        return object({{"source", juce::Array<juce::var> {source, aux}}, {"destModuleTypeString", type},
                       {"destModuleID", 0}, {"destModuleParamName", param},
                       {"plainParams", object({{"kParamAmount", amount}})}});
    };
    auto macro = params({{"kParamValue", 25.0}});
    macro.getDynamicObject()->setProperty("name", "Bright");
    return object({
        {"Oscillator0", oscillator},
        {"VoiceFilter0", params({{"kParamEnable", 1.0}, {"kParamType", "LadderMg"}, {"kParamFreq", 0.5}, {"kParamReso", 30.0}})},
        {"Env0", params({{"kParamAttack", 0.01}, {"kParamDecay", 0.5}, {"kParamSustain", 0.6}, {"kParamRelease", 0.3}})},
        {"LFO0", params({{"kParamBeatSync", 0.0}, {"kParamRate", 2.0}})},
        {"LFO1", params({{"kParamType", "Lorenz"}})},
        {"Macro0", macro},
        {"Global0", params({{"kParamMasterVolume", 0.7}, {"kParamMonoToggle", 1.0}})},
        // LFO 1 opens the cutoff by 40% of the knob; the chaos LFO 2 has
        // nowhere to go; velocity's reach into FINE is scaled by macro 1.
        {"ModSlot0", slot(6, 0, "VoiceFilter", "kParamFreq", 40.0)},
        {"ModSlot1", slot(7, 0, "Oscillator", "kParamPan", 50.0)},
        {"ModSlot2", slot(16, 25, "Oscillator", "kParamFine", 50.0)},
        {"FXRack0", object({{"FX", juce::Array<juce::var> {
            object({{"type", 6}, {"FXReverb", params({{"kParamWet", 40.0}, {"kParamSize", 60.0}})}}),
            object({{"type", 1}, {"FXFlanger", params({{"kParamWet", 50.0}})}})}}})},
    });
}

void writeSerumPreset(const juce::File& file, const juce::var& metadata, const juce::var& patch)
{
    Cbor cbor;
    cbor.write(patch);
    const auto frame = storedZstdFrame(cbor.bytes.getData(), cbor.bytes.getDataSize());
    const auto json = juce::JSON::toString(metadata, true);
    juce::MemoryOutputStream out;
    out.write("XferJson", 8);
    out.writeByte(0);
    out.writeInt64(static_cast<juce::int64>(json.getNumBytesAsUTF8()));
    out.write(json.toRawUTF8(), json.getNumBytesAsUTF8());
    out.writeInt(static_cast<int>(cbor.bytes.getDataSize()));
    out.writeInt(2);
    out.write(frame.getData(), frame.getSize());
    file.replaceWithData(out.getData(), out.getDataSize());
}

void serumSuite()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("rhino-forge-serum-test", {}, true);
    require(folder.createDirectory(), "a scratch folder for the Serum preset");
    const auto file = folder.getChildFile("BA - Test Bass.SerumPreset");
    writeSerumPreset(file, object({{"presetName", "BA - Test Bass"}, {"presetAuthor", "Tester"},
                                   {"presetDescription", "A described patch"}, {"fileType", "SerumPreset"}}),
                     serumPatch());

    SerumPresetHeader header;
    require(readSerumPresetHeader(file, header), "a Serum preset's header reads without opening its patch");
    requireText(header.author, "Tester", "with its author");
    requireText(header.style, "Bass", "and a style from its BA prefix");
    // One with no prefix, outside Serum's folder, looks for a style in the
    // folders above it and stops at the drive: this walk once never ended.
    const auto plain = folder.getChildFile("Plain.SerumPreset");
    writeSerumPreset(plain, object({{"presetName", "Plain"}, {"fileType", "SerumPreset"}}), serumPatch());
    SerumPresetHeader plainHeader;
    require(readSerumPresetHeader(plain, plainHeader) && plainHeader.style.isEmpty(),
            "a preset with no prefix outside Serum's folder has no style");

    Processor processor;
    const auto opened = processor.loadPreset(file);
    if (opened.failed()) std::cerr << "  " << opened.getErrorMessage() << '\n';
    require(opened.wasOk(), "a Serum 2 preset opens");
    const auto info = processor.presetInfo();
    requireText(info.source, "Serum 2", "and says it was converted from Serum 2");
    requireText(info.notes, "A described patch", "Serum's description is the notes");
    requireText(processor.macroName(0), "BRIGHT", "a macro's name crosses");
    requireClose(value(processor, "macro1"), 0.25f, 0.001f, "and its position");

    requireClose(value(processor, "oscAOctave"), -1.0f, 0.001f, "OCT crosses");
    requireClose(value(processor, "oscAUnison"), 3.0f, 0.001f, "the unison voices cross");
    requireClose(value(processor, "oscADetune"), 0.25f, 0.001f, "DETUNE crosses as it is");
    requireClose(value(processor, "oscALevel"), 0.5f, 0.001f, "a level is the gain it is stored as");
    requireClose(value(processor, "oscAPosition"), 0.5f, 0.001f, "POSITION is on Serum's fixed scale of 256");
    require(juce::roundToInt(value(processor, "oscAWarp1Mode")) == static_cast<int>(WarpMode::sync), "SYNC is SYNC");
    requireClose(value(processor, "oscAWarp1"), 0.4f, 0.001f, "at the same depth, the two synths' warps being matched");
    require(processor.tableStore().frameCount(0) == 2, "an embedded table brings its own frames");
    require(value(processor, "subEnable") < 0.5f, "Serum's sub is off unless it says otherwise");

    require(juce::roundToInt(value(processor, "filterType")) == static_cast<int>(FilterType::ladder),
            "the MG ladder is Forge's LADDER");
    requireClose(value(processor, "resonance"), 0.3f, 0.001f, "the resonance crosses");
    // LFO 1 rises 40% of the knob from 0.5: Forge's cutoff sits at 0.7 of
    // Serum's knob, 8 Hz times 2756 to the 0.7, and swings either side.
    requireClose(value(processor, "cutoff"), 8.0f * std::pow(22050.0f / 8.0f, 0.7f), 5.0f,
                 "the cutoff sits in the middle of what the LFO reaches");
    require(slotFor(processor, srcLfo1, "cutoff") >= 0, "and LFO 1 reaches it");
    requireClose(value(processor, "lfo1Rate"), 2.0f, 0.001f, "an LFO in HZ keeps its rate");
    require(slotFor(processor, srcVelocity, "oscAFine") >= 0, "velocity reaches FINE, scaled by the macro it was");
    requireClose(value(processor, "env1Attack"), 0.01f, 0.0001f, "envelope times are seconds in both");
    requireClose(value(processor, "env1Sustain"), 0.6f, 0.001f, "and so is the sustain");
    require(value(processor, "mono") > 0.5f, "MONO crosses");

    require(juce::roundToInt(value(processor, fxParameterId(0, 0, "Type").toRawUTF8())) == static_cast<int>(FxType::reverb),
            "the reverb takes the main rack's first slot");
    requireClose(value(processor, fxParameterId(0, 0, "Mix").toRawUTF8()), 0.4f, 0.001f, "at its own wet");
    require(warned(info, "Lorenz"), "a chaos LFO is said to be left out");
    require(warned(info, "flanger"), "and so is the flanger");

    const auto damaged = folder.getChildFile("Damaged.SerumPreset");
    damaged.replaceWithText("XferJson");
    require(processor.loadPreset(damaged).failed(), "a damaged Serum preset is refused");
    folder.deleteRecursively();
}
}

void importTests()
{
    vitalSuite();
    serumSuite();
}
}
