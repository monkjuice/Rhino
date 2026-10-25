        // The note-velocity gesture the lane's drags run inside, the lane's
        // divider, panes too short for it, the grid gestures that now pass
        // over it, a drum clip, and no clip at all. Documents of their own.
        {
            struct Count final : juce::ChangeListener
            {
                int heard = 0;
                void changeListenerCallback(juce::ChangeBroadcaster*) override { ++heard; }
            };
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for the lane's layout");
            juce::ValueTree a;
            stack.beginNoteGesture("Layout fixture");
            require(stack.addNote(1.0, 60, 2.0, &a, 100).wasOk(), "One note to edit");
            stack.endNoteGesture();
            Count everyone;
            stack.addChangeListener(&everyone);
            const auto velocityOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.velocity;
                return -1;
            };

            // Inside the gesture nothing is announced and the booth is told a
            // gesture is held; its end announces once, and only for a change.
            stack.beginNoteVelocityGesture();
            for (int step = 0; step < 5; ++step)
                require(stack.setNoteVelocities({{a, step % 2 == 0 ? 90 : 100}}), "A velocity gesture writes");
            require(everyone.heard == 0 && stack.gestureHeld() && velocityOf(a) == 90,
                    "Inside a velocity gesture each write lands at once and nobody is told");
            stack.endNoteVelocityGesture();
            require(everyone.heard == 1 && !stack.gestureHeld(), "and its end tells everyone once");
            stack.undo();
            require(velocityOf(a) == 100, "One undo takes the whole gesture back");
            everyone.heard = 0;
            stack.beginNoteVelocityGesture();
            stack.endNoteVelocityGesture();
            require(everyone.heard == 0, "A gesture that changed nothing announces nothing");
            stack.beginNoteVelocityGesture();
            stack.beginNoteVelocityGesture();
            require(stack.setNoteVelocities({{a, 80}}), "A nested gesture writes");
            stack.endNoteVelocityGesture();
            require(everyone.heard == 0, "Only the outermost end announces");
            stack.endNoteVelocityGesture();
            require(everyone.heard == 1, "and it does");
            stack.endNoteVelocityGesture();
            require(everyone.heard == 1 && !stack.gestureHeld(), "An extra end does nothing");
            stack.undo();
            stack.beginNoteVelocityGesture();
            for (int value = 91; value <= 95; ++value)
                stack.setNoteVelocities({{a, value}});
            stack.endNoteVelocityGesture();
            stack.undo();
            require(velocityOf(a) == 100, "Every step of a gesture undoes as one");

            // Outside a gesture each write is an edit of its own; the caller
            // brackets the undo step, as it does for addNote.
            everyone.heard = 0;
            stack.beginNoteGesture("Set velocity");
            require(stack.setNoteVelocities({{a, 90}}) && everyone.heard == 1, "Outside a gesture a write announces at once");
            require(!stack.setNoteVelocities({{a, 90}}) && everyone.heard == 1, "and a write that changes nothing is no edit");
            require(stack.setNoteVelocities({{a, 0}}) && velocityOf(a) == 1, "Nothing writes a note-off");
            require(stack.setNoteVelocities({{a, 300}}) && velocityOf(a) == 127, "or a value past 127");
            require(!stack.setNoteVelocities({}) && !stack.setNoteVelocities({{juce::ValueTree("NOTE"), 64}}),
                    "An empty list or a note the clip does not hold changes nothing");
            stack.endNoteGesture();
            stack.undo();
            require(velocityOf(a) == 100, "The fixture is back as it was");
            stack.removeChangeListener(&everyone);

            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto laneEvent = [&grid](juce::Point<float> down, juce::Point<float> at, bool dragged,
                                           juce::ModifierKeys mods, int clicks = 1)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        clicks, dragged);
            };
            const auto dragDivider = [&](std::initializer_list<float> offsets)
            {
                const auto from = grid.laneDividerBounds().getCentre();
                grid.mouseDown(laneEvent(from, from, false, left));
                auto last = from;
                for (const auto offset : offsets)
                {
                    last = from.translated(0.0f, offset);
                    grid.mouseDrag(laneEvent(from, last, true, left));
                }
                return std::pair<juce::Point<float>, juce::Point<float>>(from, last);
            };

            // The divider drags the lane taller or shorter, never past the
            // rows' floor, and dragged most of the way down it shuts.
            const auto quiet = stack.changeRevision;
            {
                const auto [from, last] = dragDivider({-30.0f});
                require(grid.velocityLaneHeight() == 94.0f && grid.rowAreaHeight() == 151.0f && stack.changeRevision == quiet,
                        "Dragging the divider up makes the lane taller, and is no edit of the document");
                grid.mouseUp(laneEvent(from, last, true, left));
                require(grid.velocityLaneRequested == 94.0f, "and the lane keeps the height it was left at");
            }
            {
                const auto [from, last] = dragDivider({-200.0f});
                require(grid.velocityLaneHeight() == 117.0f && grid.rowAreaHeight() == 128.0f
                            && grid.rowHeight() == StepGrid::baseRowHeight,
                        "The lane stops at the rows' floor, sixteen rows thin enough to lose their names, and the rows keep their height");
                grid.mouseUp(laneEvent(from, last, true, left));
            }
            {
                const auto from = dragDivider({200.0f}).first;
                require(!grid.isVelocityLaneShown() && grid.rowAreaHeight() == 250.0f,
                        "Dragged most of the way down the lane shuts");
                grid.mouseDrag(laneEvent(from, from.translated(0.0f, 77.0f), true, left));
                require(grid.isVelocityLaneShown() && grid.velocityLaneHeight() == 40.0f,
                        "and dragged back up in the same drag it opens again");
                grid.mouseDrag(laneEvent(from, from.translated(0.0f, 200.0f), true, left));
                grid.mouseUp(laneEvent(from, from.translated(0.0f, 200.0f), true, left));
                require(!grid.isVelocityLaneShown() && grid.velocityLaneRequested == 117.0f
                            && !grid.velocityButton.getToggleState(),
                        "Released shut, the button is off and remembers the height the lane had");
            }
            grid.setVelocityLaneShown(true);
            require(grid.velocityLaneHeight() == 117.0f, "The button brings the lane back at that height");
            {
                const auto centre = grid.laneDividerBounds().getCentre();
                grid.mouseDown(laneEvent(centre, centre, false, left, 2));
                grid.mouseUp(laneEvent(centre, centre, false, left, 2));
                require(grid.velocityLaneHeight() == 64.0f, "Double-clicking the divider puts the default height back");
            }

            // A pane too short for the lane folds it away rather than squeeze
            // the rows, and the button says so by dimming.
            grid.setSize(1000, 202);
            require(grid.velocityLaneHeight() == 0.0f && grid.isVelocityLaneShown() && grid.rowAreaHeight() == 152.0f
                        && grid.velocityButton.getAlpha() < 1.0f && grid.laneDividerBounds().isEmpty(),
                    "A fifth of a 1080p window folds the lane away and dims its button");
            grid.setSize(1000, 219);
            require(grid.velocityLaneHeight() == 36.0f, "A little taller, the lane comes back as tall as there is room for");
            grid.setSize(1000, 300);
            require(grid.velocityLaneHeight() == 64.0f && grid.velocityButton.getAlpha() == 1.0f,
                    "and with room it is its own height again");
            require(StepGrid::openingHeight == 327,
                    "The shell opens the editor tall enough for the lane and sixteen rows of their own height");
            // Zoomed in, a clip wider than the view used to bring a scrollbar
            // over the footer, which took its height from the lane and the
            // rows. The editor shows none now, so the zoom changes nothing here.
            for (const auto zoom : {1.0, 2.0})
            {
                grid.stepZoom = zoom;
                grid.syncHorizontalScroll();
                for (int height = 112; height <= 400; ++height)
                {
                    grid.setSize(1000, height);
                    const auto lane = grid.velocityLaneHeight();
                    require(lane == 0.0f
                                || (lane >= velocitylane::minimumHeight && lane <= velocitylane::maximumHeight
                                    && lane == std::floor(lane) && grid.rowHeight() >= StepGrid::octaveLabelRowHeight
                                    && grid.velocityLaneBounds().getBottom() == static_cast<float>(height) - StepGrid::footerHeight),
                            "At every height and zoom the lane is whole pixels in range, ends at the footer and leaves the rows their names");
                    // The rows stand on the lane, so only the top one is cut
                    // short, by the header's edge.
                    const auto rowsTop = grid.rowsTop();
                    require(grid.rowHeight() == StepGrid::baseRowHeight
                                && grid.visiblePitchRows()
                                       == static_cast<int>(std::ceil(grid.rowAreaHeight() / StepGrid::baseRowHeight - 0.001f))
                                && rowsTop <= StepGrid::headerHeight && rowsTop > StepGrid::headerHeight - grid.rowHeight()
                                && grid.cell(0, grid.visiblePitchRows() - 1).getBottom() == StepGrid::headerHeight + grid.rowAreaHeight(),
                            "and every height keeps the rows' height, showing as many as reach into the rows' area");
                }
            }
            grid.stepZoom = 1.0;
            grid.syncHorizontalScroll();
            grid.setSize(1000, 300);

            // A note carried down into the lane follows on the lowest row while
            // the view scrolls pitch after it; it used to freeze where the
            // pointer left the rows.
            juce::ValueTree g;
            require(stack.addNote(8.0, 53, 1.0, &g, 100).wasOk(), "A note to carry");
            grid.changeListenerCallback(nullptr);
            const auto pitchOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.pitch;
                return -1;
            };
            const auto window = grid.lowestVisiblePitch;
            const auto laneCentre = grid.velocityLaneBounds().getCentreY();
            {
                const auto body = grid.boundsFor(*grid.noteForState(g)).getCentre();
                const juce::Point<float> below {body.x, laneCentre};
                grid.mouseDown(laneEvent(body, body, false, left));
                grid.mouseDrag(laneEvent(body, below, true, left));
                require(pitchOf(g) == window, "A note dragged into the lane lands on the lowest row");
                for (int tick = 0; tick < 5; ++tick)
                    grid.timerCallback();
                require(grid.lowestVisiblePitch < window && pitchOf(g) == grid.lowestVisiblePitch,
                        "and follows the rows down as the view scrolls after it");
                grid.mouseUp(laneEvent(body, below, true, left));
                stack.undo();
                require(pitchOf(g) == 53, "One undo puts it back");
                grid.manualPitchScroll = false;
                grid.changeListenerCallback(nullptr);
            }
            {
                const auto empty = grid.cell(12, 2).getCentre();
                const juce::Point<float> below {empty.x, laneCentre};
                grid.mouseDown(laneEvent(empty, empty, false, left));
                grid.mouseDrag(laneEvent(empty, below, true, left));
                require(grid.selectionBox.getBottom() <= StepGrid::headerHeight + grid.rowAreaHeight(),
                        "A marquee dragged into the lane stays in the rows");
                const auto before = grid.lowestVisiblePitch;
                grid.timerCallback();
                grid.timerCallback();
                grid.timerCallback();
                require(grid.lowestVisiblePitch < before, "and still scrolls pitch down");
                grid.mouseUp(laneEvent(empty, below, true, left));
                grid.manualPitchScroll = false;
                grid.changeListenerCallback(nullptr);
            }
            owned.reset();
            stack.releaseAudioDevice();
        }
        {
            // A drum clip: the pads' markers, and the gutter names the pad.
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk()
                        && stack.addDevice("Drums", 0).wasOk() && stack.isPatternDrums(),
                    "A drum clip of its own");
            const auto low = stack.patternDrumLowestNote();
            juce::ValueTree kick, hat;
            stack.beginNoteGesture("Drum fixture");
            require(stack.addNote(0.0, low, 1.0, &kick, 100).wasOk() && stack.addNote(0.0, low + 6, 1.0, &hat, 100).wasOk(),
                    "A kick and a hat on the same step");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto laneEvent = [&grid](juce::Point<float> down, juce::Point<float> at, bool dragged, juce::ModifierKeys mods)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        1, dragged);
            };
            const auto velocityOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.velocity;
                return -1;
            };
            require(grid.velocityLaneHeight() == 64.0f && grid.laneNoteFor(kick)->inWindow && grid.laneNoteFor(hat)->inWindow,
                    "A drum clip has the lane too, with every pad's marker");
            const juce::Point<float> both {grid.xForStep(0.0), grid.yForVelocity(100.0f)};
            grid.setSelectedStates({hat});
            grid.mouseMove(laneEvent(both, both, false, juce::ModifierKeys()));
            require(grid.hoveredNoteState == hat
                        && grid.velocityGutterText().second
                               == stack.patternNoteName(low + 6).value_or(juce::MidiMessage::getMidiNoteName(low + 6, true, true, 3)),
                    "Of two pads struck together the selected one's marker is taken, and the gutter names its pad");
            grid.mouseDown(laneEvent(both, both, false, left));
            grid.mouseDrag(laneEvent(both, both.translated(0.0f, -4.0f), true, left));
            grid.mouseDrag(laneEvent(both, both.translated(0.0f, -13.0f), true, left));
            require(velocityOf(hat) == 121 && velocityOf(kick) == 100, "and dragging it changes that pad's note alone");
            grid.mouseUp(laneEvent(both, both.translated(0.0f, -13.0f), true, left));
            stack.undo();
            require(velocityOf(hat) == 100, "One undo");

            // The pads' rows keep the rows' height too, and scroll as a piano's
            // do: they open on the bank the rack's face shows, its first pad on
            // the bottom row, and a scroll holds until the face pages away.
            // They used to stretch to fit the sixteen pads, and never scrolled.
            grid.setSize(1000, 250);
            require(grid.rowHeight() == StepGrid::baseRowHeight && grid.visiblePitchRows() < Session::pitches
                        && grid.lowestVisiblePitch == low
                        && grid.cell(0, grid.visiblePitchRows() - 1).getBottom() == StepGrid::headerHeight + grid.rowAreaHeight(),
                    "A short editor shows fewer pads at the rows' height, the bank's first whole on the bottom row");
            juce::MouseWheelDetails notch;
            notch.deltaY = 0.25f;
            const auto overRows = grid.cell(4, 4).getCentre();
            grid.mouseWheelMove(laneEvent(overRows, overRows, false, juce::ModifierKeys()), notch);
            require(grid.lowestVisiblePitch == low + 2, "The wheel scrolls the pads' rows");
            stack.beginNoteGesture("A pad struck");
            require(stack.addNote(2.0, low + 3, 1.0).wasOk(), "A note on a pad");
            stack.endNoteGesture();
            grid.changeListenerCallback(nullptr);
            require(grid.lowestVisiblePitch == low + 2, "and the scroll holds through an edit");
            auto slot = -1;
            for (const auto& device : stack.deviceSlots(0))
                if (device.deviceId == "Drums")
                    slot = device.pluginIndex;
            require(stack.showDrumBank(0, slot, low + Session::pitches).wasOk()
                        && grid.lowestVisiblePitch == low + Session::pitches,
                    "until the rack's face pages to another bank, which the rows follow");
            owned.reset();
            stack.releaseAudioDevice();
        }
        {
            // No MIDI track, so no clip: the lane is there, empty, and nothing
            // done to it reaches the document.
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            require(stack.removeAudioTrack(2).wasOk() && stack.removeAudioTrack(0).wasOk() && !stack.hasPatternClip(),
                    "A document with no MIDI track has no clip to edit");
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            require(grid.velocityLaneHeight() == 64.0f && grid.laneNotes.empty(), "The empty editor still shows its lane");
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto centre = grid.velocityPlotBounds().getCentre();
            const auto quiet = stack.changeRevision;
            for (const auto drawing : {false, true})
            {
                grid.setDrawMode(drawing);
                grid.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, left, 1.0f, 0, 0, 0, 0,
                                                &grid, &grid, juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1, false));
                grid.mouseDrag(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.translated(0.0f, -20.0f),
                                                left, 1.0f, 0, 0, 0, 0, &grid, &grid, juce::Time::getCurrentTime(), centre,
                                                juce::Time::getCurrentTime(), 1, true));
                grid.mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre.translated(0.0f, -20.0f),
                                              left, 1.0f, 0, 0, 0, 0, &grid, &grid, juce::Time::getCurrentTime(), centre,
                                              juce::Time::getCurrentTime(), 1, true));
            }
            grid.setDrawMode(false);
            require(stack.changeRevision == quiet && grid.createComponentSnapshot(grid.getLocalBounds()).isValid(),
                    "Pressing, dragging and drawing in it change nothing, and it paints");
            owned.reset();
            stack.releaseAudioDevice();
        }
