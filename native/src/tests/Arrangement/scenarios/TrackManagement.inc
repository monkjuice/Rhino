        require(session.addAudioTrack().wasOk(), "Can create an audio track");
        require(session.trackCount() == 5 && session.trackName(4) == "Audio",
                "New audio track is visible in the arrangement model, named for where it sits");
        view.sync();
        view.resized();
        require(view.trackScrollBar.isVisible(), "Adding tracks makes the arrangement lanes vertically scrollable");
        const auto renderClipRms = [&session, &wav, &require](te::Clip& clip)
        {
            juce::TemporaryFile rendered(".wav");
            te::Renderer::Parameters parameters(*session.edit);
            parameters.destFile = rendered.getFile();
            parameters.audioFormat = &wav;
            parameters.sampleRateForAudio = 48000;
            parameters.bitDepth = 24;
            parameters.time = clip.getPosition().time;
            {
                te::Renderer::RenderTask task("Moved audio test", parameters, nullptr, nullptr);
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                    require(juce::Time::getMillisecondCounterHiRes() < deadline, "Moved render timeout");
                require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
            require(reader != nullptr, "Read moved render");
            juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
            require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read moved render samples");
            return output.getRMSLevel(0, 0, output.getNumSamples());
        };
        // Cards are the only handle on a track's row: its bottom edge sizes the
        // row, its body carries the track, and a right-click colours it. The
        // panel is given a workable height first, because every one of these
        // reads a lane position back.
        view.setSize(1000, 620);
        view.sync();
        const auto cardEvent = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                       juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                mods, 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto dragCard = [&view, &cardEvent](juce::Point<float> down, juce::Point<float> to)
        {
            view.mouseDown(cardEvent(down, down, false));
            view.mouseDrag(cardEvent(down, to, true));
            view.mouseUp(cardEvent(down, to, true));
        };

        scenario("track cards: resizing from the bottom edge");
        require(session.trackLaneHeight(0) == 0.0f, "A track starts with no height of its own");
        const auto fittedHeight = view.lane(0).getHeight();
        const auto firstEdge = juce::Point<float>(90.0f, view.lane(0).getBottom());
        dragCard(firstEdge, firstEdge.translated(0.0f, 40.0f));
        require(view.lane(0).getHeight() > fittedHeight + 30.0f, "Dragging a card edge down makes that row taller");
        require(std::abs(session.trackLaneHeight(0) - view.lane(0).getHeight()) < 0.001f,
                "The dragged height is the one the session keeps");
        require(view.lane(1).getY() >= view.lane(0).getBottom() - 0.001f,
                "The rows below are pushed down rather than overlapped");
        require(std::abs(view.lane(1).getHeight() - fittedHeight) < 0.001f,
                "A neighbour that was never dragged keeps the fitted height");

        const auto tallEdge = juce::Point<float>(90.0f, view.lane(0).getBottom());
        dragCard(tallEdge, tallEdge.translated(0.0f, -400.0f));
        require(std::abs(view.lane(0).getHeight() - view.minimumLaneHeight) < 0.001f,
                "Dragging far up stops at the minimum row height rather than collapsing the row");
        require(!view.volume[0]->isVisible() && view.mute[0]->isVisible() && view.solo[0]->isVisible(),
                "A row at its minimum keeps mute and solo and gives up the mixer line");
        const auto shortEdge = juce::Point<float>(90.0f, view.lane(0).getBottom());
        dragCard(shortEdge, shortEdge.translated(0.0f, 60.0f));
        require(view.volume[0]->isVisible() && view.pan[0]->isVisible(),
                "Growing the row past the mixer height brings the volume and pan line back");

        scenario("track cards: carrying a track to a new place");
        const auto movedName = session.trackName(0);
        const auto displacedName = session.trackName(1);
        require(movedName != displacedName, "The two tracks are told apart by name");
        const auto cardBody = juce::Point<float>(90.0f, view.lane(0).getY() + 10.0f);
        dragCard(cardBody, juce::Point<float>(90.0f, view.lane(1).getCentreY() + 6.0f));
        require(session.trackName(0) == displacedName && session.trackName(1) == movedName,
                "Dragging a card body past its neighbour swaps the two tracks");
        require(view.selectedTrack == 1, "The carried track stays selected where it landed");
        require(std::abs(session.trackLaneHeight(1) - view.lane(1).getHeight()) < 0.001f,
                "A moved track carries its row height with it");
        const auto backBody = juce::Point<float>(90.0f, view.lane(1).getY() + 10.0f);
        dragCard(backBody, juce::Point<float>(90.0f, view.lane(0).getCentreY() - 6.0f));
        require(session.trackName(0) == movedName, "The track can be carried back");

        const auto beforeClick = session.trackName(0);
        const auto restingCard = juce::Point<float>(90.0f, view.lane(0).getY() + 10.0f);
        view.mouseDown(cardEvent(restingCard, restingCard, false));
        view.mouseUp(cardEvent(restingCard, restingCard, false));
        require(session.trackName(0) == beforeClick && view.selectedTrack == 0,
                "A click on a card selects it without reordering anything");

        // A card click is what the shell opens the Device View on, so it has to
        // be told about every one of them - including the click on the card
        // already selected, which is how the view is asked for again after it
        // has been closed, and which selectTrack reports as nothing at all.
        scenario("track cards: a card click asks for that track's devices");
        {
            int focusedTrack = -1, focusedCount = 0, movedCount = 0;
            view.trackFocused = [&focusedTrack, &focusedCount](int track)
            {
                focusedTrack = track;
                ++focusedCount;
            };
            view.trackSelected = [&movedCount](int) { ++movedCount; };
            const auto clickCard = [&view, &cardEvent](juce::Point<float> at)
            {
                view.mouseDown(cardEvent(at, at, false));
                view.mouseUp(cardEvent(at, at, false));
            };
            const auto secondCard = juce::Point<float>(90.0f, view.lane(1).getY() + 10.0f);
            clickCard(secondCard);
            require(focusedCount == 1 && focusedTrack == 1, "Clicking a card reports it as focused");
            require(movedCount == 1, "and reports the working track moving with it");
            clickCard(secondCard);
            require(focusedCount == 2 && focusedTrack == 1,
                    "Clicking the card already selected reports it again");
            require(movedCount == 1, "though the working track did not move");
            const auto mainRow = juce::Point<float>(90.0f, view.masterLane().getCentreY());
            clickCard(mainRow);
            require(focusedCount == 3 && focusedTrack == session.masterTrackIndex(),
                    "The main row is a card like any other: it asks for its own effect chain");
            // Grabbing a row's bottom edge is a resize. It lands on the card
            // like a click does, and means something else entirely.
            focusedCount = 0;
            const auto rowEdge = juce::Point<float>(90.0f, view.lane(1).getBottom());
            dragCard(rowEdge, rowEdge.translated(0.0f, 18.0f));
            require(view.lane(1).getHeight() > 0.0f, "Dragging a row's edge resizes it");
            require(focusedCount == 0, "and asks for no devices on the way");
            require(session.setTrackLaneHeight(1, 0.0f).wasOk(), "The resized row is handed back");
            view.sync();
            view.resized();
            // A clip click carries the working track with it, which is not a
            // request for devices - it is a request for that clip's editor.
            view.sync();
            view.fit();
            clickCard(restingCard);
            const auto span = session.audioClipMix(id);
            const auto onClip = juce::Point<float>(view.xFor((span.startSeconds + span.endSeconds) * 0.5),
                                                   clipRowY(1));
            focusedCount = 0;
            movedCount = 0;
            view.mouseDown(cardEvent(onClip, onClip, false));
            view.mouseUp(cardEvent(onClip, onClip, false));
            require(view.selectedClipID() == id, "Clicking a clip selects it");
            require(movedCount == 1, "and carries the working track to the clip's own");
            require(focusedCount == 0, "but asks for no devices: a clip click is for the clip's editor");
            view.trackFocused = nullptr;
            view.trackSelected = nullptr;
        }

        scenario("track cards: colour and reset");
        const auto colour = Session::trackColourPalette().back();
        require(!session.trackColour(0).isTransparent(), "A track is created with a colour from the palette");
        require(session.trackColour(0) != colour, "and the one this check picks is not the one it was given");
        require(session.setTrackColour(0, colour).wasOk(), "A card takes a colour from the palette");
        require(session.trackColour(0) == colour, "The card keeps the colour it was given");
        require(session.setTrackColour(session.masterTrackIndex(), colour).failed(),
                "The main row takes no colour");
        {
            // Both settings live on the track, so a saved document carries them
            // without the edit being swapped out from under the rest of the run.
            const std::function<juce::ValueTree(const juce::ValueTree&)> findCard =
                [&findCard](const juce::ValueTree& tree) -> juce::ValueTree
            {
                if (tree.hasProperty("rhinoLaneHeight")) return tree;
                for (int i = 0; i < tree.getNumChildren(); ++i)
                    if (const auto found = findCard(tree.getChild(i)); found.isValid()) return found;
                return {};
            };
            const auto card = findCard(session.projectSnapshot());
            require(card.isValid(), "The dragged row height is written into the saved project");
            require(static_cast<double>(card.getProperty("rhinoLaneHeight")) > 0.0,
                    "The saved height is the one the card was dragged to");
            require(card.getProperty("colour").toString() == colour.toString(),
                    "The card colour is written into the saved project beside it");
        }
        require(session.setTrackColour(0, {}).wasOk(), "A card can be cleared back to no colour");
        require(session.setTrackLaneHeight(0, 0.0f).wasOk(), "A row can be handed back to the fitted height");
        view.sync();
        view.resized();
        require(std::abs(view.lane(0).getHeight() - view.laneHeight()) < 0.001f,
                "A reset row follows the panel again");

        scenario("track cards: a new card arrives complete");
        {
            const auto before = session.trackCount();
            require(session.addAudioTrack().wasOk(), "Can add a track while the arrangement is open");
            const auto card = static_cast<size_t>(before);
            require(view.mute.size() == card + 1, "The new track brings its own controls");
            // No resize between adding the track and looking: a card that only
            // fills in on the next layout pass is the bug this covers.
            require(!view.mute[card]->getBounds().isEmpty() && !view.volume[card]->getBounds().isEmpty(),
                    "A new card is placed as soon as its track exists");
            require(view.mute[card]->getRight() <= static_cast<int>(view.cardControlsWidth)
                        && view.arm[card]->getRight() <= static_cast<int>(view.cardControlsWidth)
                        && view.volume[card]->getRight() <= static_cast<int>(view.cardControlsWidth)
                        && view.pan[card]->getRight() <= static_cast<int>(view.cardControlsWidth),
                    "Every control stays in the controls column, clear of the name");
            require(view.volume[card]->getWidth() == view.pan[card]->getWidth(),
                    "The two faders are the same width");
            require(view.mute[card]->getWidth() == view.solo[card]->getWidth()
                        && view.solo[card]->getWidth() == view.arm[card]->getWidth(),
                    "and the three buttons are the same width as each other");
            require(view.mute[card]->getX() == view.volume[card]->getX(),
                    "The button line and the fader line share a left edge");
            require(view.mute[card]->getRight() <= view.solo[card]->getX()
                        && view.solo[card]->getRight() <= view.arm[card]->getX(),
                    "Mute, solo and the record dot run left to right without overlapping");
            require(view.mute[card]->getBottom() <= view.volume[card]->getY()
                        && view.mute[card]->getY() == view.solo[card]->getY()
                        && view.solo[card]->getY() == view.arm[card]->getY(),
                    "and all three share the line above the faders");
            require(session.removeAudioTrack(before).wasOk(), "The added card can be removed again");
            view.sync();
        }

        scenario("track cards: renaming");
        {
            const auto original = session.trackName(0);
            require(session.setTrackName(0, "Bassline").wasOk(), "A card can be renamed");
            require(session.trackName(0) == "Bassline", "The card takes the name it was given");
            require(session.setTrackName(0, "   ").failed(), "A blank name is refused");
            require(session.setTrackName(session.masterTrackIndex(), "Nope").failed(),
                    "The main row keeps its name");
            require(session.setTrackName(0, original).wasOk(), "The card can be named back");
        }

        scenario("track cards: the main row is one line");
        require(view.masterLane().getHeight() <= view.defaultMasterLaneHeight,
                "The main row is a single line tall");
        require(view.masterVolume.getY() >= view.masterLane().getY()
                    && view.masterVolume.getBottom() <= view.masterLane().getBottom()
                    && view.masterPan.getBottom() <= view.masterLane().getBottom(),
                "The main level and pan sit on that line beside its name");
        require(view.masterVolume.getX() >= 50, "The main controls start clear of the MAIN label");

        // The main row is pinned at the bottom, so it is dragged from the top
        // edge rather than the bottom one every other row uses.
        scenario("track cards: the main row resizes from its top edge");
        {
            const auto laneBefore = view.lane(0).getHeight();
            const auto mainEdge = juce::Point<float>(300.0f, view.masterLane().getY());
            dragCard(mainEdge, mainEdge.translated(0.0f, -40.0f));
            require(view.masterLane().getHeight() > view.defaultMasterLaneHeight + 30.0f,
                    "Dragging the main row's top edge up makes it taller");
            require(std::abs(view.lane(0).getHeight() - laneBefore) < 0.001f,
                    "A taller main row does not resize the lanes above it");
            require(view.masterLane().getBottom() <= view.getHeight(),
                    "The main row stays inside the panel");
            const auto tallMainEdge = juce::Point<float>(300.0f, view.masterLane().getY());
            dragCard(tallMainEdge, tallMainEdge.translated(0.0f, 400.0f));
            require(std::abs(view.masterLane().getHeight() - view.minimumMasterLaneHeight) < 0.001f,
                    "Dragging it back down stops at the one line it needs");
            const auto shortMainEdge = juce::Point<float>(300.0f, view.masterLane().getY());
            dragCard(shortMainEdge, shortMainEdge.translated(0.0f, view.minimumMasterLaneHeight - view.defaultMasterLaneHeight));
            require(std::abs(view.masterLane().getHeight() - view.defaultMasterLaneHeight) < 0.001f,
                    "And back up to the height it opens at");
        }

        // The clip and device panes float over the foot of the panel rather
        // than pushing it up, so the inset moves the main row and leaves every
        // lane exactly where it was.
        scenario("track cards: a covered foot moves the main row, not the lanes");
        {
            const auto laneBefore = view.lane(0).getHeight();
            const auto mainBefore = view.masterLane().getY();
            view.setBottomInset(180.0f);
            require(std::abs(view.lane(0).getHeight() - laneBefore) < 0.001f,
                    "Covering the foot of the panel leaves the lane heights alone");
            require(view.masterLane().getY() <= mainBefore - 179.0f,
                    "The main row rides up to sit above what covers it");
            require(view.masterLane().getBottom() <= view.getHeight() - 180.0f + 0.001f,
                    "The main row keeps clear of the covered strip");
            view.setBottomInset(0.0f);
            require(std::abs(view.masterLane().getY() - mainBefore) < 0.001f,
                    "Uncovering it puts the main row back");
        }
        // The main row owns everything from its top edge to the foot of the
        // panel, the strip its scrollbar sits in included, and fills all of it.
        // A lane row reaching past the bottom of the lanes used to paint its
        // card and its colour on down through that strip, which is visible
        // only in a render: there is no component to assert against.
        scenario("track cards: the main row covers what the lanes run past");
        {
            const auto restore = session.trackLaneHeight(0);
            require(session.setTrackLaneHeight(0, 240.0f).wasOk(), "A row can be dragged past the lanes");
            view.setBottomInset(180.0f);
            view.sync();
            const auto shot = view.createComponentSnapshot(view.getLocalBounds());
            // The row is ruled off from the panel's foot - the times, the
            // scrollbar - and what is under that rule is the foot's own
            // ground. Colours are named rather than spelt as hex values, so
            // re-toning the chrome cannot quietly turn this into a test of a
            // colour nobody uses.
            const auto rule = static_cast<int>(view.masterLane().getBottom()) + 1;
            require(rule < view.getHeight() - 180, "The rule under the main row is inside the panel");
            require(shot.getPixelAt(160, rule) == palette::border,
                    "The main row is ruled off from the time ruler under it");
            require(shot.getPixelAt(160, rule + 6) == palette::appBackground,
                    "And under that rule is the foot, not the lane that ran past the stack");
            // Clear of the MAIN label and both of its faders, which is the only
            // part of the row's header column that is bare fill.
            require(shot.getPixelAt(210, static_cast<int>(view.masterLane().getY()) + 10) == palette::sideSurface,
                    "And so is the row itself");
            require(shot.getPixelAt(160, view.getHeight() - 90) == palette::appBackground,
                    "Below the main row the panel leaves the covered strip to the shell");
            view.setBottomInset(0.0f);
            require(session.setTrackLaneHeight(0, restore).wasOk(), "The row is put back as it was");
            view.sync();
        }

        scenario("track management: duplicate move");
        view.setSelection({id}, id);
        view.duplicateSelected();
        auto* movedDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto movedDuplicateID = movedDuplicate->itemID;
        view.sync();
        view.setSelection({movedDuplicateID}, movedDuplicateID);
        drag({view.xFor(movedDuplicate->getPosition().time.getStart().inSeconds() + 0.1), clipRowY(1)},
             {view.xFor(0.5), clipRowY(2)});
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(movedDuplicateID) != nullptr,
                "Dragging a duplicated audio clip to a newly-created track preserves clip identity");
        require(renderClipRms(*session.findAudioClip(movedDuplicateID)) > 0.01f,
                "Duplicated audio moved to a newly-created track still renders audible audio");
        session.undo();
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr,
                "Undo restores the original audio clip after duplicate move regression");
        view.sync();
        scenario("track management: cross-track move");
        view.setSelection({id}, id);
        drag({view.xFor(0.2), clipRowY(1)}, {view.xFor(0.5), clipRowY(2)});
        require(te::getAudioTracks(*session.edit)[1]->getClips().isEmpty(), "Dragging an audio clip to another lane removes it from the source track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) != nullptr, "Dragging an audio clip to another lane preserves clip identity on the target track");
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25), "Cross-track audio drag also updates the clip time");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f, "Cross-track moved clip still renders audible audio");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip to its original track");
        require(te::getAudioTracks(*session.edit)[2]->findClipForID(id) == nullptr, "Undo removes the clip from the drag target track");
        view.sync();
        scenario("track management: instrument-lane move");
        session.applyPatternPreset(Session::PatternPreset::WavePluck);
        view.sync();
        view.setSelection({id}, id);
        drag({view.xFor(0.2), clipRowY(1)}, {view.xFor(0.5), clipRowY(0)});
        require(te::getAudioTracks(*session.edit)[0]->findClipForID(id) != nullptr,
                "Dragging an audio clip onto an instrument lane moves it there");
        require(renderClipRms(*session.findAudioClip(id)) > 0.01f,
                "Audio moved onto a Rhino Wave instrument lane still renders audible audio");
        session.undo();
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr,
                "Undo restores audio after an instrument-lane move");
        view.sync();
        scenario("track management: move during playback");
        session.togglePlayback();
        require(session.edit->getTransport().isPlaying(), "Transport starts before cross-track clip drag");
        drag({view.xFor(0.2), clipRowY(1)}, {view.xFor(0.5), clipRowY(2)});
        require(session.edit->getTransport().isPlaying(), "Cross-track clip drag keeps playback running");
        session.stop();
        session.undo();
        view.sync();
        scenario("track management: drop-created track");
        {
            // The bare strip between the last lane and the main row, whatever
            // the stack is: reading it off the panel rather than counting
            // lanes is what keeps this working as the starter stack grows.
            const auto tracksBeforeDrop = session.trackCount();
            const auto lastLaneBottom = view.lane(tracksBeforeDrop - 1).getBottom();
            require(lastLaneBottom < view.masterLane().getY(),
                    "There is bare lane strip under the last track to drop into");
            const auto belowLanes = (lastLaneBottom + view.masterLane().getY()) * 0.5f;
            drag({view.xFor(0.2), clipRowY(1)}, {view.xFor(0.5), belowLanes});
            require(session.trackCount() == tracksBeforeDrop + 1,
                    "Dragging an audio clip below the last lane creates a new track");
            require(te::getAudioTracks(*session.edit)[tracksBeforeDrop]->findClipForID(id) != nullptr,
                    "Drop-created track receives the dragged audio clip");
            require(renderClipRms(*session.findAudioClip(id)) > 0.01f,
                    "Drop-created track moved clip still renders audible audio");
            session.undo();
            require(session.trackCount() == tracksBeforeDrop, "Undo removes the track created by a clip drag");
        }
        require(te::getAudioTracks(*session.edit)[1]->findClipForID(id) != nullptr, "Undo restores the clip after a drag-created track");
        view.sync();
        scenario("track management: preset drops");
        require(view.applyBrowserDrop("rhino-browser:preset:SirenLead", 2, 0.0, true).wasOk(), "Siren lead browser row creates a synth clip");
        auto* droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Siren lead",
                "Dropped siren preset names the new clip");
        session.undo();
        scenario("track management: chord preset");
        require(view.applyBrowserDrop("rhino-browser:preset:ChordPad", 2, 0.0, true).wasOk(), "Chord pad browser row creates a synth clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Chord pad",
                "Dropped chord pad preset names the new clip");
        session.undo();
        scenario("track management: reese preset");
        require(view.applyBrowserDrop("rhino-browser:preset:ReeseBass", 2, 0.0, true).wasOk(), "Reese bass browser row creates a synth clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && droppedPreset->getName() == "Reese bass",
                "Dropped Reese bass preset names the new clip");
        session.undo();
        scenario("track management: clap preset");
        require(view.applyBrowserDrop("rhino-browser:preset:ClapKit", 2, 0.0, true).wasOk(), "Clap kit browser row creates a drum clip");
        droppedPreset = te::getAudioTracks(*session.edit)[2]->getClips().getLast();
        require(droppedPreset != nullptr && session.selectPatternClip(droppedPreset->itemID).wasOk(),
                "Dropped clap preset can be selected for note editing");
        require(session.hasNote(4, 56), "Dropped clap preset selects an editable clip with clap notes");
        session.undo();
        scenario("track management: warm pulse drop");
        const auto audio2Clips = te::getAudioTracks(*session.edit)[2]->getClips().size();
        const auto audio2Devices = session.deviceSlots(2).size();
        view.itemDropped({"rhino-browser:preset:WarmPulse", nullptr,
                          {static_cast<int>(view.xFor(0.5)), static_cast<int>(view.lane(2).getCentreY())}});
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == audio2Clips + 1,
                "Dragging a browser sound onto Audio 2 creates the clip on Audio 2");
        require(session.deviceSlots(2).size() > audio2Devices, "Dropped browser sound adds an instrument to the target track");
        view.sync();
        view.selected = te::getAudioTracks(*session.edit)[2]->getClips().getLast()->itemID;
        require(session.selectPatternClip(view.selected).wasOk(), "Selecting a non-first MIDI clip makes it editable in the note editor");

        // A track now says what it is for when it is created, instead of
        // waiting to find out. The block is self-contained - two tracks at the
        // bottom, both removed again - so the stack is left as it was found.
        scenario("track management: audio or MIDI, chosen when the track is made");
        {
            const auto before = session.trackCount();
            // Making a clip below also makes it the one the note editor is on,
            // and removing its track hands that job to whichever clip is left.
            // The scenarios after this one read the editor, so the clip they
            // were left with is put back at the end.
            const auto editedBefore = session.patternClipID;
            require(session.addTrack(Session::TrackType::midi).wasOk(), "A MIDI track can be added");
            const auto midiTrack = session.trackCount() - 1;
            require(session.trackName(midiTrack) == "MIDI", "and is named for what it is");
            require(session.trackType(midiTrack) == Session::TrackType::midi,
                    "The track reads back as a MIDI track");
            require(!session.trackHasInstrument(midiTrack),
                    "though nothing has been dropped on it yet");

            // The whole point of declaring it: a clip before an instrument.
            te::EditItemID onEmptyMidi;
            require(session.createClip(midiTrack, 0.0, &onEmptyMidi).wasOk(),
                    "A MIDI track holds a clip before it runs anything");
            require(session.importAudioAt(source.getFile(), midiTrack, 4.0).failed(),
                    "and refuses audio, the way an instrument track does");
            require(session.trackRecordInput(midiTrack) == Session::RecordInput::midi,
                    "Arming it records MIDI rather than the audio input");

            require(session.addTrack(Session::TrackType::audio).wasOk(), "An audio track can be added");
            const auto audioTrack = session.trackCount() - 1;
            require(session.trackName(audioTrack) == "Audio", "and is named for what it is");
            require(session.trackType(audioTrack) == Session::TrackType::audio,
                    "The track reads back as an audio track");
            require(session.createClip(audioTrack, 0.0).failed(),
                    "An audio track takes no MIDI clip");
            require(session.importAudioAt(source.getFile(), audioTrack, 0.0).wasOk(),
                    "but takes audio");
            require(session.trackRecordInput(audioTrack) == Session::RecordInput::audio,
                    "and records the audio input");

            // The kind travels with the document, so reopening does not hand
            // the MIDI track back as an audio one.
            {
                const std::function<bool(const juce::ValueTree&)> holdsMidiType =
                    [&holdsMidiType](const juce::ValueTree& tree) -> bool
                {
                    if (tree.getProperty("rhinoTrackType").toString() == "midi") return true;
                    for (int i = 0; i < tree.getNumChildren(); ++i)
                        if (holdsMidiType(tree.getChild(i))) return true;
                    return false;
                };
                require(holdsMidiType(session.projectSnapshot()),
                        "The MIDI track writes its kind into the saved project");
            }

            // Ctrl+T repeats the last choice from the menu. A test run never
            // reads a developer's settings, so what it sees is the default the
            // shortcut starts on.
            require(Session::lastAddedTrackType() == Session::TrackType::midi,
                    "Ctrl+T adds a MIDI track until the menu is used to say otherwise");

            require(session.removeAudioTrack(audioTrack).wasOk(), "The audio track can be removed again");
            require(session.removeAudioTrack(midiTrack).wasOk(), "and so can the MIDI track");
            require(session.trackCount() == before, "leaving the stack as it was found");
            require(session.selectPatternClip(editedBefore).wasOk(),
                    "and the note editor back on the clip it was editing");
            view.sync();
        }
