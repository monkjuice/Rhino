        // Return tracks: a new document has two at the foot of the stack, A
        // Delay and B Reverb, every other track sends nothing to either until
        // its knob is turned up, and a send is heard through the return's
        // devices, wholly wet. A document of its own, so nothing here moves
        // the tracks the scenarios after it share.
        scenario("return tracks: a new document has a delay and a reverb, and nothing is sent to them");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto trackAt = [&song](int track) -> te::AudioTrack& { return *te::getAudioTracks(*song.edit)[track]; };
            const auto sendsOn = [&trackAt](int track)
            {
                int count = 0;
                for (auto* plugin : trackAt(track).pluginList)
                    if (dynamic_cast<te::AuxSendPlugin*>(plugin) != nullptr)
                        ++count;
                return count;
            };
            const auto returns = song.returnTracks();
            require(returns.size() == 2 && song.firstReturnTrack() == 4 && song.trackCount() == 6,
                    "Two returns under the four starter tracks");
            const auto delayReturn = returns[0].track, reverbReturn = returns[1].track;
            require(returns[0].letter == "A" && returns[0].name == "Delay" && returns[1].letter == "B"
                        && returns[1].name == "Reverb" && song.isReturnTrack(delayReturn) && !song.isReturnTrack(3),
                    "A Delay and B Reverb, in that order");
            require(!returns[0].colour.isTransparent() && returns[0].colour != returns[1].colour,
                    "each coloured as any new track is");

            // Each return's chain is its way in, hidden, then its one device,
            // set wholly wet: what comes back is added to the dry track.
            const auto delaySlots = song.deviceSlots(delayReturn), reverbSlots = song.deviceSlots(reverbReturn);
            require(delaySlots.size() == 1 && delaySlots[0].type == te::DelayPlugin::xmlTypeName
                        && reverbSlots.size() == 1 && reverbSlots[0].type == te::ReverbPlugin::xmlTypeName,
                    "The delay and the reverb are the only devices the returns show");
            auto* delay = dynamic_cast<te::DelayPlugin*>(song.devicePlugin(delayReturn, delaySlots[0].pluginIndex));
            auto* reverb = dynamic_cast<te::ReverbPlugin*>(song.devicePlugin(reverbReturn, reverbSlots[0].pluginIndex));
            require(delay != nullptr && close(delay->mixProportion->getCurrentValue(), 1.0)
                        && reverb != nullptr && close(reverb->dryParam->getCurrentValue(), 0.0),
                    "both wholly wet");
            require(dynamic_cast<te::AuxReturnPlugin*>(song.devicePlugin(delayReturn, delaySlots[0].pluginIndex - 1)) != nullptr,
                    "behind the return's way in, which heads the chain after the DJ input");
            require(song.trackRecordInput(delayReturn) == Session::RecordInput::none
                        && trackAt(delayReturn).isSoloIsolate(false),
                    "A return records nothing, and plays on under another track's solo");

            // Every other track sends to both, and sends nothing.
            for (int track = 0; track < song.firstReturnTrack(); ++track)
                require(sendsOn(track) == 2 && song.hasSend(track, delayReturn) && song.hasSend(track, reverbReturn)
                            && song.trackSendDb(track, delayReturn) <= Session::minimumSendDb
                            && song.trackSendDb(track, reverbReturn) <= Session::minimumSendDb,
                        "Every track has a send to each return, at nothing");
            require(sendsOn(delayReturn) == 0 && !song.hasSend(delayReturn, reverbReturn)
                        && song.setTrackSendDb(delayReturn, reverbReturn, -6.0f).failed(),
                    "and a return sends to no other");
            require(song.deviceSlots(0).empty(), "The sends are the track's own, not devices on its chain");

            // A send runs from nothing to the track's own level, and no further.
            require(song.setTrackSendDb(1, delayReturn, 6.0f).wasOk()
                        && close(song.trackSendDb(1, delayReturn), Session::maximumSendDb),
                    "A send stops at the track's own level, 0 dB");
            song.beginTrackSendGesture(1, delayReturn);
            require(song.setTrackSendDb(1, delayReturn, -12.0f).wasOk(), "A send can be turned down");
            song.endTrackSendGesture(1, delayReturn);
            require(std::abs(song.trackSendDb(1, delayReturn) + 12.0f) < 0.05f, "and reads back what it was given");
            require(song.setTrackSendDb(1, delayReturn, Session::minimumSendDb).wasOk()
                        && song.trackSendDb(1, delayReturn) <= Session::minimumSendDb,
                    "and nothing is a send's floor");

            // The knob's travel: the engine's fader law below unity.
            require(close(SendKnob::positionForDb(0.0), 1.0) && close(SendKnob::positionForDb(Session::minimumSendDb), 0.0)
                        && std::abs(SendKnob::dbForPosition(0.5) + 13.863) < 0.01
                        && close(SendKnob::dbForPosition(SendKnob::positionForDb(-24.0)), -24.0)
                        && SendKnob::dbForPosition(0.0) <= Session::minimumSendDb,
                    "Half a turn of a send knob is about -14 dB, and its bottom is nothing");
            require(SendKnob::describe(Session::minimumSendDb) == "-inf dB" && SendKnob::describe(-12.0) == "-12.0 dB",
                    "and it reads in decibels, nothing as -inf");
            song.releaseAudioDevice();
        }

        scenario("return tracks: what a return refuses");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto delayReturn = song.firstReturnTrack();
            juce::TemporaryFile kick(".wav");
            {
                std::unique_ptr<juce::OutputStream> stream = kick.getFile().createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
                require(writer != nullptr, "A sample to refuse");
                juce::AudioBuffer<float> audio(2, 4800);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    for (int channel = 0; channel < 2; ++channel)
                        audio.setSample(channel, i, 0.5f * static_cast<float>(std::sin(i * 0.2)));
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the sample");
            }
            require(song.addInstrument(Session::Instrument::FourOsc, delayReturn).failed()
                        && song.addDevice("Drums", delayReturn).failed()
                        && song.addMidiEffect(Session::MidiEffect::RhinoArp, delayReturn).failed(),
                    "A return takes no instrument and no MIDI effect");
            require(song.addDrumSound(kick.getFile(), delayReturn).failed(), "and no drum sound");
            require(song.createClip(delayReturn, 0.0).failed()
                        && song.importAudioAt(kick.getFile(), delayReturn, 0.0).failed()
                        && song.insertPatternPreset(Session::PatternPreset::AcidSteps, delayReturn, 0.0).failed()
                        && song.insertAudioFileInSlot(kick.getFile(), delayReturn, 0).failed(),
                    "and no clip, no sample and no pattern, on its lane or in a cell");
            require(song.addDevice("Compressor", delayReturn).wasOk(), "but takes audio effects");
            const auto slots = song.deviceSlots(delayReturn);
            require(slots.size() == 2 && slots[0].type == te::DelayPlugin::xmlTypeName
                        && slots[1].type == te::CompressorPlugin::xmlTypeName,
                    "after the devices already there, so the sends reach every one of them");
            require(song.groupTracks({0, delayReturn}).failed() && song.trackGroups().empty(),
                    "A return cannot be grouped");
            // The bus goes in at the top, so the returns are found again.
            require(song.groupTracks({0, 1}).wasOk()
                        && song.addTrackToGroup(song.firstReturnTrack(), song.trackGroupBusId(0)).failed(),
                    "nor added to a group");
            require(song.copyTracks({song.firstReturnTrack()}).isEmpty() && song.duplicateTracks({song.firstReturnTrack()}).failed(),
                    "A return is not copied or duplicated");
            song.releaseAudioDevice();
        }

        scenario("return tracks: the returns keep the foot of the stack");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto idOf = [&song](int track) { return te::getAudioTracks(*song.edit)[track]->itemID; };
            const auto delayId = idOf(4), reverbId = idOf(5);
            int added = -1;
            require(song.addTrack(Session::TrackType::midi, -1, &added).wasOk() && added == 4
                        && idOf(5) == delayId && idOf(6) == reverbId,
                    "A track added at the foot lands above the returns");
            require(song.hasSend(added, 5) && song.hasSend(added, 6) && song.trackSendDb(added, 5) <= Session::minimumSendDb
                        && song.trackSendDb(added, 6) <= Session::minimumSendDb,
                    "and sends nothing to either, as every new track does");
            require(song.addTrack(Session::TrackType::audio, 5, &added).wasOk() && added == 5 && idOf(6) == delayId,
                    "Under a return is the foot of the stack too");
            require(song.moveTrack(0, song.trackCount() - 1).wasOk() && song.firstReturnTrack() == 6
                        && idOf(6) == delayId && idOf(7) == reverbId,
                    "A track carried down onto the returns stops above them");
            require(song.moveTrack(7, 0).wasOk() && idOf(6) == reverbId && idOf(7) == delayId
                        && song.returnTracks()[0].name == "Reverb" && song.returnTracks()[0].letter == "A",
                    "and a return moves only among the returns, which are lettered by where they stand");
            song.undo();
            require(idOf(6) == delayId && song.returnTracks()[0].name == "Delay", "Undo puts the returns back");

            // A clip carried, pasted or dropped onto a return lands on a lane
            // made for it above the returns.
            auto midiLane = 0;
            while (song.trackType(midiLane) != Session::TrackType::midi)
                ++midiLane;
            te::EditItemID carried;
            require(song.createClip(midiLane, 0.0, &carried).wasOk(), "A clip to carry");
            const auto before = song.trackCount();
            require(song.editClip(carried, {0.0, 2.0, 0.0}, ClipGesture::move, 7).wasOk() && song.trackCount() == before + 1
                        && te::getAudioTracks(*song.edit)[6]->findClipForID(carried) != nullptr
                        && song.isReturnTrack(7) && song.isReturnTrack(8),
                    "A clip dragged onto a return gets a lane above the returns");
            song.undo();
            std::vector<te::EditItemID> pasted;
            require(song.pasteClipRegion(song.copyClipRegion(0.0, 2.0, midiLane, midiLane), 4.0, 7, pasted).wasOk()
                        && song.trackCount() == before + 1 && pasted.size() == 1
                        && te::getAudioTracks(*song.edit)[6]->findClipForID(pasted.front()) != nullptr,
                    "and so does a paste onto one");
            song.releaseAudioDevice();
        }

        scenario("return tracks: there may be none, and more can be added");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto sendCount = [&song]
            {
                int count = 0;
                for (auto* track : te::getAudioTracks(*song.edit))
                    for (auto* plugin : track->pluginList)
                        if (dynamic_cast<te::AuxSendPlugin*>(plugin) != nullptr)
                            ++count;
                return count;
            };
            require(sendCount() == 8, "Four tracks, two sends each");
            require(song.setTrackSendDb(0, 5, -9.0f).wasOk(), "A send to the reverb");
            require(song.removeAudioTrack(5).wasOk() && song.returnTracks().size() == 1 && sendCount() == 4,
                    "Deleting a return takes every send to it along");
            song.undo();
            require(song.returnTracks().size() == 2 && sendCount() == 8 && std::abs(song.trackSendDb(0, 5) + 9.0f) < 0.05f,
                    "and undo brings the return and its sends back, at the levels they had");
            require(song.removeTracks({4, 5}).wasOk() && song.returnTracks().empty() && song.trackCount() == 4
                        && sendCount() == 0,
                    "A song can have no returns at all, and then no sends");
            {
                Arrangement cards(song);
                cards.setSize(1200, 900);
                cards.sync();
                for (const auto& knobs : cards.sends)
                    require(knobs.empty(), "and no card shows a send knob");
                require(cards.mute[0]->getX() == Arrangement::cardControlLeft
                            && std::abs(cards.cardControlsRight() - Arrangement::cardControlsWidth) < 0.001f,
                        "nor gives up room for a column of them");
            }
            int added = -1;
            require(song.addReturnTrack(&added).wasOk() && added == 4 && song.isReturnTrack(4)
                        && song.trackName(4) == "Return" && song.returnTracks().front().letter == "A",
                    "A return can be added back, at the foot of the stack");
            require(sendCount() == 4 && song.trackSendDb(2, 4) <= Session::minimumSendDb,
                    "and every track gains a send to it, at nothing");
            require(song.addReturnTrack(&added).wasOk() && added == 5 && song.returnTracks()[1].letter == "B",
                    "and another under it");
            song.undo();
            require(song.returnTracks().size() == 1 && sendCount() == 4, "One undo takes a new return away with its sends");
            for (int more = 1; more < 12; ++more)
                require(song.addReturnTrack().wasOk(), "Returns up to L");
            require(song.addReturnTrack().failed() && song.returnTracks().back().letter == "L",
                    "and no more than twelve, A to L");
            song.releaseAudioDevice();
        }

        scenario("return tracks: returns and sends reopen as they were saved");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto pluginsOn = [&song](int track, const char* type)
            {
                int count = 0;
                for (auto* plugin : te::getAudioTracks(*song.edit)[track]->pluginList)
                    if (plugin != nullptr && plugin->getPluginType() == type)
                        ++count;
                return count;
            };
            require(song.setTrackSendDb(1, 4, -9.0f).wasOk() && song.setTrackSendDb(2, 5, -3.0f).wasOk(),
                    "Two sends turned up");
            const auto mainColour = song.trackColour(song.masterTrackIndex());
            juce::TemporaryFile saved(".rhinoedit");
            require(song.restoreProject(song.projectSnapshot(), saved.getFile()).wasOk(), "The song reopens");
            require(song.trackColour(song.masterTrackIndex()) == mainColour, "with the main row's colour");
            require(song.returnTracks().size() == 2 && song.isReturnTrack(4) && song.trackName(4) == "Delay"
                        && song.trackName(5) == "Reverb" && te::getAudioTracks(*song.edit)[4]->isSoloIsolate(false),
                    "with its returns");
            require(std::abs(song.trackSendDb(1, 4) + 9.0f) < 0.05f && std::abs(song.trackSendDb(2, 5) + 3.0f) < 0.05f
                        && song.trackSendDb(0, 4) <= Session::minimumSendDb,
                    "and every send at the level it was saved at");
            for (int track = 0; track < song.trackCount(); ++track)
                require(pluginsOn(track, te::AuxSendPlugin::xmlTypeName) == (song.isReturnTrack(track) ? 0 : 2)
                            && pluginsOn(track, te::AuxReturnPlugin::xmlTypeName) == (song.isReturnTrack(track) ? 1 : 0),
                        "and nothing doubled by the reopen");
            song.releaseAudioDevice();
        }

        // What a send is heard as, measured in rendered audio rather than read
        // off the plugins: a short burst on an audio track, rendered with its
        // send to the delay at nothing and then at unity. The difference is
        // what the return added - silence until the delay's time has passed,
        // which is what wholly wet means, then the echo.
        scenario("return tracks: a send is heard through its return");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto delayReturn = song.firstReturnTrack();
            juce::TemporaryFile burst(".wav");
            {
                std::unique_ptr<juce::OutputStream> stream = burst.getFile().createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
                require(writer != nullptr, "A burst to send");
                juce::AudioBuffer<float> audio(2, 48000);
                audio.clear();
                for (int i = 0; i < 1920; ++i)
                    for (int channel = 0; channel < 2; ++channel)
                        audio.setSample(channel, i, 0.5f * static_cast<float>(std::sin(i * 2.0 * juce::MathConstants<double>::pi
                                                                                      * 1000.0 / 48000.0)));
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the burst");
            }
            constexpr int audioLane = 2;
            require(song.importAudioAt(burst.getFile(), audioLane, 0.0).wasOk(), "The burst on the starter audio track");
            const auto render = [&song, &wav, &require]
            {
                juce::TemporaryFile rendered(".wav");
                te::Renderer::Parameters parameters(*song.edit);
                parameters.destFile = rendered.getFile();
                parameters.audioFormat = &wav;
                parameters.sampleRateForAudio = 48000;
                parameters.bitDepth = 24;
                parameters.time = {{}, tracktion::core::TimePosition::fromSeconds(1.0)};
                {
                    te::Renderer::RenderTask task("Return test", parameters, nullptr, nullptr);
                    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                    while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                        require(juce::Time::getMillisecondCounterHiRes() < deadline, "Return render timeout");
                    require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
                }
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
                require(reader != nullptr, "Read the return render");
                juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
                require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read the return render's samples");
                return output;
            };
            // Each window's level, in seconds from the top of the render.
            const auto level = [](const juce::AudioBuffer<float>& audio, double from, double to)
            {
                const auto first = juce::jlimit(0, audio.getNumSamples(), static_cast<int>(from * 48000.0));
                const auto last = juce::jlimit(first, audio.getNumSamples(), static_cast<int>(to * 48000.0));
                return last > first ? audio.getRMSLevel(0, first, last - first) : 0.0f;
            };
            const auto difference = [](juce::AudioBuffer<float> wet, const juce::AudioBuffer<float>& dry)
            {
                const auto samples = std::min(wet.getNumSamples(), dry.getNumSamples());
                for (int channel = 0; channel < 2; ++channel)
                    wet.addFrom(channel, 0, dry, channel, 0, samples, -1.0f);
                return wet;
            };
            auto* delay = dynamic_cast<te::DelayPlugin*>(song.devicePlugin(delayReturn, song.deviceSlots(delayReturn)[0].pluginIndex));
            require(delay != nullptr, "The delay on A");
            const auto echo = delay->lengthMs.get() / 1000.0;
            require(echo > 0.1 && echo < 0.9, "whose echo lands inside the render");

            const auto dry = render();
            require(level(dry, 0.0, 0.04) > 0.1f && level(dry, 0.05, 1.0) < 1.0e-4f,
                    "Sending nothing, the song is the burst and nothing after it");
            require(song.setTrackSendDb(audioLane, delayReturn, 0.0f).wasOk(), "The track's send to the delay turned up");
            const auto sent = render();
            const auto added = difference(sent, dry);
            require(level(added, 0.0, echo - 0.01) < 1.0e-4f,
                    "What the return adds is silent until the delay's time: none of the dry sound comes back");
            require(level(added, echo, echo + 0.04) > 0.05f, "and then the echo");

            song.setTrackMuted(audioLane, true);
            require(level(render(), 0.0, 1.0) < 1.0e-5f, "A muted track sends nothing either");
            song.setTrackMuted(audioLane, false);

            song.setTrackSoloed(audioLane, true);
            const auto soloed = render();
            require(level(soloed, 0.0, 0.04) > 0.1f && level(soloed, echo, echo + 0.04) > 0.05f,
                    "A soloed track is heard with its echo: the returns play on under a solo");
            song.setTrackSoloed(audioLane, false);
            song.setTrackSoloed(0, true);
            require(level(render(), 0.0, 1.0) < 1.0e-5f, "and a track left out by a solo sends nothing");
            song.setTrackSoloed(0, false);

            song.setTrackSoloed(delayReturn, true);
            require(!te::getAudioTracks(*song.edit)[delayReturn + 1]->isSoloIsolate(false),
                    "Soloing a return lets the other returns drop out");
            const auto returnAlone = render();
            require(level(returnAlone, 0.0, echo - 0.01) < 1.0e-4f && level(returnAlone, echo, echo + 0.04) > 0.05f,
                    "and is heard alone, fed by the tracks that send to it");
            song.setTrackSoloed(delayReturn, false);
            require(te::getAudioTracks(*song.edit)[delayReturn + 1]->isSoloIsolate(false),
                    "and unsoloing it isolates them again");
            song.releaseAudioDevice();
        }

        scenario("return tracks: the cards");
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            Arrangement cards(song);
            cards.setSize(1200, 900);
            cards.sync();
            cards.resized();
            const auto first = song.firstReturnTrack();
            // A new document's returns open folded to their one line, leaving
            // the room to the tracks, and the arrow before a return's name
            // opens it to a return's own height. Opened, they are what the
            // checks after this were written for.
            for (const auto track : {first, first + 1})
                require(song.isTrackFolded(track)
                            && std::abs(cards.lane(track).getHeight() - Arrangement::minimumLaneHeight) < 0.001f,
                        "A new document's returns open folded, at their one line");
            for (const auto track : {first + 1, first})
            {
                const auto at = cards.disclosureBounds(track).getCentre();
                const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), at,
                                             juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                             &cards, &cards, juce::Time::getCurrentTime(), at,
                                             juce::Time::getCurrentTime(), 1, false);
                cards.mouseDown(click);
                cards.mouseUp(click);
                cards.sync();
                cards.resized();
                require(!song.isTrackFolded(track)
                            && std::abs(cards.lane(track).getHeight() - Arrangement::defaultReturnLaneHeight) < 0.001f,
                        "and its arrow opens a return to a return's own height");
            }
            require(cards.sends.size() == static_cast<size_t>(song.trackCount()), "Every card has its row of sends");
            for (int track = 0; track < song.trackCount(); ++track)
            {
                const auto& knobs = cards.sends[static_cast<size_t>(track)];
                require(knobs.size() == (track < first ? size_t {2} : size_t {0}),
                        "Two knobs on every card but a return's, which sends to no other");
                const auto index = static_cast<size_t>(track);
                for (size_t knob = 0; knob < knobs.size(); ++knob)
                {
                    const auto bounds = knobs[knob]->getBounds();
                    require(knobs[knob]->isVisible() && knobs[knob]->getParentComponent() == cards.mute[index]->getParentComponent()
                                && bounds.getX() > cards.meters[index]->getRight() && bounds.getRight() < cards.mute[index]->getX()
                                && bounds.getRight() < cards.volume[index]->getX()
                                && bounds.getY() == cards.mute[index]->getY() + static_cast<int>(knob) * (SendKnob::size + Arrangement::cardControlGap),
                            "in a column of their own between the meter and the buttons, one under another");
                    require(knobs[knob]->letter() == (knob == 0 ? "A" : "B")
                                && knobs[knob]->getValue() <= Session::minimumSendDb,
                            "lettered for its return, and at nothing");
                }
            }
            require(cards.factsFor(first).returnLetter == "A" && cards.factsFor(first + 1).returnLetter == "B"
                        && cards.factsFor(0).returnLetter.isEmpty(),
                    "A return's card is called by its letter");
            require(cards.mute[static_cast<size_t>(first)]->getX() == cards.mute[0]->getX()
                        && cards.masterVolume.getX() == cards.volume[0]->getX(),
                    "and its controls, and the main row's, line up with the cards that carry knobs");
            // A row too short for a knob leaves it off rather than growing:
            // a taller row, made by hand, brings it back.
            require(song.setTrackLaneHeight(0, Arrangement::minimumLaneHeight).wasOk(), "A track at its one line");
            cards.sync();
            cards.resized();
            require(std::abs(cards.lane(0).getHeight() - Arrangement::minimumLaneHeight) < 0.001f
                        && cards.sends[0][0]->isVisible() && !cards.sends[0][1]->isVisible(),
                    "keeps the knob that fits and leaves the other off, at the height it was given");
            require(song.setTrackLaneHeight(0, 0.0f).wasOk(), "and back to its fitted height");
            cards.sync();
            cards.resized();
            require(cards.sends[0][1]->isVisible(), "which has room for both");
            require(!cards.arm[static_cast<size_t>(first)]->isVisible() && !cards.trackInput[static_cast<size_t>(first)]->isVisible(),
                    "and has no record dot and no input");

            // A turn of a knob is a send, and the knob follows the send.
            auto& knob = *cards.sends[1][0];
            knob.onDragStart();
            knob.setValue(-12.0, juce::sendNotificationSync);
            knob.onDragEnd();
            require(std::abs(song.trackSendDb(1, first) + 12.0f) < 0.05f, "Turning a card's A knob sets its send to A");
            require(song.setTrackSendDb(1, first, Session::minimumSendDb).wasOk(), "The send set from elsewhere");
            cards.sync();
            require(knob.getValue() <= Session::minimumSendDb, "and the knob shows it");

            // The returns are pinned with the main row under the lanes that
            // scroll, the last of them on the main row's top edge.
            const auto last = song.trackCount() - 1;
            require(cards.lane(first).getY() >= cards.returnsTop() - 0.5f
                        && std::abs(cards.lane(last).getBottom() - cards.masterLane().getY()) < 0.5f
                        && cards.lane(first - 1).getY() < cards.returnsTop(),
                    "The returns sit in a block over the main row, under the lanes");
            require(cards.mute[static_cast<size_t>(first)]->getParentComponent() == &cards.returnHeaders
                        && cards.mute[0]->getParentComponent() == &cards.laneHeaders,
                    "and their controls are pinned with them");
            for (int track = 0; track < first; ++track)
                require(song.setTrackLaneHeight(track, 200.0f).wasOk(), "Lanes tall enough to scroll");
            cards.sync();
            cards.resized();
            const auto pinnedAt = cards.lane(first).getY();
            cards.trackScroll = 300.0;
            cards.updateScroll();
            cards.resized();
            require(cards.trackScroll > 0.0 && std::abs(cards.lane(first).getY() - pinnedAt) < 0.001f
                        && cards.trackScrollBar.getBottom() <= static_cast<int>(cards.returnsTop()) + 1,
                    "Scrolling the lanes leaves the returns where they are");
            require(cards.trackAt(cards.lane(first).getCentreY()) == first,
                    "and a pointer over a return finds the return, not a lane scrolled behind it");
            cards.trackScroll = 0.0;
            for (int track = 0; track < first; ++track)
                require(song.setTrackLaneHeight(track, 0.0f).wasOk(), "The lanes back to their fitted height");
            cards.sync();
            cards.resized();
            int added = -1;
            require(song.addReturnTrack(&added).wasOk(), "A third return");
            cards.sync();
            cards.resized();
            require(added == song.trackCount() - 1
                        && std::abs(cards.lane(added).getBottom() - cards.masterLane().getY()) < 0.5f,
                    "lands at the foot of the block, on the main row");
            song.undo();
            cards.sync();
            cards.resized();

            // The block opens with a heavier rule than the rows', and the main
            // row is a card on its own colour.
            const auto picture = cards.createComponentSnapshot(cards.getLocalBounds(), true, 1.0f);
            const auto ruleX = static_cast<int>(Arrangement::headerWidth) + 40;
            const auto ruleTop = static_cast<int>(std::ceil(cards.returnsTop()));
            require(picture.getPixelAt(ruleX, ruleTop + 1) == palette::barGrid
                        && picture.getPixelAt(ruleX, ruleTop + static_cast<int>(Arrangement::pinnedRuleThickness) - 1)
                               == palette::barGrid,
                    "The pinned block opens with a rule thicker than a row's");
            const auto mainColour = song.trackColour(song.masterTrackIndex());
            require(!mainColour.isTransparent(), "The main row has a colour of its own");
            const auto mainSwatch = juce::Point<int>(static_cast<int>(Arrangement::headerWidth) - 3,
                                                     static_cast<int>(cards.masterLane().getBottom()) - 3);
            require(picture.getPixelAt(mainSwatch.x, mainSwatch.y) == mainColour,
                    "and its card wears it");
            const auto chosen = Session::trackColourPalette()[3] != mainColour ? Session::trackColourPalette()[3]
                                                                             : Session::trackColourPalette()[4];
            require(song.setTrackColour(song.masterTrackIndex(), chosen).wasOk()
                        && song.trackColour(song.masterTrackIndex()) == chosen,
                    "The main row's colour can be chosen");
            cards.sync();
            require(cards.createComponentSnapshot(cards.getLocalBounds(), true, 1.0f).getPixelAt(mainSwatch.x, mainSwatch.y)
                        == chosen,
                    "and the card shows the colour chosen");
            song.undo();
            require(song.trackColour(song.masterTrackIndex()) == mainColour, "and undo puts the colour back");
            cards.sync();

            // A return's card is carried among the returns, as a track's is
            // among the lanes.
            {
                const auto press = [&cards](juce::Point<float> down, juce::Point<float> at, bool dragged)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                        &cards, &cards, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
                };
                const juce::Point<float> grab {230.0f, cards.lane(first + 1).getY() + 30.0f};
                const juce::Point<float> onto {230.0f, cards.lane(first).getY() + 10.0f};
                cards.mouseDown(press(grab, grab, false));
                cards.mouseDrag(press(grab, onto, true));
                cards.mouseUp(press(grab, onto, true));
                require(song.trackName(first) == "Reverb" && song.trackName(first + 1) == "Delay"
                            && song.returnTracks().front().letter == "A",
                        "Dragging B's card onto A's row puts the reverb first, lettered A");
                song.undo();
                cards.sync();
                require(song.trackName(first) == "Delay", "and undo puts the delay back on top");
                // Pinned at the bottom, a return's row is sized from its top
                // edge, as the main row is: dragged up, it grows.
                const auto before = cards.lane(first + 1).getHeight();
                const juce::Point<float> edge {100.0f, cards.lane(first + 1).getY()};
                cards.mouseDown(press(edge, edge, false));
                cards.mouseDrag(press(edge, edge.translated(0.0f, -20.0f), true));
                cards.mouseUp(press(edge, edge.translated(0.0f, -20.0f), true));
                cards.sync();
                cards.resized();
                require(std::abs(cards.lane(first + 1).getHeight() - (before + 20.0f)) < 0.5f
                            && std::abs(cards.lane(last).getBottom() - cards.masterLane().getY()) < 0.5f,
                        "A return's row grows when its top edge is dragged up, still sitting on the main row");
                require(song.setTrackLaneHeight(first + 1, 0.0f).wasOk(), "and goes back to its own height");
                cards.sync();
                cards.resized();
            }

            // To look at: the cards with their sends, written only when asked.
            if (const auto path = juce::SystemStats::getEnvironmentVariable("RHINO_RETURN_TRACKS_SNAPSHOT", {});
                path.isNotEmpty())
            {
                require(song.setTrackSendDb(0, first, -6.0f).wasOk() && song.setTrackSendDb(0, first + 1, -20.0f).wasOk()
                            && song.setTrackSendDb(2, first + 1, 0.0f).wasOk(),
                        "Some sends turned up for the picture");
                cards.setLookAndFeel(&theme);
                cards.sync();
                const juce::File file(path);
                file.deleteFile();
                if (auto stream = file.createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(
                        cards.createComponentSnapshot({0, 0, 420, cards.getHeight()}, true, 2.0f), *stream);
                cards.setLookAndFeel(nullptr);
            }
            song.releaseAudioDevice();
        }
