        // A track's clip slots are the cells of a DJ console's clips grid.
        // Every track has one per scene, a cell holds what its lane may hold
        // and wears its track's colour, a pattern dropped on the cell the
        // note editor has open leaves the editor on a clip that exists, the
        // cells survive a reopen, an edit reaches the deck playing that cell
        // and no other, and a drop on the grid adds a row only when something
        // lands in it. A document of its own, so nothing here depends on what
        // the scenarios before it left in their cells.
        {
            auto document = std::make_unique<Session>();
            auto& cells = *document;
            require(cells.sceneCount() == Session::defaultScenes, "A new document starts with a full set of scenes");
            // A return holds no clips, so it has no cells to offer.
            for (int track = 0; track < cells.trackCount(); ++track)
                require((cells.clipSlotAt(track, cells.sceneCount() - 1) != nullptr) != cells.isReturnTrack(track),
                        "Every track that plays has a cell for every scene");
            // The starter stack holds both kinds: MIDI, Audio, MIDI, Audio.
            auto midiTrack = -1, audioTrack = -1, otherMidiTrack = -1;
            for (int track = 0; track < cells.trackCount(); ++track)
            {
                if (cells.trackType(track) == Session::TrackType::audio)
                {
                    if (audioTrack < 0) audioTrack = track;
                }
                else if (midiTrack < 0) midiTrack = track;
                else if (otherMidiTrack < 0) otherMidiTrack = track;
            }
            require(midiTrack >= 0 && audioTrack >= 0 && otherMidiTrack >= 0, "Two MIDI lanes and an audio lane");
            require(!cells.slotClip(midiTrack, 0).hasClip && !cells.slotClip(audioTrack, 0).hasClip, "Cells start empty");

            scenario("clip cells: a pattern in a cell");
            require(cells.insertPatternPresetInSlot(Session::PatternPreset::WarmPulse, midiTrack, 0).wasOk(),
                    "A pattern preset goes in a MIDI track's cell");
            const auto filled = cells.slotClip(midiTrack, 0);
            require(filled.hasClip && filled.isMidi, "The cell reports the MIDI clip it now holds");
            require(filled.name.isNotEmpty(), "named for the preset");
            {
                auto* slot = cells.clipSlotAt(midiTrack, 0);
                require(slot != nullptr && slot->getClip() != nullptr, "The engine holds the cell's clip");
                require(slot->getClip()->isLooping(), "A cell's clip loops, as a deck plays it");
                auto* midi = dynamic_cast<te::MidiClip*>(slot->getClip());
                require(midi != nullptr && midi->getSequence().getNumNotes() > 0, "and carries the preset's notes");
            }
            require(cells.insertPatternPresetInSlot(Session::PatternPreset::HouseKit, audioTrack, 1).failed()
                        && !cells.slotClip(audioTrack, 1).hasClip,
                    "An audio track's cell refuses a pattern");
            require(cells.deleteSlotClip(midiTrack, 1).failed(), "Deleting from an empty cell reports a problem");

            scenario("clip cells: a cell's clip wears its track's colour");
            // The clip-colour rule, as on the timeline: no colour of its own
            // until C gives it one, and the engine's made-up colour is never
            // what a cell reports.
            require(filled.colour.isTransparent(), "A cell's clip has no colour of its own, so it wears its track's");
            require(cells.cycleClipColour(filled.clipID).wasOk(), "C colours it");
            const auto own = cells.slotClip(midiTrack, 0).colour;
            require(!own.isTransparent(), "and the cell reports that colour");
            require(cells.copyClipToTrackSlot(filled.clipID, midiTrack, 2).wasOk() && cells.slotClip(midiTrack, 2).colour == own,
                    "which a copy into another cell keeps");

            scenario("clip cells: a pattern dropped on the cell the note editor has open");
            {
                // A console opens a cell's clip in the note editor, and a
                // pattern dropped on that cell takes the clip's place, so the
                // editor must let go of it as it does for a delete. Read by id
                // as well: the new clip can take the freed one's address, and
                // a check through pattern() alone would pass on that.
                const auto opened = cells.slotClip(midiTrack, 2).clipID;
                require(cells.selectPatternClip(opened).wasOk(), "The note editor opens a cell's clip");
                require(cells.insertPatternPresetInSlot(Session::PatternPreset::WarmPulse, midiTrack, 2).wasOk()
                            && cells.slotClip(midiTrack, 2).hasClip && cells.slotClip(midiTrack, 2).clipID != opened,
                        "A pattern dropped on that cell takes the clip's place");
                const auto* editing = cells.findClip(cells.patternClipID);
                require(cells.hasPatternClip() && editing != nullptr && editing == &cells.pattern(),
                        "and leaves the editor on a clip that still exists");
            }

            // An audio cell's sound is read on the worker; the poll installs it.
            const auto waitForDeck = [&cells](int deck)
            {
                for (int waited = 0; waited < 800 && cells.djDeckInfo(deck).loading; ++waited)
                {
                    juce::Thread::sleep(25);
                    cells.djPoll();
                }
                return cells.djDeckInfo(deck).loaded && !cells.djDeckInfo(deck).loading;
            };
            const auto deckState = [&cells](int deck)
            {
                const auto info = cells.djDeckInfo(deck);
                return " deck " + juce::String(deck) + (info.loaded ? " loaded" : " empty") + (info.loading ? ", loading" : "")
                     + (info.error.isNotEmpty() ? ": " + info.error : juce::String()) + ";";
            };

            scenario("clip cells: an edit reaches the deck playing that cell alone");
            {
                // A deck on a cell of each of two tracks. A deck's clip is made
                // again when the cell it plays changes, and for nothing else: a
                // cell beside it, a cell of another track and the arrangement's
                // clips leave it as it is, its material and its place.
                require(cells.insertAudioFileInSlot(source.getFile(), audioTrack, 0).wasOk(), "A sample in the audio track's cell");
                require(cells.addDjDeck().wasOk() && cells.addDjDeck().wasOk(), "Two decks");
                require(cells.loadDjDeckSlot(0, midiTrack, 0).wasOk() && cells.loadDjDeckSlot(1, audioTrack, 0).wasOk(),
                        "each on a cell of its own track");
                const auto firstLanded = waitForDeck(0);
                const auto secondLanded = waitForDeck(1);
                require(firstLanded && secondLanded, ("and both have them:" + deckState(0) + deckState(1)).toRawUTF8());
                const auto first = cells.djDeckInfo(0).generation, second = cells.djDeckInfo(1).generation;
                const auto unchanged = [&cells, &first, &second] { return cells.djDeckInfo(0).generation == first && cells.djDeckInfo(1).generation == second; };
                require(cells.createSlotClip(midiTrack, 1).wasOk(), "A new clip in another cell of the first deck's track");
                require(cells.copyClipToTrackSlot(cells.slotClip(midiTrack, 1).clipID, midiTrack, 3).wasOk()
                            && cells.deleteSlotClip(midiTrack, 3).wasOk()
                            && cells.insertPatternPresetInSlot(Session::PatternPreset::AcidSteps, midiTrack, 4).wasOk(),
                        "a cell of the track copied into, deleted and given a pattern");
                require(!cells.slotClip(midiTrack, 3).hasClip, "and the deleted cell reads empty");
                require(cells.createClip(midiTrack, 0.0).wasOk(), "and a clip on the track's timeline");
                cells.djPoll();
                require(unchanged(), "leave both decks as they were");
                require(cells.selectPatternClip(cells.slotClip(midiTrack, 0).clipID).wasOk()
                            && cells.addNote(2.0, 62, 1.0).wasOk(),
                        "A note added to the cell the first deck plays");
                cells.djPoll();
                require(cells.djDeckInfo(0).generation > first && cells.djDeckInfo(1).generation == second,
                        "makes the first deck's clip again, and the second's not");
                const auto remade = cells.djDeckInfo(0).generation;
                require(cells.copySlotClipToArrangement(midiTrack, 1, 0.0, otherMidiTrack).wasOk(),
                        "A cell's clip copied to a third lane's timeline");
                cells.djPoll();
                require(cells.djDeckInfo(0).generation == remade && cells.djDeckInfo(1).generation == second,
                        "leaves both decks as they were");
            }

            scenario("clip cells: a drop on a console adds a row only when something lands");
            {
                auto console = std::make_unique<DjClipGrid>(cells, 0);
                console->setLookAndFeel(&theme);
                console->setSize(120, 400);
                console->sync();
                require(console->hasTrack() && console->trackIndex() == midiTrack, "The first console's grid shows its track's cells");
                // A cell with no colour of its own is painted in its track's:
                // the second cell's clip was made by a double-click and never
                // coloured.
                {
                    const auto picture = console->createComponentSnapshot(console->getLocalBounds());
                    const auto cell = console->cellBounds(1);
                    const auto pixel = picture.getPixelAt(cell.getRight() - 3, cell.getCentreY());
                    const auto hueGap = std::abs(pixel.getHue() - cells.trackColour(midiTrack).getHue());
                    require(pixel.getAlpha() == 255 && pixel.getSaturation() > 0.3f && std::min(hueGap, 1.0f - hueGap) < 0.03f,
                            "A cell's clip with no colour of its own is painted in its track's");
                }
                for (int scene = 0; scene < cells.sceneCount(); ++scene)
                    if (!cells.slotClip(midiTrack, scene).hasClip)
                        require(cells.createSlotClip(midiTrack, scene).wasOk(), "Every cell of the track is filled");
                const auto full = cells.sceneCount();
                require(cells.firstFreeSlot(midiTrack) < 0, "with none left free");
                require(console->applyDrop(DjClipGrid::firstFree, "rhino-browser:instrument:FourOsc", false).wasOk()
                            && cells.sceneCount() == full,
                        "An instrument dropped on a full grid joins the chain and adds no row");
                require(console->applyDrop(full, "rhino-browser:file:" + source.getFile().getFullPathName(), false).failed()
                            && cells.sceneCount() == full,
                        "A sample on a MIDI track's + row is refused and adds no row");
                require(console->applyDrop(full, "rhino-browser:preset:AcidSteps", false).wasOk()
                            && cells.sceneCount() == full + 1 && cells.slotClip(midiTrack, full).hasClip,
                        "A pattern on the + row makes the row it lands in");
                for (int track = 0; track < cells.firstReturnTrack(); ++track)
                    require(cells.clipSlotAt(track, full) != nullptr, "The new row reaches every track");
                cells.undo();
                require(cells.sceneCount() == full && !cells.slotClip(midiTrack, full).hasClip,
                        "and one undo takes the row and the clip away together");
                console->setLookAndFeel(nullptr);
            }

            // Last, because the reopen gives the document a file it never
            // writes: a clip imported after it would be stored relative to a
            // file that is not there, which resolves one folder too high
            // (wiki/pages/renders-that-never-return.md).
            scenario("clip cells: the cells survive a reopen");
            {
                const auto saved = cells.projectSnapshot();
                juce::TemporaryFile project(".rhinoedit");
                require(cells.restoreProject(saved, project.getFile()).wasOk(), "Reopen a document with filled cells");
            }
            require(cells.sceneCount() == Session::defaultScenes, "The scenes survive a reopen");
            const auto restored = cells.slotClip(midiTrack, 0);
            require(restored.hasClip && restored.isMidi && restored.name == filled.name && restored.colour == own,
                    "and so does the cell's clip, by name and colour");
            cells.releaseAudioDevice();
        }
