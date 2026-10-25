        // A drum clip's rows are its rack's assigned keys, the pads that hold a
        // sound, and the keys its notes are on, at the user's request. An empty
        // pad has no row unless notes are on it, and then its row reads as its
        // pitch, so emptying a pad under notes keeps their row. Up, Down and a
        // drag go from row to row. A rack with no key assigned shows every key,
        // as the velocity lane's drum scenario checks. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            require(stack.addDrumKit(ContentLibrary::file("Drums/Kits/808 Kit.rdk"), 0).wasOk(), "A rack with the 808 Kit");
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk()
                        && stack.isPatternDrums(),
                    "A drum clip of its own");
            const std::vector<int> kit {48, 49, 50, 52, 53, 54, 55, 56, 58, 59};
            require(stack.patternNamedNotes() == kit, "The session names the kit's ten assigned keys, lowest first");
            juce::ValueTree kick, hat, stray;
            stack.beginNoteGesture("Drum rows fixture");
            require(stack.addNote(0.0, 48, 1.0, &kick).wasOk() && stack.addNote(2.0, 58, 1.0, &hat).wasOk()
                        && stack.addNote(4.0, 51, 1.0, &stray).wasOk(),
                    "A kick, a closed hat, and a note on the empty D#2");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, StepGrid::openingHeight);
            grid.changeListenerCallback(nullptr);
            const auto pitchOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.pitch;
                return -1;
            };
            const auto press = [&grid](juce::Point<float> down, juce::Point<float> at)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        1, at != down);
            };
            const auto drag = [&](juce::Point<float> from, juce::Point<float> to)
            {
                grid.mouseDown(press(from, from));
                grid.mouseDrag(press(from, to));
                grid.mouseUp(press(from, to));
            };
            const auto undoOnce = [&]
            {
                stack.undo();
                grid.changeListenerCallback(nullptr);
            };

            const std::vector<int> rows {48, 49, 50, 51, 52, 53, 54, 55, 56, 58, 59};
            require(grid.shownPitches == rows && grid.visiblePitchRows() == 11,
                    "A row for each assigned key and one for D#2, an empty pad with a note on it");
            require(!stack.patternNoteName(51).has_value() && grid.rowName(51) == "D#2" && grid.rowName(48) == "Kick"
                        && grid.noteForState(stray) != nullptr,
                    "which reads as its pitch where the others read as their sounds, and its note shows");
            require(grid.lineOfPitch(57) < 0, "The empty A2, with no note, has no row");
            const auto rowsEnd = grid.cell(0, 10).getBottom();
            require(grid.pitchOfRow(10) == 48 && grid.pitchOfRow(0) == 59 && grid.rowsTop() == StepGrid::headerHeight
                        && rowsEnd < StepGrid::headerHeight + grid.rowAreaHeight(),
                    "the kick on the bottom row, and the rows hanging from the header with room to spare below them");
            const juce::Point<float> belowRows {500.0f, rowsEnd + 5.0f};
            require(grid.cellHit(belowRows) < 0 && grid.pitchNamedAt(belowRows) < 0
                        && grid.pitchNamedAt({StepGrid::labelWidth * 0.5f, belowRows.y}) < 0,
                    "and nothing under the pointer in that room, on the grid or the keys");

            // Up and Down go from row to row, past a key with no row.
            grid.setSelectedStates({hat});
            const juce::KeyPress up(juce::KeyPress::upKey), down(juce::KeyPress::downKey);
            require(grid.keyPressed(down) && pitchOf(hat) == 56 && grid.keyPressed(up) && pitchOf(hat) == 58,
                    "Down takes the hat to the clap's row, past the empty A2, and Up brings it back");
            undoOnce();
            undoOnce();
            require(pitchOf(hat) == 58, "Each is an undo step of its own");
            grid.setSelectedStates({stray});
            require(grid.keyPressed(up) && pitchOf(stray) == 52 && grid.lineOfPitch(51) < 0 && grid.visiblePitchRows() == 10,
                    "A note moved off the empty D#2 leaves it nothing to hold its row, and the row goes");
            undoOnce();
            require(pitchOf(stray) == 51 && grid.lineOfPitch(51) == 3, "and the undo brings both back");

            // A drag goes from row to row: the kick dragged to the snare's row
            // lands on the snare's key, a group dragged up a row goes up a row
            // each, and a note dragged into the room below lands on the lowest.
            grid.clearSelection();
            const auto kickAt = grid.boundsFor(*grid.noteForState(kick)).getCentre();
            drag(kickAt, {kickAt.x, grid.cell(0, grid.rowOfPitch(53)).getCentreY()});
            require(pitchOf(kick) == 53, "Dragged to the snare's row, the kick lands on the snare's key");
            undoOnce();
            grid.setSelectedStates({kick, hat});
            const auto groupAt = grid.boundsFor(*grid.noteForState(kick)).getCentre();
            drag(groupAt, groupAt.translated(0.0f, -grid.rowHeight()));
            require(pitchOf(kick) == 49 && pitchOf(hat) == 59,
                    "A group dragged up a row goes up a row each: the kick from 48 to 49, the hat from 58 to 59");
            undoOnce();
            grid.clearSelection();
            const auto hatAt = grid.boundsFor(*grid.noteForState(hat)).getCentre();
            drag(hatAt, {hatAt.x, belowRows.y});
            require(pitchOf(hat) == 48, "A note dragged into the room below the rows lands on the lowest one");
            undoOnce();
            grid.clearSelection();
            // The rows hold still under a drag. The note dragged off D#2 takes
            // its row away only when the button comes up, or every row would
            // move under the pointer mid-drag.
            const auto strayAt = grid.boundsFor(*grid.noteForState(stray)).getCentre();
            const juce::Point<float> strayTo {strayAt.x, grid.cell(0, grid.rowOfPitch(52)).getCentreY()};
            grid.mouseDown(press(strayAt, strayAt));
            grid.mouseDrag(press(strayAt, strayTo));
            require(pitchOf(stray) == 52 && grid.lineOfPitch(51) == 3, "Mid-drag, the row the note left stays");
            grid.mouseUp(press(strayAt, strayTo));
            require(grid.lineOfPitch(51) < 0 && grid.visiblePitchRows() == 10, "and goes with the button");
            undoOnce();
            grid.clearSelection();

            // A pane too short for every row scrolls them, a row at a time.
            grid.setSize(1000, 150);
            require(grid.visiblePitchRows() == 8 && grid.lowestVisiblePitch == 48, "A short pane shows eight of the eleven rows");
            juce::MouseWheelDetails notch;
            notch.deltaY = 0.25f;
            const auto overRows = grid.cell(4, 4).getCentre();
            grid.mouseWheelMove(press(overRows, overRows), notch);
            require(grid.lowestVisiblePitch == 50, "The wheel scrolls them by rows");
            grid.mouseWheelMove(press(overRows, overRows), notch);
            require(grid.lowestVisiblePitch == 51 && grid.pitchOfRow(0) == 59, "and stops with the last one on the top row");
            grid.setSize(1000, StepGrid::openingHeight);
            grid.manualPitchScroll = false;
            grid.changeListenerCallback(nullptr);

            // A pad given a sound gains a row; a pad emptied under notes keeps
            // its row, named for its pitch.
            auto slot = -1;
            for (const auto& device : stack.deviceSlots(0))
                if (device.deviceId == "Drums")
                    slot = device.pluginIndex;
            require(stack.loadDrumPadSample(0, slot, 57, ContentLibrary::file("Samples/TR808/TR808Kick.wav")).wasOk(),
                    "A sample on the empty A2");
            grid.changeListenerCallback(nullptr);
            require(grid.visiblePitchRows() == 12 && grid.lineOfPitch(57) == 9 && stack.patternNoteName(57).has_value(),
                    "gives it a row");
            undoOnce();
            require(grid.visiblePitchRows() == 11 && grid.lineOfPitch(57) < 0, "and the undo takes the row away again");
            auto* rack = dynamic_cast<DrumRackDevice*>(stack.patternInstrumentForTrack(0));
            require(rack != nullptr && stack.editDeviceSettings(0, slot, "Clear the kick", [rack] { rack->clearPad(48); }).wasOk(),
                    "The kick's pad is emptied");
            grid.changeListenerCallback(nullptr);
            require(!stack.patternNoteName(48).has_value() && grid.lineOfPitch(48) == 0 && grid.rowName(48) == "C2"
                        && grid.noteForState(kick) != nullptr,
                    "and keeps its row while the kick note is on it, named for its pitch now");
            undoOnce();
            require(grid.rowName(48) == "Kick", "The undo puts the pad back, named for its sound");
            require(grid.createComponentSnapshot(grid.getLocalBounds()).isValid(), "The drum rows paint");
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_NOTE_DRUMS_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(grid.createComponentSnapshot(grid.getLocalBounds()), *stream);
            }
            owned.reset();
            stack.releaseAudioDevice();
        }
