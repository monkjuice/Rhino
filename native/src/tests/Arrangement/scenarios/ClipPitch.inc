// Clip pitch: transposing a clip moves its notes and nothing else. Every hit
// has to land where it did before, and the clip has to begin and end as
// cleanly as it did. Before applyStretchPlayback, a pitched clip played from
// the engine's rendered copy, which lagged every hit by the stretcher's delay
// - about 120 ms, a sixteenth at 125 BPM - and cut the last of it off hard at
// the clip's end. A clip once warped in Beats and then unwarped kept
// SoundTouch for its pitch, which starts 13 to 21 ms early and so enters the
// clip partway into its first attack. Both were clicks, and neither shows in
// the model, so this renders and measures the audio.
{
    const auto pitchFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Rhino clip pitch " + juce::String(juce::Time::getHighResolutionTicks()));
    require(pitchFolder.createDirectory().wasOk(), "A folder for the pitch fixture and its document");
    {
        // Two seconds at 120 BPM: a 150 Hz hit with a sharp attack on every
        // beat, and a short one a sixteenth before the end, where a copy that
        // runs late is cut off. Every hit starts at zero, so the clip itself
        // has no step anywhere for a measurement to mistake for a click.
        const auto pitchSource = pitchFolder.getChildFile("Hits.wav");
        const auto hitTimes = std::array<double, 5>{0.0, 0.5, 1.0, 1.5, 1.875};
        {
            std::unique_ptr<juce::OutputStream> stream = pitchSource.createOutputStream();
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr, "A pitch fixture can be written");
            juce::AudioBuffer<float> audio(2, 96000);
            audio.clear();
            for (size_t hit = 0; hit < hitTimes.size(); ++hit)
            {
                const auto first = static_cast<int>(hitTimes[hit] * 48000.0);
                const auto decay = hit + 1 == hitTimes.size() ? 0.02 : 0.05;
                for (int i = first; i < audio.getNumSamples(); ++i)
                {
                    const auto t = (i - first) / 48000.0;
                    const auto value = static_cast<float>(0.7 * std::exp(-t / decay)
                        * std::sin(2.0 * juce::MathConstants<double>::pi * 150.0 * t));
                    audio.addSample(0, i, value);
                    audio.addSample(1, i, value);
                }
            }
            require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the pitch fixture");
        }

        Session pitched;
        require(pitched.importAudioAt(pitchSource, 2, 1.0).wasOk(), "The fixture lands one second in");
        const auto clipStartingAt = [&pitched, &require](double seconds)
        {
            auto* lane = te::getAudioTracks(*pitched.edit)[2];
            require(lane != nullptr, "The pitch fixture's lane is there");
            for (auto* clip : lane->getClips())
                if (std::abs(clip->getPosition().time.getStart().inSeconds() - seconds) < 0.001)
                    return clip->itemID;
            require(false, "A clip starts where the pitch scenario expects one");
            return te::EditItemID();
        };

        // Half a second either side of a clip, rendered at the block size the
        // device runs at, and read back.
        const auto renderAround = [&pitched, &wav, &require](double clipStart)
        {
            juce::TemporaryFile out(".wav");
            te::Renderer::Parameters parameters(*pitched.edit);
            parameters.destFile = out.getFile();
            parameters.audioFormat = &wav;
            parameters.sampleRateForAudio = 48000;
            parameters.bitDepth = 24;
            parameters.blockSizeForAudio = 480;
            parameters.time = {tracktion::core::TimePosition::fromSeconds(clipStart - 0.5),
                               tracktion::core::TimePosition::fromSeconds(clipStart + 2.5)};
            {
                te::Renderer::RenderTask task("Pitch test", parameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Pitch render timeout");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out.getFile()));
            require(reader != nullptr && reader->lengthInSamples == 144000, "A pitch render reads back whole");
            juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), 144000);
            require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "every sample of it");
            return audio;
        };
        // Where each hit starts, in seconds from the clip's start: the first
        // sample past a tenth of full scale at least 110 ms after the last
        // one, by when every hit has decayed below it; the closest two hits,
        // the short one and the next clip's first, are 125 ms apart. Only
        // inside the clip, so a neighbour's hits are not counted.
        const auto onsets = [](const juce::AudioBuffer<float>& audio)
        {
            std::vector<double> found;
            const auto* samples = audio.getReadPointer(0);
            int last = -100000;
            for (int i = 0; i < audio.getNumSamples(); ++i)
                if (std::abs(samples[i]) > 0.1f && i - last > 5280)
                {
                    last = i;
                    if (const auto at = i / 48000.0 - 0.5; at > -0.05 && at < 2.0)
                        found.push_back(at);
                }
            return found;
        };
        // The largest step from one sample to the next within 5 ms of a
        // moment, in seconds from the clip's start. A 150 Hz hit at full
        // level climbs at most 0.014 a sample, a 178 Hz one 0.017; a clip cut
        // off partway through a hit steps by up to the hit's whole level.
        const auto stepNear = [](const juce::AudioBuffer<float>& audio, double seconds)
        {
            const auto* samples = audio.getReadPointer(0);
            const auto centre = static_cast<int>((seconds + 0.5) * 48000.0);
            float largest = 0.0f;
            for (int i = std::max(1, centre - 240); i < std::min(audio.getNumSamples(), centre + 240); ++i)
                largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
            return largest;
        };
        // How much of the second hit is at a frequency: one DFT bin over the
        // hit's first tenth of a second.
        const auto toneOfSecondHit = [](const juce::AudioBuffer<float>& audio, double hertz)
        {
            const auto first = static_cast<int>((0.5 + 0.5) * 48000.0), count = 4800;
            double real = 0.0, imaginary = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto angle = 2.0 * juce::MathConstants<double>::pi * hertz * i / 48000.0;
                real += audio.getSample(0, first + i) * std::cos(angle);
                imaginary += audio.getSample(0, first + i) * std::sin(angle);
            }
            return std::sqrt(real * real + imaginary * imaginary) / count;
        };
        const auto shiftedTone = 150.0 * std::pow(2.0, 3.0 / 12.0);
        // The same hits where the plain clip has them, the same clean edges,
        // and a minor third up.
        const auto requirePitchedInTime = [&](double clipStart, const std::vector<double>& expected,
                                              const char* inTime, const char* clean, const char* moved)
        {
            const auto audio = renderAround(clipStart);
            const auto heard = onsets(audio);
            bool same = heard.size() == expected.size();
            for (size_t i = 0; same && i < heard.size(); ++i)
                same = std::abs(heard[i] - expected[i]) < 0.003;
            if (!same)
            {
                std::fprintf(stderr, "Pitched hits at");
                for (const auto at : heard)
                    std::fprintf(stderr, " %.4f", at);
                std::fprintf(stderr, " s from the clip's start\n");
            }
            require(same, inTime);
            if (stepNear(audio, 0.0) > 0.05 || stepNear(audio, 2.0) > 0.05)
                std::fprintf(stderr, "Pitched clip steps %.3f at its start and %.3f at its end\n",
                             stepNear(audio, 0.0), stepNear(audio, 2.0));
            require(stepNear(audio, 0.0) < 0.05 && stepNear(audio, 2.0) < 0.05, clean);
            require(toneOfSecondHit(audio, shiftedTone) > toneOfSecondHit(audio, 150.0) * 4.0, moved);
        };

        auto first = clipStartingAt(1.0);
        const auto plain = renderAround(1.0);
        const auto plainOnsets = onsets(plain);
        require(plainOnsets.size() == hitTimes.size(), "The plain clip's hits are all heard");
        for (size_t i = 0; i < hitTimes.size(); ++i)
            require(std::abs(plainOnsets[i] - hitTimes[i]) < 0.001, "each where the file has it");
        require(toneOfSecondHit(plain, 150.0) > toneOfSecondHit(plain, shiftedTone) * 4.0,
                "and at the pitch it was recorded at");

        require(pitched.setAudioClipPitch(first, 3.0f).wasOk(), "A clip can be pitched up a minor third");
        // Asked before rendering, because a render waits for a rendered copy
        // that only the message loop would make, and so never returns.
        require(!pitched.findAudioClip(first)->canUseProxy(),
                "A pitched clip plays through a stretcher live, not from the engine's late copy");
        requirePitchedInTime(1.0, plainOnsets, "A pitched clip's hits land where the plain clip's do",
                             "and it begins and ends without a click", "and they are a minor third up");

        // Warped in Beats and unwarped again, the clip's pitch still goes
        // through the engine's default stretcher rather than SoundTouch.
        require(pitched.setAudioClipPitch(first, 0.0f).wasOk(), "The pitch can be put back");
        require(pitched.setClipFollowsTempo(first, true).wasOk(), "The clip can be warped");
        require(pitched.clipWarp(first).mode == Session::WarpMode::beats, "in Beats");
        require(pitched.setClipFollowsTempo(first, false).wasOk(), "and unwarped again");
        require(pitched.setAudioClipPitch(first, 3.0f).wasOk(), "and pitched");
        requirePitchedInTime(1.0, plainOnsets, "A clip once warped in Beats is pitched in time",
                             "and without a click at either end", "and still a minor third up");

        // A duplicate is rebuilt from a snapshot of the clip rather than a
        // copy of its state, so it has to arrive streaming as well.
        require(pitched.duplicateClip(first).wasOk(), "A pitched clip can be duplicated");
        const auto second = clipStartingAt(3.0);
        require(close(pitched.audioClipMix(second).pitchSemitones, 3.0), "The duplicate carries the pitch");
        require(!pitched.findAudioClip(second)->canUseProxy(), "and streams it too");
        requirePitchedInTime(3.0, plainOnsets, "The duplicate's hits land where the original's do",
                             "and it begins and ends without a click", "and it is a minor third up too");

        // The choice is the clip's own property, so it comes back with the
        // document. Written to disk first, so the clip's file resolves.
        {
            const auto document = pitchFolder.getChildFile("Pitch.rhinoedit");
            const auto xml = pitched.projectSnapshot(document).createXml();
            require(xml != nullptr && xml->writeTo(document), "The pitched document is written");
            const auto read = juce::parseXML(document);
            require(read != nullptr && pitched.restoreProject(juce::ValueTree::fromXml(*read), document).wasOk(),
                    "and reopened");
        }
        first = clipStartingAt(1.0);
        require(close(pitched.audioClipMix(first).pitchSemitones, 3.0), "The reopened clip is still pitched");
        require(!pitched.findAudioClip(first)->canUseProxy(), "and still streams");
        requirePitchedInTime(1.0, plainOnsets, "A reopened pitched clip's hits land where they did",
                             "and it begins and ends without a click", "and it is still a minor third up");

        // Back at zero, nothing is stretched, so the engine's copy is the
        // cheaper way to play the clip and it is allowed again.
        require(pitched.setAudioClipPitch(first, 0.0f).wasOk(), "The pitch can be taken off");
        require(pitched.findAudioClip(first)->canUseProxy(), "and the clip plays plainly again");
    }
    pitchFolder.deleteRecursively();
}
