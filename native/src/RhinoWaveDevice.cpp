#include "RhinoWaveDevice.h"
#include "RhinoWaveTables.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
RhinoWaveDevice::RhinoWaveDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager();
    position.referTo(state, "position", undo, 0.35f);
    shape.referTo(state, "shape", undo, 0.55f);
    motion.referTo(state, "motion", undo, 0.22f);
    cutoff.referTo(state, "cutoff", undo, 6400.0f);
    filterEnv.referTo(state, "filterEnv", undo, 0.18f);
    driveDb.referTo(state, "driveDb", undo, 3.0f);
    sub.referTo(state, "sub", undo, 0.18f);
    resonance.referTo(state, "resonance", undo, 0.15f);
    attack.referTo(state, "attack", undo, 0.018f);
    decay.referTo(state, "decay", undo, 0.18f);
    sustain.referTo(state, "sustain", undo, 0.72f);
    releaseTime.referTo(state, "release", undo, 0.28f);
    unison.referTo(state, "unison", undo, 1.0f);
    detune.referTo(state, "detune", undo, 0.08f);
    width.referTo(state, "width", undo, 0.42f);
    outputDb.referTo(state, "outputDb", undo, -8.0f);
    osc2Level.referTo(state, "osc2Level", undo, 0.0f);
    osc2Tune.referTo(state, "osc2Tune", undo, 0.0f);
    lfoRate.referTo(state, "lfoRate", undo, 1.4f);
    lfoPosition.referTo(state, "lfoPosition", undo, 0.0f);
    lfoCutoff.referTo(state, "lfoCutoff", undo, 0.0f);
    lfoPitch.referTo(state, "lfoPitch", undo, 0.0f);
    lfoMotion.referTo(state, "lfoMotion", undo, 0.0f);

    positionParam = addParam("position", "Position", {0.0f, 1.0f});
    shapeParam = addParam("shape", "Shape", {0.0f, 1.0f});
    motionParam = addParam("motion", "Motion", {0.0f, 1.0f});
    cutoffParam = addParam("cutoff", "Cutoff", {80.0f, 18000.0f, 0.0f, 0.45f});
    filterEnvParam = addParam("filterEnv", "Env", {-1.0f, 1.0f});
    driveParam = addParam("driveDb", "Drive", {0.0f, 24.0f});
    subParam = addParam("sub", "Sub", {0.0f, 1.0f});
    resonanceParam = addParam("resonance", "Resonance", {0.0f, 1.0f});
    attackParam = addParam("attack", "Attack", {0.001f, 5.0f, 0.0f, 0.35f});
    decayParam = addParam("decay", "Decay", {0.001f, 8.0f, 0.0f, 0.35f});
    sustainParam = addParam("sustain", "Sustain", {0.0f, 1.0f});
    releaseParam = addParam("release", "Release", {0.001f, 10.0f, 0.0f, 0.35f});
    unisonParam = addParam("unison", "Unison", {1.0f, 4.0f, 1.0f});
    detuneParam = addParam("detune", "Detune", {0.0f, 0.35f});
    widthParam = addParam("width", "Width", {0.0f, 1.0f});
    outputParam = addParam("outputDb", "Output", {-36.0f, 6.0f});
    osc2LevelParam = addParam("osc2Level", "Osc 2", {0.0f, 1.0f});
    osc2TuneParam = addParam("osc2Tune", "Tune 2", {-24.0f, 24.0f, 1.0f});
    lfoRateParam = addParam("lfoRate", "LFO Rate", {0.05f, 20.0f, 0.0f, 0.35f});
    lfoPositionParam = addParam("lfoPosition", "LFO Pos", {-1.0f, 1.0f});
    lfoCutoffParam = addParam("lfoCutoff", "LFO Cutoff", {-1.0f, 1.0f});
    lfoPitchParam = addParam("lfoPitch", "LFO Pitch", {-12.0f, 12.0f});
    lfoMotionParam = addParam("lfoMotion", "LFO Motion", {-1.0f, 1.0f});

    for (auto pair : std::initializer_list<std::pair<te::AutomatableParameter::Ptr*, juce::CachedValue<float>*>>{
             {&positionParam, &position}, {&shapeParam, &shape}, {&motionParam, &motion}, {&cutoffParam, &cutoff},
             {&filterEnvParam, &filterEnv}, {&driveParam, &driveDb}, {&subParam, &sub}, {&resonanceParam, &resonance}, {&attackParam, &attack}, {&decayParam, &decay},
             {&sustainParam, &sustain}, {&releaseParam, &releaseTime}, {&unisonParam, &unison},
             {&detuneParam, &detune}, {&widthParam, &width}, {&outputParam, &outputDb},
             {&osc2LevelParam, &osc2Level}, {&osc2TuneParam, &osc2Tune},
             {&lfoRateParam, &lfoRate}, {&lfoPositionParam, &lfoPosition}, {&lfoCutoffParam, &lfoCutoff},
             {&lfoPitchParam, &lfoPitch}, {&lfoMotionParam, &lfoMotion}})
        (*pair.first)->attachToCurrentValue(*pair.second);

    auto percentText = [] (float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; };
    auto msText = [] (float value) { return juce::String(juce::roundToInt(value * 1000.0f)) + "ms"; };
    positionParam->valueToStringFunction = percentText;
    shapeParam->valueToStringFunction = percentText;
    motionParam->valueToStringFunction = percentText;
    cutoffParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)) + "Hz"; };
    filterEnvParam->valueToStringFunction = percentText;
    driveParam->valueToStringFunction = [] (float value) { return juce::String(value, 1) + " dB"; };
    subParam->valueToStringFunction = percentText;
    resonanceParam->valueToStringFunction = percentText;
    attackParam->valueToStringFunction = msText;
    decayParam->valueToStringFunction = msText;
    sustainParam->valueToStringFunction = percentText;
    releaseParam->valueToStringFunction = msText;
    unisonParam->valueToStringFunction = [] (float value) { return juce::String(juce::roundToInt(value)); };
    detuneParam->valueToStringFunction = percentText;
    widthParam->valueToStringFunction = percentText;
    outputParam->valueToStringFunction = [] (float value) { return juce::String(value, 1) + " dB"; };
    osc2LevelParam->valueToStringFunction = percentText;
    osc2TuneParam->valueToStringFunction = [] (float value)
    {
        return juce::String(value > 0.0f ? "+" : "") + juce::String(juce::roundToInt(value)) + " st";
    };
    lfoRateParam->valueToStringFunction = [] (float value) { return juce::String(value, 2) + " Hz"; };
    lfoPositionParam->valueToStringFunction = percentText;
    lfoCutoffParam->valueToStringFunction = percentText;
    lfoPitchParam->valueToStringFunction = [] (float value) { return juce::String(value, 1) + " st"; };
    lfoMotionParam->valueToStringFunction = percentText;
    // Construct tables before audio can start; do not first-initialize this
    // static object from the realtime render path.
    juce::ignoreUnused(rhino_wave::tables());
}

RhinoWaveDevice::~RhinoWaveDevice()
{
    notifyListenersOfDeletion();
    for (auto* parameter : getAutomatableParameters())
        parameter->detachFromCurrentValue();
}

void RhinoWaveDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    reset();
}

void RhinoWaveDevice::reset()
{
    for (auto& voice : voices)
        voice.active = false;
    syncSmoothedParameters(true);
}

void RhinoWaveDevice::midiPanic()
{
    reset();
}

void RhinoWaveDevice::trigger(int note, float velocity)
{
    auto& voice = voices[nextVoice++ % voices.size()];
    voice = {};
    voice.active = true;
    voice.note = note;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.motionPhase = static_cast<float>(nextVoice % voices.size()) / static_cast<float>(voices.size());
}

void RhinoWaveDevice::release(int note)
{
    for (auto& voice : voices)
        if (voice.active && !voice.released && voice.note == note)
        {
            voice.released = true;
            voice.releaseStart = voice.envelope;
        }
}

float RhinoWaveDevice::wave(float phase, float motionOffset, float frequency) const
{
    phase -= std::floor(phase);
    const auto positionValue = std::clamp(currentPosition + motionOffset, 0.0f, 1.0f);
    const auto& bank = rhino_wave::tables();
    const auto index = static_cast<float>(rhino_wave::tableSize) * phase;
    const auto lower = static_cast<int>(index) & rhino_wave::tableMask;
    const auto upper = (lower + 1) & rhino_wave::tableMask;
    const auto fraction = index - std::floor(index);
    const auto morph = positionValue * 4.0f;
    const auto frame = std::min(3, static_cast<int>(morph));
    const auto mix = morph - static_cast<float>(frame);
    // currentShape still has a musical effect, blending the harmonic folded
    // frame in after the regular morph. Frequency selection occurs in
    // renderVoice where pitch is known.
    const auto& table = bank.tables[rhino_wave::bandForFrequency(frequency, sampleRate)];
    const auto read = [&] (int frameIndex)
    {
        const auto& source = table[frameIndex];
        return source[lower] + (source[upper] - source[lower]) * fraction;
    };
    const auto basic = read(frame) + (read(frame + 1) - read(frame)) * mix;
    return basic + (read(4) - basic) * currentShape * 0.28f;
}

float RhinoWaveDevice::renderVoice(Voice& voice)
{
    if (!voice.active)
        return 0.0f;

    const auto dt = static_cast<float>(1.0 / sampleRate);
    const auto attackSeconds = std::max(0.001f, attackParam->getCurrentValue());
    const auto decaySeconds = std::max(0.001f, decayParam->getCurrentValue());
    const auto sustainLevel = std::clamp(sustainParam->getCurrentValue(), 0.0f, 1.0f);
    const auto releaseSeconds = std::max(0.001f, releaseParam->getCurrentValue());

    if (voice.released)
    {
        voice.envelope -= voice.releaseStart * dt / releaseSeconds;
        if (voice.envelope <= 0.0001f)
        {
            voice.active = false;
            return 0.0f;
        }
    }
    else if (voice.envelope < 1.0f)
    {
        voice.envelope = std::min(1.0f, voice.envelope + dt / attackSeconds);
    }
    else if (voice.envelope > sustainLevel)
    {
        voice.envelope = std::max(sustainLevel, voice.envelope - (1.0f - sustainLevel) * dt / decaySeconds);
    }

    voice.lfoPhase += dt * currentLfoRate;
    if (voice.lfoPhase >= 1.0f)
        voice.lfoPhase -= std::floor(voice.lfoPhase);
    const auto lfo = std::sin(voice.lfoPhase * juce::MathConstants<float>::twoPi);
    const auto baseFrequency = std::max(16.35f, static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(voice.note)));
    const auto frequency = baseFrequency * std::pow(2.0f, lfo * currentLfoPitch / 12.0f);
    const auto unisonCount = juce::jlimit(1, 4, juce::roundToInt(unisonParam->getCurrentValue()));
    voice.motionPhase += dt * (0.08f + currentMotion * 3.2f);
    if (voice.motionPhase >= 1.0f)
        voice.motionPhase -= std::floor(voice.motionPhase);
    const auto motionOffset = std::sin(voice.motionPhase * juce::MathConstants<float>::twoPi)
        * (currentMotion + lfo * currentLfoMotion) * 0.18f + lfo * currentLfoPosition * 0.22f;
    auto sample = 0.0f;
    const auto osc2Mix = std::clamp(currentOsc2Level, 0.0f, 1.0f);
    const auto osc2Ratio = std::pow(2.0f, currentOsc2Tune / 12.0f);
    const auto basePhase = voice.phase;
    const auto baseOsc2Phase = voice.osc2Phase;
    for (int i = 0; i < unisonCount; ++i)
    {
        const auto spread = unisonCount == 1 ? 0.0f : (static_cast<float>(i) / static_cast<float>(unisonCount - 1) - 0.5f);
        const auto cents = spread * currentDetune * 48.0f;
        const auto detunedPhase = basePhase * std::pow(2.0f, cents / 1200.0f);
        const auto osc1 = wave(detunedPhase + spread * currentWidth * 0.12f,
                               motionOffset + spread * currentMotion * 0.04f, frequency);
        const auto osc2 = wave(baseOsc2Phase + spread * currentWidth * 0.16f + 0.25f,
                               motionOffset + 0.18f + spread * currentMotion * 0.05f, frequency * osc2Ratio);
        sample += osc1 * (1.0f - osc2Mix) + osc2 * osc2Mix;
    }
    voice.phase += frequency * dt;
    if (voice.phase >= 1.0f)
        voice.phase -= std::floor(voice.phase);
    voice.osc2Phase += frequency * osc2Ratio * dt;
    if (voice.osc2Phase >= 1.0f)
        voice.osc2Phase -= std::floor(voice.osc2Phase);
    sample /= static_cast<float>(unisonCount);

    voice.subPhase += frequency * 0.5f * dt;
    if (voice.subPhase >= 1.0f)
        voice.subPhase -= std::floor(voice.subPhase);
    const auto subOsc = std::sin(voice.subPhase * juce::MathConstants<float>::twoPi);
    sample = sample * (1.0f - currentSub * 0.45f) + subOsc * currentSub * 0.45f;
    return sample * voice.envelope * voice.velocity;
}

void RhinoWaveDevice::syncSmoothedParameters(bool immediate)
{
    const auto update = [immediate, this] (float& current, float target)
    {
        if (immediate)
        {
            current = target;
            return;
        }
        const auto smoothing = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.018));
        current += (target - current) * smoothing;
    };

    update(currentPosition, std::clamp(positionParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentShape, std::clamp(shapeParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentMotion, std::clamp(motionParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentCutoff, std::clamp(cutoffParam->getCurrentValue(), 80.0f, static_cast<float>(sampleRate * 0.45)));
    update(currentFilterEnv, std::clamp(filterEnvParam->getCurrentValue(), -1.0f, 1.0f));
    update(currentDrive, juce::Decibels::decibelsToGain(std::clamp(driveParam->getCurrentValue(), 0.0f, 24.0f)));
    update(currentSub, std::clamp(subParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentResonance, std::clamp(resonanceParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentDetune, std::clamp(detuneParam->getCurrentValue(), 0.0f, 0.35f));
    update(currentWidth, std::clamp(widthParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentOutput, juce::Decibels::decibelsToGain(std::clamp(outputParam->getCurrentValue(), -36.0f, 6.0f)));
    update(currentOsc2Level, std::clamp(osc2LevelParam->getCurrentValue(), 0.0f, 1.0f));
    update(currentOsc2Tune, std::clamp(osc2TuneParam->getCurrentValue(), -24.0f, 24.0f));
    update(currentLfoRate, std::clamp(lfoRateParam->getCurrentValue(), 0.05f, 20.0f));
    update(currentLfoPosition, std::clamp(lfoPositionParam->getCurrentValue(), -1.0f, 1.0f));
    update(currentLfoCutoff, std::clamp(lfoCutoffParam->getCurrentValue(), -1.0f, 1.0f));
    update(currentLfoPitch, std::clamp(lfoPitchParam->getCurrentValue(), -12.0f, 12.0f));
    update(currentLfoMotion, std::clamp(lfoMotionParam->getCurrentValue(), -1.0f, 1.0f));
}

void RhinoWaveDevice::smoothParameters()
{
    syncSmoothedParameters(false);
}

void RhinoWaveDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    auto& buffer = *context.destBuffer;

    if (context.bufferForMidiMessages != nullptr)
        for (const auto& midi : *context.bufferForMidiMessages)
        {
            if (midi.isNoteOn())
                trigger(midi.getNoteNumber(), midi.getFloatVelocity());
            else if (midi.isNoteOff())
                release(midi.getNoteNumber());
            else if (midi.isAllNotesOff())
                midiPanic();
        }

    for (int frame = context.bufferStartSample; frame < context.bufferStartSample + context.bufferNumSamples; ++frame)
    {
        smoothParameters();
        auto mono = 0.0f, side = 0.0f;
        for (auto& voice : voices)
        {
            const auto voiceSample = renderVoice(voice);
            if (!voice.active && voiceSample == 0.0f)
                continue;
            const auto lfo = std::sin(voice.lfoPhase * juce::MathConstants<float>::twoPi);
            const auto cutoffHz = std::clamp(currentCutoff * std::pow(2.0f, currentFilterEnv * voice.envelope * 4.0f
                                                                      + lfo * currentLfoCutoff * 4.0f),
                                             40.0f, static_cast<float>(sampleRate * 0.45));
            const auto g = std::tan(juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
            const auto q = 0.5f + currentResonance * 11.5f;
            const auto damping = 1.0f / (2.0f * q);
            const auto h = 1.0f / (1.0f + 2.0f * damping * g + g * g);
            const auto high = (voiceSample - 2.0f * damping * voice.filterBand - voice.filterLow) * h;
            voice.filterBand += g * high;
            voice.filterLow += g * voice.filterBand;
            mono += voice.filterLow;
            side += voice.filterBand;
        }
        mono = std::tanh(mono * currentDrive) * currentOutput;
        const auto stereoSide = std::tanh(side * currentDrive) * currentOutput;
        const auto left = std::clamp(mono - stereoSide * currentWidth * 0.18f, -0.98f, 0.98f);
        const auto right = std::clamp(mono + stereoSide * currentWidth * 0.18f, -0.98f, 0.98f);
        if (buffer.getNumChannels() > 0)
            buffer.setSample(0, frame, std::clamp(buffer.getSample(0, frame) + left, -0.98f, 0.98f));
        if (buffer.getNumChannels() > 1)
            buffer.setSample(1, frame, std::clamp(buffer.getSample(1, frame) + right, -0.98f, 0.98f));
    }
}

void RhinoWaveDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, position, shape, motion, cutoff, filterEnv, driveDb, sub, resonance, attack, decay, sustain, releaseTime,
                                     unison, detune, width, outputDb, osc2Level, osc2Tune,
                                     lfoRate, lfoPosition, lfoCutoff, lfoPitch, lfoMotion);
    for (auto* parameter : getAutomatableParameters())
        parameter->updateFromAttachedValue();
    syncSmoothedParameters(true);
}
}
