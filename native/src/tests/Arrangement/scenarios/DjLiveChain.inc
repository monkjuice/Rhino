        // A track's deck heard through the track's own chain, live: a cell's
        // notes play the track's instrument and come back into the deck's
        // channel, a knob on any of the track's devices is heard at once and
        // never silences the deck, and the arrangement's clips are never a
        // deck's. Each in a document of its own.
        //
        // Most of these play the track's chain themselves, every device of it
        // in order over one block and one MIDI array, as the song's graph does
        // with the transport stopped: the real instrument, Drum Rack and Rhino
        // Arp playing the deck's notes, measured level by level. One drives the
        // engine's real graph through its hosted audio device instead, for
        // what only the graph decides - that it runs a stopped track's chain,
        // and what the song's mute and solo do (see
        // wiki/pages/hosted-device-silent-until-dispatched.md). The synth is
        // Rhino FM on its Drawbar Organ preset, which holds its level while a
        // note is held and lets go in 60 ms, so levels can be compared.
        {
            constexpr double liveRate = 48000.0;
            constexpr int liveBlock = 512;
            const auto organ = ContentLibrary::file("Presets/RhinoFM/Drawbar Organ.rnd");
            const auto rmsOf = [](const std::vector<float>& samples, size_t from = 0)
            {
                double sum = 0.0;
                for (size_t i = from; i < samples.size(); ++i) sum += static_cast<double>(samples[i]) * samples[i];
                const auto count = samples.size() > from ? samples.size() - from : 1;
                return std::sqrt(sum / static_cast<double>(count));
            };
            const auto trackOf = [](Session& song, int track) -> te::AudioTrack&
            {
                return *te::getAudioTracks(*song.edit)[track];
            };
            // Prepares every device of a track's chain, as the graph does when
            // it is built.
            const auto prepareChain = [](te::AudioTrack& track)
            {
                for (auto* plugin : track.pluginList)
                    if (plugin != nullptr)
                        plugin->initialise({{}, liveRate, liveBlock});
            };
            // One block of the booth and of the track's chain after it: the
            // booth puts the deck's notes in and takes out what the chain gave
            // back last block. Returns the booth's left channel, and in
            // `reachedSong` the chain's own output, which the song would hear.
            struct ChainRun
            {
                std::vector<float> booth;
                std::vector<float> reachedSong;
            };
            te::MidiMessageArray chainMidi;
            chainMidi.reserve(1024);
            const auto run = [&chainMidi](Session& song, te::AudioTrack& track, int blocks, bool songPlaying = false,
                                          bool rendering = false)
            {
                ChainRun out;
                std::vector<float> left(liveBlock), right(liveBlock);
                float* outputs[2] {left.data(), right.data()};
                juce::AudioBuffer<float> buffer(2, liveBlock);
                for (int b = 0; b < blocks; ++b)
                {
                    song.djProcessOffline(nullptr, 0, outputs, 2, liveBlock);
                    out.booth.insert(out.booth.end(), left.begin(), left.end());
                    buffer.clear();
                    chainMidi.clear();
                    for (auto* plugin : track.pluginList)
                        if (plugin != nullptr && plugin->isEnabled())
                            plugin->applyToBuffer(te::PluginRenderContext(&buffer, 0, liveBlock, &chainMidi, 0.0, {},
                                                                          songPlaying, false, rendering, false));
                    out.reachedSong.insert(out.reachedSong.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + liveBlock);
                }
                return out;
            };
            // The last `blocks` of a run, as samples from.
            const auto lastBlocks = [](const std::vector<float>& samples, int blocks)
            {
                return samples.size() > static_cast<size_t>(blocks * liveBlock) ? samples.size() - static_cast<size_t>(blocks * liveBlock) : 0;
            };
            // The hidden ends' places in a track's chain.
            const auto placeOf = [](te::AudioTrack& track, const char* type)
            {
                for (int i = 0; i < track.pluginList.size(); ++i)
                    if (track.pluginList[i] != nullptr && track.pluginList[i]->getPluginType() == type)
                        return i;
                return -1;
            };
            const auto countOf = [](te::AudioTrack& track, const char* type)
            {
                int count = 0;
                for (auto* plugin : track.pluginList)
                    if (plugin != nullptr && plugin->getPluginType() == type)
                        ++count;
                return count;
            };
            const auto parameterNamed = [](Session& song, int track, int slot, const juce::String& name)
            {
                const auto parameters = song.deviceParameters(track, slot);
                for (int i = 0; i < static_cast<int>(parameters.size()); ++i)
                    if (parameters[static_cast<size_t>(i)].name == name)
                        return i;
                return -1;
            };
            const auto slotOfKind = [](Session& song, int track, DeviceKind kind)
            {
                for (const auto& slot : song.deviceSlots(track))
                    if (slot.kind == kind)
                        return slot.pluginIndex;
                return -1;
            };
            // A MIDI lane of a document, the first of the starter stack's.
            const auto midiLane = [](Session& song)
            {
                for (int t = 0; t < song.trackCount(); ++t)
                    if (!song.isGroupBusTrack(t) && song.trackType(t) == Session::TrackType::midi)
                        return t;
                return -1;
            };
            // A cell of chords struck on every beat of its bar, open in the
            // note editor.
            const auto chordCell = [](Session& song, int lane, std::initializer_list<int> pitches)
            {
                if (song.createSlotClip(lane, 0).failed() || song.selectPatternClip(song.slotClip(lane, 0).clipID).failed())
                    return false;
                for (int beat = 0; beat < 4; ++beat)
                    for (const auto pitch : pitches)
                        if (song.addNote(beat * 4.0, pitch, 4.0).failed())
                            return false;
                return true;
            };

            scenario("dj live: every track carries the deck's way in and out, and a deck plays no arrangement clip");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                song.applyPatternPreset(Session::PatternPreset::WarmPulse);
                const auto lane = te::getAudioTracks(*song.edit).indexOf(dynamic_cast<te::AudioTrack*>(song.pattern().getClipTrack()));
                require(lane >= 0, "A pattern on the arrangement's timeline");
                for (auto* track : te::getAudioTracks(*song.edit))
                {
                    require(countOf(*track, DjTrackInjectorDevice::xmlTypeName) == 1
                                && countOf(*track, DjTrackTapDevice::xmlTypeName) == 1,
                            "Every track carries one hidden way in and one way out");
                    require(placeOf(*track, DjTrackInjectorDevice::xmlTypeName) == 0,
                            "the way in at the very head of the chain");
                    const auto tap = placeOf(*track, DjTrackTapDevice::xmlTypeName);
                    const auto volume = track->getVolumePlugin() != nullptr ? track->pluginList.indexOf(track->getVolumePlugin()) : -1;
                    require(tap > 0 && (volume < 0 || tap < volume), "and the way out ahead of the song's mixer");
                }
                for (const auto& slot : song.deviceSlots(lane))
                    require(slot.type != DjTrackInjectorDevice::xmlTypeName && slot.type != DjTrackTapDevice::xmlTypeName,
                            "Neither is a device the rack shows");
                const auto instrument = slotOfKind(song, lane, DeviceKind::Instrument);
                require(instrument > 0, "The instrument stands behind the way in");
                require(song.addTrack(Session::TrackType::midi).wasOk(), "A track added later");
                auto& added = trackOf(song, song.firstReturnTrack() - 1);
                require(placeOf(added, DjTrackInjectorDevice::xmlTypeName) == 0 && countOf(added, DjTrackTapDevice::xmlTypeName) == 1,
                        "carries them from the start too");

                require(song.addDjDeck().wasOk() && song.addDjDeck().wasOk(), "Two decks");
                require(song.loadDjDeckTrack(0, lane).wasOk(), "The pattern's track on the first");
                const auto info = song.djDeckInfo(0);
                require(info.kind == Session::DjSourceKind::track && info.track == lane && info.slot == -1,
                        "makes the deck the track's, playing no cell yet");
                require(!info.loaded && song.djDeckTrack(0) == nullptr,
                        "and the arrangement's pattern is not on it: a deck plays the DJ view's clips alone");
                require(song.djDeckEditClip(0) == te::EditItemID(), "Edit has no cell to open yet");
                require(song.loadDjDeckTrack(1, lane).failed(), "A track another deck plays is refused");
                require(song.loadDjDeckTrack(0, lane).wasOk(), "while the deck that has it may load it again");
                song.releaseAudioDevice();
            }

            scenario("dj live: a cell plays through the track's instrument and comes back into the deck's channel");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = midiLane(song);
                require(lane >= 0 && song.addDeviceFromPreset(organ, lane).wasOk(), "Rhino FM's organ on a MIDI lane");
                require(chordCell(song, lane, {60, 67}), "A cell of a chord on every beat");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "The deck plays the cell");
                const auto* material = song.djDeckTrack(0);
                require(material != nullptr && material->left.empty() && material->silentFrames > 0,
                        "at once, as notes with no sound of their own: the instrument makes the sound");
                require(material->midi.size() == 16 && material->midi.front().on && material->midi.front().beat == 0.0,
                        "carrying the cell's notes from its top");
                require(std::abs(material->seconds() - 60.0 / song.tempo() * song.beatsPerBar()) < 0.001,
                        "a one-bar cell, one bar long");
                require(song.djDeckEditClip(0) == song.slotClip(lane, 0).clipID, "Edit opens the cell");
                auto& track = trackOf(song, lane);
                prepareChain(track);
                song.djPlay(0);
                const auto sounding = run(song, track, 40);
                const auto heard = rmsOf(sounding.booth, lastBlocks(sounding.booth, 20));
                require(heard > 0.01, ("The instrument is heard through the deck: " + juce::String(heard, 4)).toRawUTF8());
                require(rmsOf(sounding.reachedSong) == 0.0, "and none of it reaches the song's own output");
                require(song.djDeckInfo(0).loaded && !song.djBusy(), "with nothing rendered or read on the way");

                scenario("dj live: a knob on the instrument is heard at once, held or let go");
                const auto fm = slotOfKind(song, lane, DeviceKind::Instrument);
                const auto output = parameterNamed(song, lane, fm, "Output");
                require(output >= 0, "Rhino FM's output level");
                require(song.beginDeviceParameterGesture(lane, fm, output).wasOk(), "taken hold of");
                require(song.setDeviceParameter(lane, fm, output, -18.0f).wasOk(), "and turned down 18 dB");
                const auto held = run(song, track, 30);
                const auto quieter = rmsOf(held.booth, lastBlocks(held.booth, 20));
                require(quieter > 0.001 && quieter < heard * 0.3,
                        ("The deck follows the knob while it is held, never silent: " + juce::String(heard, 4) + " to "
                         + juce::String(quieter, 4)).toRawUTF8());
                require(song.endDeviceParameterGesture(lane, fm, output).wasOk(), "The knob let go");
                const auto after = run(song, track, 30);
                require(std::abs(rmsOf(after.booth, lastBlocks(after.booth, 20)) - quieter) < quieter * 0.25,
                        "and the deck stays where the knob left it, with nothing made again");
                require(!song.djBusy(), "No render, no read, no worker");

                scenario("dj live: the song owns its tracks while it plays and in every render");
                const auto songPlays = run(song, track, 80, true);
                require(rmsOf(songPlays.booth, lastBlocks(songPlays.booth, 10)) < 1.0e-6,
                        "While the song's transport rolls the track is the song's, and the deck hears none of it");
                require(rmsOf(songPlays.reachedSong, lastBlocks(songPlays.reachedSong, 10)) < quieter * 0.05,
                        "and its way in plays the deck's notes into it no more, and lets the ones it played go");
                const auto rendering = run(song, track, 30, false, true);
                require(rmsOf(rendering.booth, lastBlocks(rendering.booth, 10)) < 1.0e-6
                            && rmsOf(rendering.reachedSong, lastBlocks(rendering.reachedSong, 10)) < quieter * 0.05,
                        "and in a render - a merge, an export - neither either");
                const auto back = run(song, track, 70);
                require(rmsOf(back.booth, lastBlocks(back.booth, 10)) > quieter * 0.5,
                        "The song stopped, the deck has the track again from its next chord");

                scenario("dj live: a cell's notes changed under the deck are played after the poll");
                const auto generation = song.djDeckInfo(0).generation;
                require(song.addNote(8.0, 72, 4.0).wasOk(), "A note added to the cell the deck plays");
                song.djPoll();
                song.djProcessOffline(liveBlock);
                const auto* remade = song.djDeckTrack(0);
                require(song.djDeckInfo(0).generation > generation && remade != nullptr && remade->midi.size() == 18,
                        "is on the deck at the next poll, made at once from the clip");
                require(std::any_of(remade->midi.begin(), remade->midi.end(), [](const DjMidiEvent& e) { return e.note == 72 && e.on; }),
                        "with the new note in it");
                require(song.djDeckInfo(0).loaded && song.djDeckState(0).isPlaying(), "and the deck plays on through it, once it has it");

                scenario("dj live: an undo on a deck's track takes the device it should");
                require(song.addDevice("Reverb", lane).wasOk(), "An effect added to the deck's track");
                const auto before = song.deviceSlots(lane).size();
                song.undo();
                const auto slots = song.deviceSlots(lane);
                require(slots.size() + 1 == before
                            && std::none_of(slots.begin(), slots.end(), [](const Session::DeviceSlot& s) { return s.name == "Reverb"; })
                            && slotOfKind(song, lane, DeviceKind::Instrument) >= 0,
                        "Undo takes the effect and leaves the instrument");
                require(placeOf(track, DjTrackInjectorDevice::xmlTypeName) == 0 && countOf(track, DjTrackTapDevice::xmlTypeName) == 1,
                        "and the deck's way in and out where they were");

                scenario("dj live: a deck taken off its track lets the notes it held there go");
                prepareChain(track);
                const auto playing = run(song, track, 30);
                require(rmsOf(playing.booth, lastBlocks(playing.booth, 10)) > 0.001, "The deck plays the track");
                song.ejectDjDeck(0);
                const auto ejected = run(song, track, 30);
                require(rmsOf(ejected.reachedSong, lastBlocks(ejected.reachedSong, 10)) < quieter * 0.05,
                        "Ejected, nothing it started on the track's instrument goes on sounding into the song");
                require(song.loadDjDeckSlot(0, lane, 0).wasOk() && song.djDeckInfo(0).loaded, "and it takes the cell again");

                scenario("dj live: a deleted cell leaves the deck its track's, playing nothing");
                require(song.deleteSlotClip(lane, 0).wasOk(), "The cell's clip deleted under the deck");
                song.djPoll();
                song.djProcessOffline(liveBlock);
                require(song.djDeckInfo(0).slot == -1 && !song.djDeckInfo(0).loaded && song.djDeckInfo(0).track == lane,
                        "The deck is still the track's and plays no cell");
                song.releaseAudioDevice();
            }

            scenario("dj live: the song's mute and solo leave a deck its track");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = midiLane(song);
                require(lane >= 0 && song.addDeviceFromPreset(organ, lane).wasOk() && chordCell(song, lane, {60}),
                        "The organ and a cell on a MIDI lane");
                auto& track = trackOf(song, lane);
                require(song.engine.getEngineBehaviour().shouldProcessMutedTracks(),
                        "A muted track still runs its devices, and only its output to the song is silenced: the deck takes "
                        "its sound from inside the chain");
                song.toggleTrackMute(lane);
                require(song.trackMixer(lane).muted, "The track muted in the song");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "goes on a deck");
                prepareChain(track);
                song.djPlay(0);
                const auto muted = run(song, track, 30);
                require(rmsOf(muted.booth, lastBlocks(muted.booth, 10)) > 0.01, "and the deck hears it all the same");
                song.toggleTrackMute(lane);
                song.releaseAudioDevice();
            }

            scenario("dj live: a Drum Rack's knobs never silence its deck");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = midiLane(song);
                require(lane >= 0 && song.addDrumKit(ContentLibrary::file("Drums/Kits/808 Kit.rdk"), lane).wasOk(),
                        "A Drum Rack with the 808 Kit on a MIDI lane");
                require(song.createSlotClip(lane, 0).wasOk() && song.selectPatternClip(song.slotClip(lane, 0).clipID).wasOk(),
                        "A cell in the editor");
                for (const auto step : {0.0, 4.0, 8.0, 12.0})
                    require(song.addNote(step, song.patternDrumLowestNote(), 1.0).wasOk(), "with the kick on every beat");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "The deck plays the cell");
                auto& track = trackOf(song, lane);
                prepareChain(track);
                song.djPlay(0);
                // A bar at 120 BPM is 2 s, 188 blocks: the four kicks are in it.
                const auto bar = static_cast<int>(std::ceil(60.0 / song.tempo() * song.beatsPerBar() * liveRate / liveBlock));
                const auto open = run(song, track, bar);
                const auto kicks = rmsOf(open.booth);
                require(kicks > 0.005, ("The kicks are heard through the deck: " + juce::String(kicks, 4)).toRawUTF8());
                const auto rack = slotOfKind(song, lane, DeviceKind::Instrument);
                const auto knobs = song.deviceParameters(lane, rack);
                require(rack >= 0 && !knobs.empty(), "The rack's controls");
                // Holding a knob is what silenced the deck until it was let go,
                // whatever the knob was set to: so it is held, and worked, at
                // the value it had, and the deck must sound as it did.
                const auto at = knobs.front().value;
                require(song.beginDeviceParameterGesture(lane, rack, 0).wasOk(), "A knob on the rack taken hold of");
                std::vector<float> heldKnob;
                for (int step = 0; step < 8; ++step)
                {
                    require(song.setDeviceParameter(lane, rack, 0, at).wasOk(), "and worked while the deck plays");
                    const auto part = run(song, track, bar / 8 + 1);
                    heldKnob.insert(heldKnob.end(), part.booth.begin(), part.booth.end());
                }
                const auto whileHeld = rmsOf(heldKnob);
                require(whileHeld > kicks * 0.5 && whileHeld < kicks * 2.0,
                        ("The deck sounds as it did the whole time the knob is held: " + juce::String(kicks, 4) + " and "
                         + juce::String(whileHeld, 4)).toRawUTF8());
                require(song.endDeviceParameterGesture(lane, rack, 0).wasOk() && !song.djBusy(), "and nothing waits on the release");
                song.releaseAudioDevice();
            }

            scenario("dj live: an arpeggiator in front of the instrument plays the deck's notes, its knobs heard at once");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = midiLane(song);
                require(lane >= 0 && song.addDeviceFromPreset(organ, lane).wasOk() && song.addDevice("RhinoArp", lane).wasOk(),
                        "Rhino Arp in front of Rhino FM's organ");
                const auto arp = slotOfKind(song, lane, DeviceKind::MidiEffect);
                require(arp > 0 && arp < slotOfKind(song, lane, DeviceKind::Instrument), "behind the way in and ahead of the synth");
                require(song.createSlotClip(lane, 0).wasOk() && song.selectPatternClip(song.slotClip(lane, 0).clipID).wasOk(),
                        "A cell in the editor");
                for (const auto pitch : {60, 64, 67})
                    require(song.addNote(0.0, pitch, 16.0).wasOk(), "with a chord held a bar long");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "The deck plays the cell");
                auto& track = trackOf(song, lane);
                prepareChain(track);
                song.djPlay(0);
                const auto arpeggiated = rmsOf(run(song, track, 60).booth, 20 * liveBlock);
                require(arpeggiated > 0.005, ("The arpeggiated chord is heard through the deck: " + juce::String(arpeggiated, 4)).toRawUTF8());
                require(song.beginDeviceParameterGesture(lane, arp, 0).wasOk(), "An arp knob taken hold of");
                std::vector<float> turning;
                for (int step = 0; step < 6; ++step)
                {
                    require(song.setDeviceParameter(lane, arp, 0, static_cast<float>(step % 3)).wasOk(), "and turned through its styles");
                    const auto part = run(song, track, 12);
                    turning.insert(turning.end(), part.booth.begin(), part.booth.end());
                }
                require(rmsOf(turning) > arpeggiated * 0.3,
                        ("The deck keeps sounding while the arp's knob turns: " + juce::String(rmsOf(turning), 4)).toRawUTF8());
                require(song.endDeviceParameterGesture(lane, arp, 0).wasOk(), "The knob let go");
                song.releaseAudioDevice();
            }

            scenario("dj live: an audio cell's sound goes through the track's own chain");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                int lane = -1;
                for (int t = 0; t < song.trackCount() && lane < 0; ++t)
                    if (!song.isGroupBusTrack(t) && song.trackType(t) == Session::TrackType::audio)
                        lane = t;
                require(lane >= 0 && song.insertAudioFileInSlot(source.getFile(), lane, 0).wasOk(), "A sample in an audio lane's cell");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "on a deck");
                for (int waited = 0; waited < 400 && song.djBusy(); ++waited)
                {
                    juce::Thread::sleep(25);
                    song.djPoll();
                }
                const auto* material = song.djDeckTrack(0);
                require(song.djDeckInfo(0).loaded && material != nullptr && !material->left.empty(),
                        ("The worker reads the cell's sound: " + song.djDeckInfo(0).error).toRawUTF8());
                require(std::abs(material->seconds() - 60.0 / song.tempo() * song.beatsPerBar()) < 0.01,
                        "padded to a whole bar, so it loops on the grid");
                auto& track = trackOf(song, lane);
                // A gain stage among the track's own devices, which land ahead
                // of the deck's way out, as every device added to a track does.
                require(song.addDevice("Utility", lane).wasOk(), "A gain stage on the audio track");
                UtilityDevice* gainStage = nullptr;
                const auto outputAt = placeOf(track, DjTrackTapDevice::xmlTypeName);
                for (int i = 0; i < outputAt; ++i)
                    if (auto* utility = dynamic_cast<UtilityDevice*>(track.pluginList[i]))
                        gainStage = utility;
                require(gainStage != nullptr, "ahead of the deck's way out");
                prepareChain(track);
                // The same stretch of the sample twice, the gain stage at 0 dB
                // and then turned down 12: the deck follows the track's chain.
                const auto stretch = [&]
                {
                    song.djSeek(0, 0.0);
                    song.djPlay(0);
                    const auto heardRun = run(song, track, 40);
                    song.djPause(0);
                    run(song, track, 4);
                    return rmsOf(heardRun.booth, 15 * liveBlock);
                };
                const auto through = stretch();
                require(through > 0.01, ("The sample is heard through the track and the deck: " + juce::String(through, 4)).toRawUTF8());
                gainStage->gain().setParameter(-12.0f, juce::sendNotificationSync);
                const auto turnedDown = stretch();
                require(std::abs(turnedDown / through - 0.251) < 0.05,
                        ("and a device of the track turned down 12 dB turns the deck down with it: "
                         + juce::String(turnedDown / through, 3)).toRawUTF8());
                // The track's own channel strip, past the way out, is the song's.
                UtilityDevice* strip = nullptr;
                for (int i = outputAt + 1; i < track.pluginList.size(); ++i)
                    if (auto* utility = dynamic_cast<UtilityDevice*>(track.pluginList[i]))
                        strip = utility;
                require(strip != nullptr, "The track's channel strip stands past the deck's way out");
                gainStage->gain().setParameter(0.0f, juce::sendNotificationSync);
                strip->gain().setParameter(-12.0f, juce::sendNotificationSync);
                const auto songTrim = stretch();
                require(std::abs(songTrim / through - 1.0) < 0.05,
                        ("and its trim leaves the deck alone: " + juce::String(songTrim / through, 3)).toRawUTF8());
                song.releaseAudioDevice();
            }

            scenario("dj live: the song's own graph plays a deck's track while the song stands still");
            {
                // Everything above plays a track's chain by hand. This drives
                // the engine's real graph instead, a block at a time through
                // its hosted audio device, with no hardware: the graph builds
                // the track's nodes, runs its plugins with the transport
                // stopped, and mutes what the song mutes. A document of its own,
                // so its engine alone is switched to the hosted device.
                auto document = std::make_unique<Session>();
                auto& song = *document;
                // The engine opened the machine's device as it was made; the
                // hosted device takes its place once that is closed.
                song.releaseAudioDevice();
                auto& hosted = song.engine.getDeviceManager().getHostedAudioDeviceInterface();
                te::HostedAudioDeviceInterface::Parameters parameters;
                parameters.sampleRate = liveRate;
                parameters.blockSize = liveBlock;
                parameters.inputChannels = 0;
                parameters.outputChannels = 2;
                hosted.initialise(parameters);
                hosted.prepareToPlay(liveRate, liveBlock);
                // A device that starts holds the engine's callback suspended
                // until a preparation the engine posts to the message thread;
                // the runner has no message loop, so it is dispatched here.
                song.engine.getDeviceManager().dispatchPendingUpdates();
                const auto lane = midiLane(song);
                int otherLane = -1;
                for (int t = lane + 1; t < song.trackCount() && otherLane < 0; ++t)
                    if (!song.isGroupBusTrack(t) && song.trackType(t) == Session::TrackType::midi)
                        otherLane = t;
                require(lane >= 0 && otherLane >= 0 && song.addDeviceFromPreset(organ, lane).wasOk() && chordCell(song, lane, {60, 67}),
                        "The organ and a cell on a MIDI lane, and a second lane");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "on a deck");
                // In the app binding a deck allocates the song's playback
                // context; the runner leaves that to whoever runs a graph.
                song.edit->getTransport().ensureContextAllocated();
                song.edit->dispatchPendingUpdatesSynchronously();
                song.engine.getDeviceManager().dispatchPendingUpdates();
                require(song.edit->getCurrentPlaybackContext() != nullptr, "The song has a playback context on the hosted device");
                require(!song.edit->getTransport().isPlaying(), "The song's transport stands still");
                song.djPlay(0);
                const auto runGraph = [&](int blocks)
                {
                    ChainRun out;
                    juce::AudioBuffer<float> songOut(2, liveBlock);
                    juce::MidiBuffer midiIn;
                    std::vector<float> left(liveBlock), right(liveBlock);
                    float* outputs[2] {left.data(), right.data()};
                    for (int b = 0; b < blocks; ++b)
                    {
                        songOut.clear();
                        midiIn.clear();
                        hosted.processBlock(songOut, midiIn);
                        out.reachedSong.insert(out.reachedSong.end(), songOut.getReadPointer(0), songOut.getReadPointer(0) + liveBlock);
                        song.djProcessOffline(nullptr, 0, outputs, 2, liveBlock);
                        out.booth.insert(out.booth.end(), left.begin(), left.end());
                    }
                    return out;
                };
                const auto first = runGraph(80);
                const auto onDeck = rmsOf(first.booth, lastBlocks(first.booth, 20));
                require(onDeck > 0.01,
                        ("The song's own graph plays the deck's notes through the track while the song stands still: "
                         + juce::String(onDeck, 4)).toRawUTF8());
                require(rmsOf(first.reachedSong, lastBlocks(first.reachedSong, 20)) < onDeck * 0.01,
                        "and the song's output carries none of it");
                // The song played and stopped again, as the DJ view stops it on
                // the way in: while it rolls the track is the song's, and once it
                // stops the graph goes on running and the deck has it back.
                auto& transport = song.edit->getTransport();
                transport.play(false);
                const auto rolling = runGraph(30);
                require(transport.isPlaying() && rmsOf(rolling.booth, lastBlocks(rolling.booth, 10)) < onDeck * 0.01,
                        "While the song rolls the track is the song's, and the deck hears none of it");
                transport.stop(false, false);
                const auto stopped = runGraph(70);
                require(rmsOf(stopped.booth, lastBlocks(stopped.booth, 10)) > onDeck * 0.5,
                        "and once it stops the deck has the track back from its next chord");
                song.toggleTrackMute(lane);
                const auto muted = runGraph(60);
                require(rmsOf(muted.booth, lastBlocks(muted.booth, 20)) > onDeck * 0.5,
                        "Muted in the song, the track is still the deck's");
                song.toggleTrackMute(lane);
                song.toggleTrackSolo(otherLane);
                const auto soloed = runGraph(60);
                require(rmsOf(soloed.booth, lastBlocks(soloed.booth, 20)) > onDeck * 0.5,
                        "and another track soloed in the song leaves it the deck's too");
                song.toggleTrackSolo(otherLane);
                song.ejectDjDeck(0);
                const auto ejected = runGraph(40);
                require(rmsOf(ejected.reachedSong, lastBlocks(ejected.reachedSong, 10)) < onDeck * 0.01,
                        "Ejected, nothing it started goes on sounding into the song");
                song.releaseAudioDevice();
            }

            scenario("dj live: a reopened document puts the deck back on its track and cell");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = midiLane(song);
                require(lane >= 0 && song.addDeviceFromPreset(organ, lane).wasOk() && chordCell(song, lane, {60}),
                        "The organ and a cell");
                require(song.addDjDeck().wasOk() && song.loadDjDeckSlot(0, lane, 0).wasOk(), "on a deck");
                {
                    const auto saved = song.projectSnapshot();
                    juce::TemporaryFile project(".rhinoedit");
                    require(song.restoreProject(saved, project.getFile()).wasOk(), "Saved and opened again");
                }
                song.djPoll();
                const auto info = song.djDeckInfo(0);
                require(info.kind == Session::DjSourceKind::track && info.track == lane && info.slot == 0 && info.loaded,
                        "The deck comes back on its track, playing its cell");
                auto& track = trackOf(song, lane);
                require(countOf(track, DjTrackInjectorDevice::xmlTypeName) == 1 && countOf(track, DjTrackTapDevice::xmlTypeName) == 1
                            && placeOf(track, DjTrackInjectorDevice::xmlTypeName) == 0,
                        "and its track carries its way in and out once each, no more");
                prepareChain(track);
                song.djPlay(0);
                require(rmsOf(run(song, track, 30).booth, 10 * liveBlock) > 0.005, "and is heard again");
                song.releaseAudioDevice();
            }
        }
