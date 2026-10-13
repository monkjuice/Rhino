        // A change repaints what it changed and nothing else: a sync that
        // finds every deck as it was repaints nothing, a key the session
        // announces repaints that key, a mark set on a stopped deck reaches
        // its screen, the mixer shows a value set without an announcement,
        // and a press on the console already focused tells the shell
        // nothing. Each panel used to repaint whole on every announcement,
        // and every press re-ran the shell's follow of the deck.
        {
            auto& damage = recordDamage(*djView);
            const auto inView = [&djView](juce::Component& component)
            {
                return djView->getLocalArea(&component, component.getLocalBounds());
            };
            const auto damageWithin = [&damage](juce::Rectangle<int> allowed)
            {
                for (const auto& area : damage.region)
                    if (!allowed.contains(area)) return false;
                return true;
            };
            for (int frame = 0; frame < 12; ++frame)
                djView->frame();
            djView->timerCallback();
            damage.region.clear();
            djView->sync();
            require(damage.region.isEmpty(), "A sync that finds every deck as it was repaints nothing");
            djView->frame();
            djView->timerCallback();
            require(damage.region.isEmpty(), "and a frame and a tick with every deck standing still repaint nothing");

            auto& trackDeck = *djView->decks[1];
            damage.region.clear();

            // A hot cue set on a stopped deck moves nothing the screen used to
            // watch for, and its flag never appeared until the deck moved.
            auto& fileDeck = *djView->decks[0];
            booth.djHotCue(0, 1);
            booth.djProcessOffline(512);
            require(booth.djDeckState(0).hotCueSeconds[1] >= 0.0 && !booth.djDeckState(0).isPlaying(), "A hot cue set on a stopped deck");
            djView->frame();
            const auto wave = djView->getLocalArea(&fileDeck.display, fileDeck.display.waveArea());
            const auto overview = djView->getLocalArea(&fileDeck.display, fileDeck.display.overviewArea());
            require(damage.region.containsRectangle(wave) && damage.region.containsRectangle(overview),
                    "reaches its screen and its overview at the next frame");
            booth.djClearHotCue(0, 1);
            booth.djProcessOffline(512);
            djView->frame();
            djView->timerCallback();
            damage.region.clear();

            // The mixer reads every value the booth holds on its tick: one set
            // without an announcement shows, and repaints its knob alone.
            booth.setDjChannelTrim(0, 6.0f);
            djView->timerCallback();
            auto& trim = djView->mixer.strips[0]->knobs[0];
            require(std::abs(trim.getValue() - 6.0) < 0.001, "The mixer shows a trim set without an announcement");
            require(!damage.region.isEmpty() && damageWithin(inView(trim)), "and repaints that knob and nothing else");
            booth.setDjChannelTrim(0, 0.0f);
            djView->timerCallback();
            damage.region.clear();

            // The tempo readout shows what the tick decided. A paint of
            // another part of the mixer in between used to take the new text
            // up without drawing it, and the readout stuck on the old.
            booth.djPlay(0);
            booth.djProcessOffline(2048);
            djView->timerCallback();
            booth.djSetTempoPercent(0, 4.0f);
            booth.djProcessOffline(2048);
            damage.region.clear();
            {
                auto& mixer = djView->mixer;
                juce::Image canvas(juce::Image::ARGB, mixer.getWidth(), mixer.getHeight(), true, juce::SoftwareImageType());
                juce::Graphics g(canvas);
                g.reduceClipRegion(mixer.strips[0]->meter.getBounds());
                mixer.paintEntireComponent(g, false);
            }
            djView->timerCallback();
            require(damage.region.intersectsRectangle(djView->getLocalArea(&djView->mixer, djView->mixer.bpmArea)),
                    "The tempo readout repaints when the master's tempo moves, whatever was painted in between");
            booth.djSetTempoPercent(0, 0.0f);
            booth.djPause(0);
            booth.djSeek(0, 0.0);
            booth.djProcessOffline(2048);
            for (int frame = 0; frame < 4; ++frame)
                djView->frame();
            djView->timerCallback();
            damage.region.clear();

            // A change the session announces may change a screen's readouts
            // and nothing else: Sync on a standing deck moves no tempo when
            // the deck is the master, and a range that clamps the pitch moves
            // the deck nowhere, so no later frame finds a change to show. The
            // sync took the new readout up as shown without drawing it, and
            // the screen went on saying +0.00% under a lit SYNC key.
            {
                const auto readout = djView->getLocalArea(&fileDeck.display, fileDeck.display.readoutArea());
                require(!booth.djDeckState(0).isPlaying() && !booth.djDeckState(0).synced, "The file deck stands, unsynced");
                booth.djSetSynced(0, true);
                djView->frame();
                require(booth.djDeckState(0).synced && damage.region.containsRectangle(readout),
                        "Sync pressed on a standing deck reaches its readout");
                damage.region.clear();
                booth.djSetSynced(0, false);
                djView->frame();
                require(damage.region.containsRectangle(readout), "and so does Sync let go");
                booth.djSetTempoRange(0, 16);
                booth.djSetTempoPercent(0, 8.0f);
                djView->frame();
                djView->timerCallback();
                damage.region.clear();
                booth.djSetTempoRange(0, 6);
                djView->frame();
                require(std::abs(booth.djDeckState(0).tempoPercent - 6.0f) < 0.001f && damage.region.containsRectangle(readout),
                        "A range that clamps a standing deck's pitch reaches its readout");
                booth.djSetTempoPercent(0, 0.0f);
                booth.djProcessOffline(512);
                djView->frame();
                djView->timerCallback();
                damage.region.clear();
            }

            // A press on the console already focused tells the shell nothing;
            // its name bar, or another console, does.
            int told = 0, toldDeck = -1;
            djView->deckSelected = [&told, &toldDeck](int deck)
            {
                ++told;
                toldDeck = deck;
            };
            djView->focusDeck(0, false);
            told = 0;
            fileDeck.selected();
            require(told == 0, "A key pressed on the focused console tells the shell nothing");
            fileDeck.nameBarPressed();
            require(told == 1 && toldDeck == 0, "Its name bar asks the shell to follow the deck again");
            trackDeck.selected();
            require(told == 2 && toldDeck == 1 && djView->selectedDeck() == 1 && trackDeck.isFocused(),
                    "Another console takes the focus and tells the shell");
            trackDeck.selected();
            require(told == 2, "and pressed again tells it nothing");
            // The console routes a press by where it lands.
            trackDeck.mouseDown(mouseAt(trackDeck, {120.0f, 8.0f}, {120.0f, 8.0f}, 1));
            require(told == 3, "A press on the name bar reaches the shell");
            trackDeck.mouseDown(mouseAt(trackDeck, {60.0f, 400.0f}, {60.0f, 400.0f}, 2));
            require(told == 3, "and a press below it on the focused console does not");
            // A track loaded on the focused deck hands the pane the new track.
            int followed = -1;
            djView->focusedSourceChanged = [&followed](int deck) { followed = deck; };
            require(booth.loadDjDeckTrack(1, 2).wasOk(), "The focused deck loads another lane");
            djView->sync();
            require(followed == 1, "and the shell is told to follow the deck's new track");
            require(booth.loadDjDeckSlot(1, 0, 0).wasOk(), "The lane's cell with the notes goes back on the deck");
            djView->sync();
            // A cell of the same track does too, so the editor moves to the
            // clip the deck plays now: a press on the focused console tells
            // the shell nothing, and the cell switched there left the editor
            // on the clip before it.
            followed = -1;
            require(booth.createSlotClip(0, 2).wasOk() && booth.loadDjDeckSlot(1, 0, 2).wasOk(),
                    "The focused deck plays another cell of its track");
            djView->sync();
            require(followed == 1 && booth.djDeckEditClip(1) == booth.slotClip(0, 2).clipID,
                    "and the shell is told to follow the deck to the cell's clip");
            followed = -1;
            require(booth.loadDjDeckSlot(1, 0, 0).wasOk(), "The deck goes back to its first cell");
            djView->sync();
            require(followed == 1, "and the shell follows it back");
            require(booth.deleteSlotClip(0, 2).wasOk(), "The cell is cleared again");
            djView->sync();
            djView->deckSelected = nullptr;
            djView->focusedSourceChanged = nullptr;
            djView->status = nullptr;
            djView->setCachedComponentImage(nullptr);
        }
