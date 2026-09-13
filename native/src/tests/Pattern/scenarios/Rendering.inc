        // Establish the drum state that the persistence round-trip verifies.
        session.applyPatternPreset(Session::PatternPreset::HouseKit);
        // A small real engine render proves MIDI reaches the synth and Utility.
        // It needs no audio hardware, synthetic large session, or progress UI.
        juce::TemporaryFile output(".wav");
        juce::WavAudioFormat wav;
        te::Renderer::Parameters parameters(*session.edit);
        parameters.destFile = output.getFile();
        parameters.audioFormat = &wav;
        parameters.sampleRateForAudio = 48000;
        parameters.bitDepth = 24;
        parameters.shouldNormalise = true;
        parameters.normaliseToLevelDb = -1.0f;
        parameters.time = session.pattern().getPosition().time;
        {
            te::Renderer::RenderTask task("Pattern test", parameters, nullptr, nullptr);
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                require(juce::Time::getMillisecondCounterHiRes() < deadline, "Render timed out");
            require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output.getFile()));
        require(reader != nullptr, "Render must create a readable WAV");
        require(reader->sampleRate == 48000.0, "Render sample rate");
        require(std::abs(static_cast<double>(reader->lengthInSamples)
                         - parameters.time.getLength().inSeconds() * parameters.sampleRateForAudio) <= 512.0,
                "Render duration follows the selected clip");
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "Read rendered audio");
        float peak = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int frame = 0; frame < audio.getNumSamples(); ++frame)
            {
                const auto sample = audio.getSample(c, frame);
                require(std::isfinite(sample), "Render must contain finite samples");
                peak = std::max(peak, std::abs(sample));
            }
        require(peak > 0.0001f && peak <= 0.90f, "Peak-normalised render must retain export headroom");
        reader.reset();

        {
            Session waveSession;
            require(waveSession.addInstrument(Session::Instrument::ThetaWave, 0).wasOk(), "Theta Wave can be activated on the pattern track");
            waveSession.setNote(0, 48, true);
            waveSession.setNote(4, 55, true);
            juce::TemporaryFile waveOutput(".wav");
            te::Renderer::Parameters waveParameters(*waveSession.edit);
            waveParameters.destFile = waveOutput.getFile();
            waveParameters.audioFormat = &wav;
            waveParameters.sampleRateForAudio = 48000;
            waveParameters.bitDepth = 24;
            waveParameters.time = waveSession.pattern().getPosition().time;
            {
                te::Renderer::RenderTask task("Theta Wave render test", waveParameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Theta Wave render timed out");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            std::unique_ptr<juce::AudioFormatReader> waveReader(formats.createReaderFor(waveOutput.getFile()));
            require(waveReader != nullptr, "Read Theta Wave render");
            juce::AudioBuffer<float> waveAudio(static_cast<int>(waveReader->numChannels), static_cast<int>(waveReader->lengthInSamples));
            require(waveReader->read(&waveAudio, 0, waveAudio.getNumSamples(), 0, true, true), "Read Theta Wave samples");
            float wavePeak = 0.0f;
            for (int c = 0; c < waveAudio.getNumChannels(); ++c)
                for (int frame = 0; frame < waveAudio.getNumSamples(); ++frame)
                {
                    const auto sample = waveAudio.getSample(c, frame);
                    require(std::isfinite(sample), "Theta Wave render must stay finite");
                    wavePeak = std::max(wavePeak, std::abs(sample));
                }
            require(wavePeak > 0.0001f && wavePeak < 1.0f, "Theta Wave output must be audible and below clipping");
        }
        {
            Session arpSession;
            arpSession.applyPatternPreset(Session::PatternPreset::ArpRun);
            require(arpSession.addMidiEffect(Session::MidiEffect::ThetaArp, 0).wasOk(), "Theta Arp can be inserted on the pattern synth");
            juce::TemporaryFile arpOutput(".wav");
            te::Renderer::Parameters arpParameters(*arpSession.edit);
            arpParameters.destFile = arpOutput.getFile();
            arpParameters.audioFormat = &wav;
            arpParameters.sampleRateForAudio = 48000;
            arpParameters.bitDepth = 24;
            arpParameters.time = arpSession.pattern().getPosition().time;
            {
                te::Renderer::RenderTask task("Theta Arp render test", arpParameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Theta Arp render timed out");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            std::unique_ptr<juce::AudioFormatReader> arpReader(formats.createReaderFor(arpOutput.getFile()));
            require(arpReader != nullptr, "Read Theta Arp render");
            juce::AudioBuffer<float> arpAudio(static_cast<int>(arpReader->numChannels), static_cast<int>(arpReader->lengthInSamples));
            require(arpReader->read(&arpAudio, 0, arpAudio.getNumSamples(), 0, true, true), "Read Theta Arp samples");
            float arpPeak = 0.0f;
            for (int c = 0; c < arpAudio.getNumChannels(); ++c)
                for (int frame = 0; frame < arpAudio.getNumSamples(); ++frame)
                {
                    const auto sample = arpAudio.getSample(c, frame);
                    require(std::isfinite(sample), "Theta Arp render must stay finite");
                    arpPeak = std::max(arpPeak, std::abs(sample));
                }
            require(arpPeak > 0.0001f && arpPeak < 1.0f, "Theta Arp render must be audible and below clipping");
            arpSession.stop();
            arpSession.panicReset(false);
        }

        {
            Session fxSession;
            require(fxSession.importAudio(output.getFile()).wasOk(), "Theta Space test imports rendered audio");
            require(fxSession.addAudioEffect(Session::AudioEffect::ThetaSpace).wasOk(), "Theta Space can be inserted for render stability");
            auto* fxTrack = te::getAudioTracks(*fxSession.edit)[1];
            const auto thetaSlot = fxTrack->pluginList.size() - 1;
            auto* thetaSpace = dynamic_cast<ThetaSpaceDevice*>(fxTrack->pluginList[thetaSlot]);
            require(thetaSpace != nullptr, "Inserted effect is Theta Space");
            require(fxSession.setDeviceParameter(1, thetaSlot, 0, 1.0f).wasOk(), "Theta Space test sets wet mix");
            require(fxSession.setDeviceParameter(1, thetaSlot, 2, 0.9f).wasOk(), "Theta Space test sets feedback");
            auto params = thetaSpace->getAutomatableParameters();
            require(params.size() >= 2 && params[1] != nullptr, "Theta Space size parameter is automatable");
            auto& sizeCurve = params[1]->getCurve();
            sizeCurve.addPoint(tracktion::core::TimePosition::fromSeconds(0.0), 0.0f, 0.0f, nullptr);
            sizeCurve.addPoint(tracktion::core::TimePosition::fromSeconds(1.0), 1.0f, 0.0f, nullptr);

            juce::TemporaryFile fxOutput(".wav");
            te::Renderer::Parameters fxParameters(*fxSession.edit);
            fxParameters.destFile = fxOutput.getFile();
            fxParameters.audioFormat = &wav;
            fxParameters.sampleRateForAudio = 48000;
            fxParameters.bitDepth = 24;
            fxParameters.time = fxTrack->getClips()[0]->getPosition().time;
            {
                te::Renderer::RenderTask task("Theta Space size automation test", fxParameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Theta Space render timed out");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            std::unique_ptr<juce::AudioFormatReader> fxReader(formats.createReaderFor(fxOutput.getFile()));
            require(fxReader != nullptr, "Read Theta Space render");
            juce::AudioBuffer<float> fxAudio(static_cast<int>(fxReader->numChannels), static_cast<int>(fxReader->lengthInSamples));
            require(fxReader->read(&fxAudio, 0, fxAudio.getNumSamples(), 0, true, true), "Read Theta Space samples");
            float fxPeak = 0.0f;
            for (int c = 0; c < fxAudio.getNumChannels(); ++c)
                for (int frame = 0; frame < fxAudio.getNumSamples(); ++frame)
                {
                    const auto sample = fxAudio.getSample(c, frame);
                    require(std::isfinite(sample), "Theta Space size automation must stay finite");
                    fxPeak = std::max(fxPeak, std::abs(sample));
                }
            require(fxPeak > 0.0001f && fxPeak <= 1.0f, "Theta Space size automation remains bounded and audible");
            fxSession.stop();
            fxSession.panicReset(false);
        }
