#include "DrumDevice.h"
#include "BinaryData.h"
#include <cmath>

namespace theta
{
DrumDevice::DrumDevice(te::PluginCreationInfo info) : Plugin(info) {}

DrumDevice::~DrumDevice()
{
    notifyListenersOfDeletion();
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
        case 53: type = VoiceType::snare; break;
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
            return sample / static_cast<float>(clapSample.getNumChannels()) * voice.velocity * 0.9f;
        }

        if (t > 0.26f) { voice.active = false; return 0.0f; }
        const auto crack = std::exp(-t * 95.0f);
        const auto body = (1.0f - std::exp(-t * 240.0f)) * std::exp(-t * 18.0f);
        const auto noise = nextNoise(voice);
        const auto highPassed = noise - voice.noise * 0.72f;
        voice.noise = noise;
        return highPassed * (0.42f * crack + 0.32f * body) * voice.velocity;
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
    const auto sourcePosition = voice.samplePosition++ * sourceRate / sampleRate;
    const auto index = static_cast<int>(sourcePosition);
    if (index >= sample.getNumSamples() - 1)
    {
        voice.active = false;
        return 0.0f;
    }
    const auto fraction = static_cast<float>(sourcePosition - index);
    auto value = 0.0f;
    for (int channel = 0; channel < sample.getNumChannels(); ++channel)
        value += sample.getSample(channel, index) * (1.0f - fraction)
               + sample.getSample(channel, index + 1) * fraction;
    return value / static_cast<float>(sample.getNumChannels()) * voice.velocity * 0.9f;
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
