        // Slice to New MIDI Track renders, so what it makes is heard rather
        // than read back off the pads: the new track, struck by its own clip,
        // has to play what the audio clip did. Also held: where the cuts fall,
        // that a slice too long for a pad is cut into parts that fit, the
        // refusals, and that one undo takes it all back.
        //
        // The fixture is twelve seconds: four short 220 Hz hits half a second
        // apart from the start, then from 1.9 s a quiet 330 Hz tone to the
        // end. At 120 BPM a hit falls on each beat, and the tone is a fifth
        // slice 10.1 s long, longer than a pad holds, so it is cut in two.
        const auto sliceScratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("Rhino slice test " + juce::String(juce::Time::getHighResolutionTicks()));
        require(sliceScratch.createDirectory().wasOk(), "A scratch folder for the slices");
        const auto sliceFixture = sliceScratch.getChildFile("hits.wav");
        {
            std::unique_ptr<juce::OutputStream> stream = sliceFixture.createOutputStream();
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr, "Create the slicing fixture");
            const auto rate = 48000.0;
            juce::AudioBuffer<float> audio(2, 12 * 48000);
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const auto time = i / rate;
                auto value = 0.0;
                for (const auto hit : {0.0, 0.5, 1.0, 1.5})
                    if (time >= hit && time < hit + 0.3)
                        value += 0.8 * std::exp(-(time - hit) / 0.05)
                               * std::sin((time - hit) * juce::MathConstants<double>::twoPi * 220.0);
                if (time >= 1.9)
                    value += 0.1 * std::sin((time - 1.9) * juce::MathConstants<double>::twoPi * 330.0);
                audio.setSample(0, i, static_cast<float>(value));
                audio.setSample(1, i, static_cast<float>(value));
            }
            require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the slicing fixture");
        }
        const auto readSliceFile = [&require](const juce::File& file, juce::AudioBuffer<float>& audio)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            require(reader != nullptr, "A slicing file can be read back");
            audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "Read its samples");
            return reader->sampleRate;
        };
        {
            Session slicing;
            slicing.projectFile = sliceScratch.getChildFile("slices.rhinoedit");
            slicing.setTempo(120.0);
            require(slicing.importAudio(sliceFixture).wasOk(), "The fixture imports");
            const auto lane = 1;
            const auto clipID = te::getAudioTracks(*slicing.edit)[lane]->getClips().getFirst()->itemID;
            const auto clipName = slicing.findClip(clipID)->getName();
            const auto slicesOnDisk = [&slicing]
            {
                return slicing.mergedAudioDirectory().findChildFiles(juce::File::findFiles, false, "*Slice*.wav").size();
            };
            const auto notesOf = [&slicing](te::EditItemID id)
            {
                std::vector<std::pair<int, double>> found;
                if (auto* midi = dynamic_cast<te::MidiClip*>(slicing.findClip(id)))
                    for (auto* note : midi->getSequence().getNotes())
                        found.push_back({note->getNoteNumber(), note->getStartBeat().inBeats()});
                std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
                return found;
            };

            te::EditItemID midiClip;
            require(slicing.createClip(0, 0.0, &midiClip).wasOk(), "A MIDI clip to try slicing");
            require(slicing.sliceClipToNewMidiTrack(midiClip).failed() && slicing.firstReturnTrack() == 4,
                    "A MIDI clip is refused: only audio is sliced");
            slicing.deleteClip(midiClip);
            const auto tooMany = slicing.sliceClipToNewMidiTrack(clipID, 0.25);
            require(tooMany.failed() && tooMany.getErrorMessage().contains("96 slices")
                        && slicing.firstReturnTrack() == 4 && slicesOnDisk() == 0,
                    "Every 1/16 of twelve seconds is 96 slices, more than the 92 pads from C1, "
                    "so it is refused before anything is made or written");

            // On the grid: a slice a beat, from the clip's start.
            Session::SliceResult made;
            require(slicing.sliceClipToNewMidiTrack(clipID, 1.0, &made).wasOk(), "The clip slices every 1/4");
            require(made.slices == 24 && made.track == lane + 1 && slicing.firstReturnTrack() == 5,
                    "into 24 slices, on a new track directly under its own");
            const auto everyBeat = notesOf(made.clip);
            auto onTheBeat = everyBeat.size() == 24;
            for (size_t i = 0; onTheBeat && i < everyBeat.size(); ++i)
                onTheBeat = everyBeat[i].first == Session::firstSliceNote + static_cast<int>(i)
                         && std::abs(everyBeat[i].second - static_cast<double>(i)) < 1.0e-3;
            require(onTheBeat, "a note a slice, a semitone up each time from C1, each on its beat");
            slicing.undo();
            require(slicing.firstReturnTrack() == 4 && !slicing.findClip(clipID)->isMuted(),
                    "One undo takes the track away and plays the clip again");

            // Through the arrangement's clip menu: the selection moves onto
            // the MIDI clip it made.
            {
                Arrangement sliceView(slicing);
                sliceView.setSize(1200, 508);
                sliceView.sync();
                sliceView.fit();
                sliceView.sliceToNewMidiTrack(clipID, Session::sliceAtTransients);
                require(slicing.firstReturnTrack() == 5, "The clip menu's command slices the clip");
                require(dynamic_cast<te::MidiClip*>(slicing.findClip(sliceView.selectedClipID())) != nullptr
                            && slicing.trackType(lane + 1) == Session::TrackType::midi,
                        "and selects the MIDI clip it made");
            }
            slicing.undo();
            require(slicing.firstReturnTrack() == 4, "which undoes like the model's own");

            // At the transients: a slice a hit, one where the tone comes in,
            // and the tone's 10.1 s in two parts a pad can each hold whole.
            require(slicing.sliceClipToNewMidiTrack(clipID, Session::sliceAtTransients, &made).wasOk(),
                    "The clip slices at its transients");
            const auto sliced = made.track;
            require(made.slices == 6, "into a slice a hit, and the tone in two");
            require(slicing.trackType(sliced) == Session::TrackType::midi && slicing.trackName(sliced) == clipName + " Slices",
                    "on a MIDI track named for the clip");
            require(slicing.trackColour(sliced) == Session::clipColour(*slicing.findClip(clipID)), "in the clip's colour");
            require(slicing.findClip(clipID)->isMuted(), "The source clip is muted, so the song does not play it twice");
            const auto hits = notesOf(made.clip);
            require(hits.size() == 6, "Its clip strikes each slice once");
            for (size_t i = 0; i < 4; ++i)
                require(hits[i].first == Session::firstSliceNote + static_cast<int>(i)
                            && hits[i].second > static_cast<double>(i) - 0.02 && hits[i].second <= static_cast<double>(i) + 1.0e-6,
                        "each hit's note falls just before its hit");
            require(std::abs(hits[4].second - 3.8) < 0.02 && hits[5].second > 13.0 && hits[5].second < 14.5,
                    "and the tone's two at its start and half way through it");
            auto* rack = dynamic_cast<DrumRackDevice*>(slicing.patternInstrumentForTrack(sliced));
            require(rack != nullptr, "The new track plays a Drum Rack");
            auto framesOnPads = 0;
            for (int note = Session::firstSliceNote; note < Session::firstSliceNote + 6; ++note)
            {
                const auto pad = rack->pad(note);
                require(pad.sound.has_value() && pad.sound->source == DrumRackEngine::Source::sample && !pad.unreadable,
                        "with a slice on each of its first six pads from C1");
                const auto file = ContentLibrary::resolveStoredPath(pad.sound->sample);
                require(file.isAChildOf(sliceScratch), "each a file of its own beside the project");
                juce::AudioBuffer<float> slice;
                const auto rate = readSliceFile(file, slice);
                require(slice.getNumSamples() <= std::ceil(rate * DrumRackEngine::longestSampleSeconds),
                        "none longer than a pad holds");
                framesOnPads += slice.getNumSamples();
            }
            require(!rack->pad(Session::firstSliceNote + 6).sound.has_value() && !rack->pad(Session::firstSliceNote - 1).sound.has_value(),
                    "and nothing on the pads either side");
            const auto deviceRate = slicing.engine.getDeviceManager().getSampleRate() > 7000.0
                ? slicing.engine.getDeviceManager().getSampleRate() : 48000.0;
            require(std::abs(framesOnPads - 12.0 * deviceRate) < deviceRate * 0.01,
                    "The slices together are the whole clip");
            require(slicing.sliceClipToNewMidiTrack(clipID).failed(), "A muted clip is refused, since it would slice to silence");

            // Heard: the slices' track, rendered, against the clip rendered on
            // its own track. Both bounce before the fader, through a track
            // with nothing else on it.
            Session::BounceResult fromSlices, fromClip;
            require(slicing.bounceTracksToNewTracks({sliced}, &fromSlices).wasOk(), "The slices' track bounces");
            require(slicing.setClipsMuted({clipID}, false).wasOk()
                        && slicing.bounceClipsToNewTracks({clipID}, &fromClip).wasOk(),
                    "and so does the clip, unmuted again");
            juce::AudioBuffer<float> played, original;
            readSliceFile(slicing.audioClipMix(fromSlices.clips.front()).sourceFile, played);
            const auto rate = readSliceFile(slicing.audioClipMix(fromClip.clips.front()).sourceFile, original);
            const auto frames = std::min(played.getNumSamples(), original.getNumSamples());
            require(frames > static_cast<int>(rate * 11.9), "Both bounces are the whole clip long");
            // Over the hits and over the cut through the middle of the tone.
            for (const auto& [from, to] : {std::pair {0.0, 1.9}, std::pair {6.5, 7.5}})
            {
                const auto first = static_cast<int>(from * rate);
                const auto count = static_cast<int>((to - from) * rate);
                auto signal = 0.0, error = 0.0;
                for (int i = first; i < first + count; ++i)
                {
                    const auto want = static_cast<double>(original.getSample(0, i));
                    const auto got = static_cast<double>(played.getSample(0, i));
                    signal += want * want;
                    error += (got - want) * (got - want);
                }
                require(signal > 0.0 && std::sqrt(error / signal) < 0.1,
                        "Struck in order, the slices play what the clip played");
            }
        }
        sliceScratch.deleteRecursively();
