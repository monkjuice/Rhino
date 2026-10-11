        // When a deck bounces again, and what its bounce holds, each in a
        // document of its own. An edit that changes nothing a deck plays
        // starts no bounce, nor does anything while a gesture is held or a
        // bounce landing under one; a note starts exactly one once the
        // document is quiet. A track deck plays its track as it sounds alone,
        // muted in the song or grouped under a bus, and as hot as it is; and
        // a document reopened from its file bounces the audio stored beside
        // it. Renders, so it sits before "recording" and the audio device
        // closing.
        {
            const auto rmsOf = [](const std::vector<float>& samples)
            {
                double sum = 0.0;
                for (const auto s : samples) sum += static_cast<double>(s) * s;
                return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
            };
            const auto peakOf = [](const DjTrack& track)
            {
                float peak = 0.0f;
                for (const auto s : track.left) peak = std::max(peak, std::abs(s));
                for (const auto s : track.right) peak = std::max(peak, std::abs(s));
                return peak;
            };
            // Under the runner a bounce renders inline; the poll installs it.
            const auto waitForDeck = [](Session& booth, int deck)
            {
                for (int waited = 0; waited < 800 && booth.djDeckInfo(deck).loading; ++waited)
                {
                    juce::Thread::sleep(25);
                    booth.djPoll();
                }
                return booth.djDeckInfo(deck).loaded && !booth.djDeckInfo(deck).loading;
            };
            // Past the half second of quiet the poll waits for after an edit.
            const auto quietPasses = [] { juce::Thread::sleep(600); };

            scenario("dj bounces: only what a deck plays bounces it again");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                song.applyPatternPreset(Session::PatternPreset::WarmPulse);
                const auto deckTrack = te::getAudioTracks(*song.edit).indexOf(dynamic_cast<te::AudioTrack*>(song.pattern().getClipTrack()));
                // The starter stack is MIDI, Audio, MIDI, Audio.
                const auto otherTrack = deckTrack == 0 ? 2 : 0;
                require(deckTrack >= 0 && song.trackType(otherTrack) == Session::TrackType::midi, "Two MIDI lanes, one with a pattern");
                require(song.addDevice("Reverb", otherTrack).wasOk() && !song.deviceSlots(otherTrack).empty(),
                        "An effect on the other track");
                const auto reverb = song.deviceSlots(otherTrack).front().pluginIndex;
                const auto knob = song.deviceParameter(otherTrack, reverb, 0);
                require(knob.has_value(), "with a knob");
                require(song.addDjDeck().wasOk() && song.loadDjDeckTrack(0, deckTrack).wasOk(), "The pattern's track on a deck");
                require(waitForDeck(song, 0), ("bounces: " + song.djDeckInfo(0).error).toRawUTF8());
                require(song.djBouncesStarted() == 1, "in one bounce");

                // The engine keeps an edit's renders - a compressed file
                // decoded, a pitched or reversed clip made over - in a folder
                // named for its project id, and building an edit purges the
                // ones its own clips do not use. The runner makes none of
                // those renders, so stand-ins are left where they would be.
                const auto deckRenders = song.djCopyFolder(0);
                const auto songRenders = song.edit->getTempDirectory(true);
                require(deckRenders != juce::File() && deckRenders != songRenders,
                        "The deck's copies keep their renders apart from the song's");
                const auto deckRender = deckRenders.getChildFile("proxy_1b2c3d.wav");
                require(deckRenders.createDirectory().wasOk() && deckRender.replaceWithText("decoded"),
                        "where a render made for one bounce is left");
                const auto deckClip = song.pattern().itemID;
                te::EditItemID otherClip;
                // A clip made opens in the note editor; the deck's goes back there.
                require(song.createClip(otherTrack, 16.0, &otherClip).wasOk() && song.selectPatternClip(deckClip).wasOk(),
                        "A clip on the other track");
                const auto songRender = songRenders.getChildFile("clip_0_" + otherClip.toString() + "_1.wav");
                require(songRender.replaceWithText("stretched"), "with a render of the song's beside it");

                // Nothing a deck plays: the song's view of a track, how it is
                // listened to, the knobs of a track the deck does not play, and
                // the deck's own synth dropped on its track again, which is the
                // same device under the name it gives the track.
                require(song.setTrackName(deckTrack, "Renamed").wasOk(), "A rename");
                require(song.setTrackColour(deckTrack, Session::trackColourPalette()[5]).wasOk(), "a colour");
                require(song.setTrackLaneHeight(deckTrack, 96.0f).wasOk(), "a lane height");
                require(song.cycleClipColour(song.pattern().itemID).wasOk(), "a clip's colour");
                song.toggleTrackMute(deckTrack);
                song.toggleTrackSolo(deckTrack);
                song.toggleTrackSolo(deckTrack);
                song.toggleTrackMute(deckTrack);
                require(song.setDeviceParameter(otherTrack, reverb, 0, (knob->minimum + knob->maximum) * 0.5f).wasOk(),
                        "a knob on another track");
                require(song.addDevice("FourOsc", deckTrack).wasOk() && song.trackName(deckTrack) != "Renamed",
                        "and the deck's synth dropped on it again");
                require(!song.djDeckInfo(0).stale, "leave the deck's bounce current");
                quietPasses();
                song.djPoll();
                require(song.djBouncesStarted() == 1, "and start no bounce");

                scenario("dj bounces: a note bounces the deck once the document is quiet");
                require(song.addNote(6.0, 100, 1.0).wasOk(), "A note drawn on the deck's track");
                require(song.djDeckInfo(0).stale, "leaves the deck behind");
                song.djPoll();
                require(song.djBouncesStarted() == 1, "and nothing bounces until the document has been quiet");
                quietPasses();
                song.djPoll();
                require(song.djBouncesStarted() == 2, "Then one bounce starts");
                for (int i = 0; i < 4; ++i)
                    song.djPoll();
                require(waitForDeck(song, 0) && !song.djDeckInfo(0).stale && song.djBouncesStarted() == 2,
                        "and only one, which lands current");
                require(song.djCopyFolder(0) == deckRenders && deckRender.existsAsFile(),
                        "Its copy kept its renders in the deck's folder, where the last one's were waiting");
                require(songRender.existsAsFile(), "and building it purged none of the song's");

                scenario("dj bounces: nothing bounces while a gesture is held");
                song.beginTrackVolumeGesture(otherTrack);
                require(song.addNote(8.0, 102, 1.0).wasOk(), "A note while a fader is held");
                quietPasses();
                song.djPoll();
                require(song.djBouncesStarted() == 2 && song.djDeckInfo(0).stale, "starts no bounce, and the deck stays behind");
                require(song.beginDeviceParameterGesture(otherTrack, reverb, 0).wasOk(), "A knob on the other track taken hold of");
                song.endTrackVolumeGesture(otherTrack);
                song.djPoll();
                require(song.djBouncesStarted() == 2, "and with the fader let go and the knob still held, none either");
                require(song.endDeviceParameterGesture(otherTrack, reverb, 0).wasOk(), "The knob let go");
                song.djPoll();
                require(song.djBouncesStarted() == 3 && waitForDeck(song, 0) && !song.djDeckInfo(0).stale,
                        "bounces the deck, once");

                scenario("dj bounces: a bounce landing under a gesture starts no other");
                require(song.rebounceDjDeck(0).wasOk() && song.djDeckInfo(0).loading, "A bounce in flight");
                require(song.addNote(12.0, 106, 1.0).wasOk() && song.djDeckInfo(0).stale,
                        "with an edit under it");
                song.beginTrackVolumeGesture(otherTrack);
                song.djPoll();
                require(!song.djDeckInfo(0).loading && song.djDeckInfo(0).stale && song.djBouncesStarted() == 4,
                        "lands while a fader is held without starting the next, and the deck stays behind");
                song.endTrackVolumeGesture(otherTrack);
                quietPasses();
                song.djPoll();
                require(song.djBouncesStarted() == 5 && waitForDeck(song, 0) && !song.djDeckInfo(0).stale,
                        "The poll bounces it once the fader is let go");

                // What a deck is behind on is what a deck running its track's
                // devices live will act on: new devices rebuild what it runs,
                // so only a clip that changes them may say so. A clip
                // duplicated on its own track, or brought from a track running
                // the same instrument, changes none.
                scenario("dj bounces: a clip changes the deck's devices only when it brings some");
                const auto bar = 60.0 / song.tempo() * song.beatsPerBar();
                const auto clipOn = [&song](int track, double startSeconds)
                {
                    te::EditItemID made;
                    return song.createClip(track, startSeconds, &made).wasOk() ? made : te::EditItemID();
                };
                // The clip where it is, on another track.
                const auto onto = [&song](te::EditItemID id, int track)
                {
                    const auto position = song.findClip(id)->getPosition();
                    return Session::ClipMove {id, {position.time.getStart().inSeconds(), position.time.getEnd().inSeconds(),
                                                   position.offset.inSeconds()}, track};
                };
                require(song.djDeckInfo(0).behindOn == Session::ModelChange::none, "A current deck is behind on nothing");
                require(song.duplicateClip(deckClip).wasOk(), "The deck's clip duplicated on its own track");
                require(song.djDeckInfo(0).behindOn == Session::ModelChange::notes,
                        "leaves the deck behind on its notes, and not on its devices");
                // A synth pattern on a lane already running the synth is notes
                // too, in the note editor's clip, on the lane or in a cell; one
                // with a patch of its own sets the synth's voicing as well.
                song.applyPatternPreset(Session::PatternPreset::AcidSteps);
                require(song.insertPatternPreset(Session::PatternPreset::ArpRun, deckTrack, 6.0 * bar).wasOk()
                            && song.insertPatternPresetInSlot(Session::PatternPreset::SirenLead, deckTrack, 0).wasOk()
                            && song.djDeckInfo(0).behindOn == Session::ModelChange::notes,
                        "So do synth patterns loaded in the editor, dropped on the lane and on a cell");
                require(song.insertPatternPresetInSlot(Session::PatternPreset::SubBass, deckTrack, 1).wasOk()
                            && song.djDeckInfo(0).behindOn == (Session::ModelChange::notes | Session::ModelChange::devices),
                        "A pattern with a patch of its own changes the synth's settings too");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0)
                            && song.djDeckInfo(0).behindOn == Session::ModelChange::none,
                        "and a bounce catches the deck up");
                require(song.addDevice("FourOsc", otherTrack).wasOk(), "The other track runs the deck's synth too");
                const auto first = clipOn(otherTrack, 2.0 * bar), second = clipOn(otherTrack, 3.0 * bar);
                require(first.isValid() && second.isValid()
                            && song.moveClips({onto(first, deckTrack), onto(second, deckTrack)}).wasOk(),
                        "Two of its clips dragged onto the deck's track");
                require(song.djDeckInfo(0).behindOn == Session::ModelChange::notes, "bring notes and no devices");
                require(song.addDevice("Drums", otherTrack).wasOk(), "The other track given a Drum Rack instead");
                const auto drums = clipOn(otherTrack, 4.0 * bar);
                require(drums.isValid() && song.editClip(drums, onto(drums, deckTrack).position, ClipGesture::move, deckTrack).wasOk()
                            && song.trackHasDrumRack(deckTrack),
                        "A clip of it dragged onto the deck's track brings the rack");
                require(song.djDeckInfo(0).behindOn == (Session::ModelChange::notes | Session::ModelChange::devices),
                        "and leaves the deck behind on its devices");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0)
                            && song.djDeckInfo(0).behindOn == Session::ModelChange::none,
                        "A bounce catches the deck up");
                require(song.addDrumKit(ContentLibrary::file("Drums/Kits/808 Kit.rdk"), otherTrack).wasOk(),
                        "The other track's rack given a kit");
                const auto hit = clipOn(otherTrack, 5.0 * bar);
                require(hit.isValid() && song.editClip(hit, onto(hit, deckTrack).position, ClipGesture::move, deckTrack).wasOk(),
                        "A clip of it dragged onto the deck's rack, still blank");
                require(song.djDeckInfo(0).behindOn == (Session::ModelChange::notes | Session::ModelChange::devices),
                        "fills the rack with the kit, which is new devices too");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0), "and the deck catches up");
                // A preset dropped on the instrument the track already runs
                // goes into it, as loading one from its name bar does: values,
                // and no new device.
                scenario("dj bounces: a preset dropped on the deck's instrument changes its values alone");
                require(song.addDevice("RhinoFM", deckTrack).wasOk() && song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0),
                        "The deck's track given Rhino FM in place of the rack, and bounced");
                require(song.addDeviceFromPreset(ContentLibrary::file("Presets/RhinoFM/Hard Bass.rnd"), deckTrack).wasOk()
                            && song.djDeckInfo(0).behindOn == Session::ModelChange::parameters,
                        "A Rhino FM preset dropped on it leaves the deck behind on values alone");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0), "and the deck catches up");

                // Last, because arming hangs input destinations off the song's
                // playback context. A deck that is not behind is never bounced,
                // so its staying current is the whole of the check.
                scenario("dj bounces: arming and monitoring bounce nothing");
                song.setTrackArmed(deckTrack, true);
                song.setTrackMonitoring(deckTrack, Session::InputMonitoring::on);
                require(song.isTrackArmed(deckTrack) && song.trackMonitoring(deckTrack) == Session::InputMonitoring::on,
                        "The deck's track armed and monitored");
                require(!song.djDeckInfo(0).stale, "leaves its bounce current");
                song.djPoll();
                require(song.djBouncesStarted() == 10, "and starts no bounce");
                song.setTrackArmed(deckTrack, false);
                song.releaseAudioDevice();
                document.reset();
                require(!deckRenders.exists(), "A deck's renders go with the booth");
                songRenders.deleteRecursively();
            }

            scenario("dj bounces: a track deck plays its track as it sounds alone");
            {
                auto document = std::make_unique<Session>();
                auto& song = *document;
                const auto lane = 1;
                require(song.trackType(lane) == Session::TrackType::audio, "An audio lane");
                require(song.importAudioAt(source.getFile(), lane, 0.0).wasOk(), "with the fixture on it");
                const auto trackId = te::getAudioTracks(*song.edit)[lane]->itemID;
                const auto laneNow = [&song, trackId]
                {
                    const auto tracks = te::getAudioTracks(*song.edit);
                    for (int t = 0; t < tracks.size(); ++t)
                        if (tracks[t]->itemID == trackId)
                            return t;
                    return -1;
                };
                song.toggleTrackMute(lane);
                require(song.trackMixer(lane).muted, "muted in the song");
                require(song.addDjDeck().wasOk() && song.loadDjDeckTrack(0, lane).wasOk() && waitForDeck(song, 0),
                        ("The muted track on a deck: " + song.djDeckInfo(0).error).toRawUTF8());
                const auto alone = rmsOf(song.djDeckTrack(0)->left);
                require(alone > 0.05, "bounces audible");
                song.toggleTrackMute(lane);

                require(song.groupTracks({lane}, "Deck bus").wasOk(), "The track grouped under a bus");
                const auto group = song.trackGroups().back();
                require(group.contains(laneNow()), "as its member");
                require(song.setTrackVolumeDb(group.busTrack, -20.0f).wasOk(), "whose fader is turned down");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0), "The track deck bounced again");
                const auto trackDeck = rmsOf(song.djDeckTrack(0)->left);
                require(std::abs(trackDeck - alone) < alone * 0.01,
                        "plays the track as it sounded alone, mute or no mute, bus or no bus");
                require(song.addDjDeck().wasOk() && song.loadDjDeckGroup(1, group.id).wasOk() && waitForDeck(song, 1),
                        ("The group on a second deck: " + song.djDeckInfo(1).error).toRawUTF8());
                const auto groupDeck = rmsOf(song.djDeckTrack(1)->left);
                require(groupDeck < trackDeck * 0.2,
                        ("while the group deck passes through the bus (" + juce::String(trackDeck, 4) + " against "
                         + juce::String(groupDeck, 4) + ")").toRawUTF8());
                require(song.setTrackVolumeDb(group.busTrack, -19.0f).wasOk() && !song.djDeckInfo(0).stale
                            && song.djDeckInfo(1).stale,
                        "and the bus fader leaves the track deck current and the group deck behind");

                scenario("dj bounces: a track past full scale reaches the deck unclipped");
                const auto clipId = te::getAudioTracks(*song.edit)[laneNow()]->getClips().getFirst()->itemID;
                require(song.setAudioClipGainDb(clipId, 12.0f).wasOk() && song.djDeckInfo(0).stale,
                        "The clip's gain at +12 dB leaves the deck behind");
                require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0), "and it bounces");
                const auto peak = peakOf(*song.djDeckTrack(0));
                require(peak > 1.5f, ("with its peaks above full scale, as a float file keeps them: " + juce::String(peak, 3)).toRawUTF8());
                song.releaseAudioDevice();
            }

            scenario("dj bounces: a reopened document bounces the audio stored beside it");
            {
                // Opened as the app opens one, from a file on disk, so that a
                // sample imported afterwards is stored relative to the
                // document's folder. A deck's copy of the document, and the
                // document itself once reopened, used to find nothing there.
                const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("Rhino deck reopen " + juce::String(juce::Time::currentTimeMillis()));
                require(folder.createDirectory().wasOk(), "A folder for the document");
                const auto documentFile = folder.getChildFile("Set.rhinoedit");
                const auto sample = folder.getChildFile("Loop.wav");
                require(source.getFile().copyFileTo(sample), "with a sample beside it");
                {
                    auto document = std::make_unique<Session>();
                    auto& song = *document;
                    const auto save = [&song, &documentFile]
                    {
                        auto state = song.projectSnapshot();
                        if (const auto xml = state.createXml())
                            xml->writeTo(documentFile);
                        return state;
                    };
                    require(song.restoreProject(save(), documentFile).wasOk(), "The document opened from its file");
                    const auto lane = 1;
                    require(song.importAudioAt(sample, lane, 0.0).wasOk(), "A sample imported after it");
                    const auto* imported = te::getAudioTracks(*song.edit)[lane]->getClips().getFirst();
                    require(imported != nullptr && !juce::File::isAbsolutePath(imported->state[te::IDs::source].toString()),
                            "is stored relative to the document");
                    const auto clipId = imported->itemID;
                    require(song.addDjDeck().wasOk() && song.loadDjDeckTrack(0, lane).wasOk() && waitForDeck(song, 0),
                            ("goes on a deck: " + song.djDeckInfo(0).error).toRawUTF8());
                    require(rmsOf(song.djDeckTrack(0)->left) > 0.05, "and the deck's copy of the document plays it");
                    const auto deckRenders = song.djCopyFolder(0);
                    require(deckRenders.createDirectory().wasOk() && deckRenders.getChildFile("proxy_1b2c3d.wav").replaceWithText("decoded"),
                            "A render the deck's copy left");
                    require(song.restoreProject(save(), documentFile).wasOk(), "The document saved and opened again");
                    require(!deckRenders.exists(), "goes with the document it was made for");
                    const auto* reopened = song.findAudioClip(clipId);
                    require(reopened != nullptr && reopened->getAudioFile().isValid(), "plays the sample in the song");
                    require(song.rebounceDjDeck(0).wasOk() && waitForDeck(song, 0)
                                && rmsOf(song.djDeckTrack(0)->left) > 0.05,
                            ("and on the deck that comes back with it: " + song.djDeckInfo(0).error).toRawUTF8());

                    // A render waits for every clip's file to open, so one whose
                    // file has gone - a document moved away from its samples, a
                    // sample deleted - is refused before anything is rendered:
                    // the bounce held the booth's worker for its minute, and a
                    // merge hung the window for good.
                    scenario("dj bounces: a sample gone from beside the document is refused at once");
                    require(song.importAudioAt(sample, lane, 2.0).wasOk(), "A second clip of the sample");
                    const auto secondId = te::getAudioTracks(*song.edit)[lane]->getClips().getLast()->itemID;
                    // Through the engine, which lets go of the file first.
                    song.engine.getAudioFileManager().releaseFile(te::AudioFile(song.engine, sample));
                    require(sample.deleteFile() && !sample.exists(), "The sample deleted from beside the document");
                    const auto asked = juce::Time::getMillisecondCounterHiRes();
                    const auto bounce = song.rebounceDjDeck(0);
                    require(bounce.failed() && bounce.getErrorMessage().contains(sample.getFileName())
                                && juce::Time::getMillisecondCounterHiRes() - asked < 5000.0,
                            ("A bounce of it is refused at once, naming the file: " + bounce.getErrorMessage()).toRawUTF8());
                    require(song.djDeckInfo(0).error == bounce.getErrorMessage() && !song.djDeckInfo(0).loading,
                            "and the deck says why");
                    const auto merge = song.mergeClips({clipId, secondId});
                    require(merge.failed() && merge.getErrorMessage().contains(sample.getFileName()),
                            "Merging its clips is refused the same way");
                    song.releaseAudioDevice();
                }
                folder.deleteRecursively();
            }
        }
