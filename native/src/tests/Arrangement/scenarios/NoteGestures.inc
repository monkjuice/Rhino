        // What the pointer does in the note editor, now that it does the same
        // thing the arrangement's pointer does: a drag selects, a double-click
        // puts one note down, and painting a run of them is what draw mode is
        // for. Plus the scroll that lets a drag reach past the panel.
        {
            // On the heap deliberately. Every scenario shares one stack frame,
            // and a StepGrid carries about 200 KB of row-addressed note caches.
            auto owned = std::make_unique<StepGrid>(session);
            auto& grid = *owned;
            grid.setSize(1000, 250);
            grid.changeListenerCallback(nullptr);

            std::vector<juce::ValueTree> previous;
            for (const auto& note : session.editorNotes()) previous.push_back(note.state);
            session.beginNoteGesture("Gesture fixture");
            session.removeNotes(previous);
            session.endNoteGesture();
            session.setEditorStepCount(16);
            grid.changeListenerCallback(nullptr);
            require(session.editorNotes().empty(), "The gesture fixture starts on an empty clip");

            const auto press = [&grid](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                       int clicks = 1,
                                       juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point, mods,
                                        1.0f, 0, 0, 0, 0, &grid, &grid, juce::Time::getCurrentTime(),
                                        down, juce::Time::getCurrentTime(), clicks, dragged);
            };
            const auto rowFor = [&grid](int pitch)
            {
                return grid.lowestVisiblePitch + grid.visiblePitchRows() - 1 - pitch;
            };

            // A plain drag across empty lanes selects. It used to draw, which
            // is exactly why a marquee needed a held key of its own.
            const auto sweepFrom = grid.cell(1, rowFor(60)).getCentre();
            const auto sweepTo = grid.cell(5, rowFor(57)).getCentre();
            grid.mouseDown(press(sweepFrom, sweepFrom, false));
            grid.mouseDrag(press(sweepFrom, sweepTo, true));
            grid.mouseUp(press(sweepFrom, sweepTo, true));
            grid.changeListenerCallback(nullptr);
            require(session.editorNotes().empty(), "Dragging across empty lanes draws nothing");

            // Double-clicking a cell is how one note is put down, matching the
            // arrangement, where double-clicking a lane puts down one clip.
            const auto addAt = grid.cell(2, rowFor(60)).getCentre();
            grid.mouseDown(press(addAt, addAt, false, 2));
            grid.mouseUp(press(addAt, addAt, false, 2));
            grid.changeListenerCallback(nullptr);
            require(session.hasNote(2, 60), "Double-clicking a cell adds a note");
            require(session.editorNotes().size() == 1, "and adds exactly one");

            require(session.addNote(6.0, 62, 1.0).wasOk(), "Can place a second note for the marquee");
            grid.changeListenerCallback(nullptr);
            const auto marqueeFrom = grid.cell(1, rowFor(63)).getCentre();
            const auto marqueeTo = grid.cell(7, rowFor(59)).getCentre();
            grid.mouseDown(press(marqueeFrom, marqueeFrom, false));
            grid.mouseDrag(press(marqueeFrom, marqueeTo, true));
            grid.mouseUp(press(marqueeFrom, marqueeTo, true));
            require(grid.selectedStates().size() == 2, "A plain drag marquee-selects the notes it swept");
            require(session.editorNotes().size() == 2, "and adds and removes nothing");

            const auto emptyPoint = grid.cell(13, rowFor(55)).getCentre();
            grid.mouseDown(press(emptyPoint, emptyPoint, false));
            grid.mouseUp(press(emptyPoint, emptyPoint, false));
            require(grid.selectedStates().empty(), "Clicking empty space clears the selection");
            require(close(grid.stepSelection.start, 13.0),
                    "and leaves the insert point on the step it landed on");

            // Draw mode: the one thing a selecting pointer cannot do is paint
            // a run of notes, so it is a mode rather than a modifier.
            require(!grid.isDrawMode(), "The editor selects until draw mode is asked for");
            require(grid.keyPressed(juce::KeyPress('B', juce::ModifierKeys(), 'b')), "B is handled");
            require(grid.isDrawMode(), "B turns draw mode on");
            const auto paintFrom = grid.cell(9, rowFor(55)).getCentre();
            const auto paintTo = grid.cell(12, rowFor(55)).getCentre();
            grid.mouseDown(press(paintFrom, paintFrom, false));
            grid.mouseDrag(press(paintFrom, paintTo, true));
            grid.mouseUp(press(paintFrom, paintTo, true));
            grid.changeListenerCallback(nullptr);
            require(session.hasNote(9, 55) && session.hasNote(10, 55)
                    && session.hasNote(11, 55) && session.hasNote(12, 55),
                    "Draw mode paints every cell the drag crossed");
            session.undo();
            grid.changeListenerCallback(nullptr);
            require(!session.hasNote(9, 55) && !session.hasNote(12, 55),
                    "One undo removes the whole painted run");
            require(grid.keyPressed(juce::KeyPress('B', juce::ModifierKeys(), 'b')), "B is handled again");
            require(!grid.isDrawMode(), "B turns draw mode back off");

            // A drag that reaches the right edge pulls the view after it, and
            // goes on pulling while the pointer is held there: the pointer
            // sends no more drags once it stops moving, so the gesture clock
            // is what carries it.
            session.setEditorStepCount(32);
            grid.changeListenerCallback(nullptr);
            require(grid.visibleStepSpan() < static_cast<double>(session.editorStepCount()),
                    "The fixture is wider than the panel shows");
            grid.stepScroll = 0.0;
            grid.syncHorizontalScroll();
            const auto edgeFrom = grid.cell(1, rowFor(58)).getCentre();
            const juce::Point<float> heldPastRight {grid.gridRight() + 40.0f, edgeFrom.y};
            grid.mouseDown(press(edgeFrom, edgeFrom, false));
            grid.mouseDrag(press(edgeFrom, heldPastRight, true));
            require(close(grid.stepScroll, 0.0),
                    "The pointer itself does not scroll: a drag that stops moving still has to travel");
            grid.timerCallback();
            const auto afterOneFrame = grid.stepScroll;
            require(afterOneFrame > 0.0, "One frame of a drag held past the right edge scrolls the grid");
            for (int frame = 0; frame < 10; ++frame) grid.timerCallback();
            require(grid.stepScroll > afterOneFrame,
                    "and it keeps scrolling while the pointer is held there, with no further drags");
            grid.mouseUp(press(edgeFrom, heldPastRight, true));
            require(grid.stepSelection.end > grid.visibleStepSpan(),
                    "so the selection reaches past what was on screen when the drag began");

            grid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        }
