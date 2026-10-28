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
            // The first audio lane of a new document.
            const auto lane = 2;
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

            // One cell of the grid at the deepest zoom, a few samples wide,
            // swept inside a clip: copy takes exactly it, cut takes exactly it
            // out and leaves both sides, and pasting it into a clip cuts a hole
            // of exactly its size. The engine will not split within a
            // millisecond of a clip's end, so it refused the second cut of
            // each, and the clip from the cell on was deleted with the cell.
            {
                const auto exactly = [](double a, double b) { return std::abs(a - b) < 1.0e-7; };
                const auto cell = beat / 4096.0;
                const auto around = [&spans](double from, double to)
                {
                    std::vector<std::pair<double, double>> found;
                    for (const auto& span : spans())
                        if (span.first >= from - 1.0e-9 && span.second <= to + 1.0e-9)
                            found.push_back(span);
                    return found;
                };
                const auto clipEnd = 12.0 + beat + sampleSeconds;
                require(slivers.importAudioAt(longBeat.getFile(), lane, 12.0).wasOk(), "A sample to cut one cell out of");
                const auto taken = slivers.copyClipRegion(12.2, 12.2 + cell, lane, lane);
                require(taken.clips.size() == 1 && exactly(taken.clips.front().end - taken.clips.front().start, cell),
                        "Copying one deepest-zoom cell inside a clip copies exactly that cell");
                require(slivers.clearClipRegion(12.2, 12.2 + cell, lane, lane).wasOk(), "Cut that cell out");
                const auto cut = around(12.0, clipEnd);
                require(cut.size() == 2 && exactly(cut[0].first, 12.0) && exactly(cut[0].second, 12.2)
                            && exactly(cut[1].first, 12.2 + cell) && exactly(cut[1].second, clipEnd),
                        "Cutting it leaves both sides of the clip, missing only that cell");
                std::vector<te::EditItemID> pasted;
                require(slivers.pasteClipRegion(taken, 12.3, lane, pasted).wasOk() && pasted.size() == 1,
                        "Paste the cell further on");
                const auto filled = around(12.0, clipEnd);
                require(filled.size() == 4 && exactly(filled[1].second, 12.3) && exactly(filled[2].first, 12.3)
                            && exactly(filled[2].second, 12.3 + cell) && exactly(filled[3].first, 12.3 + cell)
                            && exactly(filled[3].second, clipEnd),
                        "Pasting it lands on exactly its cell, the clip either side intact");
                // Half a millisecond into a clip, which the engine will not cut either.
                require(slivers.importAudioAt(longBeat.getFile(), lane, 14.0).wasOk(), "A sample to cut near its start");
                require(slivers.clearClipRegion(14.0005, 14.0005 + cell, lane, lane).wasOk(), "Cut a cell half a millisecond in");
                const auto nearStart = around(14.0, 14.0 + beat + sampleSeconds);
                require(nearStart.size() == 2 && exactly(nearStart[0].second, 14.0005)
                            && exactly(nearStart[1].first, 14.0005 + cell) && exactly(nearStart[1].second, 14.0 + beat + sampleSeconds),
                        "A cell half a millisecond into a clip is cut out, the half millisecond before it kept");
            }
        }
