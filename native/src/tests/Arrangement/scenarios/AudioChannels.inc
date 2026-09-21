        // What becomes of a clip's channels, at both ends: a mono file has to
        // be heard from both speakers, and a stereo one has to be seen as two
        // lanes.
        //
        // The audible half is the one that earned a test. A mono take - which
        // is what an interface with a single input records - played out of the
        // left side alone, because Rhino's Utility sits first on every track
        // and inherited the engine's default answer to "how many channels do
        // you put out", which is two whatever it is given. That widened the
        // track to stereo with nothing in the right channel before Volume &
        // Pan, the stage that would otherwise have duplicated a mono signal
        // into both, could see that it was mono. Reading the DSP would never
        // have shown it, so this renders and measures instead.
        {
            const auto writeFixture = [&require, &wav](const juce::File& file, int channels, float peak)
            {
                std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(channels).withBitsPerSample(24));
                require(writer != nullptr, "A channel fixture can be written");
                juce::AudioBuffer<float> audio(channels, 48000);
                for (int i = 0; i < 48000; ++i)
                {
                    const auto tone = peak * static_cast<float>(
                        std::sin(i * 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0));
                    for (int channel = 0; channel < channels; ++channel)
                        audio.setSample(channel, i, tone);
                }
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
                        "Write the channel fixture");
            };

            scenario("audio channels: a mono clip is heard from both speakers");
            juce::TemporaryFile monoSource(".wav");
            writeFixture(monoSource.getFile(), 1, 0.5f);
            Session mono;
            require(mono.importAudio(monoSource.getFile()).wasOk(), "A mono file imports");
            auto* monoTrack = te::getAudioTracks(*mono.edit)[mono.trackCount() - 1];
            require(monoTrack != nullptr && !monoTrack->getClips().isEmpty(), "and lands on a track");
            juce::TemporaryFile rendered(".wav");
            te::Renderer::Parameters parameters(*mono.edit);
            parameters.destFile = rendered.getFile();
            parameters.audioFormat = &wav;
            parameters.sampleRateForAudio = 48000;
            parameters.bitDepth = 24;
            parameters.time = monoTrack->getClips().getLast()->getPosition().time;
            {
                te::Renderer::RenderTask task("Mono clip test", parameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Mono render timeout");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            juce::AudioFormatManager renderedFormats;
            renderedFormats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(renderedFormats.createReaderFor(rendered.getFile()));
            require(reader != nullptr, "The render can be read back");
            require(reader->numChannels == 2, "and is stereo, as everything Rhino plays through is");
            juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
            require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read the rendered samples");
            const auto leftLevel = output.getRMSLevel(0, 0, output.getNumSamples());
            const auto rightLevel = output.getRMSLevel(1, 0, output.getNumSamples());
            require(leftLevel > 0.2f, "The mono signal is there on the left");
            require(std::abs(leftLevel - rightLevel) < 0.001f,
                    "and the same on the right, rather than the take playing out of one side");

            // The seen half. JUCE's drawChannels already gives each channel a
            // lane, so what is checked here is the separation: drawn edge to
            // edge at the height a track row offers, the two merge into one
            // taller waveform and a stereo clip cannot be told from a mono one.
            scenario("audio channels: a stereo clip is drawn as two lanes");
            // Full scale on purpose: a quiet clip leaves a margin at the top
            // and bottom of each lane that separates them whatever is done,
            // so only a signal that fills its lane can show whether the two
            // are being kept apart.
            juce::TemporaryFile loudMono(".wav"), loudStereo(".wav");
            writeFixture(loudMono.getFile(), 1, 1.0f);
            writeFixture(loudStereo.getFile(), 2, 1.0f);
            // How many separate bands of waveform a column of the clip crosses:
            // one for a mono file, two for a stereo one, and one for a stereo
            // one whose lanes have run together.
            const auto isWavePixel = [](juce::Colour colour)
            {
                return colour.getBlue() > 140 && colour.getGreen() > 120 && colour.getRed() < 190
                    && colour.getBlue() > colour.getRed() + 30;
            };
            const auto bandsDown = [&isWavePixel](const juce::Image& picture, int x, int top, int bottom)
            {
                auto bands = 0;
                auto inBand = false;
                for (int y = top; y < bottom; ++y)
                {
                    const auto wave = isWavePixel(picture.getPixelAt(x, y));
                    if (wave && !inBand)
                        ++bands;
                    inBand = wave;
                }
                return bands;
            };
            const auto lanesDrawnFor = [&](const juce::File& file)
            {
                Session drawn;
                require(drawn.importAudio(file).wasOk(), "The fixture imports for drawing");
                const auto drawnTrack = drawn.trackCount() - 1;
                require(drawn.setTrackLaneHeight(drawnTrack, 150.0f).wasOk(), "Its row is given room for two lanes");
                Arrangement drawnView(drawn);
                drawnView.setSize(1200, 400);
                drawnView.sync();
                drawnView.resized();
                // The thumbnail is filled in on a worker, so the picture is
                // only worth reading once something has arrived in it.
                auto bands = 0;
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
                while (juce::Time::getMillisecondCounterHiRes() < deadline)
                {
                    juce::Thread::sleep(10);
                    drawnView.sync();
                    if (drawnView.clips.empty())
                        continue;
                    const auto box = drawnView.bounds(drawnView.clips.front()).getSmallestIntegerContainer();
                    if (box.getWidth() < 8 || box.getHeight() < 40)
                        continue;
                    const auto picture = drawnView.createComponentSnapshot(drawnView.getLocalBounds());
                    // Below the header strip, which carries the clip's own
                    // colour and would read as a band of its own.
                    bands = bandsDown(picture, box.getCentreX(), box.getY() + 26, box.getBottom() - 2);
                    if (bands > 0)
                        break;
                }
                return bands;
            };
            require(lanesDrawnFor(loudMono.getFile()) == 1, "A mono clip is drawn as one lane");
            require(lanesDrawnFor(loudStereo.getFile()) == 2,
                    "and a stereo one as two, kept apart rather than merged into a single waveform");
        }
