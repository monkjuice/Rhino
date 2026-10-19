// Converting a Serum 2 preset (.SerumPreset) into a Forge patch.
//
// The file is "XferJson" and a NUL, a little-endian 64-bit length, that many
// bytes of JSON metadata (name, author, description, tags), then a 32-bit size
// and a 32-bit encoding (2, zstd) ahead of one zstd frame holding the patch as
// CBOR: a map of module name to module, each with `plainParams` listing only
// the settings that differ from Serum's default, in real units where Serum
// has them (seconds, cents, percent) and as the knob's own 0..1 where it does
// not (a cutoff). The defaults and the knob laws used here were measured
// against Serum by the serum2vital project (GPLv3; facts, not code, taken).
//
// Forge was drawn from Serum 2's manual, so most of it lands one to one: three
// wavetable oscillators with two warp stages each, the sub, the noise, four
// envelopes with ENV 1 the amplitude, eight macros, and three effects racks on
// the main output and the two busses. The matrix scales a source by an amount
// of the destination's range exactly as Serum's does, and Forge's BI is
// Serum's bipolar switch, so a routing's depth is the same amount sent through
// both synths' knob laws. What does not land — Serum's second filter, its
// sample, multisample, granular and spectral oscillators, LFOs past the six
// Forge has, its chaos generators — is left out with a warning each.
#include "ForgePresetImportInternal.h"
#include "ForgeCbor.h"
#include "ForgeProcessor.h"
#include <optional>
#include <zstd.h>

namespace rhino::forge
{
namespace
{
using import::PatchWriter;

constexpr const char* magic = "XferJson";
constexpr size_t magicLength = 9;

juce::File serumRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Xfer").getChildFile("Serum 2 Presets");
}

bool readMetadata(juce::InputStream& stream, juce::var& metadata, juce::int64& patchAt)
{
    char head[magicLength] {};
    if (stream.read(head, static_cast<int>(magicLength)) != static_cast<int>(magicLength)) return false;
    if (std::memcmp(head, magic, magicLength - 1) != 0 || head[magicLength - 1] != 0) return false;
    const auto length = stream.readInt64();
    if (length <= 0 || length > (1 << 24)) return false;
    juce::MemoryBlock text;
    if (stream.readIntoMemoryBlock(text, static_cast<int>(length)) != static_cast<size_t>(length)) return false;
    metadata = juce::JSON::parse(text.toString());
    patchAt = static_cast<juce::int64>(magicLength) + 8 + length;
    return metadata.isObject();
}

juce::Result readSerumFile(const juce::File& file, juce::var& metadata, juce::var& patch)
{
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return juce::Result::fail("Forge could not open that Serum preset.");
    juce::int64 patchAt = 0;
    if (!readMetadata(stream, metadata, patchAt)) return juce::Result::fail("That is not a Serum 2 preset.");
    stream.setPosition(patchAt);
    const auto size = static_cast<juce::uint32>(stream.readInt());
    const auto encoding = stream.readInt();
    if (encoding != 2) return juce::Result::fail("That Serum preset is stored in a way Forge does not read.");
    if (size == 0 || size > (1u << 27)) return juce::Result::fail("That Serum preset's patch is damaged.");
    juce::MemoryBlock compressed;
    stream.readIntoMemoryBlock(compressed);
    juce::MemoryBlock raw(size);
    const auto written = ZSTD_decompress(raw.getData(), raw.getSize(), compressed.getData(), compressed.getSize());
    if (ZSTD_isError(written) || written != size) return juce::Result::fail("That Serum preset's patch is damaged.");
    return readCbor(raw.getData(), raw.getSize(), patch);
}

// A preset's style from where Serum files it: the prefix its factory names
// carry ("BA - ", "LD - "), or a folder it sits in.
juce::String serumStyle(const juce::String& name, const juce::File& file)
{
    static const std::array<std::pair<const char*, const char*>, 18> prefixes {{
        {"BA", "Bass"}, {"808", "Bass"}, {"SB", "Bass"}, {"LD", "Lead"}, {"SY", "Lead"}, {"PD", "Pad"},
        {"CH", "Pad"}, {"PL", "Keys"}, {"KY", "Keys"}, {"KEY", "Keys"}, {"BL", "Keys"}, {"SQ", "Sequence"},
        {"ARP", "Sequence"}, {"SEQ", "Sequence"}, {"FX", "SFX"}, {"DR", "Percussion"}, {"KIT", "Percussion"},
        {"PERC", "Percussion"}}};
    const auto prefix = name.upToFirstOccurrenceOf(" - ", false, false).trim().toUpperCase();
    if (prefix != name.toUpperCase())
        for (const auto& [code, style] : prefixes)
            if (prefix == code) return style;
    // Up to Serum's own root, or to the drive's: a drive's root is its own
    // parent, so the walk stops where going up stops moving.
    for (auto folder = file.getParentDirectory(); folder != serumRoot() && folder.getParentDirectory() != folder;
         folder = folder.getParentDirectory())
        if (const auto style = canonicalPresetStyle(folder.getFileName()); style.isNotEmpty()) return style;
    return {};
}

juce::var child(const juce::var& from, const juce::String& key)
{
    return from.getProperty(juce::Identifier(key), {});
}

// A module's settings that differ from Serum's default, or nothing.
juce::var paramsOf(const juce::var& module)
{
    const auto params = child(module, "plainParams");
    return params.isObject() ? params : juce::var(new juce::DynamicObject());
}

float number(const juce::var& params, const char* key, float fallback)
{
    const auto value = params.getProperty(key, {});
    if (value.isVoid() || value.isString()) return fallback;
    return static_cast<float>(static_cast<double>(value));
}

juce::String word(const juce::var& params, const char* key, const juce::String& fallback = {})
{
    const auto value = params.getProperty(key, {});
    return value.isString() ? value.toString() : fallback;
}

// Serum's knob laws (measured; see the file comment).
float cutoffHz(float knob) { return 8.0f * std::pow(22050.0f / 8.0f, juce::jlimit(0.0f, 1.0f, knob)); }
float envelopeKnob(float seconds) { return std::pow(juce::jlimit(0.0f, 32.0f, seconds) / 32.0f, 0.2f); }
float envelopeSeconds(float knob) { return 32.0f * std::pow(juce::jlimit(0.0f, 1.0f, knob), 5.0f); }
// A Serum 2 level is stored as the gain itself, unlike Serum 1's knob, which
// was quadratic: read through Serum 1's law the factory presets' master
// volumes (a median of 0.24) would sit near -37 dB. Forge's level is the gain
// too, and both open at 0.75.
float levelFrom(float gain) { return juce::jlimit(0.0f, 1.0f, gain); }

constexpr int lfoSourceFirst = 6;

// Serum's own names, "VoicePanel" and "GlobalScalingEnvTime", as words.
juce::String spaced(const juce::String& name)
{
    juce::String words;
    for (int i = 0; i < name.length(); ++i)
    {
        const auto c = name[i];
        if (i > 0 && juce::CharacterFunctions::isUpperCase(c) && juce::CharacterFunctions::isLowerCase(name[i - 1]))
            words << ' ';
        words << juce::String::charToString(c);
    }
    return words.toUpperCase();
}

juce::String serumSourceName(int id)
{
    if (id >= 2 && id <= 5) return "ENV " + juce::String(id - 1);
    if (id >= 6 && id <= 15) return "LFO " + juce::String(id - 5);
    if (id >= 25 && id <= 32) return "MACRO " + juce::String(id - 24);
    switch (id)
    {
        case 1: return "MOD WHEEL";
        case 16: return "VELOCITY";
        case 17: return "NOTE";
        case 18: return "AFTERTOUCH";
        case 19: return "POLY AFTERTOUCH";
        case 20: return "NOISE OSC";
        case 21: case 22: return "NOTE-ON RANDOM";
        case 23: case 24: return "NOTE-ON ALTERNATE";
        case 33: return "PITCH BEND";
        case 34: case 35: case 36: return "MPE";
        case 37: return "RELEASE VELOCITY";
        case 38: return "FIXED";
        case 49: case 50: case 51: case 52: return "OSCILLATOR AUDIO";
        case 53: case 54: return "FILTER AUDIO";
        case 55: case 56: case 57: case 58: return "VOICE";
        case 59: return "NOTE-ON RANDOM";
        default: break;
    }
    return "SOURCE " + juce::String(id);
}

class SerumConverter
{
public:
    SerumConverter(const juce::var& metadataObject, const juce::var& patchObject, const juce::File& from,
                   PatchWriter& writer, ImportedPreset& imported)
        : metadata(metadataObject), patch(patchObject), file(from), out(writer), result(imported)
    {
    }

    void convert()
    {
        describe();
        voice();
        chooseFilter();
        for (int osc = 0; osc < oscillatorCount; ++osc) oscillator(osc);
        noise();
        sub();
        envelopes();
        fxPlaces = import::convertSerumFx(patch, out);
        routings();
        lfos();
        macros();
        sequencers();
    }

private:
    const juce::var& metadata;
    const juce::var& patch;
    juce::File file;
    PatchWriter& out;
    ImportedPreset& result;

    int filter = -1;
    FilterType filterType = FilterType::lowPass;
    std::array<juce::String, oscillatorCount * warpSlots> warpModes;
    std::map<int, int> lfoSlots;
    std::vector<import::SerumFxPlace> fxPlaces;

    juce::var module(const juce::String& name) const { return child(patch, name); }
    juce::var params(const juce::String& name) const { return paramsOf(module(name)); }

    // --- What the preset says about itself ----------------------------------

    void describe()
    {
        result.info.author = metadata.getProperty("presetAuthor", {}).toString();
        result.info.notes = metadata.getProperty("presetDescription", {}).toString();
        result.info.style = serumStyle(file.getFileNameWithoutExtension(), file);
        for (int macro = 0; macro < macroCount; ++macro)
            out.macroName(macro, child(module("Macro" + juce::String(macro)), "name").toString());
    }

    // Serum's arpeggiator patterns and clip player are sequencers Forge's
    // arpeggiator does not read, so a preset that plays through them says so.
    void sequencers()
    {
        if (number(params("Arp0"), "kParamEnabled", 0.0f) >= 0.5f)
            out.warn("ARP: Serum's arpeggiator and its pattern are not converted; Forge's arpeggiator is left off");
        if (number(params("ClipPlayer0"), "kParamEnabled", 0.0f) >= 0.5f)
            out.warn("CLIPS: Serum's clip player is not converted; the preset plays the notes it is given");
    }

    void macros()
    {
        for (int macro = 0; macro < macroCount; ++macro)
            out.set("macro" + juce::String(macro + 1),
                    number(params("Macro" + juce::String(macro)), "kParamValue", 0.0f) / 100.0f);
    }

    void voice()
    {
        const auto global = params("Global0");
        // Level for level against each synth's default master.
        out.set("output", juce::jlimit(0.0f, 1.25f, 0.75f * number(global, "kParamMasterVolume", 0.7f) / 0.7f));
        const auto mono = number(global, "kParamMonoToggle", 0.0f) >= 0.5f;
        out.setOn("mono", mono);
        out.setOn("legato", number(global, "kParamLegato", 0.0f) >= 0.5f);
        out.set("polyphony", juce::jlimit(1.0f, 16.0f, number(global, "kParamPolyCount", 16.0f)));
        out.set("glide", number(global, "kParamPortamentoTime", 0.0f), "GLIDE");
        transpose = juce::roundToInt(number(global, "kParamTranspose", 0.0f));
        // The busses' return levels, which open at unity in Serum and at
        // Forge's 0.75 here.
        out.set("bus1Level", juce::jlimit(0.0f, 1.0f, 0.75f * number(global, "kParamFXBus1Vol", 1.0f)));
        out.set("bus2Level", juce::jlimit(0.0f, 1.0f, 0.75f * number(global, "kParamFXBus2Vol", 1.0f)));
    }

    int transpose = 0;

    // --- The filter -------------------------------------------------------------

    // Which of Serum's two filters a source feeds: its routing names the
    // filters, and its balance, filter 1 at -100 and filter 2 at +100, which
    // of them. A switched-off filter passes what reaches it untouched.
    bool sourceOn(int slot) const
    {
        return number(params("Oscillator" + juce::String(slot)), "kParamEnable", slot == 0 ? 1.0f : 0.0f) >= 0.5f;
    }

    bool feeds(int slot, int which) const
    {
        const auto routing = params("RoutingSlot" + juce::String(slot));
        if (word(routing, "kParamRoutingDest", slot == 0 ? "kRoutingDestFilter" : "kRoutingDestDirect") != "kRoutingDestFilter")
            return false;
        const auto balance = number(routing, "kParamFilterBalance", -100.0f);
        return which == 0 ? balance < 34.0f : balance > -34.0f;
    }

    void chooseFilter()
    {
        const auto first = number(params("VoiceFilter0"), "kParamEnable", 0.0f) >= 0.5f;
        const auto second = number(params("VoiceFilter1"), "kParamEnable", 0.0f) >= 0.5f;
        // Forge's one filter is whichever of Serum's the sources actually
        // reach the more of, filter 1 when they tie.
        const auto listeners = [this] (int which)
        {
            auto count = 0;
            for (int slot = 0; slot < 5; ++slot) if (sourceOn(slot) && feeds(slot, which)) ++count;
            return count;
        };
        filter = first && second ? (listeners(1) > listeners(0) ? 1 : 0) : first ? 0 : second ? 1 : -1;
        out.setOn("filterEnable", filter >= 0);
        if (first && second)
            out.warn("FILTER " + juce::String(filter == 0 ? 2 : 1) + ": Forge has one filter, so only FILTER "
                     + juce::String(filter + 1) + " was converted");
        if (filter < 0) return;

        const auto settings = params("VoiceFilter" + juce::String(filter));
        const auto name = word(settings, "kParamType", "MgL12");
        bool exact = true;
        filterType = filterTypeFor(name, exact);
        if (!exact)
            out.warn("FILTER: Serum's " + name + " is approximated by Forge's " + filterTypeName(static_cast<int>(filterType)));
        out.setIndex("filterType", static_cast<int>(filterType));
        out.set("cutoff", cutoffHz(number(settings, "kParamFreq", 1.0f)));
        out.set("resonance", number(settings, "kParamReso", 10.0f) / 100.0f);
        out.set("drive", number(settings, "kParamDrive", 0.0f) / 100.0f);
        out.set("filterMix", number(settings, "kParamWet", 100.0f) / 100.0f);
        out.set("filterLevel", juce::jlimit(0.0f, 1.0f, number(settings, "kParamLevelOut", 1.0f)));
        out.set("filterFreq", varToForge(number(settings, "kParamVar", 0.0f) / 100.0f));
        out.setOn("filterKeyTrack", number(settings, "kParamKeyTrack", 0.0f) >= 25.0f);
    }

    // The second field reads Serum's VAR: an absolute corner on the dual types,
    // where both synths mean a frequency of its own, and the knob's own 0..1
    // everywhere else, where both mean FAT, MORPH, DAMP and the rest.
    float varToForge(float knob) const
    {
        if (filterCategoryOf(filterType) == FilterCategory::dual) return filterSecondAt(cutoffHz(knob));
        return juce::jlimit(0.0f, 1.0f, knob);
    }

    static FilterType filterTypeFor(const juce::String& name, bool& exact)
    {
        exact = true;
        static const std::array<std::pair<const char*, FilterType>, 40> named {{
            {"LadderMg", FilterType::ladder}, {"LadderAcid", FilterType::acid}, {"DirtyMg", FilterType::dirtyLadder},
            {"LadderEMS", FilterType::ems}, {"LH12", FilterType::lowHigh}, {"LB12", FilterType::lowBand},
            {"LP12", FilterType::lowPeak}, {"LN12", FilterType::lowNotch}, {"HB12", FilterType::highBand},
            {"HP12", FilterType::highPeak}, {"HN12", FilterType::highNotch}, {"BP12", FilterType::bandPeak},
            {"BN12", FilterType::bandNotch}, {"PP12", FilterType::peakPeak}, {"PN12", FilterType::peakNotch},
            {"NN12", FilterType::notchNotch}, {"LBH12", FilterType::morphLowBandHigh},
            {"LBH24", FilterType::morphLowBandHigh}, {"LPH12", FilterType::morphLowPeakHigh},
            {"LPH24", FilterType::morphLowPeakHigh}, {"LNH12", FilterType::morphLowNotchHigh},
            {"LNH24", FilterType::morphLowNotchHigh}, {"BPN12", FilterType::morphBandPeakNotch},
            {"BPN24", FilterType::morphBandPeakNotch}, {"Diffuser", FilterType::diffusor},
            {"Reverb1", FilterType::reverb}, {"RM", FilterType::ringMod}, {"RMT", FilterType::ringMod},
            {"SNH1", FilterType::sampleHold}, {"Scream", FilterType::scream}, {"BandReject", FilterType::notch},
            {"FormantONE", FilterType::formant}, {"FormantTWO", FilterType::formant}, {"FormantTWB", FilterType::formant},
            {"N12", FilterType::notch}, {"N24", FilterType::notch}, {"P12", FilterType::peak},
            {"B12", FilterType::bandPass}, {"B24", FilterType::bandPass}, {"ZDF_A", FilterType::lowPass}}};
        for (const auto& [serum, forge] : named)
            if (name == serum) return forge;
        if (name.startsWith("MgL") || name.startsWith("L") && name.substring(1).containsOnly("0123456789"))
            return FilterType::lowPass;
        if (name.startsWith("H") && name.substring(1).containsOnly("0123456789")) return FilterType::highPass;
        exact = false;
        if (name.startsWith("Comb") || name.startsWith("DistComb")) return FilterType::comb;
        if (name.startsWith("Flange")) return FilterType::flanger;
        if (name.startsWith("Phase") || name == "Allpasses") return FilterType::phaser;
        if (name.startsWith("Scream")) return FilterType::scream;
        if (name.startsWith("Formant")) return FilterType::formant;
        if (name == "Wsp") return FilterType::dirtyLadder;
        if (name == "DJMixer") return FilterType::morphLowBandHigh;
        if (name.containsIgnoreCase("BPF")) return FilterType::bandPass;
        return FilterType::lowPass;
    }

    // --- The oscillators ---------------------------------------------------------

    // Where a source goes: its routing slot names the filter, the output or
    // nothing but the effects busses. Slot 0 opens on the filter and the
    // others on the output.
    void routeSource(int slot, const juce::String& routeId, const juce::String& sendPrefix, const juce::String& what)
    {
        const auto routing = params("RoutingSlot" + juce::String(slot));
        const auto destination = word(routing, "kParamRoutingDest", slot == 0 ? "kRoutingDestFilter" : "kRoutingDestDirect");
        out.setOn(routeId, filter >= 0 && feeds(slot, filter));
        const auto send1 = number(routing, "kParamFXBus1Level", 0.0f) / 100.0f;
        const auto send2 = number(routing, "kParamFXBus2Level", 0.0f) / 100.0f;
        out.set(sendPrefix + "Send1", send1);
        out.set(sendPrefix + "Send2", send2);
        if (destination == "kRoutingDestNone")
            out.warn(what + ": Serum sends it only to the effects busses; Forge also plays it straight to the output");
        // Serum's sends do not follow the level: a source turned all the way
        // down and sent to a bus is heard through the bus. Forge's sends come
        // after the level, so such a source takes its send as its level.
        const auto levelId = sendPrefix + "Level";
        const auto send = juce::jmax(send1, send2);
        if (out.valueOf(levelId) < 0.05f && send > 0.0f)
        {
            out.set(levelId, send);
            out.warn(what + ": heard only through its FX bus send in Serum, so its send is used as its level");
        }
    }

    juce::File tableFile(const juce::String& relative) const
    {
        if (relative.isEmpty()) return {};
        // Some references carry a leading slash or two; they are still
        // relative to the tables folder.
        const auto path = relative.replaceCharacter('\\', '/').trimCharactersAtStart("/");
        if (juce::File::isAbsolutePath(path)) return juce::File(path);
        for (const auto& root : {serumRoot().getChildFile("Tables"), serumRoot().getChildFile("Tables").getChildFile("S2 Tables"),
                                 serumRoot()})
            if (const auto found = root.getChildFile(path); found.existsAsFile()) return found;
        return {};
    }

    // An oscillator's table: drawn into the preset, or one of Serum's files.
    void table(int osc, const juce::var& wavetable, const juce::String& name)
    {
        if (const auto* embedded = wavetable.getProperty("embeddedWTData", {}).getArray())
        {
            // A drawn table, its frames end to end. Counted from the data
            // rather than from numFrames, which can be left over from the
            // table it replaced; thinned evenly past what Forge holds.
            const auto available = embedded->size() / wavetableFrameSize;
            if (available < 1) return;
            const auto frames = juce::jmin(maxEditableFrames, available);
            std::vector<float> samples(static_cast<size_t>(frames) * wavetableFrameSize);
            for (int frame = 0; frame < frames; ++frame)
            {
                const auto pick = frames == 1 ? 0 : juce::roundToInt(static_cast<double>(frame) * (available - 1) / (frames - 1));
                for (int i = 0; i < wavetableFrameSize; ++i)
                    samples[static_cast<size_t>(frame * wavetableFrameSize + i)] =
                        static_cast<float>(static_cast<double>((*embedded)[pick * wavetableFrameSize + i]));
            }
            out.table(osc, samples, frames, wavetable.getProperty("tableDisplayName", "CUSTOM").toString().toUpperCase());
            return;
        }
        const auto relative = wavetable.getProperty("relativePathToWT", {}).toString();
        if (relative.isEmpty()) return;
        const auto found = tableFile(relative);
        std::vector<float> samples;
        auto frames = 0;
        if (found == juce::File {} || readWavetableFrames(found, samples, frames).failed())
        {
            out.warn(name + ": its table " + relative + " is not on this machine; it plays Forge's own");
            return;
        }
        out.table(osc, samples, frames, found.getFileNameWithoutExtension().toUpperCase());
    }

    static std::optional<WarpMode> warpFor(const juce::String& name, int osc, bool& signedAmount, bool& negative)
    {
        signedAmount = negative = false;
        // OSC and OSC2 name the other two oscillators in order: B and C on A,
        // A and C on B, A and B on C. Forge names them round the ring.
        const std::array<std::array<int, 2>, 3> partners {{{1, 2}, {0, 2}, {0, 1}}};
        const auto next = [osc] (int other) { return other == (osc + 1) % oscillatorCount; };
        const auto partner = [&] (bool second) { return partners[static_cast<size_t>(osc)][second ? 1 : 0]; };
        const auto osc2 = name.endsWith("OSC2");
        const auto fromOsc = name.contains("_OSC");
        const auto pick = [&] (WarpMode toNext, WarpMode toPrevious) { return next(partner(osc2)) ? toNext : toPrevious; };

        if (name == "kSync") return WarpMode::sync;
        if (name == "kBendPos") return WarpMode::bendUp;
        if (name == "kBendNeg") return WarpMode::bendDown;
        if (name == "kBendPosNeg") return WarpMode::bendBoth;
        if (name == "kPWM") return WarpMode::pwm;
        if (name.startsWith("kASYM"))
        {
            signedAmount = name != "kASYMPosNeg";
            negative = name == "kASYMNeg";
            return WarpMode::asym;
        }
        if (name == "kFlip") return WarpMode::flip;
        if (name == "kMirror") return WarpMode::mirror;
        if (name == "kQuantize") return WarpMode::quantize;
        if (name == "kEvenOdd") return WarpMode::oddEven;
        if (name == "kFilterLPF") return WarpMode::lowPass;
        if (name == "kFilterHPF") return WarpMode::highPass;
        if (name == "kFilterBPF") return WarpMode::bandPass;
        if (name == "kDistTube" || name == "kDistStompBox" || name == "kDistAsym") return WarpMode::tube;
        if (name == "kDistSoftClip" || name == "kDistSoftSat") return WarpMode::softClip;
        if (name == "kDistHardClip" || name == "kDistZeroSquare") return WarpMode::hardClip;
        if (name.startsWith("kDistDiode")) return WarpMode::diode;
        if (name == "kDistLinFold") return WarpMode::linearFold;
        if (name == "kDistSinFold" || name == "kDistSineShaper") return WarpMode::sineFold;
        if (name == "kDistRectify") return WarpMode::rectify;
        if (name == "kDistTapeSat") return WarpMode::tapeSat;
        if (name == "kSelfPD") return WarpMode::pdSelf;
        if (name.contains("FILT")) return std::nullopt;
        if (name.startsWith("kPD_") || name.startsWith("kFMP_"))
            return fromOsc ? pick(WarpMode::pdOsc, WarpMode::pdOscPrevious)
                 : name.endsWith("SUB") ? WarpMode::pdSub : WarpMode::pdNoise;
        if (name.startsWith("kFMX_"))
            return fromOsc ? pick(WarpMode::fmExpOsc, WarpMode::fmExpOscPrevious)
                 : name.endsWith("SUB") ? WarpMode::fmExpSub : WarpMode::fmExpNoise;
        if (name.startsWith("kFM_"))
            return fromOsc ? pick(WarpMode::fmOsc, WarpMode::fmOscPrevious)
                 : name.endsWith("SUB") ? WarpMode::fmSub : WarpMode::fmNoise;
        if (name.startsWith("kAM_"))
            return fromOsc ? pick(WarpMode::amOsc, WarpMode::amOscPrevious)
                 : name.endsWith("SUB") ? WarpMode::amSub : WarpMode::amNoise;
        if (name.startsWith("kRM_"))
            return fromOsc ? pick(WarpMode::rmOsc, WarpMode::rmOscPrevious)
                 : name.endsWith("SUB") ? WarpMode::rmSub : WarpMode::rmNoise;
        return std::nullopt;
    }

    static float warpDepth(float knob, bool signedAmount, bool negative)
    {
        const auto a = juce::jlimit(0.0f, 1.0f, knob);
        if (!signedAmount) return a;
        return negative ? 0.5f - 0.5f * a : 0.5f + 0.5f * a;
    }

    void oscillator(int osc)
    {
        const auto name = "OSC " + oscillatorLetter(osc);
        const auto moduleName = "Oscillator" + juce::String(osc);
        const auto settings = params(moduleName);
        auto enabled = number(settings, "kParamEnable", osc == 0 ? 1.0f : 0.0f) >= 0.5f;
        const auto kind = word(settings, "kParamType");
        if (enabled && kind.isNotEmpty() && kind != "kOsc_Wavetable")
        {
            out.warn(name + ": Serum's " + kind.fromFirstOccurrenceOf("kOsc_", false, false).toLowerCase()
                     + " oscillator has no counterpart in Forge, so it is left out");
            enabled = false;
        }
        const auto prefix = juce::String(oscillatorPrefix(osc));
        out.setOn(prefix + "Enable", enabled);
        out.setIndex(prefix + "Mode", static_cast<int>(OscMode::wavetable));

        const auto wavetable = child(module(moduleName), "WTOsc" + juce::String(osc));
        const auto wt = paramsOf(wavetable);
        if (enabled) table(osc, wavetable, name);
        // POSITION is on a fixed scale of 1 to 256 whatever the table holds.
        out.set(prefix + "Position", juce::jlimit(0.0f, 1.0f, (number(wt, "kParamTablePos", 1.0f) - 1.0f) / 255.0f));

        const auto semitones = 12.0f * number(settings, "kParamOctave", 0.0f) + number(settings, "kParamPitch", 0.0f)
                             + number(settings, "kParamCoarsePit", 0.0f) + static_cast<float>(transpose);
        const auto octave = juce::jlimit(-4, 4, static_cast<int>(semitones / 12.0f));
        out.set(prefix + "Octave", static_cast<float>(octave));
        out.set(prefix + "Semitone", semitones - 12.0f * static_cast<float>(octave), name + " PITCH");
        out.set(prefix + "Fine", number(settings, "kParamFine", 0.0f));
        const auto voices = juce::roundToInt(number(settings, "kParamUnison", 1.0f));
        out.set(prefix + "Unison", static_cast<float>(juce::jlimit(1, unisonMax, voices)));
        if (enabled && voices > unisonMax)
            out.warn(name + ": " + juce::String(voices) + " unison voices, Forge stacks " + juce::String(unisonMax));
        out.set(prefix + "Detune", number(settings, "kParamDetune", 0.2f));
        if (enabled && voices > 1 && word(settings, "kParamUnisonStack").isNotEmpty())
            out.warn(name + ": Serum's unison stacks (octaves, fifths) have no counterpart; it is stacked as plain unison");
        out.set(prefix + "Pan", number(settings, "kParamPan", 0.0f) / 50.0f);
        out.set(prefix + "Level", levelFrom(number(settings, "kParamVolume", 0.75f)));

        for (int stage = 0; stage < warpSlots; ++stage)
        {
            const auto menu = word(wt, stage == 0 ? "kParamWarpMenu" : "kParamWarpMenu2");
            const auto modeId = prefix + "Warp" + juce::String(stage + 1) + "Mode";
            const auto depthId = prefix + "Warp" + juce::String(stage + 1);
            warpModes[static_cast<size_t>(osc * warpSlots + stage)] = menu;
            if (menu.isEmpty() || menu == "kNone" || menu == "kOff") continue;
            bool signedAmount = false, negative = false;
            const auto mode = warpFor(menu, osc, signedAmount, negative);
            if (!mode.has_value())
            {
                if (enabled) out.warn(name + ": warp " + menu.fromFirstOccurrenceOf("k", false, false).toUpperCase()
                                      + " has no counterpart in Forge");
                continue;
            }
            out.setIndex(modeId, static_cast<int>(*mode));
            out.set(depthId, warpDepth(number(wt, stage == 0 ? "kParamWarp" : "kParamWarp2", 0.0f),
                                       signedAmount, negative));
        }
        routeSource(osc, "route" + oscillatorLetter(osc), prefix, name);
    }

    // Serum's noise oscillator plays a sample of noise; Forge's generates one,
    // and the source nearest the sample's name stands in for it.
    void noise()
    {
        const auto settings = params("Oscillator3");
        const auto enabled = number(settings, "kParamEnable", 0.0f) >= 0.5f;
        out.setOn("noiseEnable", enabled);
        if (!enabled) return;
        const auto path = child(child(module("Oscillator3"), "NoiseOsc3"), "relativePathToNoiseSample").toString();
        const auto lower = path.toLowerCase();
        auto source = NoiseSource::white;
        if (lower.contains("pink")) source = NoiseSource::pink;
        else if (lower.contains("brown")) source = NoiseSource::brown;
        else if (lower.contains("vinyl")) source = NoiseSource::vinyl;
        else if (lower.contains("crackl")) source = NoiseSource::crackle;
        else if (lower.contains("wind")) source = NoiseSource::wind;
        else if (lower.contains("hiss") || lower.contains("tape")) source = NoiseSource::tapeHiss;
        else if (lower.contains("hum")) source = NoiseSource::hardwareHum;
        else if (lower.contains("geiger")) source = NoiseSource::geiger;
        else if (lower.contains("metal")) source = NoiseSource::metallic;
        else if (path.isNotEmpty() && !lower.contains("white"))
            out.warn("NOISE: Serum's \"" + juce::File::createFileWithoutCheckingPath(path).getFileNameWithoutExtension()
                     + "\" is played as Forge's white noise");
        out.setIndex("noiseSource", static_cast<int>(source));
        out.set("noiseLevel", levelFrom(number(settings, "kParamVolume", 0.75f)));
        out.set("noisePan", number(settings, "kParamPan", 0.0f) / 50.0f);
        routeSource(3, "routeNoise", "noise", "NOISE");
    }

    void sub()
    {
        const auto settings = params("Oscillator4");
        const auto enabled = number(settings, "kParamEnable", 0.0f) >= 0.5f;
        out.setOn("subEnable", enabled);
        if (!enabled) return;
        static const juce::StringArray shapes {"kSine", "kRoundRect", "kTriangle", "kSaw", "kSquare", "kPulse"};
        const auto shape = word(paramsOf(child(module("Oscillator4"), "SubOsc4")), "kParamShape", "kSine");
        out.setIndex("subWave", juce::jmax(0, shapes.indexOf(shape)));
        out.set("subOctave", number(settings, "kParamOctave", 0.0f), "SUB OCTAVE");
        out.set("subLevel", levelFrom(number(settings, "kParamVolume", 0.75f)));
        out.set("subPan", number(settings, "kParamPan", 0.0f) / 50.0f);
        routeSource(4, "routeSub", "sub", "SUB");
    }

    // --- Envelopes and LFOs -------------------------------------------------------

    void envelopes()
    {
        for (int env = 0; env < envCount; ++env)
        {
            const auto settings = params("Env" + juce::String(env));
            const auto caption = "ENV " + juce::String(env + 1);
            out.set(envParameterId(env, "Attack"), number(settings, "kParamAttack", 0.005f), caption + " ATTACK");
            out.set(envParameterId(env, "Decay"), number(settings, "kParamDecay", 2.0f), caption + " DECAY");
            out.set(envParameterId(env, "Sustain"), number(settings, "kParamSustain", 1.0f));
            out.set(envParameterId(env, "Release"), number(settings, "kParamRelease", 0.075f), caption + " RELEASE");
            if (number(settings, "kParamHold", 0.0f) > 0.005f)
                out.warn(caption + ": its hold stage is left out; Forge's envelopes have none");
        }
    }

    int lfoSlot(int serum)
    {
        if (const auto found = lfoSlots.find(serum); found != lfoSlots.end()) return found->second;
        const auto slot = static_cast<int>(lfoSlots.size());
        if (slot >= lfoCount) return -1;
        lfoSlots[serum] = slot;
        return slot;
    }

    // An LFO's curve: points with y measured down from the top and a tension
    // per segment, half being straight. Fitted with Forge's bends, or one of
    // Forge's own four shapes when it is one of them. Serum's default, with
    // no points stored, is a triangle.
    void lfoShape(const juce::var& curve, int slot)
    {
        const auto* xs = curve.getProperty("xVals", {}).getArray();
        const auto* ys = curve.getProperty("yVals", {}).getArray();
        const auto* tensions = curve.getProperty("curveVals", {}).getArray();
        if (xs == nullptr || ys == nullptr || xs->size() < 2 || ys->size() < xs->size())
        {
            out.setIndex(lfoParameterId(slot, "Shape"), static_cast<int>(LfoShape::triangle));
            return;
        }
        std::vector<float> px, py, pc;
        for (int i = 0; i < xs->size(); ++i)
        {
            px.push_back(juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>((*xs)[i]))));
            py.push_back(1.0f - juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>((*ys)[i]))));
            pc.push_back(tensions != nullptr && i < tensions->size() ? static_cast<float>(static_cast<double>((*tensions)[i])) : 0.5f);
            if (px.back() >= 1.0f) break;
        }
        const auto sample = [&px, &py, &pc] (float x)
        {
            if (x <= px.front()) return 2.0f * py.front() - 1.0f;
            for (size_t i = 0; i + 1 < px.size(); ++i)
            {
                if (x > px[i + 1]) continue;
                const auto width = px[i + 1] - px[i];
                auto t = width > 0.0f ? (x - px[i]) / width : 1.0f;
                t = import::vitalPowerScale(t, (pc[i] - 0.5f) * 24.0f);
                return 2.0f * (py[i] + (py[i + 1] - py[i]) * t) - 1.0f;
            }
            return 2.0f * py.back() - 1.0f;
        };
        for (int shape = 0; shape < lfoShapeCount; ++shape)
        {
            auto worst = 0.0f;
            for (int i = 0; i <= 64; ++i)
            {
                const auto phase = static_cast<float>(i) / 64.0f;
                worst = juce::jmax(worst, std::abs(sample(phase) - lfoWave(static_cast<LfoShape>(shape), phase)));
            }
            if (worst < 0.08f)
            {
                out.setIndex(lfoParameterId(slot, "Shape"), shape);
                return;
            }
        }
        const auto table = import::lfoTableFrom(px, sample);
        if (table.custom) out.lfoShape(slot, table, "SERUM LFO");
    }

    void lfos()
    {
        for (const auto& [serum, slot] : lfoSlots)
        {
            const auto moduleName = "LFO" + juce::String(serum);
            const auto settings = params(moduleName);
            const auto caption = "LFO " + juce::String(serum + 1);
            const auto type = word(settings, "kParamType");
            if (type == "RandomSH")
            {
                out.setIndex(lfoParameterId(slot, "Type"), static_cast<int>(LfoType::sampleHold));
                out.set(lfoParameterId(slot, "Smooth"), number(settings, "kParamSmooth", 0.0f) / 100.0f);
            }
            else
            {
                out.setIndex(lfoParameterId(slot, "Type"), static_cast<int>(LfoType::normal));
                if (type == "Path") out.warn(caption + ": its path is played as Serum's curve; Forge's PATH is not set from it");
                lfoShape(child(module(moduleName), "curveData"), slot);
            }

            // Both readings store the knob as 100 n^4: hertz in HZ, and in
            // BPM a step of fifteen, 32 bars at the bottom and 1/256 near the
            // top, an octave a step.
            auto rate = number(settings, "kParamRate", 6.25f);
            if (number(settings, "kParamRate10x", 0.0f) >= 0.5f) rate *= 10.0f;
            if (number(settings, "kParamBeatSync", 1.0f) >= 0.5f)
            {
                const auto knob = std::pow(juce::jmax(0.0f, rate) / 100.0f, 0.25f);
                const auto step = juce::jlimit(0, 13, juce::roundToInt(knob * 14.0f));
                auto beats = std::pow(2.0f, static_cast<float>(7 - step));
                if (number(settings, "kParamDotted", 0.0f) >= 0.5f) beats *= 1.5f;
                if (number(settings, "kParamTriplets", 0.0f) >= 0.5f) beats *= 2.0f / 3.0f;
                const auto division = import::nearestLfoDivision(beats);
                out.setIndex(lfoParameterId(slot, "RateUnit"), 1);
                out.setIndex(lfoParameterId(slot, "Division"), division);
                if (std::abs(std::log2(lfoDivisions()[static_cast<size_t>(division)].beats / beats)) > 0.05f)
                    out.warn(caption + ": a cycle of " + juce::String(beats, 3) + " beats is not one of Forge's divisions; set to "
                             + lfoDivisions()[static_cast<size_t>(division)].label);
            }
            else
            {
                out.setIndex(lfoParameterId(slot, "RateUnit"), 0);
                out.set(lfoParameterId(slot, "Rate"), rate, caption + " RATE");
            }
            const auto mode = word(settings, "kParamMode");
            out.setIndex(lfoParameterId(slot, "Mode"), mode == "Free" ? static_cast<int>(LfoMode::free)
                                                      : mode == "Envelope" ? static_cast<int>(LfoMode::envelope)
                                                                           : static_cast<int>(LfoMode::trigger));
        }
    }

    // --- Routings -------------------------------------------------------------------

    // Where a Serum control lands in Forge, how its stored value becomes the
    // position of Serum's own knob, and how a knob position becomes Forge's
    // plain value. A routing moves Serum's knob by its amount, so its depth is
    // the distance in Forge's 0..1 between where the knob sits and where the
    // amount takes it.
    struct Target
    {
        juce::String id;
        float knob = 0.0f;
        std::function<float(float)> toForge;
    };

    std::optional<Target> targetFor(const juce::String& type, int index, const juce::String& param)
    {
        const auto identity = [] (float k) { return k; };
        if (type == "Oscillator" && index >= 0 && index <= 4)
        {
            const auto settings = params("Oscillator" + juce::String(index));
            if (index == 3 || index == 4)
            {
                const auto prefix = juce::String(index == 3 ? "noise" : "sub");
                if (param == "kParamVolume")
                    return Target {prefix + "Level", number(settings, "kParamVolume", 0.75f), levelFrom};
                if (param == "kParamPan")
                    return Target {prefix + "Pan", (number(settings, "kParamPan", 0.0f) + 50.0f) / 100.0f,
                                   [] (float k) { return 2.0f * k - 1.0f; }};
                return std::nullopt;
            }
            const auto prefix = juce::String(oscillatorPrefix(index));
            if (param == "kParamVolume")
                return Target {prefix + "Level", number(settings, "kParamVolume", 0.75f), levelFrom};
            if (param == "kParamPan")
                return Target {prefix + "Pan", (number(settings, "kParamPan", 0.0f) + 50.0f) / 100.0f,
                               [] (float k) { return 2.0f * k - 1.0f; }};
            if (param == "kParamFine")
                return Target {prefix + "Fine", (number(settings, "kParamFine", 0.0f) + 100.0f) / 200.0f,
                               [] (float k) { return 200.0f * k - 100.0f; }};
            if (param == "kParamDetune")
                return Target {prefix + "Detune", number(settings, "kParamDetune", 0.2f), identity};
            // The pitch controls all land on SEMI, moving it by however many
            // semitones Serum's knob moved: PITCH spans 24, COARSE 128.
            const auto semitone = out.valueOf(prefix + "Semitone");
            if (param == "kParamPitch")
            {
                const auto start = (number(settings, "kParamPitch", 0.0f) + 12.0f) / 24.0f;
                return Target {prefix + "Semitone", start, [semitone, start] (float k) { return semitone + 24.0f * (k - start); }};
            }
            if (param == "kParamCoarsePit")
            {
                const auto start = (number(settings, "kParamCoarsePit", 0.0f) + 64.0f) / 128.0f;
                return Target {prefix + "Semitone", start, [semitone, start] (float k) { return semitone + 128.0f * (k - start); }};
            }
            if (param == "kParamOctave")
            {
                const auto octave = out.valueOf(prefix + "Octave");
                const auto start = (number(settings, "kParamOctave", 0.0f) + 4.0f) / 8.0f;
                return Target {prefix + "Octave", start, [octave, start] (float k) { return octave + 8.0f * (k - start); }};
            }
            return std::nullopt;
        }
        if (type == "WTOsc" && index >= 0 && index < oscillatorCount)
        {
            const auto prefix = juce::String(oscillatorPrefix(index));
            const auto wt = paramsOf(child(module("Oscillator" + juce::String(index)), "WTOsc" + juce::String(index)));
            if (param == "kParamTablePos")
                return Target {prefix + "Position", (number(wt, "kParamTablePos", 1.0f) - 1.0f) / 255.0f, identity};
            if (param == "kParamWarp" || param == "kParamWarp2")
            {
                const auto stage = param == "kParamWarp" ? 0 : 1;
                const auto& menu = warpModes[static_cast<size_t>(index * warpSlots + stage)];
                bool signedAmount = false, negative = false;
                if (menu.isEmpty() || !warpFor(menu, index, signedAmount, negative).has_value()) return std::nullopt;
                return Target {prefix + "Warp" + juce::String(stage + 1), number(wt, param.toRawUTF8(), 0.0f),
                               [signedAmount, negative] (float k) { return warpDepth(k, signedAmount, negative); }};
            }
            return std::nullopt;
        }
        if (type == "VoiceFilter" && index == filter)
        {
            const auto settings = params("VoiceFilter" + juce::String(index));
            if (param == "kParamFreq") return Target {"cutoff", number(settings, "kParamFreq", 1.0f), cutoffHz};
            if (param == "kParamReso") return Target {"resonance", number(settings, "kParamReso", 10.0f) / 100.0f, identity};
            if (param == "kParamDrive") return Target {"drive", number(settings, "kParamDrive", 0.0f) / 100.0f, identity};
            if (param == "kParamWet") return Target {"filterMix", number(settings, "kParamWet", 100.0f) / 100.0f, identity};
            if (param == "kParamLevelOut") return Target {"filterLevel", juce::jlimit(0.0f, 1.0f, number(settings, "kParamLevelOut", 1.0f)), identity};
            if (param == "kParamVar")
                return Target {"filterFreq", number(settings, "kParamVar", 0.0f) / 100.0f, [this] (float k) { return varToForge(k); }};
            return std::nullopt;
        }
        if (type == "Env" && index >= 0 && index < envCount)
        {
            const auto settings = params("Env" + juce::String(index));
            if (param == "kParamAttack")
                return Target {envParameterId(index, "Attack"), envelopeKnob(number(settings, "kParamAttack", 0.005f)), envelopeSeconds};
            if (param == "kParamDecay")
                return Target {envParameterId(index, "Decay"), envelopeKnob(number(settings, "kParamDecay", 2.0f)), envelopeSeconds};
            if (param == "kParamRelease")
                return Target {envParameterId(index, "Release"), envelopeKnob(number(settings, "kParamRelease", 0.075f)), envelopeSeconds};
            if (param == "kParamSustain")
                return Target {envParameterId(index, "Sustain"), number(settings, "kParamSustain", 1.0f), identity};
            return std::nullopt;
        }
        if (type.startsWith("FX"))
            if (const auto fx = import::serumFxTarget(fxPlaces, type, index, param); fx.has_value())
                return Target {fx->id, fx->knob, fx->toForge};
        return std::nullopt;
    }

    float depthFor(const Target& target, float knob, float amount) const
    {
        const auto from = out.normalised(target.id, target.toForge(juce::jlimit(0.0f, 1.0f, knob)));
        const auto to = out.normalised(target.id, target.toForge(juce::jlimit(0.0f, 1.0f, knob + amount)));
        return to - from;
    }

    // Where a source usually sits, for folding an aux source's scaling into
    // the depth it scales: a macro or the wheel as saved, velocity at a firm
    // hit, the note at middle C, an LFO at its average.
    std::optional<float> typical(int id) const
    {
        if (id >= 25 && id <= 32) return number(params("Macro" + juce::String(id - 25)), "kParamValue", 0.0f) / 100.0f;
        if (id == 1) return number(params("Global0"), "kParamModWheel", 0.0f) / 100.0f;
        if (id == 16) return 0.8f;
        if (id == 17) return 60.0f / 127.0f;
        if (id >= 6 && id <= 15) return 0.5f;
        if (id == 38) return 1.0f;
        return std::nullopt;
    }

    int sourceFor(int id, juce::StringArray& lost)
    {
        if (id == 1) return static_cast<int>(ModSource::modWheel);
        if (id >= 2 && id <= 5) return static_cast<int>(ModSource::env1) + (id - 2);
        if (id >= lfoSourceFirst && id <= 15)
        {
            const auto type = word(params("LFO" + juce::String(id - lfoSourceFirst)), "kParamType");
            if (type == "Rossler" || type == "Lorenz")
            {
                lost.addIfNotAlreadyThere("LFO " + juce::String(id - 5) + " (a " + type + " chaos generator)");
                return 0;
            }
            const auto slot = lfoSlot(id - lfoSourceFirst);
            if (slot >= 0) return static_cast<int>(ModSource::lfo1) + slot;
            lost.addIfNotAlreadyThere("LFO " + juce::String(id - 5) + " (Forge's six LFOs are all in use)");
            return 0;
        }
        if (id == 16) return static_cast<int>(ModSource::velocity);
        if (id == 17) return static_cast<int>(ModSource::note);
        if (id >= 25 && id <= 32) return static_cast<int>(ModSource::macro1) + (id - 25);
        lost.addIfNotAlreadyThere(serumSourceName(id));
        return 0;
    }

    void routings()
    {
        juce::StringArray lostSources, unreachable, folded;
        struct Route
        {
            int source;
            Target target;
            float amount;
            bool bipolar;
            juce::String caption;
        };
        std::vector<Route> routes;
        std::map<juce::String, float> knobs;
        std::map<juce::String, const Target*> shifted;

        for (int index = 0; index < 64; ++index)
        {
            const auto slot = module("ModSlot" + juce::String(index));
            const auto* source = slot.getProperty("source", {}).getArray();
            if (source == nullptr || source->isEmpty()) continue;
            const auto settings = paramsOf(slot);
            if (number(settings, "kParamBypass", 0.0f) >= 0.5f) continue;
            auto amount = number(settings, "kParamAmount", 0.0f) / 100.0f;
            const auto id = static_cast<int>((*source)[0]);
            const auto aux = source->size() > 1 ? static_cast<int>((*source)[1]) : 0;
            if (aux > 0)
            {
                // An aux source scales the routing; folded at where it sits.
                const auto position = typical(aux);
                if (position.has_value())
                {
                    const auto inverted = number(settings, "kParamAuxInverted", 0.0f) >= 0.5f;
                    amount *= inverted ? 1.0f - *position : *position;
                    folded.addIfNotAlreadyThere(serumSourceName(aux));
                }
                else
                    lostSources.addIfNotAlreadyThere(serumSourceName(aux) + " scaling a routing");
            }
            if (std::abs(amount) < 1.0e-4f) continue;

            const auto type = slot.getProperty("destModuleTypeString", {}).toString();
            const auto destination = static_cast<int>(slot.getProperty("destModuleID", 0));
            const auto param = slot.getProperty("destModuleParamName", {}).toString();
            const auto caption = spaced(type) + " " + juce::String(destination % 100 + 1) + " "
                               + spaced(param.fromFirstOccurrenceOf("kParam", false, false));
            const auto target = targetFor(type, destination, param);
            if (!target.has_value())
            {
                unreachable.addIfNotAlreadyThere(caption.toUpperCase());
                continue;
            }
            const auto bipolar = number(settings, "kParamBipolar", 0.0f) >= 0.5f;
            // FIXED is a constant: it moves the setting itself rather than
            // sweeping it, so it is folded into the knob.
            if (id == 38)
            {
                knobs.emplace(target->id, target->knob);
                knobs[target->id] += bipolar ? amount * 0.5f : amount;
                shifted[target->id] = nullptr;
                routes.push_back({0, *target, 0.0f, false, caption});
                continue;
            }
            const auto forgeSource = sourceFor(id, lostSources);
            if (forgeSource == 0) continue;
            routes.push_back({forgeSource, *target, amount, bipolar, serumSourceName(id) + " -> " + caption.toUpperCase()});
        }

        // A unipolar LFO rises from the setting; Forge's LFOs swing either
        // side of it, so the setting moves by half the reach first. The
        // reach is what the knob can actually travel: an LFO taking a level
        // of 0.3 down by a whole range only ever reaches nothing, and
        // centring on the unclamped reach would park the level below zero
        // and leave the routing nothing to do.
        for (auto& route : routes)
        {
            knobs.emplace(route.target.id, route.target.knob);
            if (route.source == 0 || lfoIndexOf(route.source) < 0 || route.bipolar) continue;
            route.amount = juce::jlimit(0.0f, 1.0f, route.target.knob + route.amount) - route.target.knob;
            knobs[route.target.id] += route.amount * 0.5f;
            shifted[route.target.id] = nullptr;
        }
        for (auto& [id, ignored] : shifted)
            for (const auto& route : routes)
                if (route.target.id == id)
                {
                    out.set(id, route.target.toForge(juce::jlimit(0.0f, 1.0f, knobs[id])));
                    break;
                }

        for (const auto& route : routes)
        {
            if (route.source == 0) continue;
            const auto lfo = lfoIndexOf(route.source) >= 0;
            // As in the Vital conversion: an LFO swings a whole depth either
            // way, BI half a depth either way.
            const auto depth = depthFor(route.target, knobs[route.target.id], lfo ? route.amount * 0.5f : route.amount);
            out.route(route.source, route.target.id, depth, route.bipolar && !lfo, route.caption);
        }

        if (!lostSources.isEmpty())
            out.warn("Routings from " + lostSources.joinIntoString(", ") + " are left out; Forge has no such source");
        if (!unreachable.isEmpty())
            out.warn("Routings to " + unreachable.joinIntoString(", ") + " are left out; Forge cannot modulate "
                     + (unreachable.size() == 1 ? "it" : "them"));
        if (!folded.isEmpty())
            out.warn(folded.joinIntoString(", ") + " scale" + (folded.size() == 1 ? "s" : "")
                     + " other routings as Serum's aux source; Forge's matrix cannot, so each depth is fixed where "
                       "its source usually sits");
        const auto wheel = number(params("Global0"), "kParamModWheel", 0.0f);
        if (wheel > 5.0f)
            out.warn("MOD WHEEL: Serum opens this preset with the wheel at " + juce::String(juce::roundToInt(wheel))
                     + "%; raise Forge's mod wheel to hear it as saved");
    }
};
}

bool readSerumPresetHeader(const juce::File& file, SerumPresetHeader& header)
{
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return false;
    juce::var metadata;
    juce::int64 patchAt = 0;
    if (!readMetadata(stream, metadata, patchAt)) return false;
    header.name = metadata.getProperty("presetName", file.getFileNameWithoutExtension()).toString();
    header.author = metadata.getProperty("presetAuthor", {}).toString();
    header.description = metadata.getProperty("presetDescription", {}).toString();
    header.style = serumStyle(header.name, file);
    return true;
}

juce::Result importSerumPreset(const juce::File& file, const juce::AudioProcessorValueTreeState& parameters,
                               ImportedPreset& result)
{
    juce::var metadata, patch;
    const auto read = readSerumFile(file, metadata, patch);
    if (read.failed()) return read;
    if (!patch.isObject()) return juce::Result::fail("That Serum preset holds no patch.");
    PatchWriter writer(parameters, result);
    SerumConverter(metadata, patch, file, writer, result).convert();
    writer.finish();
    return juce::Result::ok();
}
}
