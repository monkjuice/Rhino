        // The DJ booth through the session: decks added, a track bounced onto
        // one and a file read onto another, the deck played offline, a bounce
        // going stale under an edit and made again, a group bounced through
        // its bus, the set surviving a reopen, Live routing the track's input,
        // the note editor's clip, and the view painted and pressed. Renders,
        // so it sits before "recording" and the audio device closing.
        //
        // The group is made in a session of its own: from where this scenario
        // sits in the runner, a clip imported onto a newly added track never
        // renders (wiki/pages/renders-that-never-return.md), and the booth
        // was ruled out as the cause before the group moved.
        {
            const auto rmsOf = [](const std::vector<float>& samples)
            {
                double sum = 0.0;
                for (const auto s : samples) sum += static_cast<double>(s) * s;
                return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
            };
            // A bounce renders on the worker; the poll installs it.
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
            require(session.djEngineAttached(), "The booth is on the audio device once a deck exists");
            require(session.djDeckInfo(0).kind == Session::DjSourceKind::none && !session.djDeckInfo(0).loaded,
                    "A new deck holds nothing");

            scenario("dj booth: a track bounced onto a deck");
            session.applyPatternPreset(Session::PatternPreset::WarmPulse);
            const auto deckTrack = te::getAudioTracks(*session.edit)
                .indexOf(dynamic_cast<te::AudioTrack*>(session.pattern().getClipTrack()));
            require(deckTrack >= 0, "The pattern clip sits on a track");
            require(session.loadDjDeckTrack(0, deckTrack).wasOk(), "A MIDI track bounces onto a deck");
            require(session.djDeckInfo(0).loading, "on the worker");
            require(waitForDeck(session, 0), ("The worker finishes the bounce: " + session.djDeckInfo(0).error).toRawUTF8());
            auto info = session.djDeckInfo(0);
            require(info.kind == Session::DjSourceKind::track && info.loaded && !info.loading && info.track == deckTrack,
                    "The deck reports the track it holds");
            require(info.name == session.trackName(deckTrack), "and is named for it");
            const auto* bounced = session.djDeckTrack(0);
            require(bounced != nullptr && bounced->length() > 0, "The bounce is on the deck");
            require(close(bounced->analysis.bpm, session.tempo()) && close(bounced->analysis.firstBeatSeconds, 0.0),
                    "The bounce carries the song's tempo with its grid from the top");
            const auto barSeconds = 60.0 / session.tempo() * session.beatsPerBar();
            const auto bars = bounced->seconds() / barSeconds;
            require(bars >= 1.0 - 0.001 && std::abs(bars - std::round(bars)) < 0.01, "The bounce is whole bars long");
            require(rmsOf(bounced->left) > 0.001, "and carries the instrument's sound");
            session.djProcessOffline(512);
            auto state = session.djDeckState(0);
            require(state.transport == Session::DjDeckState::Transport::stopped, "A loaded deck stands stopped");
            require(state.loop.active && close(state.loop.startSeconds, 0.0) && close(state.loop.endSeconds, bounced->seconds()),
                    "A bounced track loops whole");
            require(close(state.lengthSeconds, bounced->seconds()) && close(state.bpm, session.tempo()), "The state reads the material");

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

            scenario("dj booth: a bounce goes stale and is made again");
            const auto generationBefore = session.djDeckInfo(0).generation;
            session.applyPatternPreset(Session::PatternPreset::AcidSteps);
            require(session.djDeckInfo(0).stale, "Changing the song marks the bounce stale");
            require(session.rebounceDjDeck(0).wasOk(), "The deck bounces again");
            require(waitForDeck(session, 0), ("and the worker finishes it: " + session.djDeckInfo(0).error).toRawUTF8());
            require(!session.djDeckInfo(0).stale && session.djDeckInfo(0).loaded, "and is fresh");
            require(session.djDeckInfo(0).generation > generationBefore, "New material has a new generation");
            session.djProcessOffline(512);
            require(session.djDeckState(0).loop.active, "The whole-track loop survives a bounce made again");

            scenario("dj booth: a group bounced through its bus");
            {
                auto document = std::make_unique<Session>();
                require(document->addDjDeck().wasOk(), "A deck in a session of its own");
                require(document->addAudioTrack().wasOk(), "A clean track for the group");
                const auto fresh = document->trackCount() - 1;
                require(document->importAudioAt(source.getFile(), fresh, 0.0).wasOk(), "with audio on it");
                require(document->groupTracks({fresh}, "Deck group").wasOk(), "grouped under a bus");
                const auto group = document->trackGroups().back();
                require(document->loadDjDeckGroup(0, group.id).wasOk(),
                        ("A group bounces onto a deck: " + document->djDeckInfo(0).error).toRawUTF8());
                require(waitForDeck(*document, 0), ("and the worker finishes it: " + document->djDeckInfo(0).error).toRawUTF8());
                const auto groupInfo = document->djDeckInfo(0);
                require(groupInfo.kind == Session::DjSourceKind::group && groupInfo.loaded && groupInfo.track == group.busTrack,
                        "The deck reports the group and its bus");
                const auto* grouped = document->djDeckTrack(0);
                require(grouped != nullptr && rmsOf(grouped->left) > 0.01, "The group's bounce carries its member's audio");
                const auto loud = rmsOf(grouped->left);
                // The discriminating step: the bus's own fader is in the bounce.
                require(document->setTrackVolumeDb(group.busTrack, -20.0f).wasOk(), "The bus fader can be turned down");
                require(document->djDeckInfo(0).stale, "which makes the bounce stale");
                require(document->rebounceDjDeck(0).wasOk(), "Bounced again");
                require(waitForDeck(*document, 0), "and finished");
                const auto* quieter = document->djDeckTrack(0);
                require(quieter != nullptr && rmsOf(quieter->left) < loud * 0.2,
                        "the group's bounce passes through the bus's fader");
                require(document->setDjDeckLive(0, true).failed(), "A group has nothing to play live");
                document->ejectDjDeck(0);
                require(!document->djDeckInfo(0).loaded && document->djDeckInfo(0).kind == Session::DjSourceKind::none,
                        "Eject empties the deck");
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
            {
                const auto savedSet = session.projectSnapshot();
                juce::TemporaryFile setProject(".rhinoedit");
                require(session.restoreProject(savedSet, setProject.getFile()).wasOk(), "Reopen a project with a DJ set");
            }
            require(session.djDeckCount() == 2, "The decks come back");
            info = session.djDeckInfo(0);
            require(info.kind == Session::DjSourceKind::track && info.name == session.trackName(deckTrack) && info.track == deckTrack,
                    "A track deck remembers its track");
            require(session.djDeckInfo(1).kind == Session::DjSourceKind::file && session.djDeckInfo(1).file == source.getFile(),
                    "A file deck remembers its file");
            require(close(session.djChannel(1).fader, 0.25) && session.djQuantise() == Session::DjQuantise::beat
                    && session.djFx().type == 2, "The mixer, the quantise and the effect come back");
            require(!info.loaded && info.stale, "A reopened deck waits for its material");
            juce::Thread::sleep(600);
            for (int waited = 0; waited < 400 && (!session.djDeckInfo(0).loaded || !session.djDeckInfo(1).loaded); ++waited)
            {
                session.djPoll();
                juce::Thread::sleep(25);
            }
            require(session.djDeckInfo(0).loaded, ("The poll bounces the track deck again: " + session.djDeckInfo(0).error).toRawUTF8());
            require(session.djDeckInfo(1).loaded, ("and reads the file deck again: " + session.djDeckInfo(1).error).toRawUTF8());
            session.djProcessOffline(512);
            require(close(session.djDeckState(0).hotCueSeconds[3], 0.5), "The hot cue is where it was");
            session.setDjQuantise(Session::DjQuantise::bar);

            scenario("dj booth: live and the note editor");
            // After every render: switching monitoring on arms the track, and
            // arming allocates the playback context a render must not find.
            const auto monitoringBefore = session.trackMonitoring(deckTrack);
            const auto live = session.setDjDeckLive(0, true);
            require(live.wasOk() || live.getErrorMessage().isNotEmpty(),
                    "Live says whether the input behind the track is there");
            require(session.trackMonitoring(deckTrack) == Session::InputMonitoring::on, "The track monitors its input");
            require(session.djDeckInfo(0).live, "and the deck says it is live");
            require(session.setDjDeckLive(0, false).wasOk(), "Live can be switched off");
            require(session.trackMonitoring(deckTrack) == monitoringBefore, "which puts the monitoring back");
            require(session.djDeckEditClip(0) == session.pattern().itemID, "Edit opens the track's clip");
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
                // The display drew the bounce's waveform: the lows are blue.
                auto bluePixels = 0;
                const auto wave = view->decks[0]->display.getBounds().translated(view->decks[0]->getX(), view->decks[0]->getY());
                for (int y = wave.getY(); y < wave.getBottom(); y += 2)
                    for (int x = wave.getX(); x < wave.getRight(); x += 2)
                    {
                        const auto pixel = picture.getPixelAt(x, y);
                        if (pixel.getBlue() > 180 && pixel.getRed() < 120) ++bluePixels;
                    }
                require(bluePixels > 20, "The screen draws the low band of the bounce");
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_DJ_SNAPSHOT", {}); shot.isNotEmpty())
                {
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(picture, *stream);
                }
                view->setLookAndFeel(nullptr);
            }

            scenario("dj booth: removing decks and refusals");
            require(session.removeDjDeck(0).wasOk() && session.djDeckCount() == 1, "A deck can be removed");
            require(session.djDeckInfo(0).kind == Session::DjSourceKind::file, "and the deck above closes the gap");
            require(session.loadDjDeckTrack(0, session.masterTrackIndex()).failed(), "The main row cannot be bounced");
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
