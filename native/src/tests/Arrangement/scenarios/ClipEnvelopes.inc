// Clip envelopes: an audio clip's own Gain, Pan and Pitch envelopes, Live's
// clip envelopes. They are the clip's, so a move, a trim, a split, a copy, an
// undo, a reverse, a merge and a saved document all have to keep them on the
// sound they were drawn against - and the engine has to play them, which only
// a render can show. So this renders and measures: a level, a side, a pitch
// and where the hits land.
{
    const auto envelopeFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Rhino clip envelopes " + juce::String(juce::Time::getHighResolutionTicks()));
    require(envelopeFolder.createDirectory().wasOk(), "A folder for the envelope fixtures and their document");
    {
        // Two seconds at 48 kHz: a steady 200 Hz tone on both sides, which a
        // level and a side are read from, and the hits ClipPitch.inc times -
        // a 150 Hz hit with a sharp attack on every beat at 120 BPM and a
        // short one a sixteenth before the end.
        const auto writeFixture = [&wav, &require](const juce::File& file, const std::function<float(int)>& sample)
        {
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr, "An envelope fixture can be written");
            juce::AudioBuffer<float> audio(2, 96000);
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                audio.setSample(0, i, sample(i));
                audio.setSample(1, i, sample(i));
            }
            require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the envelope fixture");
        };
        const auto toneFile = envelopeFolder.getChildFile("Tone.wav");
        writeFixture(toneFile, [](int i)
        {
            return static_cast<float>(0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0));
        });
        const auto hitsFile = envelopeFolder.getChildFile("Hits.wav");
        const auto hitTimes = std::array<double, 5>{0.0, 0.5, 1.0, 1.5, 1.875};
        writeFixture(hitsFile, [&hitTimes](int i)
        {
            auto value = 0.0;
            for (size_t hit = 0; hit < hitTimes.size(); ++hit)
            {
                const auto first = static_cast<int>(hitTimes[hit] * 48000.0);
                if (i < first) continue;
                const auto t = (i - first) / 48000.0;
                value += 0.7 * std::exp(-t / (hit + 1 == hitTimes.size() ? 0.02 : 0.05))
                    * std::sin(2.0 * juce::MathConstants<double>::pi * 150.0 * t);
            }
            return static_cast<float>(value);
        });

        Session enveloped;
        require(enveloped.importAudioAt(toneFile, 1, 1.0).wasOk(), "The tone lands one second in");
        const auto clipStartingAt = [&enveloped, &require](double seconds)
        {
            auto* lane = te::getAudioTracks(*enveloped.edit)[1];
            require(lane != nullptr, "The envelope fixture's lane is there");
            for (auto* clip : lane->getClips())
                if (std::abs(clip->getPosition().time.getStart().inSeconds() - seconds) < 0.001)
                    return clip->itemID;
            require(false, "A clip starts where the envelope scenario expects one");
            return te::EditItemID();
        };
        // Half a second before a moment and two and a half after, rendered at
        // the block size the device runs at, and read back.
        const auto renderFrom = [&enveloped, &wav, &require](double clipStart)
        {
            juce::TemporaryFile out(".wav");
            te::Renderer::Parameters parameters(*enveloped.edit);
            parameters.destFile = out.getFile();
            parameters.audioFormat = &wav;
            parameters.sampleRateForAudio = 48000;
            parameters.bitDepth = 24;
            parameters.blockSizeForAudio = 480;
            parameters.time = {tracktion::core::TimePosition::fromSeconds(clipStart - 0.5),
                               tracktion::core::TimePosition::fromSeconds(clipStart + 2.5)};
            {
                te::Renderer::RenderTask task("Envelope test", parameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    if (juce::Time::getMillisecondCounterHiRes() >= deadline)
                    {
                        std::fprintf(stderr, "Envelope render from %.3f s timed out\n", clipStart);
                        require(false, "Envelope render timeout");
                    }
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out.getFile()));
            require(reader != nullptr && reader->lengthInSamples == 144000, "An envelope render reads back whole");
            juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), 144000);
            require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "every sample of it");
            return audio;
        };
        // The level of one side over a stretch, in seconds from the clip's
        // start: the tone's RMS is 0.354 at the clip's own level.
        const auto level = [](const juce::AudioBuffer<float>& audio, int channel, double from, double to)
        {
            return audio.getRMSLevel(channel, static_cast<int>((from + 0.5) * 48000.0),
                                     static_cast<int>((to - from) * 48000.0));
        };
        const auto heldAt = [&level](const juce::AudioBuffer<float>& audio, double from, double to, double share)
        {
            const auto expected = 0.3536 * share;
            return std::abs(level(audio, 0, from, to) - expected) < 0.01 + expected * 0.02
                && std::abs(level(audio, 1, from, to) - expected) < 0.01 + expected * 0.02;
        };
        const auto plugins = [&enveloped](te::EditItemID id)
        {
            auto* clip = enveloped.findAudioClip(id);
            return clip != nullptr && clip->getPluginList() != nullptr ? clip->getPluginList()->size() : -1;
        };
        // Full level for the first second of the clip, half after it.
        const std::vector<Session::AutomationPoint> halfway {{0.0, 1.0f}, {1.0, 1.0f}, {1.0, 0.5f}, {2.0, 0.5f}};
        const auto requireHalvedAt = [&](double clipStart, double stepAfterStart, const char* message)
        {
            const auto audio = renderFrom(clipStart);
            const auto before = level(audio, 0, stepAfterStart - 0.4, stepAfterStart - 0.05);
            const auto after = level(audio, 0, stepAfterStart + 0.05, stepAfterStart + 0.4);
            if (!heldAt(audio, stepAfterStart - 0.4, stepAfterStart - 0.05, 1.0)
                || !heldAt(audio, stepAfterStart + 0.05, stepAfterStart + 0.4, 0.5))
                std::fprintf(stderr, "Gain envelope: %.4f before the step, %.4f after it\n", before, after);
            require(heldAt(audio, stepAfterStart - 0.4, stepAfterStart - 0.05, 1.0)
                    && heldAt(audio, stepAfterStart + 0.05, stepAfterStart + 0.4, 0.5), message);
        };

        auto tone = clipStartingAt(1.0);
        require(plugins(tone) == 0, "A clip with no envelope carries no device for one");
        require(heldAt(renderFrom(1.0), 0.2, 1.8, 1.0), "The plain tone plays at its own level");

        // Gain. The envelope is a share of the knob, and a device plays it.
        require(enveloped.setClipEnvelope(tone, Session::ClipEnvelope::gain, halfway).wasOk(),
                "A gain envelope can be drawn on an audio clip");
        require(enveloped.clipEnvelopesInUse(tone)[0] && !enveloped.clipEnvelopesInUse(tone)[1],
                "The clip says it has a gain envelope and no other");
        require(plugins(tone) == 1 && enveloped.clipPluginCount(tone) == 0,
                "A device plays it, and no effect badge counts that device");
        requireHalvedAt(1.0, 1.0, "The gain envelope halves the clip a second in, as drawn");
        require(enveloped.setAudioClipGainDb(tone, -6.0206f).wasOk(), "The clip's own gain can still move");
        {
            const auto audio = renderFrom(1.0);
            require(heldAt(audio, 0.2, 0.9, 0.5) && heldAt(audio, 1.1, 1.8, 0.25),
                    "and the envelope takes its share of wherever the knob is");
        }
        require(enveloped.setAudioClipGainDb(tone, 0.0f).wasOk(), "The knob goes back");

        // Undo takes the envelope and its device back together.
        enveloped.undo();
        enveloped.undo();
        enveloped.undo();
        require(enveloped.clipEnvelope(tone, Session::ClipEnvelope::gain).empty() && plugins(tone) == 0,
                "Undo takes back the envelope and the device that played it");
        require(heldAt(renderFrom(1.0), 0.2, 1.8, 1.0), "and the clip plays plainly again");
        enveloped.redo();
        require(enveloped.clipEnvelope(tone, Session::ClipEnvelope::gain).size() == halfway.size() && plugins(tone) == 1,
                "Redo brings both back");
        requireHalvedAt(1.0, 1.0, "and the envelope plays again");

        // It is the clip's, so it goes where the clip goes.
        require(enveloped.editClip(tone, {2.0, 4.0, 0.0}, ClipGesture::move).wasOk(), "The clip moves a second on");
        tone = clipStartingAt(2.0);
        requireHalvedAt(2.0, 1.0, "and its envelope moves with it");
        // Trimming the start keeps every point on the sound it was drawn on.
        require(enveloped.editClip(tone, {2.5, 4.0, 0.5}, ClipGesture::trimLeft).wasOk(),
                "The clip's start can be trimmed half a second");
        tone = clipStartingAt(2.5);
        requireHalvedAt(2.5, 0.5, "and the step is still a second into the material");

        // Pan, as a balance on what the clip's own pan made.
        require(enveloped.setClipEnvelope(tone, Session::ClipEnvelope::pan, {{0.0, 1.0f}}).wasOk(),
                "A pan envelope can be drawn too");
        require(plugins(tone) == 1, "on the same device as the gain");
        {
            const auto audio = renderFrom(2.5);
            require(level(audio, 0, 0.1, 0.4) < 0.002 && std::abs(level(audio, 1, 0.1, 0.4) - 0.3536) < 0.012,
                    "Pan held hard right leaves only the right side");
            require(level(audio, 0, 0.6, 1.4) < 0.002 && std::abs(level(audio, 1, 0.6, 1.4) - 0.1768) < 0.008,
                    "and the gain envelope still halves it after the step");
        }
        require(enveloped.clearClipEnvelope(tone, Session::ClipEnvelope::pan).wasOk(), "The pan envelope can be cleared");
        require(plugins(tone) == 1 && !enveloped.clipEnvelopesInUse(tone)[1], "leaving the gain envelope's device");

        // A split leaves each half its own part of the envelope.
        require(enveloped.splitClip(tone, 3.0).wasOk(), "The clip can be split");
        const auto right = clipStartingAt(3.0);
        require(plugins(right) == 1, "The right half carries a device of its own");
        requireHalvedAt(2.5, 0.5, "and the step still falls where it did");

        // A copy carries it by value, and plays it where it lands.
        require(enveloped.duplicateClip(right).wasOk(), "The right half can be duplicated");
        const auto copy = clipStartingAt(4.0);
        require(enveloped.clipEnvelope(copy, Session::ClipEnvelope::gain).size() == halfway.size() && plugins(copy) == 1,
                "The duplicate carries the envelope and a device to play it");
        {
            const auto audio = renderFrom(4.0);
            require(heldAt(audio, 0.05, 0.45, 0.5), "and plays the half-level part it was copied from");
        }

        // Reversed, the clip's window moves within its file, and the envelope
        // stays on the clip. Read off the model rather than rendered: the
        // engine plays a reversed clip from a copy it renders on the message
        // loop, which an offline render on this thread would wait for forever.
        {
            const auto before = enveloped.audioClipMix(copy);
            const auto pointsBefore = enveloped.clipEnvelope(copy, Session::ClipEnvelope::gain);
            require(enveloped.setAudioClipReversed(copy, true).wasOk(), "The copy can be reversed");
            const auto after = enveloped.audioClipMix(copy);
            const auto pointsAfter = enveloped.clipEnvelope(copy, Session::ClipEnvelope::gain);
            require(std::abs(after.offsetSeconds - before.offsetSeconds) > 0.5,
                    "Reversing a trimmed copy moves its window within the file");
            for (const auto at : {0.05, 0.5, 0.95})
                require(close(clipEnvelopeValueAt(pointsBefore, before.offsetSeconds + at, 1.0f),
                              clipEnvelopeValueAt(pointsAfter, after.offsetSeconds + at, 1.0f)),
                        "and its envelope stays where it was on the clip");
            require(enveloped.setAudioClipReversed(copy, false).wasOk(), "and it plays forwards again");
            const auto restored = enveloped.audioClipMix(copy);
            require(close(restored.offsetSeconds, before.offsetSeconds), "with its window back");
            require(heldAt(renderFrom(4.0), 0.05, 0.45, 0.5), "and its envelope where it was");
        }

        // A merge renders without the devices and carries the envelopes onto
        // the clip it makes, so it plays what the parts played.
        {
            const auto left = clipStartingAt(2.5);
            Session::MergeResult merged;
            require(enveloped.mergeClips({left, right}, &merged).wasOk() && merged.clips.size() == 1,
                    "The two halves can be merged");
            require(!merged.lostClipEffects, "Envelopes are not effects lost to a merge");
            require(plugins(merged.clips.front()) == 1, "The merged clip carries the envelope's device");
            requireHalvedAt(2.5, 0.5, "and halves where the parts did");
        }

        // Pitch. The hits have to land where the plain clip's do, begin and
        // end cleanly, and move by what the envelope says when it says it.
        require(enveloped.importAudioAt(hitsFile, 1, 8.0).wasOk(), "The hits land at eight seconds");
        auto hits = clipStartingAt(8.0);
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
        const auto stepNear = [](const juce::AudioBuffer<float>& audio, double seconds)
        {
            const auto* samples = audio.getReadPointer(0);
            const auto centre = static_cast<int>((seconds + 0.5) * 48000.0);
            float largest = 0.0f;
            for (int i = std::max(1, centre - 240); i < std::min(audio.getNumSamples(), centre + 240); ++i)
                largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
            return largest;
        };
        // How much of a hit is at a frequency: one DFT bin over its first
        // tenth of a second.
        const auto toneOfHitAt = [](const juce::AudioBuffer<float>& audio, double hitSeconds, double hertz)
        {
            const auto first = static_cast<int>((hitSeconds + 0.5) * 48000.0), count = 4800;
            double real = 0.0, imaginary = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const auto angle = 2.0 * juce::MathConstants<double>::pi * hertz * i / 48000.0;
                real += audio.getSample(0, first + i) * std::cos(angle);
                imaginary += audio.getSample(0, first + i) * std::sin(angle);
            }
            return std::sqrt(real * real + imaginary * imaginary) / count;
        };
        const auto plainAudio = renderFrom(8.0);
        const auto plainHits = onsets(plainAudio);
        require(plainHits.size() == hitTimes.size(), "The plain hits are all heard");
        // The loudest a hit gets in its first 40 ms.
        const auto peakOfHitAt = [](const juce::AudioBuffer<float>& audio, double hitSeconds)
        {
            return audio.getMagnitude(0, static_cast<int>((hitSeconds + 0.5) * 48000.0), 1920);
        };
        // Plain for the first second, an octave up from there on.
        require(enveloped.setClipEnvelope(hits, Session::ClipEnvelope::pitch,
                                          {{0.0, 0.0f}, {0.9, 0.0f}, {0.9, 12.0f}, {2.0, 12.0f}}).wasOk(),
                "A pitch envelope can be drawn");
        // The shifter, and ahead of it the level device, which plays the
        // clip's fades while a shifter is on the clip.
        require(plugins(hits) == 2, "and a shifter plays it");
        juce::AudioBuffer<float> pitchedAudio;
        {
            pitchedAudio = renderFrom(8.0);
            const auto& audio = pitchedAudio;
            const auto heard = onsets(audio);
            bool same = heard.size() == plainHits.size();
            for (size_t i = 0; same && i < heard.size(); ++i)
                same = std::abs(heard[i] - plainHits[i]) < 0.003;
            if (!same)
            {
                std::fprintf(stderr, "Pitch-enveloped hits at");
                for (const auto at : heard)
                    std::fprintf(stderr, " %.4f", at);
                std::fprintf(stderr, " s from the clip's start\n");
            }
            require(same, "A pitch envelope moves no hit");
            if (stepNear(audio, 0.0) > 0.05 || stepNear(audio, 2.0) > 0.05)
                std::fprintf(stderr, "Pitch-enveloped clip steps %.3f at its start and %.3f at its end\n",
                             stepNear(audio, 0.0), stepNear(audio, 2.0));
            require(stepNear(audio, 0.0) < 0.05 && stepNear(audio, 2.0) < 0.05,
                    "and the clip begins and ends without a click");
            require(toneOfHitAt(audio, 0.5, 150.0) > toneOfHitAt(audio, 0.5, 300.0) * 4.0,
                    "Before the step the hits are at their own pitch");
            require(toneOfHitAt(audio, 1.0, 300.0) > toneOfHitAt(audio, 1.0, 150.0) * 4.0,
                    "and after it an octave up");
        }
        // The clip's own fades come after the shifter in the engine, and have
        // to fall where they do on a plain clip rather than a delay early.
        require(enveloped.setAudioClipFadeOut(hits, 0.3).wasOk(), "The pitched clip can fade out");
        {
            const auto audio = renderFrom(8.0);
            const auto held = peakOfHitAt(audio, 1.5) / peakOfHitAt(pitchedAudio, 1.5);
            const auto faded = peakOfHitAt(audio, 1.875) / peakOfHitAt(pitchedAudio, 1.875);
            if (held < 0.85f || faded < 0.2f || faded > 0.7f)
                std::fprintf(stderr, "Pitched clip with a fade out: the hit before it at %.3f, the one inside it at %.3f\n",
                             held, faded);
            require(held > 0.85f && faded > 0.2f && faded < 0.7f,
                    "and the fade takes down the last hit and nothing before it");
        }
        require(close(enveloped.audioClipMix(hits).fadeOutSeconds, 0.3)
                    && enveloped.findAudioClip(hits)->getFadeOut().inSeconds() == 0.0,
                "Its fade is Rhino's to play while the clip has a pitch envelope, and reads as it was set");
        require(enveloped.setAudioClipFadeOut(hits, 0.0).wasOk(), "The fade can go again");

        // It is saved with the clip and comes back playing.
        {
            const auto document = envelopeFolder.getChildFile("Envelopes.rhinoedit");
            const auto xml = enveloped.projectSnapshot(document).createXml();
            require(xml != nullptr && xml->writeTo(document), "The enveloped document is written");
            const auto read = juce::parseXML(document);
            require(read != nullptr && enveloped.restoreProject(juce::ValueTree::fromXml(*read), document).wasOk(),
                    "and reopened");
        }
        hits = clipStartingAt(8.0);
        require(enveloped.clipEnvelope(hits, Session::ClipEnvelope::pitch).size() == 4 && plugins(hits) == 2,
                "The reopened clip keeps its pitch envelope and its shifter");
        {
            const auto audio = renderFrom(8.0);
            require(toneOfHitAt(audio, 1.0, 300.0) > toneOfHitAt(audio, 1.0, 150.0) * 4.0,
                    "and plays it again");
        }
        requireHalvedAt(2.5, 0.5, "The merged clip's gain envelope comes back too");

        require(enveloped.setAudioClipFadeIn(hits, 0.25).wasOk(), "The pitched clip can fade in");
        require(enveloped.clearClipEnvelope(hits, Session::ClipEnvelope::pitch).wasOk(), "A pitch envelope can be cleared");
        require(plugins(hits) == 0, "and its shifter goes with it");
        require(close(enveloped.audioClipMix(hits).fadeInSeconds, 0.25)
                    && close(enveloped.findAudioClip(hits)->getFadeIn().inSeconds(), 0.25),
                "and the engine plays its fade again");

        // The editor: an envelope chosen, drawn by a press, carried by travel,
        // finer with Shift, taken away by a double-click. On the heap, as
        // every scenario's panel is (tests/README.md).
        {
            auto owned = std::make_unique<AudioClipPanel>(enveloped);
            auto& panel = *owned;
            panel.setClip(hits);
            panel.setSize(880, 220);
            panel.resized();
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
            const auto panelEvent = [&panel](juce::Point<float> down, juce::Point<float> at, bool dragged,
                                             juce::ModifierKeys mods, int clicks = 1)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &panel, &panel, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        clicks, dragged);
            };
            const auto gainPoints = [&] { return enveloped.clipEnvelope(hits, Session::ClipEnvelope::gain); };
            require(!panel.envelopeShown() && panel.envelopeButton.getButtonText() == "Envelope: None",
                    "The editor shows no envelope until one is chosen");
            require(panel.getLocalBounds().contains(panel.envelopeButton.getBounds())
                        && !panel.envelopeButton.getBounds().intersects(panel.subtitle.getBounds())
                        && !panel.envelopeButton.getBounds().intersects(panel.title.getBounds()),
                    "and its chooser sits in the header between the clip's name and its file's");
            panel.chooseEnvelopeMenuItem(1);
            require(panel.envelopeShown() && panel.envelopeButton.getButtonText() == "Envelope: Gain",
                    "Choosing Gain draws its envelope over the waveform");
            const auto area = panel.envelopeArea();
            require(!area.isEmpty() && panel.waveArea.toFloat().contains(area), "inside the waveform's box");

            // A press halfway along at half height puts a node there.
            const juce::Point<float> press {panel.xForContentTime(1.0), area.getCentreY()};
            panel.mouseDown(panelEvent(press, press, false, left));
            panel.mouseUp(panelEvent(press, press, false, left));
            require(gainPoints().size() == 1 && std::abs(gainPoints()[0].timeSeconds - 1.0) < 0.01
                        && std::abs(gainPoints()[0].value - 0.5f) < 0.02f,
                    "A press puts a gain node where it lands");
            require(panel.names[0].getText().contains("\xe2\x80\xa2"), "and the Gain knob says it has an envelope");
            const auto undoSteps = enveloped.edit->getUndoManager().getUndoDescriptions().size();

            // A press on the node that lets go changes nothing and records
            // nothing.
            const juce::Point<float> node {panel.xForContentTime(1.0), panel.yForEnvelopeValue(gainPoints()[0].value)};
            require(panel.envelopeNodeAt(node) == 0, "The node is found where it is drawn");
            panel.mouseDown(panelEvent(node, node, false, left));
            panel.mouseUp(panelEvent(node, node, false, left));
            require(enveloped.edit->getUndoManager().getUndoDescriptions().size() == undoSteps,
                    "A click on a node that does not move it is no edit");

            // Fifty pixels up is a quarter of the range at 200 px of travel.
            const auto before = gainPoints()[0].value;
            panel.mouseDown(panelEvent(node, node, false, left));
            panel.mouseDrag(panelEvent(node, node.translated(0.0f, -50.0f), true, left));
            require(panel.draggingNode == 0, "A drag carries the node it took");
            panel.mouseUp(panelEvent(node, node.translated(0.0f, -50.0f), true, left));
            require(std::abs(gainPoints()[0].value - (before + 0.25f)) < 0.005f,
                    "and moves its level by how far the pointer travelled");
            require(enveloped.edit->getUndoManager().getUndoDescriptions().size() == undoSteps + 1,
                    "in one undo step");
            const auto raised = gainPoints()[0].value;
            const juce::Point<float> again {panel.xForContentTime(gainPoints()[0].timeSeconds), panel.yForEnvelopeValue(raised)};
            panel.mouseDown(panelEvent(again, again, false, shift));
            panel.mouseDrag(panelEvent(again, again.translated(0.0f, 40.0f), true, shift));
            panel.mouseUp(panelEvent(again, again.translated(0.0f, 40.0f), true, shift));
            require(std::abs(gainPoints()[0].value - (raised - 0.02f)) < 0.003f, "Shift moves it a tenth as far");

            // A double-click takes it away.
            const juce::Point<float> last {panel.xForContentTime(gainPoints()[0].timeSeconds),
                                           panel.yForEnvelopeValue(gainPoints()[0].value)};
            panel.mouseDoubleClick(panelEvent(last, last, false, left, 2));
            require(gainPoints().empty() && !panel.names[0].getText().contains("\xe2\x80\xa2"),
                    "A double-click on a node takes it away, and the knob's mark with it");

            // Pitch lands on a whole semitone unless Shift is held.
            panel.chooseEnvelopeMenuItem(3);
            const juce::Point<float> pitched {panel.xForContentTime(0.5), area.getY() + area.getHeight() * 0.3f};
            panel.mouseDown(panelEvent(pitched, pitched, false, left));
            panel.mouseUp(panelEvent(pitched, pitched, false, left));
            const auto pitch = enveloped.clipEnvelope(hits, Session::ClipEnvelope::pitch);
            require(pitch.size() == 1 && pitch[0].value == std::round(pitch[0].value) && pitch[0].value > 0.0f,
                    "A pitch node lands on a whole semitone");
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_CLIP_ENVELOPE_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                // The runners install no look of their own; the app's is set
                // for the picture so it shows what the app draws.
                Theme look;
                panel.setLookAndFeel(&look);
                const auto picture = panel.createComponentSnapshot(panel.getLocalBounds());
                panel.setLookAndFeel(nullptr);
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picture, *stream);
            }
            panel.chooseEnvelopeMenuItem(13);
            require(enveloped.clipEnvelope(hits, Session::ClipEnvelope::pitch).empty() && plugins(hits) == 0,
                    "The menu's Clear takes the pitch envelope and its devices off the clip");
            panel.chooseEnvelopeMenuItem(4);
            require(!panel.envelopeShown(), "and None leaves the waveform to the clip");
        }
    }
    envelopeFolder.deleteRecursively();
}
