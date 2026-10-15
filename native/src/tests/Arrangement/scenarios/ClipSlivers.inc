        // Nothing leaves a clip too short to hear or grab. A drum sample one
        // sample longer than a beat, laid on the grid, crosses the next line by
        // that one sample, and copying the region after it used to take that
        // sample as a clip of its own, which a paste then laid down as a sliver
        // that played nothing. Making room had the same flaw for an overlap of
        // a few milliseconds: the engine refuses a cut within one, not within
        // ten. Neither leaves a piece shorter than a split by hand may now. A
        // document of its own.
        {
            Session slivers;
            const auto beat = 60.0 / slivers.tempo();
            const auto sampleSeconds = 1.0 / 48000.0;
            juce::TemporaryFile longBeat(".wav");
            {
                std::unique_ptr<juce::OutputStream> stream = longBeat.getFile().createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
                require(writer != nullptr, "A beat-and-a-sample fixture can be written");
                juce::AudioBuffer<float> audio(2, juce::roundToInt(beat * 48000.0) + 1);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    for (int channel = 0; channel < 2; ++channel)
                        audio.setSample(channel, i, 0.5f * static_cast<float>(
                            std::sin(i * 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0)));
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
                        "Write the beat-and-a-sample fixture");
            }
            const auto lane = 1;
            const auto spans = [&slivers, lane]
            {
                std::vector<std::pair<double, double>> found;
                for (auto* clip : te::getAudioTracks(*slivers.edit)[lane]->getClips())
                    found.emplace_back(clip->getPosition().time.getStart().inSeconds(),
                                       clip->getPosition().time.getEnd().inSeconds());
                std::sort(found.begin(), found.end());
                return found;
            };
            const auto clipStarting = [&slivers, lane](double seconds)
            {
                for (auto* clip : te::getAudioTracks(*slivers.edit)[lane]->getClips())
                    if (std::abs(clip->getPosition().time.getStart().inSeconds() - seconds) < 1.0e-6)
                        return clip->itemID;
                return te::EditItemID();
            };
            const auto shortest = [&spans]
            {
                auto least = 1.0e9;
                for (const auto& span : spans())
                    least = std::min(least, span.second - span.first);
                return least;
            };

            // A clip moved onto the start of one that runs a few milliseconds
            // past its own end takes the whole of it, rather than leaving those
            // milliseconds as a stub.
            const auto stub = 0.005;
            require(slivers.importAudioAt(longBeat.getFile(), lane, 2.0).wasOk(), "A sample a beat and a sample long");
            require(slivers.importAudioAt(longBeat.getFile(), lane, 20.0).wasOk(), "and another to move onto it");
            const auto mover = clipStarting(20.0);
            require(slivers.editClip(mover, {20.0, 20.0 + beat - stub, 0.0}, ClipGesture::trimRight).wasOk(),
                    "trimmed a few milliseconds short of a beat");
            require(slivers.editClip(mover, {2.0, 2.0 + beat - stub, 0.0}, ClipGesture::move).wasOk(),
                    "and moved onto the first");
            require(spans().size() == 1 && close(spans().front().first, 2.0) && close(spans().front().second, 2.0 + beat - stub),
                    "An overhang shorter than a split may leave goes with the clip it belonged to");

            // One landing a few milliseconds after the start of another
            // replaces it whole rather than leaving a stub in front of itself.
            require(slivers.importAudioAt(longBeat.getFile(), lane, 4.0).wasOk(), "A sample to land just inside of");
            const auto lander = clipStarting(2.0);
            require(lander != te::EditItemID()
                        && slivers.editClip(lander, {4.0 + stub, 4.0 + beat, 0.0}, ClipGesture::move).wasOk(),
                    "A clip moved to a few milliseconds after its start");
            require(clipStarting(4.0) == te::EditItemID() && spans().size() == 1,
                    "The stub in front of the clip that landed goes with the clip it belonged to");
            require(shortest() >= minimumSplitSeconds, "and nothing on the lane is shorter than a split may leave");

            // A region whose edge falls a sample inside a clip does not copy
            // that sample as a clip; one wholly inside it copies whatever its size.
            require(slivers.importAudioAt(longBeat.getFile(), lane, 6.0).wasOk(), "A sample to copy the beat after");
            require(slivers.copyClipRegion(6.0 + beat, 7.0, lane, lane).clips.empty(),
                    "Copying the beat after a clip that reaches a sample into it copies nothing");
            const auto whole = slivers.copyClipRegion(6.0, 7.0, lane, lane);
            require(whole.clips.size() == 1 && close(whole.clips.front().end - whole.clips.front().start, beat + sampleSeconds),
                    "and a region around the whole clip still copies all of it");

            // A real overhang still splits as it always did.
            require(slivers.importAudioAt(longBeat.getFile(), lane, 8.0).wasOk(), "A sample to cut into");
            require(slivers.clearClipRegion(8.0 + beat * 0.5, 9.0, lane, lane).wasOk(), "A region over its second half");
            require(clipStarting(8.0) != te::EditItemID()
                        && close(slivers.findClip(clipStarting(8.0))->getPosition().time.getEnd().inSeconds(), 8.0 + beat * 0.5),
                    "A clip crossing an edge by more than a split must leave is still cut there");
        }
