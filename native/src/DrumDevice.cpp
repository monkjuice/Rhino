#include "DrumDevice.h"
#include "BinaryData.h"
#include <cmath>

namespace rhino
{
DrumDevice::DrumDevice(te::PluginCreationInfo info) : Plugin(info)
{
    kitIndex.referTo(state, "kit", getUndoManager(), 0.0f);
    kitSelect = addParam("kit", "Kit", {0.0f, static_cast<float>(kitCount - 1)});
    kitSelect->attachToCurrentValue(kitIndex);
}

DrumDevice::~DrumDevice()
{
    notifyListenersOfDeletion();
    kitSelect->detachFromCurrentValue();
}

void DrumDevice::restorePluginStateFromValueTree(const juce::ValueTree& source)
{
    te::copyPropertiesToCachedValues(source, kitIndex);
    kitSelect->updateFromAttachedValue();
}

juce::String DrumDevice::kitName(Kit kit)
{
    switch (kit)
    {
        case Kit::Rhino808: return "Rhino 808";
        case Kit::House:    return "House Kit";
        case Kit::Break:    return "Break Kit";
        case Kit::Minimal:  return "Minimal Kit";
        case Kit::Clap:     return "Clap Kit";
    }
    return {};
}

DrumDevice::Kit DrumDevice::kit() const
{
    const auto index = juce::jlimit(0, kitCount - 1, juce::roundToInt(kitSelect->getCurrentValue()));
    return static_cast<Kit>(index);
}

void DrumDevice::setKit(Kit kit)
{
    kitSelect->setParameter(static_cast<float>(static_cast<int>(kit)), juce::sendNotification);
}

// Each kit is the same sample set shaped differently: the tuning, how far the
// sample is allowed to ring, and its level. Read from the audio thread, so this
// is a pure lookup with no allocation.
DrumDevice::VoiceShape DrumDevice::shapeFor(VoiceType type) const
{
    switch (kit())
    {
        case Kit::House:
            switch (type)
            {
                case VoiceType::kick:      return {0.92f, 0.0f, 1.12f};
                case VoiceType::snare:     return {1.04f, 0.22f, 0.90f};
                case VoiceType::closedHat: return {1.10f, 0.05f, 0.85f};
                case VoiceType::openHat:   return {1.02f, 0.30f, 0.80f};
                default:                   return {1.0f, 0.0f, 0.95f};
            }
        case Kit::Break:
            switch (type)
            {
                case VoiceType::kick:      return {1.06f, 0.0f, 1.0f};
                case VoiceType::snare:     return {0.94f, 0.0f, 1.10f};
                case VoiceType::closedHat: return {1.18f, 0.035f, 0.9f};
                case VoiceType::openHat:   return {1.12f, 0.18f, 0.82f};
                default:                   return {1.04f, 0.0f, 1.0f};
            }
        case Kit::Minimal:
            switch (type)
            {
                case VoiceType::kick:      return {1.0f, 0.16f, 0.95f};
                case VoiceType::snare:     return {1.12f, 0.12f, 0.72f};
                case VoiceType::closedHat: return {1.25f, 0.025f, 0.68f};
                case VoiceType::openHat:   return {1.15f, 0.10f, 0.62f};
                default:                   return {1.0f, 0.12f, 0.8f};
            }
        case Kit::Clap:
            switch (type)
            {
                case VoiceType::kick:      return {0.96f, 0.0f, 1.08f};
                case VoiceType::clap:      return {1.0f, 0.0f, 1.15f};
                case VoiceType::closedHat: return {1.06f, 0.045f, 0.8f};
                case VoiceType::openHat:   return {1.0f, 0.22f, 0.75f};
                default:                   return {1.0f, 0.0f, 0.95f};
            }
        case Kit::Rhino808:
            break;
    }
    return {};
}

void DrumDevice::initialise(const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    loadSamples();
    reset();
}

void DrumDevice::reset()
{
    for (auto& voice : voices)
        voice.active = false;
}

void DrumDevice::midiPanic()
{
    reset();
}

bool DrumDevice::hasNameForMidiNoteNumber(int note, int midiChannel, juce::String& name)
{
    juce::ignoreUnused(midiChannel);
    if (note == 48) name = "Kick";
    else if (note == 50) name = "Low Tom";
    else if (note == 52) name = "Mid Tom";
    else if (note == 53) name = "Snare";
    else if (note == 54) name = "High Tom";
    else if (note == 56) name = "Clap";
    else if (note == 58) name = "Closed Hat";
    else if (note == 59) name = "Open Hat";
    else return false;
    return true;
}

void DrumDevice::trigger(int note, float velocity)
{
    VoiceType type;
    switch (note)
    {
        case 48: type = VoiceType::kick; break;
        case 50: type = VoiceType::lowTom; break;
        case 52: type = VoiceType::midTom; break;
        case 53: type = kit() == Kit::Clap ? VoiceType::clap : VoiceType::snare; break;
        case 54: type = VoiceType::highTom; break;
        case 56: type = VoiceType::clap; break;
        case 58: type = VoiceType::closedHat; break;
        case 59: type = VoiceType::openHat; break;
        default: return;
    }

    if (type == VoiceType::closedHat)
        for (auto& existing : voices)
            if (existing.active && existing.type == VoiceType::openHat)
                existing.active = false;

    auto& voice = voices[nextVoice++ % voices.size()];
    voice.type = type;
    voice.active = true;
    voice.age = 0.0f;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.phase = 0.0f;
    voice.noise = 0.0f;
    voice.samplePosition = 0;
    voice.seed = voice.seed * 1664525u + 1013904223u + static_cast<uint32_t>(note * 97);
}

float DrumDevice::nextNoise(Voice& voice) noexcept
{
    voice.seed = voice.seed * 1664525u + 1013904223u;
    return static_cast<float>((static_cast<int>((voice.seed >> 9) & 0xffff) - 32768) / 32768.0);
}

float DrumDevice::render(Voice& voice)
{
    const auto t = voice.age;
    const auto dt = static_cast<float>(1.0 / sampleRate);
    voice.age += dt;

    const auto sampleForVoice = [type = voice.type]() -> int
    {
        switch (type)
        {
            case VoiceType::kick:      return 0;
            case VoiceType::snare:     return 1;
            case VoiceType::closedHat: return 2;
            case VoiceType::openHat:   return 3;
            case VoiceType::lowTom:    return 4;
            case VoiceType::midTom:    return 5;
            case VoiceType::highTom:   return 6;
            default:                    return -1;
        }
    }();
    if (sampleForVoice >= 0)
        return renderSample(voice, tr808Samples[static_cast<size_t>(sampleForVoice)],
                            tr808SampleRates[static_cast<size_t>(sampleForVoice)]);
    juce::ignoreUnused(t, dt);

    if (voice.type == VoiceType::clap)
    {
        if (clapSample.getNumSamples() > 0)
        {
            const auto sourcePosition = voice.samplePosition++ * clapSampleRate / sampleRate;
            const auto index = static_cast<int>(sourcePosition);
            if (index >= clapSample.getNumSamples() - 1)
            {
                voice.active = false;
                return 0.0f;
            }
            const auto frac = static_cast<float>(sourcePosition - index);
            auto sample = 0.0f;
            for (int channel = 0; channel < clapSample.getNumChannels(); ++channel)
                sample += clapSample.getSample(channel, index) * (1.0f - frac)
                        + clapSample.getSample(channel, index + 1) * frac;
            return sample / static_cast<float>(clapSample.getNumChannels()) * voice.velocity * 0.9f
                 * shapeFor(voice.type).gain;
        }

        if (t > 0.26f) { voice.active = false; return 0.0f; }
        const auto crack = std::exp(-t * 95.0f);
        const auto body = (1.0f - std::exp(-t * 240.0f)) * std::exp(-t * 18.0f);
        const auto noise = nextNoise(voice);
        const auto highPassed = noise - voice.noise * 0.72f;
        voice.noise = noise;
        return highPassed * (0.42f * crack + 0.32f * body) * voice.velocity * shapeFor(voice.type).gain;
    }

    return 0.0f;
}

void DrumDevice::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples == 0)
        return;

    SCOPED_REALTIME_CHECK
    const auto* midiMessages = context.bufferForMidiMessages;
    const auto renderFrame = [this, &context](int localFrame)
    {
        float sample = 0.0f;
        for (auto& voice : voices)
            if (voice.active)
                sample += render(voice);
        sample = std::clamp(sample, -0.95f, 0.95f);
        const auto frame = context.bufferStartSample + localFrame;
        for (int channel = 0; channel < context.destBuffer->getNumChannels(); ++channel)
            context.destBuffer->setSample(channel, frame, std::clamp(context.destBuffer->getSample(channel, frame) + sample, -0.95f, 0.95f));
    };

    if (midiMessages == nullptr)
    {
        for (int localFrame = 0; localFrame < context.bufferNumSamples; ++localFrame)
            renderFrame(localFrame);
        return;
    }

    if (midiMessages->isAllNotesOff)
        midiPanic();
    auto midi = midiMessages->begin();
    const auto midiEnd = midiMessages->end();
    for (int localFrame = 0; localFrame < context.bufferNumSamples; ++localFrame)
    {
        while (midi != midiEnd && juce::roundToInt(midi->getTimeStamp() * sampleRate) <= localFrame)
        {
            if (midi->isNoteOn())
                trigger(midi->getNoteNumber(), midi->getFloatVelocity());
            else if (midi->isAllNotesOff())
                midiPanic();
            ++midi;
        }
        renderFrame(localFrame);
    }
}

void DrumDevice::loadSample(juce::AudioBuffer<float>& destination, double& sourceRate,
                            const void* data, int dataSize)
{
    if (destination.getNumSamples() > 0)
        return;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(data, static_cast<size_t>(dataSize), false)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return;

    sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    const auto samples = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,
                                                               static_cast<juce::int64>(std::ceil(sampleRate))));
    destination.setSize(static_cast<int>(reader->numChannels), samples);
    reader->read(&destination, 0, samples, 0, true, true);
}

float DrumDevice::renderSample(Voice& voice, const juce::AudioBuffer<float>& sample, double sourceRate)
{
    if (sample.getNumSamples() < 2)
    {
        voice.active = false;
        return 0.0f;
    }
    const auto shape = shapeFor(voice.type);
    // The kit's rate resamples the pad, which is what tunes it.
    const auto elapsed = static_cast<float>(voice.samplePosition / sampleRate);
    const auto sourcePosition = voice.samplePosition++ * sourceRate * shape.rate / sampleRate;
    const auto index = static_cast<int>(sourcePosition);
    if (index >= sample.getNumSamples() - 1)
    {
        voice.active = false;
        return 0.0f;
    }
    auto envelope = 1.0f;
    if (shape.decay > 0.0f)
    {
        if (elapsed >= shape.decay)
        {
            voice.active = false;
            return 0.0f;
        }
        // Short linear fade to zero at the decay point, so a shortened pad does
        // not click when it stops.
        const auto remaining = 1.0f - elapsed / shape.decay;
        envelope = remaining * remaining;
    }
    const auto fraction = static_cast<float>(sourcePosition - index);
    auto value = 0.0f;
    for (int channel = 0; channel < sample.getNumChannels(); ++channel)
        value += sample.getSample(channel, index) * (1.0f - fraction)
               + sample.getSample(channel, index + 1) * fraction;
    return value / static_cast<float>(sample.getNumChannels()) * voice.velocity * 0.9f * shape.gain * envelope;
}

void DrumDevice::loadSamples()
{
    loadSample(clapSample, clapSampleRate, BinaryData::HandClap01_09_flac, BinaryData::HandClap01_09_flacSize);
    loadSample(tr808Samples[0], tr808SampleRates[0], BinaryData::TR808Kick_wav, BinaryData::TR808Kick_wavSize);
    loadSample(tr808Samples[1], tr808SampleRates[1], BinaryData::TR808Snare_wav, BinaryData::TR808Snare_wavSize);
    loadSample(tr808Samples[2], tr808SampleRates[2], BinaryData::TR808ClosedHat_wav, BinaryData::TR808ClosedHat_wavSize);
    loadSample(tr808Samples[3], tr808SampleRates[3], BinaryData::TR808OpenHat_wav, BinaryData::TR808OpenHat_wavSize);
    loadSample(tr808Samples[4], tr808SampleRates[4], BinaryData::TR808LowTom_wav, BinaryData::TR808LowTom_wavSize);
    loadSample(tr808Samples[5], tr808SampleRates[5], BinaryData::TR808MidTom_wav, BinaryData::TR808MidTom_wavSize);
    loadSample(tr808Samples[6], tr808SampleRates[6], BinaryData::TR808HighTom_wav, BinaryData::TR808HighTom_wavSize);
}
}
