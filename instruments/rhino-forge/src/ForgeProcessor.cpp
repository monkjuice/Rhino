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
