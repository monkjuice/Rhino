#include "ForgeProcessor.h"
#include "ForgeEditor.h"

// The processor: what the host calls, and what it hands the engine. A block
// reads the parameters into a Patch and a Modulation and renders from those,
// so nothing below this line touches a parameter object on the audio thread.
//
// Two files beside this one define the same class: ForgeParameters.cpp
// declares every parameter and what its value reads as, and ForgeProcessorState.cpp
// carries state, presets and wavetable files.
namespace rhino::forge
{
Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "RhinoForgeState", parameterLayout())
{
}

void Processor::prepareToPlay(double sampleRate, int)
{
    core.initialise(sampleRate);
    arp.reset();
    preparedSampleRate = juce::jmax(1.0, sampleRate);
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

// How long one arp step lasts. The same division of labour the LFOs use: the
// tempo is resolved into seconds here, and ForgeArp.h never sees a BPM.
//
// TRIP and DOT scale whichever division is chosen rather than being entries in
// the list, which is what keeps that list seven long instead of twenty-one. Both
// at once is a dotted triplet, which is a real if unusual rate, so neither
// switch cancels the other.
float Processor::arpStepSeconds() const
{
    const auto value = [this] (const char* id) { return state.getRawParameterValue(id)->load(); };
    if (value("arpRateUnit") < 0.5f)
        return 1.0f / juce::jmax(0.01f, value("arpRate"));

    const auto beats = arpDivisions()[static_cast<size_t>(
        juce::jlimit(0, arpDivisionCount - 1, juce::roundToInt(value("arpDivision"))))].beats;
    const auto bpm = hostBpm.load(std::memory_order_relaxed);
    // Standing in for a host that reports no tempo, so a synced arp still runs
    // at a musical rate in a standalone rather than stopping dead.
    const auto tempo = bpm > 0.0 ? bpm : 120.0;
    auto scale = 1.0f;
    if (value("arpTriplet") >= 0.5f) scale *= 2.0f / 3.0f;
    if (value("arpDotted") >= 0.5f) scale *= 1.5f;
    return static_cast<float>(60.0 / tempo) * beats * scale;
}

// Everything the arp reads, gathered once per block the way the Patch is, so
// `advance` touches no parameter object on the audio thread.
ArpSettings Processor::arpSettings() const
{
    const auto value = [this] (const char* id) { return state.getRawParameterValue(id)->load(); };
    const auto on = [&value] (const char* id) { return value(id) >= 0.5f; };
    const auto shape = [&value] (const char* id)
    {
        return static_cast<ArpShape>(juce::jlimit(0, arpShapeCount - 1, juce::roundToInt(value(id))));
    };

    ArpSettings settings;
    settings.enabled = on("arpEnable");
    settings.shape = shape("arpShape");
    settings.stepSeconds = arpStepSeconds();
    settings.offset = juce::roundToInt(value("arpOffset"));
    settings.repeats = juce::roundToInt(value("arpRepeats"));
    settings.gate = value("arpGate");
    settings.chance = value("arpChance");
    settings.chancePre = on("arpChancePre");
    settings.latch = on("arpLatch");
    settings.thru = on("arpThru");
    settings.shift = juce::roundToInt(value("arpShift"));
    settings.range = juce::roundToInt(value("arpRange"));
    settings.rangeShape = shape("arpRangeShape");
    settings.retriggerOnNote = on("arpRetrigNote");
    settings.retriggerFirstOnly = on("arpRetrigFirst");
    settings.retriggerOnRate = on("arpRetrigRateOn");
    settings.retriggerSeconds = arpDivisions()[static_cast<size_t>(
        juce::jlimit(0, arpDivisionCount - 1, juce::roundToInt(value("arpRetrigRate"))))].beats
        * static_cast<float>(60.0 / (hostBpm.load(std::memory_order_relaxed) > 0.0
                                         ? hostBpm.load(std::memory_order_relaxed) : 120.0));
    // OFF is the first entry, so the divisions start at one.
    const auto quant = juce::jlimit(0, arpDivisionCount, juce::roundToInt(value("arpLaunchQuant")));
    settings.launchQuantBeats = quant <= 0
        ? 0.0f : arpDivisions()[static_cast<size_t>(quant - 1)].beats;
    settings.velocityOn = on("arpVelEnable");
    settings.velocityRetrigger = on("arpVelRetrig");
    settings.velocityDecay = value("arpVelDecay");
    settings.velocityTarget = value("arpVelTarget");
    return settings;
}

// Where the next division of the bar falls, as a delay in samples. Off, or a
// host that is not rolling, is no delay at all: quantising against a transport
// that is not moving would hold the first note for ever.
void Processor::quantiseArpStart(int sampleIndex, const ArpSettings& settings)
{
    if (settings.launchQuantBeats <= 0.0f || !hostPlaying) return;
    const auto bpm = hostBpm.load(std::memory_order_relaxed);
    if (bpm <= 0.0) return;

    const auto beatsPerSample = bpm / 60.0 / preparedSampleRate;
    const auto ppq = hostPpq + sampleIndex * beatsPerSample;
    const auto quant = static_cast<double>(settings.launchQuantBeats);
    const auto next = std::ceil(ppq / quant) * quant;
    arp.delayFirstStep((next - ppq) / juce::jmax(1.0e-9, beatsPerSample));
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
        {
            if (const auto bpm = position->getBpm())
                hostBpm.store(*bpm, std::memory_order_relaxed);
            hostPlaying = position->getIsPlaying();
            if (const auto ppq = position->getPpqPosition()) hostPpq = *ppq;
        }

    // Anything played on the editor's keyboard joins the host's own notes
    // before a single sample is rendered.
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    // The racks divide the tempo themselves: a synced LFO's rate is resolved
    // into Hertz before the patch is built, but a delay's division has to be
    // read against the tempo at the moment it is rendered.
    core.setTempo(hostBpm.load(std::memory_order_relaxed));
    core.setPitchWheel((pitchWheelValue() - 8192) / 8192.0f);
    core.setModWheel(modWheelValue() / 127.0f);
    const auto values = patch();
    const auto mods = modulation();
    const auto arpValues = arpSettings();

    // The arp holds its notes on a gate clock of its own, so switching it off
    // mid-phrase is the one moment nothing else would ever come back to stop
    // them. Releasing the latch is the same kind of edge: the chord it was
    // holding has to go when no finger is on it.
    if (arpWasEnabled && !arpValues.enabled)
    {
        arp.flush([this] (int note) { core.noteOff(note); });
        arp.allNotesOff();
    }
    if (arpWasLatched && !arpValues.latch) arp.releaseLatch();
    // LAUNCH: the arp switched on starts its pattern from the beginning rather
    // than from wherever the last one left off.
    if (!arpWasEnabled && arpValues.enabled && state.getRawParameterValue("arpRetrigLaunch")->load() >= 0.5f)
        arp.restart(arpValues);
    arpWasEnabled = arpValues.enabled;
    arpWasLatched = arpValues.latch;

    auto event = midi.cbegin();
    const auto end = midi.cend();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (event != end)
        {
            const auto metadata = *event;
            if (metadata.samplePosition > i) break;
            const auto message = metadata.getMessage();
            if (message.isPitchWheel())
            {
                setPitchWheel(message.getPitchWheelValue());
                core.setPitchWheel((pitchWheelValue() - 8192) / 8192.0f);
            }
            else if (message.isController() && message.getControllerNumber() == 1)
            {
                setModWheel(message.getControllerValue());
                core.setModWheel(modWheelValue() / 127.0f);
            }
            // With the arp on, the keys feed the pattern rather than the
            // voices, and THRU is what decides whether they also reach the
            // voices directly — the MIDI THRU port the manual likens it to.
            //
            // Core allocates a voice per note number, so a key passed through
            // and the same pitch played by the arp are one voice rather than
            // two, and the arp's gate releases it. That is audible only where
            // the arp is playing the very note being held — untransposed, it
            // always is — so THRU is heard as it is meant to be under a
            // transposing pattern and as a shortened root under a plain one.
            // Giving the two paths separate voices means keying allocation on
            // something other than the note, which is a change to the voice
            // allocator rather than to the arp.
            if (arpValues.enabled)
            {
                const auto wasPlaying = arp.isPlaying();
                if (message.isNoteOn())
                {
                    arp.noteOn(message.getNoteNumber(), message.getFloatVelocity(), arpValues);
                    // A pattern that has just started from nothing is the one
                    // moment LAUNCH QUANT applies: it holds the first step back
                    // to the host's grid, and says nothing about the steps after
                    // it, which the rate already spaces.
                    if (!wasPlaying && arp.isPlaying()) quantiseArpStart(i, arpValues);
                }
                else if (message.isNoteOff()) arp.noteOff(message.getNoteNumber(), arpValues);
                else if (message.isAllNotesOff()) arp.allNotesOff();
                // While the arp is on, CC64 works the latch rather than
                // sustaining notes, which is what the manual says Serum does
                // and what a pedal under an arpeggio is actually wanted for.
                else if (message.isSustainPedalOn()) arp.sustain(true);
                else if (message.isSustainPedalOff()) arp.sustain(false);
            }

            if (!arpValues.enabled || arpValues.thru)
            {
                if (message.isNoteOn()) core.noteOn(message.getNoteNumber(), message.getFloatVelocity(), values);
                else if (message.isNoteOff()) core.noteOff(message.getNoteNumber());
                else if (message.isAllNotesOff()) core.allNotesOff();
            }
            ++event;
        }
        // Between the events and the render, so a note the arp starts on this
        // sample is sounding in this sample rather than the next one.
        arp.advance(arpValues, preparedSampleRate,
                    [this, &values] (int note, float velocity) { core.noteOn(note, velocity, values); },
                    [this] (int note) { core.noteOff(note); });

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

float Processor::filterTypeValue() const
{
    return state.getRawParameterValue("filterType")->load();
}

// The filter's second field is a plain 0..1 and means whatever the type says it
// means, so this is where a number becomes a reading. Every line of it runs the
// arithmetic the engine runs, out of ForgeFilter.h rather than copied, which is
// what binds "2.00 kHz" in the bubble to the corner that is actually there.
juce::String Processor::filterFreqText(float value) const
{
    const auto type = filterTypeOf(filterTypeValue());
    const auto held = juce::jlimit(0.0f, 1.0f, value);
    const FilterShape shape {type, state.getRawParameterValue("cutoff")->load(),
                             state.getRawParameterValue("resonance")->load(), held,
                             getSampleRate() > 0.0 ? getSampleRate() : 48000.0};
    const auto percent = [held] { return juce::String(juce::roundToInt(held * 100.0f)) + " %"; };
    const auto hertz = [] (float hz)
    {
        return hz >= 1000.0f ? juce::String(hz / 1000.0f, 2) + " kHz"
                             : juce::String(juce::roundToInt(hz)) + " Hz";
    };

    switch (filterCategoryOf(type))
    {
        case FilterCategory::dual:
            // The second filter's own corner, in the unit a corner is read in.
            return hertz(filterSecondHz(shape));

        case FilterCategory::morph:
        {
            const auto weights = filterMorphWeights(held);
            const auto& taps = filterInfo(type).taps;
            const auto name = [] (FilterTap tap)
            {
                switch (tap)
                {
                    case FilterTap::high:  return "HP";
                    case FilterTap::band:  return "BP";
                    case FilterTap::notch: return "NT";
                    case FilterTap::peak:  return "PK";
                    case FilterTap::low:   break;
                }
                return "LP";
            };
            // Which response it is on, or the two it is between — the same
            // treatment an oscillator's POSITION gets, and for the same
            // reason: "where in the morph am I?" is not a percentage.
            for (int i = 0; i < 3; ++i)
                if (weights[static_cast<size_t>(i)] >= 0.999f) return name(taps[static_cast<size_t>(i)]);
            const auto first = held < 0.5f ? 0 : 1;
            return juce::String(name(taps[static_cast<size_t>(first)])) + ">"
                 + name(taps[static_cast<size_t>(first + 1)]) + " " + percent();
        }

        case FilterCategory::resonator:
        case FilterCategory::character:
            if (type == FilterType::comb || type == FilterType::flanger || type == FilterType::reverb)
                return hertz(filterDampHz(shape));
            if (type == FilterType::phaser || type == FilterType::diffusor)
                return juce::String(filterStagesOf(shape));
            if (type == FilterType::formant)
                return juce::String((held - 0.5f) * 2.0f, 2) + " oct";
            if (type == FilterType::ringMod)
                return held <= 0.0f ? juce::String("OFF")
                                    : hertz(filterClampHz(shape.cutoff, shape.sampleRate)
                                            * std::exp2(held));
            return percent();

        case FilterCategory::basic:
        case FilterCategory::analog:
            break;
    }
    return percent();
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
            if (knob == 1) return asMilliseconds(fxScaled(value, 0.0f, 0.2f));
            if (knob == 4) return hertz(fxHertz(value, 20.0f, 1200.0f));
            if (knob == 5) return hertz(fxHertz(value, 1200.0f, 20000.0f));
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
            if (knob == 4) return juce::String(fxScaled(value, -100.0f, 100.0f), 0)
                                 + (value < 0.5f ? " L" : value > 0.5f ? " R" : " C");
            return percent();

        case FxType::compressor:
            if (knob == 0) return juce::String(fxCompressorThreshold(value), 1) + " dB";
            if (knob == 1) return juce::String(fxCompressorRatio(value), 1) + " : 1";
            if (knob == 2) return asMilliseconds(fxCompressorAttack(value));
            if (knob == 3) return asMilliseconds(fxCompressorRelease(value));
            if (knob == 4)
            {
                const auto automatic = fxModeOf(fxTypes()[static_cast<size_t>(FxType::compressor)].modeB,
                                                 state.getRawParameterValue(
                                                     fxParameterId(rack, slot, "ModeB"))->load()) == 1;
                return automatic ? juce::String("AUTO")
                                 : juce::String(fxCompressorMakeup(value), 1) + " dB";
            }
            if (knob == 5) return juce::String(fxCompressorKnee(value), 1) + " dB";
            break;

        case FxType::phaser:
            if (knob == 0)
            {
                const auto synced = fxModeOf(fxTypes()[static_cast<size_t>(FxType::phaser)].modeA,
                                             state.getRawParameterValue(
                                                 fxParameterId(rack, slot, "ModeA"))->load()) == 1;
                if (synced) return juce::String(fxDivisionAt(value).label);
                return juce::String(fxScaled(value, 0.02f, 8.0f, 2.0f), 2) + " Hz";
            }
            if (knob == 1) return juce::String(fxScaled(value, 0.0f, 4.0f), 2) + " oct";
            if (knob == 2) return hertz(fxHertz(value, 80.0f, 8000.0f));
            if (knob == 3) return juce::String(fxScaled(value, -100.0f, 100.0f), 0) + " %";
            if (knob == 4) return juce::String(fxScaled(value, -180.0f, 180.0f), 0)
                                 + juce::String::fromUTF8("\xc2\xb0");
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
    result.subWave = value("subWave");
    result.subOctave = value("subOctave");
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
    result.filterFreq = value("filterFreq");
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
        setting.table = lfoTable(lfo);
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
        result.slots[static_cast<size_t>(slot)] = {value(id("Source")), value(id("Dest")),
                                                   value(id("Depth")), value(id("Bipolar"))};
    }
    return result;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rhino::forge::Processor();
}
