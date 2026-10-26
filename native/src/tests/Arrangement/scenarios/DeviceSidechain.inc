        // Any device with a sidechain input takes another track's audio:
        // Tracktion's compressor here, Rhino Duck where a build of it is found.
        // What the model offers and refuses, then what is heard, measured in
        // rendered audio. In documents of their own, and before "gestures and
        // persistence" closes the audio device.
        {
            const auto findDevice = [](Session& song, const juce::String& id)
            {
                for (int track = 0; track < song.trackCount(); ++track)
                    for (const auto& slot : song.deviceSlots(track))
                        if (slot.deviceId == id)
                            return std::pair<int, int> {track, slot.pluginIndex};
                return std::pair<int, int> {-1, -1};
            };
            const auto offers = [](const std::vector<Session::SidechainSource>& sources, int track)
            {
                return std::any_of(sources.begin(), sources.end(),
                                   [track](const auto& source) { return source.track == track; });
            };

            scenario("sidechain: what a device may take audio from");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                // MIDI, Audio, MIDI, Audio, then A Delay and B Reverb.
                require(song.addDevice("Compressor", 3).wasOk(), "A compressor on the second audio track");
                auto [track, slot] = findDevice(song, "Compressor");
                require(track == 3 && song.deviceTakesSidechain(track, slot),
                        "takes a sidechain: it declares an input past its own pair");
                auto* compressor = dynamic_cast<te::CompressorPlugin*>(song.devicePlugin(track, slot));
                require(compressor != nullptr && !compressor->useSidechainTrigger.get(),
                        "and listens to its own track until a source is chosen");
                const auto delay = song.firstReturnTrack();
                require(!song.deviceTakesSidechain(delay, song.deviceSlots(delay).front().pluginIndex),
                        "A delay takes none");
                const auto sources = song.deviceSidechainSources(track, slot);
                require(offers(sources, 0) && offers(sources, 1) && offers(sources, 2),
                        "Every track above it is offered");
                require(!offers(sources, track), "but not its own");
                require(!offers(sources, delay) && !offers(sources, delay + 1),
                        "nor a return it sends to: the return already carries its sound");
                require(song.setDeviceSidechainSource(track, slot, delay).failed(),
                        "and the model refuses one all the same");

                require(song.setDeviceSidechainSource(track, slot, 1).wasOk()
                            && song.deviceSidechainSource(track, slot) == 1 && compressor->useSidechainTrigger.get(),
                        "Choosing the first audio track routes it and tells the compressor to listen to it");
                song.undo();
                require(song.deviceSidechainSource(track, slot) == -1 && !compressor->useSidechainTrigger.get(),
                        "One undo takes back the routing and the compressor's switch together");
                require(song.setDeviceSidechainSource(track, slot, 1).wasOk(), "Routed again");

                // A loop through another device's sidechain: the first audio
                // track's own compressor may not take the track it drives.
                require(song.addDevice("Compressor", 1).wasOk(), "A second compressor, on the track that drives the first");
                auto secondSlot = -1;
                for (const auto& device : song.deviceSlots(1))
                    if (device.deviceId == "Compressor")
                        secondSlot = device.pluginIndex;
                require(secondSlot >= 0 && !offers(song.deviceSidechainSources(1, secondSlot), track),
                        "It is not offered the track it already feeds");
                require(song.setDeviceSidechainSource(1, secondSlot, track).failed(),
                        "and taking it is refused, which would be a loop through two sidechains");
                require(offers(song.deviceSidechainSources(1, secondSlot), 0), "Any other track still is");

                // A group's bus carries its members, so a member cannot take
                // it. Grouping a track whose device already takes the bus
                // turns the routing into a loop, and grouping clears it.
                require(song.clearDeviceSidechainSource(track, slot).wasOk(), "The first compressor routed to nothing");
                // Tracks move as groups form, so the compressor is found again
                // by its engine id each time rather than by where it was.
                const auto compressorId = song.devicePlugin(track, slot)->itemID;
                const auto locate = [&song, compressorId]
                {
                    for (int candidate = 0; candidate < song.trackCount(); ++candidate)
                        for (const auto& device : song.deviceSlots(candidate))
                            if (auto* plugin = song.devicePlugin(candidate, device.pluginIndex);
                                plugin != nullptr && plugin->itemID == compressorId)
                                return std::pair<int, int> {candidate, device.pluginIndex};
                    return std::pair<int, int> {-1, -1};
                };
                require(song.groupTracks({0}).wasOk(), "The first track grouped alone");
                const auto group = song.trackGroups().front();
                std::tie(track, slot) = locate();
                require(track >= 0 && !group.covers(track), "The compressor's track is outside the group");
                require(song.setDeviceSidechainSource(track, slot, group.busTrack).wasOk(),
                        "and may take the bus, which carries nothing of it");
                require(song.addTrackToGroup(track, group.id).wasOk(), "Its track joins the group");
                std::tie(track, slot) = locate();
                require(track >= 0 && song.trackGroupId(track) == group.id, "and the compressor is now on a member");
                require(song.deviceSidechainSource(track, slot) == -1,
                        "whose sidechain from its own bus is cleared rather than left feeding back");
                require(!offers(song.deviceSidechainSources(track, slot), group.busTrack),
                        "and the bus is no longer offered");
                song.undo();
                std::tie(track, slot) = locate();
                require(track >= 0 && song.trackGroupId(track) != group.id
                            && song.isGroupBusTrack(song.deviceSidechainSource(track, slot)),
                        "One undo puts the track outside the group with its sidechain on the bus");
            }

            // A short burst of 60 Hz from 0.25 s to 0.45 s on the first audio
            // track, muted, and a steady 1 kHz tone on the second. What is
            // rendered is the tone alone, and its level in a window says what
            // the device on it did there.
            const auto writeFixture = [&wav, &require](const juce::File& file, auto&& sampleAt)
            {
                std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
                require(writer != nullptr, "A sidechain fixture");
                juce::AudioBuffer<float> audio(2, 48000);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    for (int channel = 0; channel < 2; ++channel)
                        audio.setSample(channel, i, sampleAt(i));
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the fixture");
            };
            juce::TemporaryFile kick(".wav"), tone(".wav");
            constexpr auto twoPi = 2.0 * juce::MathConstants<double>::pi;
            writeFixture(kick.getFile(), [twoPi](int i)
            {
                return i >= 12000 && i < 21600 ? 0.8f * static_cast<float>(std::sin(twoPi * 60.0 * i / 48000.0)) : 0.0f;
            });
            writeFixture(tone.getFile(), [twoPi](int i)
            {
                return 0.3f * static_cast<float>(std::sin(twoPi * 1000.0 * i / 48000.0));
            });
            const auto prepare = [&](Session& song)
            {
                require(song.importAudioAt(kick.getFile(), 1, 0.0).wasOk(), "The burst on the first audio track");
                require(song.importAudioAt(tone.getFile(), 3, 0.0).wasOk(), "and the tone on the second");
                song.setTrackMuted(1, true);
            };
            const auto render = [&wav, &require](Session& song)
            {
                juce::TemporaryFile rendered(".wav");
                te::Renderer::Parameters parameters(*song.edit);
                parameters.destFile = rendered.getFile();
                parameters.audioFormat = &wav;
                parameters.sampleRateForAudio = 48000;
                parameters.bitDepth = 24;
                parameters.time = {{}, tracktion::core::TimePosition::fromSeconds(1.0)};
                {
                    te::Renderer::RenderTask task("Sidechain test", parameters, nullptr, nullptr);
                    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                    while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                        require(juce::Time::getMillisecondCounterHiRes() < deadline, "Sidechain render timeout");
                    require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
                }
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
                require(reader != nullptr, "Read the sidechain render");
                juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
                require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read its samples");
                return output;
            };
            // A window's level against another's, in dB.
            const auto dipDb = [](const juce::AudioBuffer<float>& audio, double from, double to, double refFrom, double refTo)
            {
                const auto rmsOf = [&audio](double a, double b)
                {
                    const auto first = juce::jlimit(0, audio.getNumSamples(), static_cast<int>(a * 48000.0));
                    const auto last = juce::jlimit(first, audio.getNumSamples(), static_cast<int>(b * 48000.0));
                    return last > first ? audio.getRMSLevel(0, first, last - first) : 0.0f;
                };
                return 20.0 * std::log10(std::max(1.0e-9f, rmsOf(from, to)) / std::max(1.0e-9f, rmsOf(refFrom, refTo)));
            };
            const auto setByName = [&require](Session& song, int track, int slot, const char* name, bool highest)
            {
                const auto parameters = song.deviceParameters(track, slot);
                for (int i = 0; i < static_cast<int>(parameters.size()); ++i)
                    if (parameters[static_cast<size_t>(i)].name == name)
                    {
                        const auto& parameter = parameters[static_cast<size_t>(i)];
                        require(song.setDeviceParameter(track, slot, i, highest ? parameter.maximum : parameter.minimum).wasOk(),
                                "Set a device's control");
                        return;
                    }
                require(false, "A device's control by name");
            };

            scenario("sidechain: a muted burst on one track compresses another");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                prepare(song);
                require(song.addDevice("Compressor", 3).wasOk(), "A compressor on the tone");
                const auto [track, slot] = findDevice(song, "Compressor");
                // As hard as it goes: the lowest threshold, an infinite ratio
                // and the quickest attack and release.
                setByName(song, track, slot, "Threshold", false);
                setByName(song, track, slot, "Ratio", false);
                setByName(song, track, slot, "Attack", false);
                setByName(song, track, slot, "Release", false);

                const auto own = render(song);
                require(std::abs(dipDb(own, 0.30, 0.40, 0.70, 0.95)) < 1.0,
                        "With no source it hears only the tone, and squeezes it evenly throughout");
                require(song.setDeviceSidechainSource(track, slot, 1).wasOk(), "The burst's track is its source");
                const auto driven = render(song);
                require(dipDb(driven, 0.30, 0.40, 0.70, 0.95) < -12.0,
                        "and the tone drops under the burst, which is heard from a muted track");
                require(std::abs(dipDb(driven, 0.05, 0.20, 0.70, 0.95)) < 1.0,
                        "and is left alone either side of it");
            }

            // Only where a Duck build is found, and always where the
            // development build is, so a skip is never silent there.
            scenario("sidechain: a muted burst triggers Rhino Duck");
            auto duckDocument = std::make_unique<Session>();
            if (juce::File(juce::String(RHINO_SOURCE_DIR))
                    .getChildFile("../effects/rhino-duck/build/RhinoDuck_artefacts/Release/VST3/Rhino Duck.vst3").exists())
                require(duckDocument->isExternalAvailable("RhinoDuck"), "Rhino finds the development Duck build to render with");
            if (auto document = std::move(duckDocument); document->isExternalAvailable("RhinoDuck"))
            {
                auto& song = *document;
                prepare(song);
                require(song.addDevice("RhinoDuck", 3).wasOk(), "Rhino Duck on the tone");
                const auto [track, slot] = findDevice(song, "RhinoDuck");
                require(song.deviceTakesSidechain(track, slot), "It has a sidechain input, so the rack offers it one");
                // The Audio trigger and the whole depth. Its Classic curve holds
                // the tone at nothing for 30 ms after a hit, which spans the
                // 16 ms the Duck delays itself by, compensated or not.
                setByName(song, track, slot, "Trigger", true);
                setByName(song, track, slot, "Mix", true);

                const auto unrouted = render(song);
                require(std::abs(dipDb(unrouted, 0.270, 0.278, 0.10, 0.20)) < 1.0,
                        "In Audio mode with no source its sidechain hears silence and nothing is ducked");
                require(song.setDeviceSidechainSource(track, slot, 1).wasOk(), "The burst's track is its source");
                const auto routed = render(song);
                require(dipDb(routed, 0.270, 0.278, 0.10, 0.20) < -20.0,
                        "and the burst's onset ducks the tone");
            }
        }
