        // The DJ booth through the session: decks added, a track's cell played
        // on one and a file read onto another, the deck played offline, the
        // arrangement's clips left to the arrangement and a cell's own edit
        // played, an audio cell, a drum cell and a cell on a bare track, the
        // set surviving a reopen, Live routing the track's input, the note
        // editor's clip, the view painted and pressed, and a deck taken off
        // from among four playing. A track's deck heard through its chain is
        // DjLiveChain.inc's.
        {
            const auto rmsOf = [](const std::vector<float>& samples)
            {
                double sum = 0.0;
                for (const auto s : samples) sum += static_cast<double>(s) * s;
                return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
            };
            // An audio cell's sound and a file are read on the worker; the poll
            // installs them.
            const auto waitForDeck = [](Session& booth, int deck)
            {
                for (int waited = 0; waited < 800 && booth.djDeckInfo(deck).loading; ++waited)
                {
                    juce::Thread::sleep(25);
                    booth.djPoll();
                }
                return booth.djDeckInfo(deck).loaded && !booth.djDeckInfo(deck).loading;
            };
            require(session.djDeckCount() == 0, "A new project has no decks");
            require(session.addDjDeck().wasOk() && session.addDjDeck().wasOk(), "Two decks can be added");
            require(session.djDeckCount() == 2, "and both are counted");
            // In the app the first deck puts the booth on the audio device. A
            // runner drives it itself, a block at a time, so it never goes on
            // one here: the device's thread would render the same decks.
            require(!session.djEngineAttached(), "Under the test runner the booth stays off the audio device");
            require(session.djDeckInfo(0).kind == Session::DjSourceKind::none && !session.djDeckInfo(0).loaded,
                    "A new deck holds nothing");

            scenario("dj booth: a track's cell on a deck");
            session.applyPatternPreset(Session::PatternPreset::WarmPulse);
            const auto deckTrack = te::getAudioTracks(*session.edit)
                .indexOf(dynamic_cast<te::AudioTrack*>(session.pattern().getClipTrack()));
            require(deckTrack >= 0, "The pattern clip sits on a track");
            const auto arrangementPattern = session.pattern().itemID;
            require(session.loadDjDeckTrack(0, deckTrack).wasOk(), "A track goes on a deck");
            require(session.djDeckInfo(0).slot == -1 && !session.djDeckInfo(0).loaded && session.djDeckTrack(0) == nullptr,
                    "and plays nothing until a cell is chosen: the arrangement's pattern is not the deck's");
            require(session.copyClipToTrackSlot(arrangementPattern, deckTrack, 0).wasOk(),
                    "The pattern copied into the track's first cell");
            require(session.loadDjDeckSlot(0, deckTrack, 0).wasOk(), "The deck plays the cell");
            auto info = session.djDeckInfo(0);
            require(info.kind == Session::DjSourceKind::track && info.loaded && !info.loading && info.track == deckTrack && info.slot == 0,
                    "The deck reports the track and the cell it plays");
            require(info.name == session.trackName(deckTrack) + " / " + session.slotClip(deckTrack, 0).name, "and is named for them");
            const auto* material = session.djDeckTrack(0);
            require(material != nullptr && material->length() > 0, "The cell is on the deck, at once");
            require(close(material->analysis.bpm, session.tempo()) && close(material->analysis.firstBeatSeconds, 0.0),
                    "on the song's grid from its top");
            const auto barSeconds = 60.0 / session.tempo() * session.beatsPerBar();
            const auto bars = material->seconds() / barSeconds;
            require(bars >= 1.0 - 0.001 && std::abs(bars - std::round(bars)) < 0.01, "whole bars long");
            require(!material->midi.empty() && material->left.empty(), "as the cell's notes, which the track's instrument sounds");
            session.djProcessOffline(512);
            auto state = session.djDeckState(0);
            require(state.transport == Session::DjDeckState::Transport::stopped, "A loaded deck stands stopped");
            require(state.loop.active && close(state.loop.startSeconds, 0.0) && close(state.loop.endSeconds, material->seconds()),
                    "A cell loops whole");
            require(close(state.lengthSeconds, material->seconds()) && close(state.bpm, session.tempo()), "The state reads the material");

            scenario("dj booth: the deck plays");
            session.djPlay(0);
            session.djProcessOffline(4096);
            state = session.djDeckState(0);
            require(state.isPlaying() && state.positionSeconds > 0.05, "The deck plays and moves");
            const auto master = session.djMaster();
            require(master.masterDeck == 0 && close(master.bpm, session.tempo()), "The playing deck is the master at the song's tempo");
            require(state.master, "and says so");
            session.djPause(0);
            session.djProcessOffline(1024);
            require(!session.djDeckState(0).isPlaying(), "and pauses");

            scenario("dj booth: the arrangement's clips are the arrangement's; a cell's own edit is the deck's");
            const auto generationBefore = session.djDeckInfo(0).generation;
            session.applyPatternPreset(Session::PatternPreset::AcidSteps);
            session.djPoll();
            require(session.djDeckInfo(0).generation == generationBefore,
                    "Changing the arrangement's pattern leaves the deck's cell as it was");
            require(session.selectPatternClip(session.slotClip(deckTrack, 0).clipID).wasOk()
                        && session.addNote(14.0, 64, 1.0).wasOk(),
                    "A note added to the deck's own cell");
            session.djPoll();
            require(session.djDeckInfo(0).generation > generationBefore && session.djDeckInfo(0).loaded,
                    "is on the deck at the next poll, made again from the cell");
            session.djProcessOffline(512);
            require(session.djDeckState(0).loop.active, "The whole-cell loop survives the cell made again");
            require(session.selectPatternClip(arrangementPattern).wasOk(), "The editor goes back to the arrangement's pattern");

            scenario("dj booth: an audio cell, a drum cell and a cell on a bare track");
            {
                auto document = std::make_unique<Session>();
                require(document->addDjDeck().wasOk(), "A deck in a session of its own");
                int audioLane = -1, drumLane = -1, secondMidi = -1;
                for (int t = 0; t < document->trackCount(); ++t)
                {
                    if (document->isGroupBusTrack(t)) continue;
                    if (document->trackType(t) == Session::TrackType::audio)
                    {
                        if (audioLane < 0) audioLane = t;
                    }
                    else if (drumLane < 0) drumLane = t;
                    else if (secondMidi < 0) secondMidi = t;
                }
                require(audioLane >= 0 && drumLane >= 0 && secondMidi >= 0, "An audio lane and two MIDI lanes");
                require(document->insertAudioFileInSlot(source.getFile(), audioLane, 0).wasOk(), "A sample in the audio lane's cell");
                require(document->loadDjDeckSlot(0, audioLane, 0).wasOk(), "goes on the deck");
                // An audio cell on a deck opens its audio clip, for the audio
                // editor, as a MIDI cell opens its notes.
                require(document->findAudioClip(document->djDeckEditClip(0)) != nullptr,
                        "and the deck's clip is the cell's audio clip");
                require(waitForDeck(*document, 0), ("and its sound is read: " + document->djDeckInfo(0).error).toRawUTF8());
                require(document->djDeckTrack(0) != nullptr && rmsOf(document->djDeckTrack(0)->left) > 0.01,
                        "The cell's sound is on the deck");
                require(document->setDjDeckLive(0, true).failed(), "An audio track has no instrument to play live");
                document->ejectDjDeck(0);
                require(!document->djDeckInfo(0).loaded && document->djDeckInfo(0).kind == Session::DjSourceKind::none,
                        "Eject empties the deck");
                require(document->insertPatternPresetInSlot(Session::PatternPreset::HouseKit, drumLane, 0).wasOk(),
                        "A drum pattern in a MIDI lane's first cell brings a kit");
                require(document->trackHasDrumRack(drumLane), "on a Drum Rack");
                require(document->loadDjDeckSlot(0, drumLane, 0).wasOk(), "The deck plays the cell");
                require(document->djDeckTrack(0) != nullptr && !document->djDeckTrack(0)->midi.empty(),
                        "at once, with the drums' notes in it");
                document->ejectDjDeck(0);
                // The steps reported silent on 2026-10-10: a new cell on a bare
                // MIDI track, a Drum Rack with a kit added after it, and a note
                // drawn in the editor.
                require(document->createSlotClip(secondMidi, 0).wasOk(), "A new cell on a bare MIDI track");
                require(document->loadDjDeckSlot(0, secondMidi, 0).wasOk() && document->djDeckInfo(0).loaded,
                        "The deck plays it at once, though nothing sounds it yet");
                const auto bare = document->djDeckInfo(0).generation;
                require(document->addDrumKit(ContentLibrary::file("Drums/Kits/808 Kit.rdk"), secondMidi).wasOk(),
                        "A Drum Rack with a kit, added after");
                document->djPoll();
                require(document->djDeckInfo(0).generation == bare,
                        "is the track's to sound at once, with nothing on the deck made again");
                require(document->selectPatternClip(document->slotClip(secondMidi, 0).clipID).wasOk(), "The editor opens the cell's clip");
                require(document->isPatternDrums(), "as a drum editor");
                require(document->addNote(8.0, document->patternDrumLowestNote(), 1.0).wasOk(), "A note drawn on the first pad");
                document->djPoll();
                const auto* drawn = document->djDeckTrack(0);
                require(document->djDeckInfo(0).generation > bare && drawn != nullptr && drawn->midi.size() == 2,
                        "is on the deck at the next poll");
                document->ejectDjDeck(0);
                document->releaseAudioDevice();
            }

            scenario("dj booth: a file read onto a deck");
            require(session.loadDjDeckFile(1, source.getFile()).wasOk(), "A file loads onto a deck");
            require(session.djDeckInfo(1).loading, "on a worker");
            for (int waited = 0; waited < 400 && session.djDeckInfo(1).loading; ++waited)
            {
                juce::Thread::sleep(25);
                session.djPoll();
            }
            info = session.djDeckInfo(1);
            require(info.loaded && !info.loading && info.error.isEmpty(), ("The worker finishes the read: " + info.error).toRawUTF8());
            require(info.kind == Session::DjSourceKind::file && info.file == source.getFile(), "The deck reports the file");
            require(session.djDeckTrack(1) != nullptr && close(session.djDeckTrack(1)->seconds(), 1.0), "and its length");
            require(session.loadDjDeckFile(1, juce::File("C:/nowhere/missing.wav")).failed(), "A missing file is refused");

            scenario("dj booth: the set survives a reopen");
            session.djSeek(0, 0.5);
            session.djProcessOffline(512);
            session.djHotCue(0, 3);
            session.djProcessOffline(512);
            require(close(session.djDeckState(0).hotCueSeconds[3], 0.5), "A hot cue is set where the deck stands");
            session.setDjChannelFader(1, 0.25f);
            session.setDjQuantise(Session::DjQuantise::beat);
            session.setDjFxType(2);
            session.setDjChannelComp(1, 0.4f);
            session.setDjChannelEq(1, 2, -6.0f);
            session.setDjChannelSend(1, 0.3f);
            session.setDjChannelFaderCurve(1, 2);
            session.djSetBrake(0, 0.5f);
            session.djSetQuantiseSnap(0, false);
            session.setDjFxBand(0, false);
            session.setDjMicMode(2);
            session.setDjSendFxType(1);
            session.setDjCueLevel(-6.0f);
            session.setDjMonoSplit(true);
            {
                const auto savedSet = session.projectSnapshot();
                juce::TemporaryFile setProject(".rhinoedit");
                require(session.restoreProject(savedSet, setProject.getFile()).wasOk(), "Reopen a project with a DJ set");
            }
            require(session.djDeckCount() == 2, "The decks come back");
            info = session.djDeckInfo(0);
            require(info.kind == Session::DjSourceKind::track && info.track == deckTrack && info.slot == 0,
                    "A track deck remembers its track and its cell");
            require(session.djDeckInfo(1).kind == Session::DjSourceKind::file && session.djDeckInfo(1).file == source.getFile(),
                    "A file deck remembers its file");
            require(close(session.djChannel(1).fader, 0.25) && session.djQuantise() == Session::DjQuantise::beat
                    && session.djFx().type == 2, "The mixer, the quantise and the effect come back");
            {
                const auto channel = session.djChannel(1);
                require(close(channel.comp, 0.4) && close(channel.eqDb[2], -6.0) && close(channel.send, 0.3) && channel.faderCurve == 2,
                        "The channel's compressor, four-band EQ, send and fader curve come back");
                require(!session.djFx().bands[0] && session.djMic().mode == 2 && session.djSendFx().type == 1
                        && close(session.djMaster().cueLevelDb, -6.0) && session.djMaster().monoSplit,
                        "The effect's bands, the mic, the send unit and the headphones come back");
            }
            require(!info.loaded, "A reopened deck waits for its material");
            for (int waited = 0; waited < 400 && (!session.djDeckInfo(0).loaded || !session.djDeckInfo(1).loaded); ++waited)
            {
                session.djPoll();
                juce::Thread::sleep(25);
            }
            require(session.djDeckInfo(0).loaded, ("The poll puts the cell back on the track deck: " + session.djDeckInfo(0).error).toRawUTF8());
            require(session.djDeckInfo(1).loaded, ("and reads the file deck again: " + session.djDeckInfo(1).error).toRawUTF8());
            session.djProcessOffline(512);
            require(close(session.djDeckState(0).hotCueSeconds[3], 0.5), "The hot cue is where it was");
            require(close(session.djDeckState(0).brakeSeconds, 0.5) && !session.djDeckState(0).quantiseSnap,
                    "A deck's brake and quantize come back with it");
            session.setDjQuantise(Session::DjQuantise::bar);
            session.djSetBrake(0, 0.0f);
            session.djSetQuantiseSnap(0, true);

            scenario("dj booth: cue search and tap tempo");
            // The cue point stands at the top and hot cue D at half a second.
            session.djSeek(0, 0.2);
            session.djProcessOffline(512);
            session.djJumpToCue(0, true);
            session.djProcessOffline(512);
            require(close(session.djDeckState(0).positionSeconds, 0.5), "Cue search forward lands on the next cue point");
            session.djJumpToCue(0, false);
            session.djProcessOffline(512);
            require(close(session.djDeckState(0).positionSeconds, 0.0), "and back lands on the one before, the cue at the top");
            for (int tap = 0; tap < 4; ++tap)
            {
                session.djTapTempo();
                juce::Thread::sleep(300);
            }
            const auto tapped = session.djMaster().tapBpm;
            require(tapped > 170.0 && tapped < 215.0,
                    ("Four taps 300 ms apart set the effect's tempo near 200: " + juce::String(tapped, 1)).toRawUTF8());

            scenario("dj booth: the mic comes in through the booth's callback");
            {
                // The device's first input is the mic. Fed through the same
                // callback the device calls, with the mic on, the mic's meter
                // reads the tone; off, it reads nothing.
                session.setDjMicMode(1);
                std::vector<float> spoken(512);
                for (size_t i = 0; i < spoken.size(); ++i)
                    spoken[i] = 0.5f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 300.0 * static_cast<double>(i) / 48000.0));
                const float* inputs[1] {spoken.data()};
                for (int i = 0; i < 4; ++i)
                    session.djProcessOffline(inputs, 1, 512);
                require(session.djMic().meter > 0.3f, "The device's first input reaches the mic through the booth's callback");
                session.setDjMicMode(0);
                session.djProcessOffline(inputs, 1, 512);
                require(session.djMic().meter < 0.001f, "and the mic off reads nothing");
            }

            scenario("dj booth: clips on the console");
            {
                // The console's grid is the track's clip slots, the DJ view's
                // own clips. A double-click on an empty cell makes a one-bar
                // clip, the deck plays it through the track, the editors open
                // it, and a clip copied across from the timeline keeps the
                // lane's kind.
                require(session.createSlotClip(deckTrack, 1).wasOk(), "A one-bar clip in the second cell");
                const auto cell = session.slotClip(deckTrack, 1);
                require(cell.hasClip && cell.isMidi, "which is a MIDI clip");
                require(session.findClip(cell.clipID) != nullptr, "that findClip sees in its slot");
                require(session.createSlotClip(deckTrack, 1).failed(), "A cell already holding a clip refuses another");
                require(session.loadDjDeckSlot(0, deckTrack, 1).wasOk(), "The deck plays the cell");
                require(session.djDeckInfo(0).slot == 1 && session.djDeckEditClip(0) == cell.clipID,
                        "The deck says which cell it plays, and edits that cell's clip");
                const auto* slotMaterial = session.djDeckTrack(0);
                require(slotMaterial != nullptr && close(slotMaterial->seconds(), barSeconds), "A one-bar clip plays a bar");
                int audioTrack = -1;
                for (int t = 0; t < session.trackCount(); ++t)
                    if (!session.isGroupBusTrack(t) && session.trackType(t) == Session::TrackType::audio)
                    {
                        audioTrack = t;
                        break;
                    }
                require(audioTrack >= 0, "An audio track to hold the rule against");
                require(session.copyClipToTrackSlot(arrangementPattern, deckTrack, 2).wasOk(),
                        "The pattern clip copies from the timeline into the third cell");
                require(session.slotClip(deckTrack, 2).hasClip, "where it sits as a copy");
                require(session.copyClipToTrackSlot(arrangementPattern, audioTrack, 0).failed(),
                        "but not into an audio track's cell");
                require(session.createSlotClip(audioTrack, 0).failed(), "and an audio track's cell makes no MIDI clip");
                require(session.addTrack(Session::TrackType::midi).wasOk(), "A fresh MIDI lane");
                const auto freshLane = session.trackCount() - 1;
                require(session.copySlotClipToArrangement(deckTrack, 1, 0.0, freshLane).wasOk(), "takes a cell's clip as a copy");
                require(te::getAudioTracks(*session.edit)[freshLane]->getClips().size() == 1, "and gains the clip");
                require(session.copySlotClipToArrangement(deckTrack, 1, 0.0, audioTrack).failed(), "An audio lane refuses it");
                // The grid, pressed as the console presses it.
                auto grid = std::make_unique<DjClipGrid>(session, 0);
                grid->setSize(120, 200);
                grid->sync();
                require(grid->hasTrack() && grid->litCell() == 1, "The grid knows its track and lights the cell the deck plays");
                require(grid->cellAt({10, 1}) == 0 && grid->cellAt({10, DjClipGrid::cellHeight + DjClipGrid::cellGap + 1}) == 1,
                        "Its cells are the scenes, from the top: the arrangement has no cell");
                require(grid->chooseCell(0).wasOk() && session.djDeckInfo(0).slot == 0 && grid->litCell() == 0,
                        "Pressing the first cell plays it, lit");
                require(grid->createClipAt(3).wasOk() && session.djDeckInfo(0).slot == 3,
                        "A double-click on the fourth cell makes a clip there that the deck plays");
                require(grid->applyDrop(4, "rhino-clip:" + arrangementPattern.toString(), false).wasOk(),
                        "A clip dragged from the timeline lands in the fifth cell");
                require(session.slotClip(deckTrack, 4).hasClip, "as a copy");
                // A copy beside its source takes a name of its own, and the
                // note editor lets go of a clip deleted under it.
                require(grid->applyDrop(5, "rhino-slot:" + juce::String(deckTrack) + ":1", false).wasOk(),
                        "The second cell copies into the sixth");
                require(session.slotClip(deckTrack, 5).hasClip && session.slotClip(deckTrack, 5).name != session.slotClip(deckTrack, 1).name,
                        ("named on from it: " + session.slotClip(deckTrack, 5).name).toRawUTF8());
                require(session.selectPatternClip(session.slotClip(deckTrack, 5).clipID).wasOk(), "The note editor opens the copy");
                require(session.deleteSlotClip(deckTrack, 5).wasOk(), "Deleting it under the editor");
                require(session.hasPatternClip() && session.findClip(session.pattern().itemID) != nullptr,
                        "leaves the editor on a clip that still exists");
                require(grid->applyDrop(5, "rhino-browser:file:" + source.getFile().getFullPathName(), false).failed(),
                        "A sample dropped on a MIDI track's cell is refused");
                require(session.deleteSlotClip(deckTrack, 4).wasOk(), "A cell's clip can be deleted");
                // The cell a deck plays survives a reopen.
                require(session.loadDjDeckSlot(0, deckTrack, 0).wasOk() && waitForDeck(session, 0), "The deck plays the first cell again");
                {
                    const auto saved = session.projectSnapshot();
                    juce::TemporaryFile project(".rhinoedit");
                    require(session.restoreProject(saved, project.getFile()).wasOk(), "Reopen with a deck on a cell");
                }
                require(session.djDeckInfo(0).slot == 0, "The deck remembers its cell");
                for (int waited = 0; waited < 400 && (!session.djDeckInfo(0).loaded || !session.djDeckInfo(1).loaded); ++waited)
                {
                    session.djPoll();
                    juce::Thread::sleep(25);
                }
                require(session.djDeckInfo(0).loaded && session.djDeckInfo(1).loaded,
                        ("and both decks load again: " + session.djDeckInfo(0).error).toRawUTF8());
                // The cells past the first are cleared again and the deck plays
                // the first, for the scenarios that follow.
                for (int scene = 1; scene < session.sceneCount(); ++scene)
                    if (session.slotClip(deckTrack, scene).hasClip)
                        require(session.deleteSlotClip(deckTrack, scene).wasOk(), "The cells are cleared again");
                require(session.loadDjDeckSlot(0, deckTrack, 0).wasOk() && session.djDeckInfo(0).loaded, "The deck plays its first cell");
                require(session.selectPatternClip(arrangementPattern).wasOk(), "and the editor the arrangement's pattern");
            }

            scenario("dj booth: live and the note editor");
            const auto monitoringBefore = session.trackMonitoring(deckTrack);
            const auto live = session.setDjDeckLive(0, true);
            require(live.wasOk() || live.getErrorMessage().isNotEmpty(),
                    "Live says whether the input behind the track is there");
            require(session.trackMonitoring(deckTrack) == Session::InputMonitoring::on, "The track monitors its input");
            require(session.djDeckInfo(0).live, "and the deck says it is live");
            require(session.setDjDeckLive(0, false).wasOk(), "Live can be switched off");
            require(session.trackMonitoring(deckTrack) == monitoringBefore, "which puts the monitoring back");
            require(session.djDeckEditClip(0) == session.slotClip(deckTrack, 0).clipID, "Edit opens the cell the deck plays");
            require(session.djDeckHasDrumRack(0) == session.trackHasDrumRack(deckTrack), "A deck knows whether its track runs a Drum Rack");

            scenario("dj booth: the view");
            {
                // The view, built as the shell builds it, with a track deck and
                // a file deck on it: a paint smoke test, the panels inside the
                // view and off each other, a key pressed through its callback,
                // and the screen ticked as the display's refresh ticks it.
                auto view = std::make_unique<DjView>(session);
                view->setLookAndFeel(&theme);
                view->setSize(1240, 720);
                require(view->decks.size() == 2, "The view shows a panel per deck");
                const auto inside = [](juce::Rectangle<int> inner, juce::Rectangle<int> outer) { return outer.contains(inner); };
                for (auto& panel : view->decks)
                    require(inside(panel->getBounds(), view->getLocalBounds()) && !panel->getBounds().intersects(view->mixer.getBounds()),
                            "Each deck stands inside the view and clear of the mixer");
                require(!view->decks[0]->getBounds().intersects(view->decks[1]->getBounds()), "The two decks stand clear of each other");
                for (auto& panel : view->decks)
                    for (auto* child : panel->getChildren())
                        require(inside(child->getBounds(), panel->getLocalBounds()), "A deck's keys sit inside its console");
                require(view->mixer.strips.size() == 2, "The mixer has a strip per deck");
                require(view->mixer.getBottom() == view->getHeight() - 4,
                        "The mixer and its FX rack fill the centre column");
                require(view->mixer.fxType.getY() > view->mixer.crossfader.getBottom()
                            && view->mixer.sendType.getY() > view->mixer.crossfader.getBottom(),
                        "The FX rack stands below the mixer");
                require(view->mixer.strips[0]->knobs[0].getWidth() >= 32
                            && view->mixer.masterKnobs[DjMixerPanel::masterLevel].getWidth() >= 32,
                        "The mixer controls use the larger working size");
                // A third deck added while the view stands lands its strip
                // beside the others, not under the master section: the view
                // syncs the mixer before it lays out (the bug of 2026-10-09).
                require(session.addDjDeck().wasOk(), "A third deck while the view shows");
                view->sync();
                require(view->decks.size() == 3 && view->mixer.strips.size() == 3, "gets its panel and its strip");
                {
                    const auto masterLeft = view->mixer.getWidth() - DjMixerPanel::masterWidth - 4;
                    for (auto& strip : view->mixer.strips)
                        require(strip->fader.getRight() <= masterLeft && strip->side.getRight() <= masterLeft
                                && view->mixer.getLocalBounds().contains(strip->fader.getBounds()),
                                "Every strip stands clear of the master section without a resize");
                }
                require(session.removeDjDeck(2).wasOk(), "and the third deck goes again");
                view->sync();
                require(view->decks.size() == 2 && view->mixer.strips.size() == 2, "leaving two");
                // A press on a console focuses its deck: the mark moves, the
                // shell is told, and Space plays and pauses that deck alone.
                int focusedDeck = -1;
                view->deckSelected = [&focusedDeck](int deck) { focusedDeck = deck; };
                view->decks[1]->selected();
                require(view->selectedDeck() == 1 && focusedDeck == 1, "A press on the second console focuses it and tells the shell");
                require(view->decks[1]->isFocused() && !view->decks[0]->isFocused(), "and the mark moves to it");
                view->togglePlayFocused();
                session.djProcessOffline(2048);
                require(session.djDeckState(1).isPlaying() && !session.djDeckState(0).isPlaying(),
                        "Space plays the focused deck and no other");
                view->togglePlayFocused();
                session.djProcessOffline(1024);
                require(!session.djDeckState(1).isPlaying(), "and pauses it again");
                view->decks[0]->selected();
                require(view->decks[0]->isFocused() && !view->decks[1]->isFocused(), "The first console takes the focus back");
                view->decks[0]->play.onClick();
                session.djProcessOffline(2048);
                require(session.djDeckState(0).isPlaying(), "The Play key plays the deck");
                for (int frame = 0; frame < 5; ++frame)
                {
                    for (auto& panel : view->decks)
                        panel->tickDisplay();
                    view->timerCallback();
                    session.djProcessOffline(512);
                }
                view->decks[0]->hotCues[0]->onClick();
                session.djProcessOffline(512);
                require(session.djDeckState(0).hotCueSeconds[0] >= 0.0, "A hot cue pad sets the cue");
                view->decks[0]->play.onClick();
                session.djProcessOffline(1024);
                require(!session.djDeckState(0).isPlaying(), "and Play again pauses");
                view->sync();
                auto picture = view->createComponentSnapshot(view->getLocalBounds());
                require(picture.isValid() && picture.getWidth() == 1240, "The view paints");
                // Each screen draws what its deck plays in the mid band's
                // colour: the file deck its 440 Hz tone, the track deck the
                // notes of its cell, which the screen draws in place of a
                // waveform.
                const auto midPixels = [&picture, &view](int deck)
                {
                    auto count = 0;
                    const auto& panel = *view->decks[static_cast<size_t>(deck)];
                    const auto area = panel.display.getBounds().translated(panel.getX(), panel.getY());
                    for (int y = area.getY(); y < area.getBottom(); y += 2)
                        for (int x = area.getX(); x < area.getRight(); x += 2)
                        {
                            const auto pixel = picture.getPixelAt(x, y);
                            if (pixel.getRed() > 120 && pixel.getRed() > 2 * pixel.getBlue() && pixel.getGreen() > pixel.getBlue())
                                ++count;
                        }
                    return count;
                };
                require(midPixels(1) > 20, "The file deck's screen draws its tone");
                require(midPixels(0) > 20, "and the track deck's screen the notes of its cell");
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_DJ_SNAPSHOT", {}); shot.isNotEmpty())
                {
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(picture, *stream);
                }
                view->setLookAndFeel(nullptr);
            }

            scenario("dj booth: a deck taken off leaves the others playing");
            {
                // Four decks play the fixture and the second is taken off.
                // The third and fourth close up on the console and play on
                // from where they stood, a block a block, never silent and
                // never read again: each deck plays in an engine slot of its
                // own, and only the one taken off is emptied.
                auto document = std::make_unique<Session>();
                auto& booth = *document;
                for (int deck = 0; deck < 4; ++deck)
                    require(booth.addDjDeck().wasOk() && booth.loadDjDeckFile(deck, source.getFile()).wasOk(), "Four decks read the fixture");
                for (int waited = 0; waited < 400 && booth.djBusy(); ++waited)
                {
                    juce::Thread::sleep(25);
                    booth.djPoll();
                }
                for (int deck = 0; deck < 4; ++deck)
                {
                    require(booth.djDeckInfo(deck).loaded, ("Each deck has the fixture: " + booth.djDeckInfo(deck).error).toRawUTF8());
                    booth.djPlay(deck);
                }
                for (int played = 0; played < 4; ++played)
                    booth.djProcessOffline(480);
                require(booth.djDeckState(2).isPlaying() && booth.djDeckState(3).isPlaying(), "All four play");
                const auto* third = booth.djDeckTrack(2);
                const auto* fourth = booth.djDeckTrack(3);
                const auto thirdAt = booth.djDeckState(2).positionSeconds, fourthAt = booth.djDeckState(3).positionSeconds;
                const auto generation = booth.djDeckInfo(2).generation;
                // The deck going has settings of its own; the third has a
                // trim and carries the beat effect.
                booth.setDjChannelTrim(1, -6.0f);
                booth.djSetBrake(1, 1.0f);
                booth.setDjChannelTrim(2, -3.0f);
                booth.setDjFxTarget(2);
                require(booth.removeDjDeck(1).wasOk() && booth.djDeckCount() == 3, "The second deck is taken off");
                require(booth.djDeckTrack(1) == third && booth.djDeckTrack(2) == fourth, "The decks after it close up with their material");
                require(std::abs(booth.djChannel(1).trimDb + 3.0f) < 0.001f && booth.djFx().target == 1,
                        "and their strips and the beat effect move up with them");
                for (int played = 1; played <= 10; ++played)
                {
                    booth.djProcessOffline(480);
                    for (int deck = 1; deck <= 2; ++deck)
                    {
                        const auto standing = booth.djDeckState(deck);
                        const auto from = deck == 1 ? thirdAt : fourthAt;
                        require(standing.isPlaying() && std::abs(standing.positionSeconds - (from + played * 0.01)) < 1.0e-9
                                    && booth.djChannel(deck).meter > 0.01f,
                                ("and play on from where they stood, a block a block, never silent: deck " + juce::String(deck + 1)
                                 + " at " + juce::String(standing.positionSeconds, 4) + " s").toRawUTF8());
                    }
                }
                require(!booth.djBusy() && booth.djDeckInfo(1).loaded && booth.djDeckInfo(1).generation == generation,
                        "No deck is read again");
                // A deck added now takes the slot let go of, and none of what
                // the deck that played there was set to.
                require(booth.addDjDeck().wasOk() && booth.djDeckCount() == 4, "A deck added after");
                require(booth.djChannel(3).trimDb == 0.0f && booth.djDeckState(3).brakeSeconds == 0.0f
                            && booth.djDeckState(3).transport == Session::DjDeckState::Transport::empty,
                        "starts as a new deck does, whoever played in its slot");
                booth.releaseAudioDevice();
            }

            scenario("dj booth: removing decks and refusals");
            require(session.removeDjDeck(0).wasOk() && session.djDeckCount() == 1, "A deck can be removed");
            require(session.djDeckInfo(0).kind == Session::DjSourceKind::file, "and the deck above closes the gap");
            require(session.loadDjDeckTrack(0, session.masterTrackIndex()).failed(), "The main row cannot go on a deck");
            require(session.loadDjDeckTrack(5, 0).failed(), "A deck that does not exist refuses a load");
            require(session.setDjDeckLive(0, true).failed(), "A file deck has nothing to play live");
            for (int i = 0; i < Session::maximumDjDecks; ++i)
                session.addDjDeck();
            require(session.djDeckCount() == Session::maximumDjDecks && session.addDjDeck().failed(), "Six decks is the booth");
            while (session.djDeckCount() > 0)
                require(session.removeDjDeck(session.djDeckCount() - 1).wasOk(), "Every deck can be removed");
            require(session.djDeckCount() == 0, "and the booth is empty again");
            session.djProcessOffline(512);
        }
