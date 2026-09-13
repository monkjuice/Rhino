#include "../Session.h"
#include <cmath>
#include <source_location>
#include <stdexcept>

namespace theta
{
int runSelfTest()
{
    try
    {
        auto require = [](bool valid, const std::source_location location = std::source_location::current())
        {
            if (!valid)
                throw std::runtime_error("Native device check failed at line " + std::to_string(location.line()));
        };
        Session session;
        require(session.utility != nullptr);
        auto& device = *session.utility;
        device.gain().setParameter(-6.0f, juce::dontSendNotification);
        device.initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> buffer(2, 520);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        te::PluginRenderContext context(&buffer, 4, 512, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(context);
        const auto expected = std::pow(10.0f, -6.0f / 20.0f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i)
                require(std::abs(buffer.getSample(c, i) - (i >= 4 && i < 516 ? expected : 1.0f)) < 1.0e-5f);

        // Exercise smoothing and equal gain on every channel; frame offsets
        // and stereo consistency catch errors hidden by single-channel tones.
        device.gain().setParameter(0.0f, juce::dontSendNotification);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 520; ++i) buffer.setSample(c, i, 1.0f);
        device.applyToBuffer(context);
        require(buffer.getSample(0, 4) > expected && buffer.getSample(0, 4) < 1.0f);
        require(std::abs(buffer.getSample(0, 515) - 1.0f) < 1.0e-5f);
        for (int i = 4; i < 516; ++i)
            require(buffer.getSample(0, i) == buffer.getSample(1, i));
        te::PluginRenderContext noAudio(nullptr, 0, 0, nullptr, 0.0, {}, false, false, true, false);
        device.applyToBuffer(noAudio);

        auto restored = device.state.createCopy();
        restored.setProperty("gainDb", -12.0f, nullptr);
        device.restorePluginStateFromValueTree(restored);
        require(std::abs(device.gain().getCurrentValue() + 12.0f) < 1.0e-5f);
        auto xml = device.state.createXml();
        auto roundTrip = juce::ValueTree::fromXml(*xml);
        roundTrip.removeProperty(te::IDs::id, nullptr);
        auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
        require(dynamic_cast<UtilityDevice*>(reloaded.get()) != nullptr);
        require(std::abs(reloaded->getAutomatableParameterByID("gainDb")->getCurrentValue() + 12.0f) < 1.0e-5f);
        device.deinitialise();

        session.drums->initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> drumBuffer(2, 4096);
        drumBuffer.clear();
        te::MidiMessageArray midi;
        midi.addMidiMessage(juce::MidiMessage::noteOn(1, 56, 1.0f), 0.0, {});
        te::PluginRenderContext drumContext(&drumBuffer, 0, drumBuffer.getNumSamples(), &midi, 0.0, {}, true, false, true, false);
        session.drums->applyToBuffer(drumContext);
        float clapPeak = 0.0f;
        for (int c = 0; c < drumBuffer.getNumChannels(); ++c)
            for (int i = 0; i < drumBuffer.getNumSamples(); ++i)
            {
                const auto sample = drumBuffer.getSample(c, i);
                require(std::isfinite(sample));
                clapPeak = std::max(clapPeak, std::abs(sample));
            }
        require(clapPeak > 0.0001f && clapPeak < 1.0f);
        session.drums->deinitialise();

        auto bloomPlugin = session.edit->getPluginCache().createNewPlugin(ThetaBloomDevice::xmlTypeName, {});
        auto* bloom = dynamic_cast<ThetaBloomDevice*>(bloomPlugin.get());
        require(bloom != nullptr);
        require(bloom->getAutomatableParameters().size() == 6);
        bloom->getAutomatableParameterByID("bloom")->setParameter(0.7f, juce::dontSendNotification);
        bloom->getAutomatableParameterByID("clouds")->setParameter(0.55f, juce::dontSendNotification);
        bloom->getAutomatableParameterByID("plate")->setParameter(0.42f, juce::dontSendNotification);
        bloom->initialise({{}, 48000.0, 256});
        juce::AudioBuffer<float> bloomBuffer(2, 1024);
        for (int i = 0; i < bloomBuffer.getNumSamples(); ++i)
        {
            const auto impulse = i == 0 ? 0.5f : 0.0f;
            bloomBuffer.setSample(0, i, impulse);
            bloomBuffer.setSample(1, i, impulse * 0.8f);
        }
        te::PluginRenderContext bloomContext(&bloomBuffer, 0, bloomBuffer.getNumSamples(), nullptr, 0.0, {}, false, false, true, false);
        bloom->applyToBuffer(bloomContext);
        float bloomEnergy = 0.0f;
        for (int c = 0; c < bloomBuffer.getNumChannels(); ++c)
            for (int i = 0; i < bloomBuffer.getNumSamples(); ++i)
            {
                const auto sample = bloomBuffer.getSample(c, i);
                require(std::isfinite(sample));
                bloomEnergy += std::abs(sample);
            }
        require(bloomEnergy > 0.01f);
        auto bloomState = bloom->state.createCopy();
        bloomState.setProperty("chorus", 0.25f, nullptr);
        bloom->restorePluginStateFromValueTree(bloomState);
        require(std::abs(bloom->getAutomatableParameterByID("chorus")->getCurrentValue() - 0.25f) < 1.0e-5f);
        bloom->deinitialise();

        auto wavePlugin = session.edit->getPluginCache().createNewPlugin(ThetaWaveDevice::xmlTypeName, {});
        auto* wave = dynamic_cast<ThetaWaveDevice*>(wavePlugin.get());
        require(wave != nullptr);
        wave->initialise({{}, 48000.0, 512});
        juce::AudioBuffer<float> waveBuffer(2, 4096);
        waveBuffer.clear();
        te::MidiMessageArray lowMidi;
        lowMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 5, 1.0f), 0.0, {});
        te::PluginRenderContext waveContext(&waveBuffer, 0, waveBuffer.getNumSamples(), &lowMidi, 0.0, {}, true, false, true, false);
        wave->applyToBuffer(waveContext);
        float wavePeak = 0.0f;
        for (int c = 0; c < waveBuffer.getNumChannels(); ++c)
            for (int i = 0; i < waveBuffer.getNumSamples(); ++i)
            {
                const auto sample = waveBuffer.getSample(c, i);
                require(std::isfinite(sample));
                wavePeak = std::max(wavePeak, std::abs(sample));
            }
        require(wavePeak < 1.0f);
        wave->reset();
        auto wavePosition = wave->getAutomatableParameterByID("position");
        auto waveShape = wave->getAutomatableParameterByID("shape");
        auto waveMotion = wave->getAutomatableParameterByID("motion");
        auto waveOsc2 = wave->getAutomatableParameterByID("osc2Level");
        auto waveTune2 = wave->getAutomatableParameterByID("osc2Tune");
        auto waveCutoff = wave->getAutomatableParameterByID("cutoff");
        auto waveLfoRate = wave->getAutomatableParameterByID("lfoRate");
        auto waveLfoPosition = wave->getAutomatableParameterByID("lfoPosition");
        auto waveLfoCutoff = wave->getAutomatableParameterByID("lfoCutoff");
        auto waveLfoPitch = wave->getAutomatableParameterByID("lfoPitch");
        auto waveLfoMotion = wave->getAutomatableParameterByID("lfoMotion");
        require(wavePosition != nullptr && waveShape != nullptr && waveMotion != nullptr
                && waveOsc2 != nullptr && waveTune2 != nullptr && waveCutoff != nullptr
                && waveLfoRate != nullptr && waveLfoPosition != nullptr && waveLfoCutoff != nullptr
                && waveLfoPitch != nullptr && waveLfoMotion != nullptr);
        juce::AudioBuffer<float> sweepBuffer(2, 512);
        te::MidiMessageArray sweepMidi;
        float sweepPeak = 0.0f, maxJump = 0.0f, previousSample = 0.0f;
        for (int block = 0; block < 32; ++block)
        {
            sweepBuffer.clear();
            sweepMidi.clear();
            if (block == 0)
                sweepMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 61, 0.8f), 0.0, {});
            if (block == 16)
                sweepMidi.addMidiMessage(juce::MidiMessage::noteOn(1, 73, 0.55f), 0.0, {});
            const auto phase = static_cast<float>(block) / 31.0f;
            wavePosition->setParameter(phase, juce::dontSendNotification);
            waveShape->setParameter(1.0f - phase * 0.7f, juce::dontSendNotification);
            waveMotion->setParameter(0.1f + phase * 0.75f, juce::dontSendNotification);
            waveOsc2->setParameter(phase, juce::dontSendNotification);
            waveTune2->setParameter(block % 2 == 0 ? 12.0f : -12.0f, juce::dontSendNotification);
            waveCutoff->setParameter(350.0f + phase * 12000.0f, juce::dontSendNotification);
            waveLfoRate->setParameter(0.25f + phase * 12.0f, juce::dontSendNotification);
            waveLfoPosition->setParameter(-0.6f + phase * 1.2f, juce::dontSendNotification);
            waveLfoCutoff->setParameter(0.5f - phase, juce::dontSendNotification);
            waveLfoPitch->setParameter(-4.0f + phase * 8.0f, juce::dontSendNotification);
            waveLfoMotion->setParameter(phase * 0.75f, juce::dontSendNotification);
            te::PluginRenderContext sweepContext(&sweepBuffer, 0, sweepBuffer.getNumSamples(), &sweepMidi, 0.0, {}, true, false, true, false);
            wave->applyToBuffer(sweepContext);
            for (int i = 0; i < sweepBuffer.getNumSamples(); ++i)
            {
                const auto sample = sweepBuffer.getSample(0, i);
                require(std::isfinite(sample));
                sweepPeak = std::max(sweepPeak, std::abs(sample));
                maxJump = std::max(maxJump, std::abs(sample - previousSample));
                previousSample = sample;
            }
        }
        require(sweepPeak > 0.0001f && sweepPeak < 1.0f && maxJump < 0.9f);
        wave->deinitialise();

        session.releaseAudioDevice();
        return 0;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog(error.what());
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
