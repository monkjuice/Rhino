// Converting a Vital preset (.vital, one JSON object) into a Forge patch.
//
// Vital stores every control in its own domain — a cutoff as a MIDI note, an
// envelope time as the fourth root of seconds, a rate as a power of two — and
// a modulation as an amount of the control's whole stored range. So every
// setting is turned into real units first and then into Forge's parameter, and
// every routing's depth is worked out by sending the control's stored value to
// each end of the routing's reach, converting both ends, and taking the
// difference in Forge's own 0..1. What lands on the knob is then what Vital was
// playing at both ends of the sweep, whatever the two synths' curves are
// between them.
//
// Forge has four envelopes to Vital's six, six LFOs to eight and eight macros
// to four. Vital's envelopes and LFOs are given Forge's in the order the patch
// uses them, ENV 1 staying the amplitude; one of Vital's two filters is
// converted, into FILTER 1, and the other, anything past what Forge has and
// the random sources are left out with a warning each. See ForgePresetImport.h.
#include "ForgePresetImportInternal.h"
#include <optional>

namespace rhino::forge
{
namespace
{
using import::PatchWriter;

float noteToHz(float note) { return 8.1757989f * std::pow(2.0f, note / 12.0f); }

// Cycles per beat at each of Vital's tempo indices: Freeze, 32/1 ... 1/64.
float tempoCyclesPerBeat(int index)
{
    static constexpr std::array<float, 13> ratios {0.0f, 1.0f / 128.0f, 1.0f / 64.0f, 1.0f / 32.0f, 1.0f / 16.0f,
                                                   1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
    return ratios[static_cast<size_t>(juce::jlimit(0, 12, index))];
}

// How many beats one cycle of a synced Vital rate lasts, given its sync mode
// (1 tempo, 2 dotted, 3 triplet) and tempo index; zero for Freeze.
float tempoBeats(int sync, float tempo)
{
    const auto ratio = tempoCyclesPerBeat(static_cast<int>(tempo + 0.3f));
    if (ratio <= 0.0f) return 0.0f;
    auto beats = 1.0f / ratio;
    if (sync == 2) beats *= 1.5f;
    if (sync == 3) beats *= 2.0f / 3.0f;
    return beats;
}

const char* oscPrefix(int osc) { return oscillatorPrefix(osc); }

juce::String oscId(int osc, const juce::String& suffix) { return juce::String(oscPrefix(osc)) + suffix; }

juce::String vitalSourceCaption(const juce::String& source)
{
    return source.replaceCharacter('_', ' ').toUpperCase().replace("MACRO CONTROL", "MACRO");
}

class VitalConverter
{
public:
    VitalConverter(const juce::var& rootObject, PatchWriter& writer, ImportedPreset& imported)
        : root(rootObject), settings(rootObject.getProperty("settings", {})), out(writer), result(imported)
    {
    }

    void convert()
    {
        describe();
        voice();
        chooseFilter();
        for (int osc = 0; osc < oscillatorCount; ++osc) oscillator(osc);
        sample();
        out.setOn("subEnable", false);
        envelopeSlot("env_1");
        // Before the routings, which can reach an effect's wet/dry.
        effects();
        routings();
        envelopes();
        lfos();
        macros();
    }

private:
    const juce::var& root;
    juce::var settings;
    PatchWriter& out;
    ImportedPreset& result;

    std::array<int, oscillatorCount> lastPositions {};
    // Which of an oscillator's two warp stages holds Vital's spectral morph and
    // which its distortion, or -1 for neither.
    std::array<int, oscillatorCount> morphStage {-1, -1, -1};
    std::array<int, oscillatorCount> distortionStage {-1, -1, -1};
    std::array<int, oscillatorCount> morphTypes {};
    std::array<int, oscillatorCount> distortionTypes {};
    // Vital's filter that became Forge's: 1 or 2, or 0 for none.
    int filter = 0;
    FilterType filterType = FilterType::lowPass;
    bool sampleIsNoise = false;
    std::map<juce::String, int> envSlots, lfoSlots;
    // Each Vital effect that became a rack slot, by its key prefix.
    std::map<juce::String, int> fxSlots;

    float get(const juce::String& key, float fallback) const
    {
        const auto value = settings.getProperty(juce::Identifier(key), {});
        return value.isVoid() ? fallback : static_cast<float>(value);
    }

    bool on(const juce::String& key) const { return get(key, 0.0f) >= 0.5f; }

    // --- What the preset says about itself -------------------------------------

    void describe()
    {
        result.info.author = root.getProperty("author", {}).toString();
        result.info.notes = root.getProperty("comments", {}).toString();
        result.info.style = canonicalPresetStyle(root.getProperty("preset_style", {}).toString());
        for (int macro = 0; macro < 4; ++macro)
            out.macroName(macro, root.getProperty(juce::Identifier("macro" + juce::String(macro + 1)), {}).toString());
    }

    void macros()
    {
        for (int macro = 0; macro < 4; ++macro)
            out.set("macro" + juce::String(macro + 1), get("macro_control_" + juce::String(macro + 1), 0.0f));
    }

    void voice()
    {
        const auto polyphony = juce::roundToInt(get("polyphony", 8.0f));
        out.set("polyphony", static_cast<float>(juce::jlimit(1, 16, polyphony)));
        out.setOn("mono", polyphony <= 1);
        out.setOn("legato", on("legato"));
        const auto glide = get("portamento_time", -10.0f);
        out.set("glide", glide <= -9.9f ? 0.0f : std::pow(2.0f, glide), "GLIDE");
        // Level for level against each synth's own default, so a patch Vital
        // plays at its usual loudness plays at Forge's.
        const auto decibels = std::sqrt(juce::jmax(0.0f, get("volume", 5473.04f))) - 80.0f;
        out.set("output", 0.75f * juce::Decibels::decibelsToGain(decibels + 6.02f));
    }

    // --- The filter ---------------------------------------------------------------

    // Which oscillators a filter hears: an oscillator's destination is filter 1,
    // filter 2 or both, or past them to the effects.
    int listeners(int which) const
    {
        auto count = 0;
        for (int osc = 0; osc < oscillatorCount; ++osc)
        {
            if (!on("osc_" + juce::String(osc + 1) + "_on")) continue;
            const auto destination = juce::roundToInt(get("osc_" + juce::String(osc + 1) + "_destination", 0.0f));
            if (destination == 2 || destination == which - 1) ++count;
        }
        return count;
    }

    bool modulated(const juce::String& key) const
    {
        if (const auto* list = settings.getProperty("modulations", {}).getArray())
            for (const auto& entry : *list)
                if (entry.getProperty("destination", {}).toString() == key) return true;
        return false;
    }

    void chooseFilter()
    {
        const auto first = on("filter_1_on"), second = on("filter_2_on");
        if (first && second) filter = listeners(2) > listeners(1) ? 2 : 1;
        else filter = first ? 1 : second ? 2 : 0;
        out.setOn("filterEnable", filter != 0);
        if (filter == 0) return;
        if (first && second)
            out.warn("FILTER " + juce::String(filter == 1 ? 2 : 1) + ": only one of Vital's filters is converted, "
                     "so only FILTER " + juce::String(filter) + " was");

        const auto key = [this] (const char* suffix) { return "filter_" + juce::String(filter) + "_" + suffix; };
        const auto model = juce::roundToInt(get(key("model"), 0.0f));
        const auto style = juce::roundToInt(get(key("style"), 0.0f));
        const auto blend = get(key("blend"), 0.0f);
        // A blend parked at one end is that response; anywhere between, or
        // swept by a routing, is the morph that travels the same three.
        const auto morphing = modulated(key("blend")) || (std::abs(blend) > 0.05f && std::abs(blend - 1.0f) > 0.05f
                                                           && std::abs(blend - 2.0f) > 0.05f);
        auto morph = -1.0f;
        switch (model)
        {
            case 5: filterType = FilterType::formant; break;
            case 6: filterType = style == 0 || style == 3 ? FilterType::comb : FilterType::flanger; break;
            case 7: filterType = FilterType::phaser; break;
            case 4: filterType = FilterType::acid; break;
            default:
                if (style == 2) { filterType = FilterType::morphLowNotchHigh; morph = blend * 0.5f; }
                else if (style == 3) filterType = FilterType::notch;
                else if (style == 4) { filterType = FilterType::morphBandPeakNotch; morph = blend * 0.5f; }
                else if (morphing) { filterType = FilterType::morphLowBandHigh; morph = blend * 0.5f; }
                else if (blend > 1.5f) filterType = FilterType::highPass;
                else if (blend > 0.5f) filterType = FilterType::bandPass;
                else if (model == 1) filterType = FilterType::dirtyLadder;
                else if (model == 2 || style == 1) filterType = FilterType::ladder;
                else filterType = FilterType::lowPass;
                break;
        }
        if (model == 5 || model == 4)
            out.warn("FILTER " + juce::String(filter) + ": Vital's " + juce::String(model == 5 ? "formant" : "diode")
                     + " filter is approximated by Forge's " + filterTypeName(static_cast<int>(filterType)));
        out.setIndex("filterType", static_cast<int>(filterType));
        out.set("filterFreq", morph >= 0.0f ? morph : filterSecondInit(filterType));
        out.set("cutoff", noteToHz(get(key("cutoff"), 60.0f)));
        out.set("resonance", get(key("resonance"), 0.5f));
        out.set("drive", driveFrom(get(key("drive"), 0.0f)));
        out.set("filterMix", get(key("mix"), 1.0f));
        out.setOn("filterKeyTrack", get(key("keytrack"), 0.0f) >= 0.25f);
    }

    // Vital's drive is a gain in decibels; Forge's is how far its saturator is
    // pushed, whose gain is one plus twelve times the knob.
    static float driveFrom(float decibels)
    {
        return juce::jlimit(0.0f, 1.0f, (juce::Decibels::decibelsToGain(decibels) - 1.0f) / 12.0f);
    }

    // --- The oscillators -------------------------------------------------------

    static float levelFrom(float stored) { return juce::jlimit(0.0f, 1.0f, stored * stored * 1.5f); }

    float detuneFrom(int osc, float stored) const
    {
        const auto range = get("osc_" + juce::String(osc + 1) + "_detune_range", 2.0f);
        // Vital spreads its outer voices by the range times the percentage;
        // Forge's outer voices sit at 0.7 semitone at the top of DETUNE.
        return juce::jlimit(0.0f, 1.0f, range * stored * stored / 100.0f / (unisonSpreadSemitones * 0.5f));
    }

    // A Vital spectral morph amount as Forge's LPF or HPF warp depth: Vital's
    // cutoff is 2^(10a) harmonics, Forge's 2^(7(1-d)) for the low pass and a
    // quarter of 2^(7d) for the high pass.
    static float morphFrom(int type, float amount)
    {
        const auto harmonic = std::pow(2.0f, 10.0f * juce::jlimit(0.0f, 1.0f, amount));
        if (type == 7) return juce::jlimit(0.0f, 1.0f, 1.0f - std::log2(harmonic + 1.0f) / 7.0f);
        return juce::jlimit(0.0f, 1.0f, std::log2(4.0f * harmonic) / 7.0f);
    }

    // A Vital distortion amount as the depth of the warp it became.
    float distortionFrom(int osc, float amount) const
    {
        const auto a = juce::jlimit(0.0f, 1.0f, amount);
        switch (distortionTypes[static_cast<size_t>(osc)])
        {
            case 1: return juce::jlimit(0.0f, 1.0f, (std::pow(2.0f, 8.0f * a - 4.0f) - 1.0f) / 7.0f);
            case 3:
            {
                const auto steps = std::pow(2.0f, 1.0f + 27.2f * std::pow(1.0f - a, 3.0f));
                return juce::jlimit(0.0f, 1.0f, 1.0f - std::sqrt(juce::jmax(0.0f, steps - 2.0f) / 254.0f));
            }
            case 6: return juce::jlimit(0.0f, 1.0f, a / 0.9f);
            // Vital's FM moves the phase by six cycles at the top; Forge's PD
            // moves it by under half of one.
            case 7: case 8: return juce::jlimit(0.0f, 1.0f, 6.0f * a * a / 0.42f);
            default: break;
        }
        return a;
    }

    // Which Forge warp a Vital distortion is on this oscillator, or OFF.
    static WarpMode warpFor(int type, int osc)
    {
        // Vital names an FM or RM partner as A or B: oscillator 1's A is 2
        // and its B is 3; 2's are 1 and 3; 3's are 1 and 2. Forge names them
        // by the ring, next and previous.
        const auto partner = [osc] (bool first)
        {
            const std::array<std::array<int, 2>, 3> partners {{{1, 2}, {0, 2}, {0, 1}}};
            return partners[static_cast<size_t>(osc)][first ? 0 : 1];
        };
        const auto next = [osc] (int other) { return other == (osc + 1) % oscillatorCount; };
        switch (type)
        {
            case 1: return WarpMode::sync;
            case 3: return WarpMode::quantize;
            case 4: return WarpMode::bendBoth;
            case 5: return WarpMode::asym;
            case 6: return WarpMode::pwm;
            case 7: return next(partner(true)) ? WarpMode::pdOsc : WarpMode::pdOscPrevious;
            case 8: return next(partner(false)) ? WarpMode::pdOsc : WarpMode::pdOscPrevious;
            case 10: return next(partner(true)) ? WarpMode::rmOsc : WarpMode::rmOscPrevious;
            case 11: return next(partner(false)) ? WarpMode::rmOsc : WarpMode::rmOscPrevious;
            default: break;
        }
        return WarpMode::off;
    }

    void oscillator(int osc)
    {
        const auto key = [osc] (const char* suffix) { return "osc_" + juce::String(osc + 1) + "_" + suffix; };
        const auto name = "OSC " + juce::String(osc + 1);
        const auto enabled = on(key("on"));
        out.setOn(oscId(osc, "Enable"), enabled);
        out.setIndex(oscId(osc, "Mode"), static_cast<int>(OscMode::wavetable));

        // The table, rendered only for an oscillator that plays.
        if (enabled)
            if (const auto* tables = settings.getProperty("wavetables", {}).getArray(); tables != nullptr && osc < tables->size())
            {
                std::vector<float> samples;
                auto frames = 0;
                juce::StringArray lost;
                const auto& table = (*tables)[osc];
                if (import::renderVitalWavetable(table, maxEditableFrames, samples, frames,
                                                 lastPositions[static_cast<size_t>(osc)], lost))
                    out.table(osc, samples, frames, table.getProperty("name", "VITAL").toString().toUpperCase());
                for (const auto& line : lost) out.warn(name + ": " + line);
            }
        out.set(oscId(osc, "Position"), positionFrom(osc, get(key("wave_frame"), 0.0f)));

        // Tuning: the global transpose and tune ride on every oscillator.
        const auto transpose = juce::roundToInt(get(key("transpose"), 0.0f) + get("voice_transpose", 0.0f));
        const auto octave = juce::jlimit(-4, 4, transpose / 12);
        out.set(oscId(osc, "Octave"), static_cast<float>(octave));
        out.set(oscId(osc, "Semitone"), static_cast<float>(transpose - octave * 12), name + " TRANSPOSE");
        out.set(oscId(osc, "Fine"), (get(key("tune"), 0.0f) + get("voice_tune", 0.0f)) * 100.0f);

        const auto voices = juce::roundToInt(get(key("unison_voices"), 1.0f));
        out.set(oscId(osc, "Unison"), static_cast<float>(juce::jlimit(1, unisonMax, voices)));
        if (enabled && voices > unisonMax)
            out.warn(name + ": " + juce::String(voices) + " unison voices, Forge stacks " + juce::String(unisonMax));
        out.set(oscId(osc, "Detune"), detuneFrom(osc, get(key("unison_detune"), 4.47214f)));
        out.set(oscId(osc, "Blend"), get(key("unison_blend"), 0.8f));
        if (enabled && voices > 1 && juce::roundToInt(get(key("stack_style"), 0.0f)) != 0)
            out.warn(name + ": Vital's unison stack styles (octaves, chords, harmonics) have no counterpart; "
                            "it is stacked as plain unison");
        out.set(oscId(osc, "Pan"), get(key("pan"), 0.0f));
        out.set(oscId(osc, "Level"), levelFrom(get(key("level"), 0.707107f)));

        // The two warp stages: the spectral morph reads the table and the
        // distortion moves the phase it is read at, which is the order Forge's
        // first and second stages run in.
        auto stage = 0;
        const auto morph = juce::roundToInt(get(key("spectral_morph_type"), 0.0f));
        morphTypes[static_cast<size_t>(osc)] = morph;
        if (morph == 7 || morph == 8)
        {
            morphStage[static_cast<size_t>(osc)] = stage;
            out.setIndex(oscId(osc, ("Warp" + juce::String(stage + 1) + "Mode")),
                         static_cast<int>(morph == 7 ? WarpMode::lowPass : WarpMode::highPass));
            out.set(oscId(osc, ("Warp" + juce::String(stage + 1))),
                    morphFrom(morph, get(key("spectral_morph_amount"), 0.5f)));
            ++stage;
        }
        else if (enabled && morph != 0)
        {
            static const juce::StringArray morphNames {"", "Vocode", "Formant Scale", "Harmonic Stretch",
                                                       "Inharmonic Stretch", "Smear", "Random Amplitudes", "Low Pass",
                                                       "High Pass", "Phase Disperse", "Shepard Tone", "Spectral Time Skew"};
            out.warn(name + ": spectral warp " + morphNames[juce::jlimit(0, 11, morph)].toUpperCase()
                     + " has no counterpart in Forge");
        }
        const auto distortion = juce::roundToInt(get(key("distortion_type"), 0.0f));
        distortionTypes[static_cast<size_t>(osc)] = distortion;
        const auto mode = warpFor(distortion, osc);
        if (mode != WarpMode::off)
        {
            distortionStage[static_cast<size_t>(osc)] = stage;
            out.setIndex(oscId(osc, ("Warp" + juce::String(stage + 1) + "Mode")), static_cast<int>(mode));
            const auto amount = get(key("distortion_amount"), 0.5f);
            out.set(oscId(osc, ("Warp" + juce::String(stage + 1))), distortionFrom(osc, amount));
            if ((distortion == 7 || distortion == 8) && enabled && 6.0f * amount * amount > 0.42f)
                out.warn(name + ": Vital's FM runs deeper than Forge's PD reaches, so it is at Forge's full depth");
        }
        else if (enabled && distortion != 0)
        {
            static const juce::StringArray distortionNames {"", "Sync", "Formant", "Quantize", "Bend", "Squeeze", "Pulse",
                                                            "FM", "FM", "FM from the sample", "RM", "RM",
                                                            "RM from the sample"};
            out.warn(name + ": warp " + distortionNames[juce::jlimit(0, 12, distortion)].toUpperCase()
                     + " has no counterpart in Forge");
        }

        // Through Forge's filter when Vital sent it to the filter that became
        // Forge's, or to both; to the main output otherwise.
        const auto destination = juce::roundToInt(get(key("destination"), 0.0f));
        const auto filtered = filter != 0 && (destination == 2 || destination == filter - 1
                                              || (destination <= 1 && seriesInto(destination + 1)));
        out.setOn("route" + oscillatorLetter(osc), filtered);
        if (enabled && juce::roundToInt(get(key("midi_track"), 1.0f)) == 0)
            out.warn(name + ": it ignores the keyboard in Vital, which Forge's oscillators cannot");
    }

    // Whether Vital's other filter feeds the one Forge kept, in series.
    bool seriesInto(int from) const
    {
        if (filter == 0 || from == filter) return false;
        return on("filter_" + juce::String(filter) + "_filter_input");
    }

    float positionFrom(int osc, float frame) const
    {
        const auto last = lastPositions[static_cast<size_t>(osc)];
        return last <= 0 ? 0.0f : juce::jlimit(0.0f, 1.0f, std::floor(frame) / static_cast<float>(last));
    }

    // Vital's sample oscillator is most often a noise: played by Forge's noise
    // module, the source picked by the sample's name.
    void sample()
    {
        if (!on("sample_on")) return;
        const auto name = settings.getProperty("sample", {}).getProperty("name", {}).toString();
        const auto lower = name.toLowerCase();
        auto source = NoiseSource::white;
        sampleIsNoise = true;
        if (lower.contains("pink")) source = NoiseSource::pink;
        else if (lower.contains("brown")) source = NoiseSource::brown;
        else if (lower.contains("vinyl")) source = NoiseSource::vinyl;
        else if (lower.contains("crackle")) source = NoiseSource::crackle;
        else if (lower.contains("wind")) source = NoiseSource::wind;
        else if (lower.contains("hiss") || lower.contains("tape")) source = NoiseSource::tapeHiss;
        else if (lower.contains("hum")) source = NoiseSource::hardwareHum;
        else if (!lower.contains("white") && !lower.contains("noise"))
            out.warn("SAMPLE: \"" + name + "\" is played as Forge's white noise; Forge has no sample oscillator");
        out.setOn("noiseEnable", true);
        out.setIndex("noiseSource", static_cast<int>(source));
        out.set("noiseLevel", levelFrom(get("sample_level", 0.707107f)));
        out.set("noisePan", get("sample_pan", 0.0f));
        const auto destination = juce::roundToInt(get("sample_destination", 3.0f));
        out.setOn("routeNoise", filter != 0 && (destination == 2 || destination == filter - 1));
        if (juce::roundToInt(get("sample_transpose", 0.0f)) != 0 || on("sample_keytrack"))
            out.warn("SAMPLE: its pitch is not converted; Forge's noise has none");
    }

    // --- Envelopes and LFOs ----------------------------------------------------

    // Forge's envelope or LFO for one of Vital's, given the first time a
    // routing asks for it; -1 when Forge has none left.
    int envelopeSlot(const juce::String& vital)
    {
        if (const auto found = envSlots.find(vital); found != envSlots.end()) return found->second;
        // ENV 1 is the amplitude in both, so it is always ENV 1.
        const auto slot = vital == "env_1" ? 0 : static_cast<int>(envSlots.size() + (envSlots.count("env_1") ? 0 : 1));
        if (slot >= envCount) return -1;
        envSlots[vital] = slot;
        return slot;
    }

    int lfoSlot(const juce::String& vital)
    {
        if (const auto found = lfoSlots.find(vital); found != lfoSlots.end()) return found->second;
        const auto slot = static_cast<int>(lfoSlots.size());
        if (slot >= lfoCount) return -1;
        lfoSlots[vital] = slot;
        return slot;
    }

    static float envelopeSeconds(float stored) { return std::pow(juce::jmax(0.0f, stored), 4.0f); }

    void envelopes()
    {
        for (const auto& [vital, slot] : envSlots)
        {
            const auto key = [&vital] (const char* suffix) { return vital + "_" + suffix; };
            const auto caption = "ENV " + juce::String(slot + 1);
            out.set(envParameterId(slot, "Attack"), envelopeSeconds(get(key("attack"), 0.1495f)), caption + " ATTACK");
            out.set(envParameterId(slot, "Decay"), envelopeSeconds(get(key("decay"), 1.0f)), caption + " DECAY");
            out.set(envParameterId(slot, "Release"), envelopeSeconds(get(key("release"), 0.5476f)), caption + " RELEASE");
            // Vital squares its amplitude envelope, so a sustain of a half
            // plays at a quarter; Forge's amplitude is the envelope itself.
            const auto sustain = get(key("sustain"), 1.0f);
            out.set(envParameterId(slot, "Sustain"), slot == ampEnv ? sustain * sustain : sustain);
            const auto delay = envelopeSeconds(get(key("delay"), 0.0f));
            const auto hold = envelopeSeconds(get(key("hold"), 0.0f));
            if (delay > 0.002f || hold > 0.002f)
                out.warn(caption + ": its delay and hold stages are left out; Forge's envelopes have neither");
        }
    }

    // An LFO's shape: Vital's line, its y measured down from the top, turned
    // into Forge's -1..1 and fitted with Forge's bends; or one of Forge's own
    // four shapes when it is one of them.
    void lfoShape(const juce::var& line, int slot)
    {
        const auto sample = [&line] (float phase) { return 1.0f - 2.0f * import::vitalLineValue(line, phase); };
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
        std::vector<float> corners;
        if (const auto* points = line.getProperty("points", {}).getArray())
            for (int i = 0; i + 1 < points->size(); i += 2) corners.push_back(static_cast<float>((*points)[i]));
        const auto table = import::lfoTableFrom(corners, sample);
        if (table.custom) out.lfoShape(slot, table, line.getProperty("name", "VITAL").toString());
    }

    void lfos()
    {
        const auto* shapes = settings.getProperty("lfos", {}).getArray();
        for (const auto& [vital, slot] : lfoSlots)
        {
            const auto index = vital.getTrailingIntValue() - 1;
            const auto key = [&vital] (const char* suffix) { return vital + "_" + suffix; };
            const auto caption = "LFO " + juce::String(index + 1);
            out.setIndex(lfoParameterId(slot, "Type"), static_cast<int>(LfoType::normal));
            if (shapes != nullptr && index >= 0 && index < shapes->size()) lfoShape((*shapes)[index], slot);

            const auto sync = juce::roundToInt(get(key("sync"), 1.0f));
            if (sync >= 1 && sync <= 3)
            {
                const auto beats = tempoBeats(sync, get(key("tempo"), 7.0f));
                if (beats <= 0.0f)
                {
                    out.setIndex(lfoParameterId(slot, "RateUnit"), 0);
                    out.set(lfoParameterId(slot, "Rate"), 0.05f);
                    out.warn(caption + ": frozen in Vital; it runs at Forge's slowest rate");
                }
                else
                {
                    const auto division = import::nearestLfoDivision(beats);
                    out.setIndex(lfoParameterId(slot, "RateUnit"), 1);
                    out.setIndex(lfoParameterId(slot, "Division"), division);
                    const auto landed = lfoDivisions()[static_cast<size_t>(division)].beats;
                    if (std::abs(std::log2(landed / beats)) > 0.05f)
                        out.warn(caption + ": a cycle of " + juce::String(beats, 3) + " beats is not one of Forge's "
                                 "divisions; set to " + lfoDivisions()[static_cast<size_t>(division)].label);
                }
            }
            else
            {
                auto hz = std::pow(2.0f, get(key("frequency"), 1.0f));
                if (sync == 4)
                {
                    hz = noteToHz(60.0f + get(key("keytrack_transpose"), -12.0f) + get(key("keytrack_tune"), 0.0f));
                    out.warn(caption + ": its rate follows the keyboard in Vital; Forge's is fixed at middle C's");
                }
                out.setIndex(lfoParameterId(slot, "RateUnit"), 0);
                out.set(lfoParameterId(slot, "Rate"), hz, caption + " RATE");
            }
            // TRIG restarts on every note, OFF free-runs, ENV runs once.
            const auto type = juce::roundToInt(get(key("sync_type"), 0.0f));
            out.setIndex(lfoParameterId(slot, "Mode"), type == 1 ? static_cast<int>(LfoMode::free)
                                                        : type == 2 || type == 3 ? static_cast<int>(LfoMode::envelope)
                                                                                 : static_cast<int>(LfoMode::trigger));
            if (type >= 4) out.warn(caption + ": Vital's loop points are not converted; it loops whole");
        }
    }

    // --- Routings ----------------------------------------------------------------

    // Where a Vital control lands in Forge: the parameter, the control's stored
    // range in Vital, and how a stored value becomes Forge's.
    struct Target
    {
        juce::String id;
        float low = 0.0f, high = 1.0f;
        std::function<float(float)> toForge;
    };

    std::optional<Target> targetFor(const juce::String& key)
    {
        const auto identity = [] (float v) { return v; };
        for (int osc = 0; osc < oscillatorCount; ++osc)
        {
            const auto prefix = "osc_" + juce::String(osc + 1) + "_";
            if (!key.startsWith(prefix)) continue;
            const auto suffix = key.fromFirstOccurrenceOf(prefix, false, false);
            if (suffix == "level") return Target {oscId(osc, "Level"), 0.0f, 1.0f, levelFrom};
            if (suffix == "pan") return Target {oscId(osc, "Pan"), -1.0f, 1.0f, identity};
            if (suffix == "wave_frame")
                return Target {oscId(osc, "Position"), 0.0f, 256.0f, [this, osc] (float v) { return positionFrom(osc, v); }};
            if (suffix == "transpose")
            {
                const auto octave = out.valueOf(oscId(osc, "Octave")) * 12.0f;
                return Target {oscId(osc, "Semitone"), -48.0f, 48.0f, [octave] (float v) { return v - octave; }};
            }
            if (suffix == "tune") return Target {oscId(osc, "Fine"), -1.0f, 1.0f, [] (float v) { return v * 100.0f; }};
            if (suffix == "unison_detune")
                return Target {oscId(osc, "Detune"), 0.0f, 10.0f, [this, osc] (float v) { return detuneFrom(osc, v); }};
            if (suffix == "distortion_amount" && distortionStage[static_cast<size_t>(osc)] >= 0)
                return Target {oscId(osc, ("Warp" + juce::String(distortionStage[static_cast<size_t>(osc)] + 1))),
                               0.0f, 1.0f, [this, osc] (float v) { return distortionFrom(osc, v); }};
            if (suffix == "spectral_morph_amount" && morphStage[static_cast<size_t>(osc)] >= 0)
            {
                const auto type = morphTypes[static_cast<size_t>(osc)];
                return Target {oscId(osc, ("Warp" + juce::String(morphStage[static_cast<size_t>(osc)] + 1))),
                               0.0f, 1.0f, [type] (float v) { return morphFrom(type, v); }};
            }
            return std::nullopt;
        }
        if (filter != 0 && key.startsWith("filter_" + juce::String(filter) + "_"))
        {
            const auto suffix = key.fromFirstOccurrenceOf("filter_" + juce::String(filter) + "_", false, false);
            if (suffix == "cutoff") return Target {"cutoff", 8.0f, 136.0f, noteToHz};
            if (suffix == "resonance") return Target {"resonance", 0.0f, 1.0f, identity};
            if (suffix == "drive") return Target {"drive", 0.0f, 20.0f, driveFrom};
            if (suffix == "mix") return Target {"filterMix", 0.0f, 1.0f, identity};
            if (suffix == "blend" && filterCategoryOf(filterType) == FilterCategory::morph)
                return Target {"filterFreq", 0.0f, 2.0f, [] (float v) { return v * 0.5f; }};
            return std::nullopt;
        }
        for (const auto& [vital, slot] : envSlots)
        {
            if (!key.startsWith(vital + "_")) continue;
            const auto suffix = key.fromFirstOccurrenceOf(vital + "_", false, false);
            if (suffix == "attack" || suffix == "decay" || suffix == "release")
                return Target {envParameterId(slot, suffix == "attack" ? "Attack" : suffix == "decay" ? "Decay" : "Release"),
                               0.0f, 2.37842f, envelopeSeconds};
            if (suffix == "sustain") return Target {envParameterId(slot, "Sustain"), 0.0f, 1.0f, identity};
        }
        if (sampleIsNoise && key == "sample_level") return Target {"noiseLevel", 0.0f, 1.0f, levelFrom};
        if (sampleIsNoise && key == "sample_pan") return Target {"noisePan", -1.0f, 1.0f, identity};
        // An effect's wet/dry is the one control every rack slot has; the
        // others are the knobs a patch most often sweeps, on the slot the
        // effect became.
        for (const auto& [effect, slot] : fxSlots)
        {
            const auto mix = effect == "flanger" ? juce::String() : effect == "distortion" || effect == "compressor"
                || effect == "filter_fx" ? effect + "_mix" : effect + "_dry_wet";
            if (key == mix) return Target {fxParameterId(0, slot, "Mix"), 0.0f, 1.0f, identity};
            const auto knob = [slot] (int which) { return fxParameterId(0, slot, "Knob") + juce::String(which); };
            if (effect == "filter_fx" && key == "filter_fx_cutoff")
                return Target {knob(1), 8.0f, 136.0f, [] (float v) { return fxUnitHz(noteToHz(v), 30.0f, 18000.0f); }};
            if (effect == "filter_fx" && key == "filter_fx_resonance") return Target {knob(2), 0.0f, 1.0f, identity};
            if (effect == "distortion" && key == "distortion_drive")
                return Target {knob(1), -30.0f, 30.0f, [] (float v) { return fxUnit(v, -6.0f, 30.0f); }};
            if (effect == "phaser" && key == "phaser_center")
                return Target {knob(3), 8.0f, 136.0f, [] (float v) { return fxUnitHz(noteToHz(v), 80.0f, 8000.0f); }};
            if (effect == "chorus" && key == "chorus_mod_depth") return Target {knob(4), 0.0f, 1.0f, identity};
            if (effect == "delay" && key == "delay_feedback")
                return Target {knob(3), -1.0f, 1.0f, [] (float v) { return std::abs(v); }};
            if (effect == "eq" && (key == "eq_low_gain" || key == "eq_high_gain"))
                return Target {knob(key == "eq_low_gain" ? 3 : 6), -15.0f, 15.0f,
                               [] (float v) { return fxUnit(v, -18.0f, 18.0f); }};
        }
        return std::nullopt;
    }

    // A routing's reach in Forge's terms, worked out at both ends in Vital's.
    // `half` is a bipolar routing, which swings half the amount either way.
    float depthFor(const Target& target, float base, float amount) const
    {
        const auto range = target.high - target.low;
        const auto clampStored = [&target] (float v) { return juce::jlimit(target.low, target.high, v); };
        const auto from = out.normalised(target.id, target.toForge(clampStored(base)));
        const auto to = out.normalised(target.id, target.toForge(clampStored(base + amount * range)));
        return to - from;
    }

    int sourceFor(const juce::String& source, const juce::String& destination)
    {
        if (source.startsWith("env_"))
        {
            const auto slot = envelopeSlot(source);
            if (slot >= 0) return static_cast<int>(ModSource::env1) + slot;
            lose(source, destination, "Forge's four envelopes are all in use");
            return 0;
        }
        if (source.startsWith("lfo_"))
        {
            const auto slot = lfoSlot(source);
            if (slot >= 0) return static_cast<int>(ModSource::lfo1) + slot;
            lose(source, destination, "Forge's six LFOs are all in use");
            return 0;
        }
        if (source.startsWith("macro_control_"))
            return static_cast<int>(ModSource::macro1) + juce::jlimit(0, 3, source.getTrailingIntValue() - 1);
        if (source == "velocity") return static_cast<int>(ModSource::velocity);
        if (source == "note") return static_cast<int>(ModSource::note);
        if (source == "mod_wheel") return static_cast<int>(ModSource::modWheel);
        lose(source, destination, "Forge has no " + vitalSourceCaption(source).toLowerCase() + " source");
        return 0;
    }

    // One warning per source that went missing, however many routings it fed.
    void lose(const juce::String& source, const juce::String& destination, const juce::String& why)
    {
        auto& routes = lostSources[source];
        routes.addIfNotAlreadyThere(vitalSourceCaption(destination));
        lostReasons[source] = why;
    }

    std::map<juce::String, juce::StringArray> lostSources;
    std::map<juce::String, juce::String> lostReasons;

    void routings()
    {
        const auto* list = settings.getProperty("modulations", {}).getArray();
        if (list == nullptr) return;

        struct Route
        {
            int index;
            juce::String source, destination;
        };
        std::vector<Route> all;
        for (int i = 0; i < list->size(); ++i)
        {
            const auto source = (*list)[i].getProperty("source", {}).toString();
            const auto destination = (*list)[i].getProperty("destination", {}).toString();
            if (source.isEmpty() || destination.isEmpty()) continue;
            if (get("modulation_" + juce::String(i + 1) + "_bypass", 0.0f) >= 0.5f) continue;
            all.push_back({i, source, destination});
        }

        // A routing can scale another routing's depth, which Forge's matrix
        // cannot. The scaling source's share is folded into the depth it
        // scales, at a position that source typically sits at: a macro or the
        // wheel where the preset was saved, velocity at a firm hit, the note at
        // middle C, an LFO or a random source at its average. Vital's common
        // idiom, velocity scaling an envelope's reach into a level, then still
        // plays. An envelope or a pressure source has no such position and is
        // left out. A depth runs -1..1, so a source at the top of an amount of
        // one moves it by two.
        std::map<int, float> amounts;
        for (const auto& route : all)
            amounts[route.index] = get("modulation_" + juce::String(route.index + 1) + "_amount", 0.0f);
        const auto typical = [this] (const juce::String& source) -> std::optional<float>
        {
            if (source.startsWith("macro_control_")) return get(source, 0.0f);
            if (source == "mod_wheel") return get("mod_wheel", 0.0f);
            if (source == "velocity") return 0.8f;
            if (source == "note") return 60.0f / 127.0f;
            if (source.startsWith("lfo_") || source.startsWith("random")) return 0.5f;
            return std::nullopt;
        };
        juce::StringArray foldedSources, lostDepthSources;
        for (const auto& route : all)
        {
            if (!route.destination.startsWith("modulation_") || !route.destination.endsWith("_amount")) continue;
            const auto scaled = route.destination.fromFirstOccurrenceOf("modulation_", false, false).getIntValue() - 1;
            const auto position = typical(route.source);
            if (!position.has_value() || amounts.count(scaled) == 0)
            {
                lostDepthSources.addIfNotAlreadyThere(vitalSourceCaption(route.source));
                continue;
            }
            const auto prefix = "modulation_" + juce::String(route.index + 1) + "_";
            const auto swing = get(prefix + "bipolar", 0.0f) >= 0.5f ? *position - 0.5f : *position;
            amounts[scaled] = juce::jlimit(-1.0f, 1.0f, amounts[scaled] + swing * amounts[route.index] * 2.0f);
            foldedSources.addIfNotAlreadyThere(vitalSourceCaption(route.source));
        }
        const auto amountOf = [&amounts] (int index) { return amounts[index]; };

        // Allocated in the order the routings use them, so the first LFO the
        // patch uses is LFO 1, whatever Vital numbered it.
        juce::StringArray unreachable;
        std::vector<std::tuple<Route, int, Target>> mapped;
        for (const auto& route : all)
        {
            if (route.destination.startsWith("modulation_") || std::abs(amountOf(route.index)) < 1.0e-4f) continue;
            const auto target = targetFor(route.destination);
            if (!target.has_value())
            {
                unreachable.addIfNotAlreadyThere(vitalSourceCaption(route.destination));
                continue;
            }
            const auto source = sourceFor(route.source, route.destination);
            if (source == 0) continue;
            mapped.emplace_back(route, source, *target);
        }

        // A unipolar LFO in Vital rises from the setting; Forge's LFOs swing
        // either side of it. The setting moves by half the reach first, for
        // every such routing, and the depths are worked out from where it ends.
        // The reach is what the control can actually travel: an LFO taking a
        // level down by more than it has only ever reaches nothing, and
        // centring on the whole reach would park the level below zero.
        std::map<juce::String, float> stored;
        std::map<juce::String, const Target*> shifted;
        for (const auto& [route, source, target] : mapped)
            stored.emplace(target.id, get(route.destination, target.low));
        for (const auto& [route, source, target] : mapped)
        {
            const auto prefix = "modulation_" + juce::String(route.index + 1) + "_";
            if (lfoIndexOf(source) < 0 || get(prefix + "bipolar", 0.0f) >= 0.5f) continue;
            const auto range = target.high - target.low;
            const auto base = get(route.destination, target.low);
            amounts[route.index] = (juce::jlimit(target.low, target.high, base + amountOf(route.index) * range) - base) / range;
            stored[target.id] += amountOf(route.index) * 0.5f * range;
            shifted[target.id] = &target;
        }
        for (const auto& [id, target] : shifted)
            out.set(id, target->toForge(juce::jlimit(target->low, target->high, stored[id])));

        for (const auto& [route, source, target] : mapped)
        {
            const auto prefix = "modulation_" + juce::String(route.index + 1) + "_";
            const auto amount = amountOf(route.index);
            const auto bipolar = get(prefix + "bipolar", 0.0f) >= 0.5f;
            const auto lfo = lfoIndexOf(source) >= 0;
            // An LFO already swings a whole depth either way, so its depth is
            // half the reach. A bipolar routing of anything else is BI, which
            // takes half a source from it before the depth applies, so it
            // swings half a depth either way and keeps the whole reach.
            const auto depth = depthFor(target, stored[target.id], lfo ? amount * 0.5f : amount);
            out.route(source, target.id, depth, bipolar && !lfo,
                      vitalSourceCaption(route.source) + " -> " + vitalSourceCaption(route.destination));
        }

        for (const auto& [source, destinations] : lostSources)
            out.warn(vitalSourceCaption(source) + " -> " + destinations.joinIntoString(", ") + ": " + lostReasons[source]);
        if (!unreachable.isEmpty())
            out.warn("Routings to " + unreachable.joinIntoString(", ") + " are left out; Forge cannot modulate "
                     + (unreachable.size() == 1 ? "it" : "them"));
        if (!foldedSources.isEmpty())
            out.warn(foldedSources.joinIntoString(", ") + " scale" + (foldedSources.size() == 1 ? "s" : "")
                     + " other routings' depths; Forge's matrix cannot, so each depth is fixed where its source "
                       "usually sits (a macro or the wheel as saved, velocity at a firm hit)");
        if (!lostDepthSources.isEmpty())
            out.warn(lostDepthSources.joinIntoString(", ") + " scale" + (lostDepthSources.size() == 1 ? "s" : "")
                     + " other routings' depths; Forge's matrix cannot, so that scaling is left out");

        // Forge keeps the mod wheel out of a patch, as a performance control
        // is, so a preset saved with the wheel up opens with it down.
        const auto wheel = get("mod_wheel", 0.0f);
        auto wheelRoutes = 0;
        for (const auto& route : all) if (route.source == "mod_wheel") ++wheelRoutes;
        if (wheel > 0.05f && wheelRoutes > 0)
            out.warn("MOD WHEEL: Vital opens this preset with the wheel at " + juce::String(juce::roundToInt(wheel * 100.0f))
                     + "%; raise Forge's mod wheel to hear it as saved");
    }

    // --- Effects -------------------------------------------------------------------

    // Vital's chain, first processed first, as the indices its effects are
    // numbered by: chorus, compressor, delay, distortion, EQ, filter, flanger,
    // phaser, reverb.
    std::array<int, 9> effectOrder() const
    {
        std::array<int, 9> order {};
        for (int i = 0; i < 9; ++i) order[static_cast<size_t>(i)] = i;
        auto code = static_cast<int>(get("effect_chain_order", 0.0f));
        for (int i = 0; i < 9; ++i)
        {
            const auto remaining = 9 - i;
            const auto index = remaining - 1;
            const auto inverse = code % remaining;
            code /= remaining;
            const auto picked = order[static_cast<size_t>(index - inverse)];
            for (int j = index - inverse; j < index; ++j) order[static_cast<size_t>(j)] = order[static_cast<size_t>(j + 1)];
            order[static_cast<size_t>(index)] = picked;
        }
        return order;
    }

    static float fxUnit(float value, float low, float high, float skew = 1.0f)
    {
        const auto unit = juce::jlimit(0.0f, 1.0f, (value - low) / (high - low));
        return skew == 1.0f ? unit : std::pow(unit, 1.0f / skew);
    }

    static float fxUnitHz(float hz, float low, float high)
    {
        return juce::jlimit(0.0f, 1.0f, std::log(juce::jmax(low, hz) / low) / std::log(high / low));
    }

    static float modeValue(int choice, int count) { return count <= 1 ? 0.0f : static_cast<float>(choice) / static_cast<float>(count - 1); }

    // The knob that picks a delay or rate division nearest a cycle of so
    // many beats.
    static float divisionKnob(float beats)
    {
        auto best = 0;
        for (int i = 0; i < fxDivisionCount; ++i)
            if (std::abs(std::log2(fxDivisions()[static_cast<size_t>(i)].beats / beats))
                < std::abs(std::log2(fxDivisions()[static_cast<size_t>(best)].beats / beats)))
                best = i;
        return modeValue(best, fxDivisionCount);
    }

    void effects()
    {
        static const std::array<const char*, 9> names {"chorus", "compressor", "delay", "distortion", "eq",
                                                       "filter_fx", "flanger", "phaser", "reverb"};
        auto slot = 0;
        for (const auto effect : effectOrder())
        {
            const juce::String name(names[static_cast<size_t>(effect)]);
            if (!on(name + "_on")) continue;
            if (slot >= fxSlotCount) break;
            const auto id = [slot] (const char* suffix) { return fxParameterId(0, slot, suffix); };
            const auto knob = [&id, this] (int which, float value)
            {
                out.set(id("Knob") + juce::String(which + 1), juce::jlimit(0.0f, 1.0f, value));
            };
            const auto key = [&name] (const char* suffix) { return name + "_" + suffix; };
            const auto rate = [this, &key, &knob, &id] (int unitMode, bool modeIsA)
            {
                const auto sync = juce::roundToInt(get(key("sync"), 1.0f));
                if (sync >= 1 && sync <= 3)
                {
                    const auto beats = tempoBeats(sync, get(key("tempo"), 4.0f));
                    out.set(id(modeIsA ? "ModeA" : "ModeB"), modeValue(1, unitMode));
                    knob(0, beats > 0.0f ? divisionKnob(beats) : 1.0f);
                }
                else
                {
                    out.set(id(modeIsA ? "ModeA" : "ModeB"), 0.0f);
                    knob(0, fxUnit(std::pow(2.0f, get(key("frequency"), -3.0f)), 0.02f, 8.0f, 2.0f));
                }
            };

            fxSlots[name] = slot;
            if (name == "chorus")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::chorus));
                rate(2, true);
                knob(1, fxUnit(std::pow(2.0f, get(key("delay_1"), -9.0f)) * 1000.0f, 0.5f, 30.0f));
                knob(2, fxUnit(std::pow(2.0f, get(key("delay_2"), -7.0f)) * 1000.0f, 0.5f, 30.0f));
                knob(3, get(key("mod_depth"), 0.5f));
                knob(4, std::abs(get(key("feedback"), 0.4f)));
                knob(5, fxUnitHz(noteToHz(get(key("cutoff"), 60.0f)), 120.0f, 16000.0f));
                out.set(id("Mix"), get(key("dry_wet"), 0.5f));
            }
            else if (name == "delay")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::delay));
                const auto sync = juce::roundToInt(get(key("sync"), 1.0f));
                const auto leftSeconds = sync >= 1 && sync <= 3 ? 0.0f : std::pow(2.0f, -get(key("frequency"), 2.0f));
                if (sync >= 1 && sync <= 3)
                {
                    out.set(id("ModeB"), modeValue(1, 2));
                    const auto beats = tempoBeats(sync, get(key("tempo"), 9.0f));
                    knob(0, beats > 0.0f ? divisionKnob(beats) : 0.5f);
                }
                else
                {
                    out.set(id("ModeB"), 0.0f);
                    knob(0, fxUnit(leftSeconds, 0.01f, fxMaxDelayTime, 2.0f));
                }
                knob(1, 0.5f);
                const auto style = juce::roundToInt(get(key("style"), 0.0f));
                out.set(id("ModeA"), modeValue(style >= 2 ? 1 : 0, 2));
                knob(2, std::abs(get(key("feedback"), 0.5f)));
                knob(3, fxUnitHz(noteToHz(get(key("filter_cutoff"), 60.0f)), 200.0f, 16000.0f));
                knob(4, 1.0f - get(key("filter_spread"), 1.0f));
                out.set(id("Mix"), get(key("dry_wet"), 0.3334f));
            }
            else if (name == "reverb")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::reverb));
                const auto decay = std::pow(2.0f, get(key("decay_time"), 0.0f));
                out.set(id("ModeA"), modeValue(decay > 3.0f ? 1 : 0, 2));
                knob(0, 0.25f * get(key("size"), 0.5f) + 0.75f * (0.5f + std::log2(decay) / 8.0f));
                knob(1, fxUnit(get(key("delay"), 0.0f), 0.0f, 0.2f));
                knob(2, juce::jlimit(0.0f, 1.0f, -get(key("high_shelf_gain"), -1.0f) / 6.0f));
                knob(3, 1.0f);
                knob(4, fxUnitHz(noteToHz(get(key("pre_low_cutoff"), 0.0f)), 20.0f, 1200.0f));
                knob(5, fxUnitHz(noteToHz(get(key("pre_high_cutoff"), 110.0f)), 1200.0f, 20000.0f));
                out.set(id("Mix"), get(key("dry_wet"), 0.25f));
            }
            else if (name == "distortion")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::distortion));
                static constexpr std::array<int, 6> shapes {1, 2, 4, 5, 6, 7};
                out.set(id("ModeA"), modeValue(shapes[static_cast<size_t>(juce::jlimit(0, 5,
                                                   juce::roundToInt(get(key("type"), 0.0f))))], 8));
                knob(0, fxUnit(get(key("drive"), 0.0f), -6.0f, 30.0f));
                const auto order = juce::roundToInt(get(key("filter_order"), 0.0f));
                const auto highPass = get(key("filter_blend"), 0.0f) >= 1.0f;
                const auto choice = order == 0 ? 0 : order == 1 ? (highPass ? 1 : 2) : (highPass ? 3 : 4);
                out.set(id("ModeB"), modeValue(choice, 5));
                knob(1, fxUnitHz(noteToHz(get(key("filter_cutoff"), 80.0f)), 40.0f, 16000.0f));
                const auto r = get(key("filter_resonance"), 0.5f);
                knob(2, fxUnit(0.5f + 15.5f * r * r * r, 0.4f, 8.0f));
                out.set(id("Mix"), get(key("mix"), 1.0f));
            }
            else if (name == "eq")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::equaliser));
                const auto q = [this, &key] (const char* band)
                {
                    const auto r = std::pow(get(key(band), 0.3163f), 2.0f);
                    return fxUnit(0.5f + 15.5f * r * r * r, 0.2f, 6.0f);
                };
                out.set(id("ModeA"), modeValue(juce::roundToInt(get(key("low_mode"), 0.0f)) == 1 ? 2 : 0, 3));
                knob(0, fxUnitHz(noteToHz(get(key("low_cutoff"), 40.0f)), 20.0f, 2000.0f));
                knob(1, q("low_resonance"));
                knob(2, fxUnit(get(key("low_gain"), 0.0f), -18.0f, 18.0f));
                out.set(id("ModeB"), modeValue(juce::roundToInt(get(key("high_mode"), 0.0f)) == 1 ? 2 : 0, 3));
                knob(3, fxUnitHz(noteToHz(get(key("high_cutoff"), 100.0f)), 500.0f, 18000.0f));
                knob(4, q("high_resonance"));
                knob(5, fxUnit(get(key("high_gain"), 0.0f), -18.0f, 18.0f));
                if (std::abs(get(key("band_gain"), 0.0f)) > 0.5f || juce::roundToInt(get(key("band_mode"), 0.0f)) == 1)
                    out.warn("EQ: its middle band is left out; Forge's equaliser has two");
                out.set(id("Mix"), 1.0f);
            }
            else if (name == "compressor")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::compressor));
                knob(0, fxUnit(get(key("band_upper_threshold"), -25.0f), -60.0f, 0.0f));
                const auto ratio = get(key("band_upper_ratio"), 0.857f);
                knob(1, fxUnit(ratio >= 0.95f ? 20.0f : 1.0f / (1.0f - ratio), 1.0f, 20.0f, 1.7f));
                knob(2, fxUnit(0.0014f * std::exp(8.0f * get(key("attack"), 0.5f) - 4.0f), 0.0001f, 0.1f, 2.0f));
                knob(3, fxUnit(0.028f * std::exp(8.0f * get(key("release"), 0.5f) - 4.0f), 0.01f, 1.0f, 2.0f));
                knob(4, fxUnit(get(key("band_gain"), 11.7f), 0.0f, 24.0f));
                knob(5, 0.25f);
                out.set(id("ModeB"), 0.0f);
                out.set(id("Mix"), get(key("mix"), 1.0f));
                if (juce::roundToInt(get(key("enabled_bands"), 0.0f)) == 0)
                    out.warn("COMPRESSOR: Vital's three bands are folded into Forge's one, set from the middle band");
            }
            else if (name == "flanger")
            {
                // Vital's centre is a pitch whose period is the delay, and its
                // sweep is in pitch around it; Forge's sweep is in time from
                // DELAY up. The depth crosses as a share of each one's range,
                // and DELAY is put half of it below the centre, so the sweep
                // straddles the same place.
                out.setIndex(id("Type"), static_cast<int>(FxType::flanger));
                rate(2, true);
                const auto depth = juce::jlimit(0.0f, 1.0f, get(key("mod_depth"), 0.5f));
                knob(1, depth);
                knob(2, fxUnit(juce::jlimit(-0.95f, 0.95f, get(key("feedback"), 0.5f)), -0.95f, 0.95f));
                const auto centre = 1000.0f / noteToHz(get(key("center"), 64.0f));
                knob(3, fxUnit(juce::jlimit(0.1f, 10.0f, centre - fxFlangerDepthMs(depth) * 0.5f), 0.1f, 10.0f, 2.0f));
                auto degrees = get(key("phase_offset"), 0.333333f) * 360.0f;
                if (degrees > 180.0f) degrees -= 360.0f;
                knob(4, fxUnit(degrees, -180.0f, 180.0f));
                knob(5, 0.5f);
                out.set(id("Mix"), get(key("dry_wet"), 0.5f));
            }
            else if (name == "phaser")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::phaser));
                rate(2, true);
                knob(1, fxUnit(get(key("mod_depth"), 24.0f) / 12.0f, 0.0f, 4.0f));
                knob(2, fxUnitHz(noteToHz(get(key("center"), 80.0f)), 80.0f, 8000.0f));
                knob(3, 0.5f + 0.5f * get(key("feedback"), 0.5f));
                knob(4, fxUnit(get(key("phase_offset"), 0.333333f) * 360.0f - 180.0f, -180.0f, 180.0f));
                knob(5, 1.0f);
                out.set(id("Mix"), get(key("dry_wet"), 1.0f));
            }
            else if (name == "filter_fx")
            {
                out.setIndex(id("Type"), static_cast<int>(FxType::filter));
                const auto blend = get(key("blend"), 0.0f);
                const auto style = juce::roundToInt(get(key("style"), 0.0f));
                const auto model = style == 2 || style == 3 ? 3 : blend > 1.5f ? 4 : blend > 0.5f ? 7 : style == 1 ? 2 : 0;
                out.set(id("ModeA"), modeValue(model, 8));
                knob(0, fxUnitHz(noteToHz(get(key("cutoff"), 60.0f)), 30.0f, 18000.0f));
                knob(1, get(key("resonance"), 0.5f));
                knob(2, driveFrom(get(key("drive"), 0.0f)));
                knob(3, 0.0f);
                knob(4, 0.5f);
                out.set(id("Mix"), get(key("mix"), 1.0f));
            }
            out.set(id("Level"), 1.0f);
            ++slot;
        }
    }
};

}

juce::Result importVitalPreset(const juce::File& file, const juce::AudioProcessorValueTreeState& parameters,
                               ImportedPreset& result)
{
    juce::var root;
    const auto parsed = juce::JSON::parse(file.loadFileAsString(), root);
    if (parsed.failed() || !root.isObject()) return juce::Result::fail("That Vital preset is not readable JSON.");
    if (!root.getProperty("settings", {}).isObject()) return juce::Result::fail("That Vital preset has no settings.");
    PatchWriter writer(parameters, result);
    VitalConverter(root, writer, result).convert();
    writer.finish();
    return juce::Result::ok();
}
}
