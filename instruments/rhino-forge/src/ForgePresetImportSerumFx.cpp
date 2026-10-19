// Serum 2's effects racks, converted onto Forge's.
//
// Serum has a rack on the main output and one on each of its two FX busses;
// so has Forge, and the three map rack for rack. Each entry of a Serum rack is
// one module, processed in list order, with only the settings that differ from
// its default stored; so each Forge slot starts from what its type opens on
// and takes Serum's settings over it, converted through the knob laws the
// serum2vital project measured. A module Forge has no type for — the flanger,
// the Hyper/Dimension, the frequency shifter, the convolver, the utility and
// the band splitters — is left out with a warning.
#include "ForgePresetImportInternal.h"

namespace rhino::forge::import
{
namespace
{
juce::var paramsOf(const juce::var& module)
{
    const auto params = module.getProperty("plainParams", {});
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

float logHz(float knob, float low, float high) { return low * std::pow(high / low, juce::jlimit(0.0f, 1.0f, knob)); }

float unit(float value, float low, float high, float skew = 1.0f)
{
    const auto at = juce::jlimit(0.0f, 1.0f, (value - low) / (high - low));
    return skew == 1.0f ? at : std::pow(at, 1.0f / skew);
}

float unitHz(float hz, float low, float high)
{
    return juce::jlimit(0.0f, 1.0f, std::log(juce::jmax(low, hz) / low) / std::log(high / low));
}

float modeValue(int choice, int count) { return count <= 1 ? 0.0f : static_cast<float>(choice) / static_cast<float>(count - 1); }

// The knob that picks the rack's division nearest a time of so many beats.
float divisionKnob(float beats)
{
    auto best = 0;
    for (int i = 0; i < fxDivisionCount; ++i)
        if (std::abs(std::log2(fxDivisions()[static_cast<size_t>(i)].beats / beats))
            < std::abs(std::log2(fxDivisions()[static_cast<size_t>(best)].beats / beats)))
            best = i;
    return modeValue(best, fxDivisionCount);
}

// Serum's synced delay stores its knob as the time it would read unsynced,
// (1 + 500 n^4) ms, and steps through its divisions along that knob.
float delayBeats(float stored)
{
    const auto knob = std::pow(juce::jmax(0.0f, (stored * 1000.0f - 1.0f) / 500.0f), 0.25f);
    const auto step = juce::roundToInt(juce::jlimit(0.0f, 1.0f, knob) * 228.0f);
    static constexpr std::array<std::pair<int, float>, 12> runs {{
        {10, 1.0f / 64.0f}, {31, 1.0f / 64.0f}, {51, 1.0f / 32.0f}, {72, 1.0f / 16.0f}, {93, 0.125f}, {114, 0.25f},
        {134, 0.5f}, {155, 1.0f}, {176, 2.0f}, {196, 4.0f}, {217, 8.0f}, {228, 16.0f}}};
    for (const auto& [last, beats] : runs)
        if (step <= last) return beats;
    return 16.0f;
}

// Serum's filter names onto the rack filter's eight models.
int filterModel(const juce::String& name, bool& exact)
{
    exact = true;
    if (name == "MgL6" || name == "L6") return 1;
    if (name == "MgL12" || name == "L12" || name == "LadderMg" || name == "ZDF_A") return 0;
    if (name.startsWith("MgL") || name == "L18" || name == "L24" || name == "LadderAcid" || name == "LadderEMS") return 2;
    if (name == "H6") return 5;
    if (name.startsWith("H") && name.substring(1).containsOnly("0123456789")) return 4;
    if (name == "HP12") return 4;
    if (name.startsWith("B") && name.substring(1).containsOnly("0123456789")) return 7;
    if (name == "BandReject" || name.startsWith("N") && name.substring(1).containsOnly("0123456789")) return 3;
    if (name == "P12" || name == "PP12") return 6;
    exact = false;
    return 0;
}

// Starts a slot on what its type opens on, as choosing the type on the panel
// does, so a setting Serum left at its default lands on Forge's.
void startSlot(PatchWriter& out, int rack, int slot, FxType type)
{
    const auto& info = fxTypes()[static_cast<size_t>(type)];
    const auto id = [rack, slot] (const char* suffix) { return fxParameterId(rack, slot, suffix); };
    out.setIndex(id("Type"), static_cast<int>(type));
    out.set(id("ModeA"), 0.0f);
    out.set(id("ModeB"), 0.0f);
    out.setOn(id("Bypass"), false);
    for (int knob = 0; knob < fxKnobCount; ++knob)
        out.set(id("Knob") + juce::String(knob + 1), info.init[static_cast<size_t>(knob)]);
    out.set(id("Mix"), info.initMix);
    out.set(id("Level"), 1.0f);
}
}

std::vector<SerumFxPlace> convertSerumFx(const juce::var& patch, PatchWriter& out)
{
    std::vector<SerumFxPlace> places;
    juce::StringArray missing;
    for (int rack = 0; rack < rackCount; ++rack)
    {
        const auto* list = patch.getProperty(juce::Identifier("FXRack" + juce::String(rack)), {})
                               .getProperty("FX", {}).getArray();
        if (list == nullptr) continue;
        auto slot = 0;
        for (int position = 0; position < list->size(); ++position)
        {
            const auto& entry = (*list)[position];
            SerumFxPlace place;
            place.rack = rack;
            place.position = position;
            if (const auto* object = entry.getDynamicObject())
                for (const auto& property : object->getProperties())
                    if (property.name.toString().startsWith("FX"))
                    {
                        place.type = property.name.toString();
                        place.params = paramsOf(property.value);
                    }
            const auto params = place.params;
            if (place.type.isEmpty() || number(params, "kParamEnable", 1.0f) < 0.5f)
            {
                places.push_back(place);
                continue;
            }
            const auto knob = [&out, rack, &slot] (int which, float value)
            {
                out.set(fxParameterId(rack, slot, "Knob") + juce::String(which + 1), juce::jlimit(0.0f, 1.0f, value));
            };
            const auto mode = [&out, rack, &slot] (const char* which, int choice, int count)
            {
                out.set(fxParameterId(rack, slot, which), modeValue(choice, count));
            };
            const auto mix = [&out, rack, &slot, &params] (float fallback)
            {
                out.set(fxParameterId(rack, slot, "Mix"), number(params, "kParamWet", fallback * 100.0f) / 100.0f);
            };
            const auto has = [&params] (const char* key) { return !params.getProperty(key, {}).isVoid(); };
            const auto synced = number(params, "kParamBeatSync", 0.0f) >= 0.5f;

            if (slot >= fxSlotCount)
            {
                out.warn(juce::String(rackName(rack)) + " EFFECTS: Serum's rack holds more than Forge's eight slots; "
                         "the rest are left out");
                break;
            }
            const auto& type = place.type;
            if (type == "FXReverb")
            {
                startSlot(out, rack, slot, FxType::reverb);
                const auto kind = word(params, "kParamType", "kPlate");
                mode("ModeA", kind == "kHall" || kind == "kAbyss" || kind == "kSpace" ? 1 : 0, 2);
                if (has("kParamSize")) knob(0, number(params, "kParamSize", 50.0f) / 100.0f);
                if (has("kParamPreDelay")) knob(1, unit(number(params, "kParamPreDelay", 0.0f), 0.0f, 0.2f));
                if (has("kParamWidth")) knob(3, number(params, "kParamWidth", 100.0f) / 100.0f);
                if (has("kParamFreq")) knob(4, number(params, "kParamFreq", 25.0f) / 100.0f);
                if (has("kParamFreqB")) knob(5, number(params, "kParamFreqB", 82.0f) / 100.0f);
                mix(0.3f);
            }
            else if (type == "FXDelay")
            {
                startSlot(out, rack, slot, FxType::delay);
                const auto left = number(params, "kParamTimeL", 0.25f);
                const auto offsetLeft = number(params, "kParamOffsetL", 1.0f);
                const auto offsetRight = number(params, "kParamOffsetR", 1.0f);
                if (number(params, "kParamBeatSync", 1.0f) >= 0.5f)
                {
                    mode("ModeB", 1, 2);
                    knob(0, divisionKnob(delayBeats(left) * offsetLeft));
                }
                else
                {
                    mode("ModeB", 0, 2);
                    knob(0, unit(left * offsetLeft, 0.01f, fxMaxDelayTime, 2.0f));
                }
                knob(1, juce::jlimit(0.0f, 1.0f, offsetRight / juce::jmax(0.05f, offsetLeft) - 0.5f));
                mode("ModeA", juce::roundToInt(number(params, "kParamMode", 0.0f)) == 1 ? 1 : 0, 2);
                if (has("kParamFeedback")) knob(2, number(params, "kParamFeedback", 35.0f) / 100.0f);
                if (has("kParamFreq")) knob(3, unitHz(number(params, "kParamFreq", 2000.0f), 200.0f, 16000.0f));
                if (has("kParamBW")) knob(4, 1.0f - unit(number(params, "kParamBW", 4.0f), 0.75f, 8.25f));
                mix(0.3f);
            }
            else if (type == "FXChorus")
            {
                startSlot(out, rack, slot, FxType::chorus);
                mode("ModeA", synced ? 1 : 0, 2);
                if (has("kParamRate"))
                    knob(0, synced ? divisionKnob(1.0f) : unit(number(params, "kParamRate", 0.2f), 0.02f, 8.0f, 2.0f));
                if (has("kParamDelay")) knob(1, unit(number(params, "kParamDelay", 5.0f), 0.5f, 30.0f));
                if (has("kParamDelay2")) knob(2, unit(number(params, "kParamDelay2", 10.0f), 0.5f, 30.0f));
                if (has("kParamDepth")) knob(3, unit(number(params, "kParamDepth", 2.0f), 0.0f, 6.0f));
                if (has("kParamFeedback")) knob(4, number(params, "kParamFeedback", 20.0f) / 100.0f);
                if (has("kParamFilt")) knob(5, unitHz(number(params, "kParamFilt", 8000.0f), 120.0f, 16000.0f));
                mix(0.5f);
            }
            else if (type == "FXDistortion")
            {
                startSlot(out, rack, slot, FxType::distortion);
                const auto shape = word(params, "kParamMode", "kTube");
                const auto choice = shape == "kSoftClip" || shape == "kSoftSat" ? 1
                                  : shape == "kHardClip" || shape == "kZeroSquare" ? 2
                                  : shape.startsWith("kDiode") ? 3
                                  : shape == "kLinFold" ? 4
                                  : shape == "kSinFold" || shape == "kSineShaper" ? 5
                                  : shape == "kDownsample" ? 7 : 0;
                mode("ModeA", choice, 8);
                if (has("kParamDrive")) knob(0, number(params, "kParamDrive", 30.0f) / 100.0f);
                const auto placement = juce::roundToInt(number(params, "kParamPrePost", 0.0f));
                const auto highPass = number(params, "kParamLPHP", 0.0f) >= 50.0f;
                mode("ModeB", placement == 0 ? 0 : placement == 1 ? (highPass ? 1 : 2) : (highPass ? 3 : 4), 5);
                if (has("kParamFreq")) knob(1, unitHz(logHz(number(params, "kParamFreq", 0.5f), 8.0f, 13290.0f), 40.0f, 16000.0f));
                if (has("kParamBW")) knob(2, 1.0f - unit(number(params, "kParamBW", 2.0f), 0.075f, 7.575f));
                mix(1.0f);
            }
            else if (type == "FXEQ")
            {
                startSlot(out, rack, slot, FxType::equaliser);
                mode("ModeA", juce::jlimit(0, 2, juce::roundToInt(number(params, "kParamType1", 0.0f))), 3);
                mode("ModeB", juce::jlimit(0, 2, juce::roundToInt(number(params, "kParamType2", 0.0f))), 3);
                if (has("kParamFreq1")) knob(0, unitHz(number(params, "kParamFreq1", 200.0f), 20.0f, 2000.0f));
                if (has("kParamReso1")) knob(1, number(params, "kParamReso1", 50.0f) / 100.0f);
                if (has("kParamGain1")) knob(2, unit(number(params, "kParamGain1", 0.0f), -18.0f, 18.0f));
                if (has("kParamFreq2")) knob(3, unitHz(number(params, "kParamFreq2", 2000.0f), 500.0f, 18000.0f));
                if (has("kParamReso2")) knob(4, number(params, "kParamReso2", 50.0f) / 100.0f);
                if (has("kParamGain2")) knob(5, unit(number(params, "kParamGain2", 0.0f), -18.0f, 18.0f));
                mix(1.0f);
            }
            else if (type == "FXComp")
            {
                startSlot(out, rack, slot, FxType::compressor);
                // The threshold runs down from 0 dB at nothing: factory
                // presets end on a limiter stored at 0, which read the other
                // way up would hold the patch at -60 dB and silence it.
                if (has("kParamThresh")) knob(0, 1.0f - juce::jlimit(0.0f, 1.0f, number(params, "kParamThresh", 0.0f)));
                if (has("kParamRatio")) knob(1, unit(juce::jmin(20.0f, number(params, "kParamRatio", 4.0f)), 1.0f, 20.0f, 1.7f));
                if (has("kParamAttack")) knob(2, unit(number(params, "kParamAttack", 10.0f) / 1000.0f, 0.0001f, 0.1f, 2.0f));
                if (has("kParamRelease")) knob(3, unit(number(params, "kParamRelease", 100.0f) / 1000.0f, 0.01f, 1.0f, 2.0f));
                if (has("kParamMakeup"))
                    knob(4, unit(juce::Decibels::gainToDecibels(number(params, "kParamMakeup", 1.0f)), 0.0f, 24.0f));
                mix(1.0f);
                if (number(params, "kParamMultiband", 0.0f) >= 0.5f)
                    out.warn("COMPRESSOR: Serum's multiband compressor is folded into Forge's single band");
            }
            else if (type == "FXFilter")
            {
                startSlot(out, rack, slot, FxType::filter);
                bool exact = true;
                const auto name = word(params, "kParamType", "MgL6");
                mode("ModeA", filterModel(name, exact), 8);
                if (!exact) out.warn("FILTER EFFECT: Serum's " + name + " is played as Forge's MG LOW 12");
                knob(0, unitHz(logHz(number(params, "kParamFreq", 1.0f), 8.0f, 22050.0f), 30.0f, 18000.0f));
                if (has("kParamReso")) knob(1, number(params, "kParamReso", 20.0f) / 100.0f);
                if (has("kParamDrive")) knob(2, number(params, "kParamDrive", 0.0f) / 100.0f);
                if (has("kParamVar")) knob(3, number(params, "kParamVar", 25.0f) / 100.0f);
                mix(1.0f);
            }
            else if (type == "FXPhaser")
            {
                startSlot(out, rack, slot, FxType::phaser);
                mode("ModeA", synced ? 1 : 0, 2);
                if (has("kParamRate"))
                    knob(0, synced ? divisionKnob(1.0f) : unit(number(params, "kParamRate", 0.2f), 0.02f, 8.0f, 2.0f));
                if (has("kParamDepth")) knob(1, number(params, "kParamDepth", 50.0f) / 100.0f);
                if (has("kParamFreq")) knob(2, unitHz(number(params, "kParamFreq", 600.0f), 80.0f, 8000.0f));
                if (has("kParamFeedback")) knob(3, 0.5f + number(params, "kParamFeedback", 50.0f) / 200.0f);
                if (has("kParamNumPoles"))
                {
                    const auto poles = number(params, "kParamNumPoles", 6.0f);
                    mode("ModeB", poles < 5.0f ? 0 : poles < 7.0f ? 1 : poles < 10.0f ? 2 : 3, 4);
                }
                mix(0.7f);
            }
            else
            {
                static const std::map<juce::String, const char*> names {
                    {"FXFlanger", "flanger"}, {"FXHyperD", "Hyper/Dimension"}, {"FXBode", "frequency shifter"},
                    {"FXConv", "convolution reverb"}, {"FXUtils", "utility"}, {"FXSplit", "band splitter"},
                    {"FXSplit3", "band splitter"}, {"FXSplitMS", "mid/side splitter"}};
                const auto found = names.find(type);
                missing.addIfNotAlreadyThere(found != names.end() ? juce::String(found->second) : type);
                places.push_back(place);
                continue;
            }
            place.slot = slot++;
            places.push_back(place);
        }
    }
    if (!missing.isEmpty())
        out.warn("EFFECTS: Forge's racks have no " + missing.joinIntoString(", ") + ", so "
                 + (missing.size() == 1 ? "it is" : "they are") + " left out");
    return places;
}

std::optional<SerumFxTarget> serumFxTarget(const std::vector<SerumFxPlace>& places, const juce::String& type,
                                           int moduleId, const juce::String& param)
{
    const auto rack = moduleId / 100, position = moduleId % 100;
    const SerumFxPlace* place = nullptr;
    for (const auto& candidate : places)
        if (candidate.rack == rack && candidate.position == position && candidate.type == type && candidate.slot >= 0)
            place = &candidate;
    if (place == nullptr) return std::nullopt;
    const auto& params = place->params;
    const auto knob = [place] (int which) { return fxParameterId(place->rack, place->slot, "Knob") + juce::String(which); };
    const auto identity = [] (float k) { return k; };

    if (param == "kParamWet")
        return SerumFxTarget {fxParameterId(place->rack, place->slot, "Mix"), number(params, "kParamWet", 100.0f) / 100.0f, identity};
    if (type == "FXFilter" && param == "kParamFreq")
        return SerumFxTarget {knob(1), number(params, "kParamFreq", 1.0f),
                              [] (float k) { return unitHz(logHz(k, 8.0f, 22050.0f), 30.0f, 18000.0f); }};
    if (type == "FXFilter" && param == "kParamReso")
        return SerumFxTarget {knob(2), number(params, "kParamReso", 20.0f) / 100.0f, identity};
    if (type == "FXFilter" && param == "kParamDrive")
        return SerumFxTarget {knob(3), number(params, "kParamDrive", 0.0f) / 100.0f, identity};
    if (type == "FXDistortion" && param == "kParamDrive")
        return SerumFxTarget {knob(1), number(params, "kParamDrive", 30.0f) / 100.0f, identity};
    if (type == "FXReverb" && param == "kParamSize")
        return SerumFxTarget {knob(1), number(params, "kParamSize", 50.0f) / 100.0f, identity};
    if (type == "FXDelay" && param == "kParamFeedback")
        return SerumFxTarget {knob(3), number(params, "kParamFeedback", 35.0f) / 100.0f, identity};
    if (type == "FXEQ" && (param == "kParamGain1" || param == "kParamGain2"))
    {
        const auto first = param == "kParamGain1";
        return SerumFxTarget {knob(first ? 3 : 6), (number(params, first ? "kParamGain1" : "kParamGain2", 0.0f) + 24.0f) / 48.0f,
                              [] (float k) { return unit(48.0f * k - 24.0f, -18.0f, 18.0f); }};
    }
    if (type == "FXEQ" && (param == "kParamFreq1" || param == "kParamFreq2"))
    {
        const auto first = param == "kParamFreq1";
        const auto hz = number(params, first ? "kParamFreq1" : "kParamFreq2", first ? 200.0f : 2000.0f);
        return SerumFxTarget {knob(first ? 1 : 4), unitHz(hz, 22.0f, 20000.0f),
                              [first] (float k) { return unitHz(logHz(k, 22.0f, 20000.0f), first ? 20.0f : 500.0f,
                                                                first ? 2000.0f : 18000.0f); }};
    }
    if (type == "FXPhaser" && param == "kParamFreq")
        return SerumFxTarget {knob(3), unitHz(number(params, "kParamFreq", 600.0f), 20.0f, 18000.0f),
                              [] (float k) { return unitHz(logHz(k, 20.0f, 18000.0f), 80.0f, 8000.0f); }};
    return std::nullopt;
}
}
