        // The booth at its edges, each in a document of its own: a deck taken
        // off with the beat effect on it, a tempo held past the fader's
        // range, and material arriving while the engine's queue is full.
        {
            // A block of the booth's output, the first channel, as the device
            // would be handed it.
            const auto render = [](Session& booth, int blocks)
            {
                std::vector<float> played, left(480), right(480);
                float* outputs[2] {left.data(), right.data()};
                for (int b = 0; b < blocks; ++b)
                {
                    booth.djProcessOffline(nullptr, 0, outputs, 2, 480);
                    played.insert(played.end(), left.begin(), left.end());
                }
                return played;
            };

            scenario("dj booth edges: the beat effect on a deck taken off falls silent");
            {
                // Two documents play the fixture on two decks, the second's
                // fader down; in one the echo is on the second deck and
                // switched on. Taking the second deck off leaves that echo on
                // nothing, so the first deck plays out the same to the sample
                // as in the document without it. It used to move onto the
                // master and echo the whole mix.
                const auto playTwoAndTakeOne = [&](Session& booth, bool echo)
                {
                    require(booth.addDjDeck().wasOk() && booth.addDjDeck().wasOk(), "Two decks");
                    require(booth.loadDjDeckFile(0, source.getFile()).wasOk() && booth.loadDjDeckFile(1, source.getFile()).wasOk(),
                            "read the fixture");
                    for (int waited = 0; waited < 400 && booth.djBusy(); ++waited)
                    {
                        juce::Thread::sleep(25);
                        booth.djPoll();
                    }
                    require(booth.djDeckInfo(0).loaded && booth.djDeckInfo(1).loaded, "and both have it");
                    booth.setDjChannelFader(1, 0.0f);
                    if (echo)
                    {
                        booth.setDjFxType(1);
                        booth.setDjFxBeats(0.125f);
                        booth.setDjFxTarget(1);
                        booth.setDjChannelFx(1, true);
                        booth.setDjFxOn(true);
                    }
                    // The second fader settles all the way down before
                    // anything plays.
                    render(booth, 20);
                    booth.djPlay(0);
                    booth.djPlay(1);
                    render(booth, 4);
                    require(booth.djDeckState(0).isPlaying() && booth.djDeckState(1).isPlaying(), "Both play");
                    require(booth.removeDjDeck(1).wasOk() && booth.djDeckCount() == 1, "The second deck is taken off");
                    return render(booth, 40);
                };
                auto withEcho = std::make_unique<Session>();
                auto plain = std::make_unique<Session>();
                const auto echoed = playTwoAndTakeOne(*withEcho, true);
                const auto dry = playTwoAndTakeOne(*plain, false);
                const auto fx = withEcho->djFx();
                require(fx.on && fx.target == withEcho->djDeckCount(),
                        "The echo stays on, assigned past the console to the channel the deck went from");
                float largest = 0.0f, difference = 0.0f;
                for (size_t i = 0; i < dry.size() && i < echoed.size(); ++i)
                {
                    largest = std::max(largest, std::abs(dry[i]));
                    difference = std::max(difference, std::abs(echoed[i] - dry[i]));
                }
                require(largest > 0.05f, "The deck left plays on");
                require(echoed.size() == dry.size() && difference == 0.0f,
                        ("and is not echoed: the output is the plain document's to the sample (" + juce::String(difference, 6)
                         + " apart at most)").toRawUTF8());
                withEcho->releaseAudioDevice();
                plain->releaseAudioDevice();
            }

            scenario("dj booth edges: a tempo held past the fader's range");
            {
                // Two decks on one-bar cells of two bare MIDI tracks, one
                // each, silent on the song's grid. The first plays at +10 per
                // cent on a fader of +-10, the second synced to it at +-6;
                // when the first stops, the second takes over holding +10 per
                // cent. Its fader shows that, on a range widened to reach it,
                // so the readout and the fader agree and the first touch moves
                // nothing. A cell of a third track on it then plays at its own
                // tempo.
                auto document = std::make_unique<Session>();
                auto& song = *document;
                require(song.trackType(0) == Session::TrackType::midi && song.trackType(1) == Session::TrackType::midi
                            && !song.trackHasInstrument(0) && !song.trackHasInstrument(1),
                        "The starter stack's two MIDI tracks, bare");
                require(song.createSlotClip(0, 0).wasOk() && song.createSlotClip(1, 0).wasOk(), "a cell on each");
                require(song.addDjDeck().wasOk() && song.addDjDeck().wasOk(), "Two decks");
                require(song.loadDjDeckSlot(0, 0, 0).wasOk() && song.loadDjDeckSlot(1, 1, 0).wasOk()
                            && song.djDeckInfo(0).loaded && song.djDeckInfo(1).loaded,
                        "take a cell each, silent on the song's grid");
                song.setDjQuantise(Session::DjQuantise::off);
                render(song, 1);
                const auto bpm = song.djDeckState(0).bpm;
                const auto bar = song.djDeckState(0).lengthSeconds;
                require(bpm > 0.0 && bar > 0.0, "with a tempo");
                // How far a deck moves in ten blocks, its rate settled.
                const auto travel = [&](int deck)
                {
                    render(song, 40);
                    const auto from = song.djDeckState(deck).positionSeconds;
                    render(song, 10);
                    auto moved = song.djDeckState(deck).positionSeconds - from;
                    if (moved < 0.0) moved += bar;
                    return moved;
                };
                song.djSetTempoRange(0, 10);
                song.djSetTempoPercent(0, 10.0f);
                song.djPlay(0);
                song.djSetSynced(1, true);
                song.djPlay(1);
                render(song, 40);
                require(close(song.djDeckState(1).effectiveBpm, bpm * 1.1), "The second deck follows the first at +10 per cent");
                song.djPause(0);
                render(song, 20);
                const auto held = song.djDeckState(1);
                require(held.master && close(held.effectiveBpm, bpm * 1.1), "and takes over as master, holding that tempo");
                require(held.tempoRange == 10 && std::abs(held.tempoPercent - 10.0f) < 1.0e-3f,
                        ("Its fader shows the held tempo, its range widened to reach it (" + juce::String(held.tempoPercent, 3)
                         + " per cent of " + juce::String(held.tempoRange) + ")").toRawUTF8());
                require(std::abs(bpm * (1.0 + held.tempoPercent / 100.0) - held.effectiveBpm) < 1.0e-3, "and agrees with the readout");
                const auto before = travel(1);
                // The fader taken hold of where it stands.
                song.djSetTempoPercent(1, held.tempoPercent);
                const auto after = travel(1);
                require(std::abs(after / before - 1.0) < 1.0e-4,
                        ("The first touch of the fader moves the tempo not at all (" + juce::String(after / before, 6) + " of it)").toRawUTF8());
                require(song.djDeckState(1).tempoRange == 10 && close(song.djDeckState(1).effectiveBpm, bpm * 1.1),
                        "and the deck keeps the range it was shown on");

                // Held again, at +12 per cent, then a new cell on it.
                song.djSetTempoRange(0, 16);
                song.djSetTempoPercent(0, 12.0f);
                song.djSetMaster(0);
                song.djPlay(0);
                render(song, 40);
                song.djPause(0);
                render(song, 20);
                require(song.djDeckState(1).master && close(song.djDeckState(1).effectiveBpm, bpm * 1.12),
                        "The second deck holds +12 per cent when the first stops again");
                require(song.addTrack(Session::TrackType::midi).wasOk() && song.createSlotClip(song.firstReturnTrack() - 1, 0).wasOk()
                            && song.loadDjDeckSlot(1, song.firstReturnTrack() - 1, 0).wasOk() && song.djDeckInfo(1).loaded,
                        "A cell of a third track on it");
                const auto fresh = travel(1);
                require(close(song.djDeckState(1).effectiveBpm, bpm) && std::abs(fresh / (before / 1.1) - 1.0) < 1.0e-4,
                        ("plays at the tempo its new settings give it, its own (" + juce::String(song.djDeckState(1).effectiveBpm, 2)
                         + " BPM)").toRawUTF8());
                song.releaseAudioDevice();
            }

            scenario("dj booth edges: material waits for room in a full queue");
            {
                // The engine's queue drains only while blocks run, so with the
                // device stopped a deck's keys fill it. A cell landing then
                // waits for room with its material, and is handed over once a
                // block has run, rather than thrown away and made again.
                auto document = std::make_unique<Session>();
                auto& song = *document;
                song.applyPatternPreset(Session::PatternPreset::WarmPulse);
                const auto deckTrack = te::getAudioTracks(*song.edit).indexOf(dynamic_cast<te::AudioTrack*>(song.pattern().getClipTrack()));
                require(deckTrack >= 0 && song.copyClipToTrackSlot(song.pattern().itemID, deckTrack, 0).wasOk() && song.addDjDeck().wasOk(),
                        "A deck and a track with a pattern in its first cell");
                render(song, 1);
                for (int key = 0; key < DjEngine::queueSize - 1; ++key)
                    song.djPause(0);
                require(song.loadDjDeckSlot(0, deckTrack, 0).wasOk(), "The cell goes on the deck");
                require(!song.djDeckInfo(0).loaded && song.djDeckInfo(0).loading,
                        "and its material waits for room, still loading");
                const auto generation = song.djDeckInfo(0).generation;
                juce::Thread::sleep(600);
                for (int poll = 0; poll < 10; ++poll)
                    song.djPoll();
                require(!song.djDeckInfo(0).loaded && song.djDeckInfo(0).generation == generation,
                        "Nothing is made again for it while it waits");
                render(song, 1);
                song.djPoll();
                require(song.djDeckInfo(0).loaded && !song.djDeckInfo(0).loading, "A block makes room, and the poll hands it over");
                render(song, 1);
                require(song.djDeckState(0).loop.active && song.djDeckState(0).lengthSeconds > 0.0,
                        "with its settings behind it: the cell loops whole");
                song.releaseAudioDevice();
            }
        }
