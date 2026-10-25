        // Drawing velocities, the lane's marquee, what the lane leaves alone,
        // and what the pointer and the Info View say over it. A document of its
        // own; every edit is undone before the next check.
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
                    "A clip of its own for drawing velocities");
            // P and Q are a chord at the first step; S is below the pitch
            // window; T and U share one step.
            juce::ValueTree p, q, r, s, t, u;
            stack.beginNoteGesture("Velocity drawing fixture");
            require(stack.addNote(0.0, 60, 1.0, &p, 100).wasOk() && stack.addNote(0.0, 64, 1.0, &q, 100).wasOk()
                        && stack.addNote(4.0, 62, 1.0, &r, 100).wasOk() && stack.addNote(8.0, 30, 1.0, &s, 50).wasOk()
                        && stack.addNote(12.0, 62, 0.5, &t, 100).wasOk() && stack.addNote(12.5, 62, 0.5, &u, 100).wasOk(),
                    "The drawing fixture's notes go down");
            stack.endNoteGesture();
            // Before the grid, so it outlives it: a failed check inside a drag
            // unwinds through ~StepGrid, whose end of the gesture announces.
            Count everyone;
            stack.addChangeListener(&everyone);
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
            const auto stroke = [&](juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods)
            {
                grid.mouseDown(laneEvent(from, from, false, mods));
                if (to != from)
                    grid.mouseDrag(laneEvent(from, to, true, mods));
                grid.mouseUp(laneEvent(from, to, to != from, mods));
            };
            const auto at = [&grid](double step, float velocity)
            {
                return juce::Point<float>(grid.xForStep(step), grid.yForVelocity(velocity));
            };
            const auto velocityOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.velocity;
                return -1;
            };
            const auto head = [&grid](const juce::ValueTree& state)
            {
                const auto* note = grid.laneNoteFor(state);
                return juce::Point<float>(grid.xForStep(note->start), grid.yForVelocity(static_cast<float>(note->velocity)));
            };
            const auto selectionIs = [&grid](std::vector<juce::ValueTree> expected)
            {
                auto actual = grid.selectedStates();
                if (actual.size() != expected.size()) return false;
                for (const auto& state : expected)
                    if (std::find(actual.begin(), actual.end(), state) == actual.end()) return false;
                return true;
            };
            const auto undoOnce = [&]
            {
                stack.undo();
                grid.changeListenerCallback(nullptr);
            };
            require(grid.lowestVisiblePitch == 49 && grid.laneNoteFor(s) != nullptr && !grid.laneNoteFor(s)->inWindow,
                    "The drawing fixture's window is 49 to 64, with one note below it");

            // A pencil sets the notes in each step it crosses, at the height it
            // crosses it, and a fast stroke skips no step on the way.
            require(grid.keyPressed(juce::KeyPress('B', juce::ModifierKeys(), 'b')) && grid.isDrawMode(), "B is draw mode");
            grid.clearSelection();
            everyone.heard = 0;
            const auto from = at(0.5, 40.0f);
            grid.mouseDown(laneEvent(from, from, false, left));
            require(velocityOf(p) == 40 && velocityOf(q) == 40 && velocityOf(r) == 100 && everyone.heard == 0,
                    "A press in draw mode sets the notes in the step under it");
            const auto to = at(8.5, 120.0f);
            grid.mouseDrag(laneEvent(from, to, true, left));
            require(velocityOf(s) == 120 && velocityOf(r) == 80 && velocityOf(p) == 40 && velocityOf(t) == 100
                        && velocityOf(u) == 100,
                    "One fast stroke sets every step it crossed along a straight line");
            grid.mouseUp(laneEvent(from, to, true, left));
            require(everyone.heard == 1 && grid.selectedStates().empty() && stack.editorNotes().size() == 6,
                    "A stroke is announced once, selects nothing and adds no note");
            undoOnce();
            require(velocityOf(p) == 100 && velocityOf(q) == 100 && velocityOf(r) == 100 && velocityOf(s) == 50,
                    "and is one undo step");

            // With notes selected only they are painted, however much the
            // stroke crosses.
            grid.setSelectedStates({r});
            stroke(at(0.5, 30.0f), at(15.5, 30.0f), left);
            require(velocityOf(r) == 30 && velocityOf(p) == 100 && velocityOf(s) == 50 && velocityOf(t) == 100,
                    "A stroke paints only the selected notes");
            undoOnce();
            grid.clearSelection();

            // Alt paints one note at a time, at its own start; without it the
            // whole step takes the value.
            const auto altLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            stroke(at(12.5, 30.0f), at(12.5, 30.0f), altLeft);
            require(velocityOf(u) == 30 && velocityOf(t) == 100, "With Alt a press paints only the note it is on");
            undoOnce();
            stroke(at(12.75, 30.0f), at(12.75, 30.0f), left);
            require(velocityOf(t) == 30 && velocityOf(u) == 30, "Without it every note starting in the step is painted");
            undoOnce();
            require(grid.keyPressed(juce::KeyPress('B', juce::ModifierKeys(), 'b')) && !grid.isDrawMode(), "B again selects");

            // A press on empty lane sweeps a marquee that catches markers by
            // their dots, and the span it swept is the region.
            stroke(at(2.5, 127.0f), at(4.5, 90.0f), left);
            require(selectionIs({r}) && grid.stepSelection.start == 2.0 && grid.stepSelection.end == 5.0,
                    "A lane marquee selects the markers it covers and leaves the steps it swept as the region");
            grid.clearSelection();
            stroke(at(7.5, 65.0f), at(8.5, 35.0f), left);
            require(selectionIs({s}) && grid.noteForState(s) == nullptr,
                    "and catches a note the pitch window has scrolled past");
            const auto ctrl = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
            stroke(at(2.5, 127.0f), at(4.5, 90.0f), ctrl);
            require(selectionIs({s, r}), "A Ctrl marquee adds to the selection");
            // Cut and paste carry every selected note, one scrolled out of the
            // pitch window included: delete takes them all.
            const auto shapeOf = [&stack]
            {
                std::vector<std::tuple<double, int, int>> shape;
                for (const auto& note : stack.editorNotes())
                    shape.emplace_back(note.startSteps, note.pitch, note.velocity);
                std::sort(shape.begin(), shape.end());
                return shape;
            };
            const auto shapeBefore = shapeOf();
            require(grid.keyPressed(juce::KeyPress('X', juce::ModifierKeys::commandModifier, 'x'))
                        && stack.editorNotes().size() == 4,
                    "Ctrl+X cuts both notes the lane selected");
            require(grid.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 'v')) && shapeOf() == shapeBefore,
                    "and Ctrl+V gives both back, where they were, with their velocities");
            undoOnce();
            undoOnce();
            require(shapeOf() == shapeBefore, "Undoing the paste and the cut leaves the fixture as it was");
            grid.setSelectedStates({s, r});
            stroke(at(6.5, 64.0f), at(6.5, 64.0f), left);
            require(grid.selectedStates().empty() && grid.stepSelection.active && !grid.stepSelection.isRange()
                        && grid.stepSelection.start == 6.0,
                    "A click on empty lane clears the selection and leaves the insert point there");

            // The right button, a double-click and the gutter do nothing to
            // the notes: a marker can stand for a note nobody can see. The
            // right button opens the note's menu instead, caught here.
            const auto quiet = stack.changeRevision;
            const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
            int menus = 0;
            grid.presentMenu = [&menus](juce::PopupMenu, std::function<void(int)>) { ++menus; };
            stroke(head(p), head(p).translated(0.0f, -20.0f), right);
            require(velocityOf(p) == 100 && stack.editorNotes().size() == 6 && stack.changeRevision == quiet && menus == 1,
                    "The right button neither erases nor drags in the lane; it opens the note's menu");
            grid.presentMenu = nullptr;
            grid.mouseDown(laneEvent(at(6.5, 64.0f), at(6.5, 64.0f), false, left, 2));
            grid.mouseUp(laneEvent(at(6.5, 64.0f), at(6.5, 64.0f), false, left, 2));
            require(stack.editorNotes().size() == 6 && stack.changeRevision == quiet,
                    "Double-clicking the lane puts no note down");
            const juce::Point<float> gutter {20.0f, grid.velocityLaneBounds().getCentreY()};
            grid.mouseDown(laneEvent(gutter, gutter, false, left));
            require(grid.gesture == StepGrid::Gesture::none, "A press on the gutter starts nothing");
            grid.mouseUp(laneEvent(gutter, gutter, false, left));

            // The pointer says what a press will do.
            const auto cursorAt = [&](juce::Point<float> point, juce::ModifierKeys mods)
            {
                grid.mouseMove(laneEvent(point, point, false, mods));
                return grid.getMouseCursor();
            };
            const auto dividerCentre = grid.laneDividerBounds().getCentre();
            const auto none = juce::ModifierKeys();
            const auto rBody = grid.boundsFor(*grid.noteForState(r)).getCentre();
            require(cursorAt(dividerCentre, none) == juce::MouseCursor::UpDownResizeCursor, "The divider resizes");
            require(cursorAt(gutter, none) == juce::MouseCursor::NormalCursor, "The gutter is only a scale");
            require(cursorAt(head(r), none) == juce::MouseCursor::UpDownResizeCursor, "A marker moves up and down");
            require(cursorAt(head(r), juce::ModifierKeys(juce::ModifierKeys::commandModifier)) == juce::MouseCursor::NormalCursor,
                    "and with Ctrl held it is only gathered");
            require(cursorAt(at(6.5, 64.0f), none) == juce::MouseCursor::CrosshairCursor, "Empty lane sweeps a marquee");
            require(cursorAt(rBody, none) == juce::MouseCursor::DraggingHandCursor, "The grid's own pointer is unchanged");
            grid.setDrawMode(true);
            require(cursorAt(head(r), none) == juce::MouseCursor::CrosshairCursor, "In draw mode the lane is a pencil");
            require(grid.tooltipAt(at(6.5, 64.0f)).contains("paint"), "and the Info View says it paints");
            grid.setDrawMode(false);
            require(grid.tooltipAt(dividerCentre).contains("resize")
                        && grid.tooltipAt(at(6.5, 64.0f)).contains("Drag a marker")
                        && grid.tooltipAt(gutter).contains("127"),
                    "The Info View explains the divider, the lane and its scale");
            require(grid.tooltipAt(rBody).isEmpty(), "and stays quiet over the grid");

            // A stroke held past the edge paints what scrolls into view, and
            // never scrolls pitch. Last, because it changes the resolution.
            stack.setEditorStepCount(32);
            grid.changeListenerCallback(nullptr);
            require(grid.horizontalScroll.isVisible() && grid.velocityLaneHeight() == 64.0f,
                    "A clip wider than the view keeps its lane over the scrollbar");
            juce::ValueTree w;
            require(stack.addNote(20.0, 63, 1.0, &w, 100).wasOk(), "A note beyond the view");
            grid.changeListenerCallback(nullptr);
            grid.clearSelection();
            const auto pitchBefore = grid.lowestVisiblePitch;
            grid.setDrawMode(true);
            const auto edgeFrom = at(2.5, 30.0f);
            const juce::Point<float> pastEdge {grid.gridRight() + 40.0f, edgeFrom.y};
            grid.mouseDown(laneEvent(edgeFrom, edgeFrom, false, left));
            grid.mouseDrag(laneEvent(edgeFrom, pastEdge, true, left));
            require(velocityOf(r) == 30 && velocityOf(s) == 50, "The stroke paints what is in view");
            for (int tick = 0; tick < 40; ++tick)
                grid.timerCallback();
            require(grid.stepScroll > 0.0 && velocityOf(w) == 30 && velocityOf(s) == 30 && velocityOf(t) == 30
                        && velocityOf(u) == 30 && velocityOf(p) == 100 && velocityOf(q) == 100,
                    "Held past the edge, it paints the steps that scroll in under it");
            require(grid.lowestVisiblePitch == pitchBefore, "and a stroke in the lane never scrolls pitch");
            grid.mouseUp(laneEvent(edgeFrom, pastEdge, true, left));
            undoOnce();
            require(velocityOf(r) == 100 && velocityOf(w) == 100 && velocityOf(s) == 50, "One undo takes the stroke back");
            grid.setDrawMode(false);

            stack.removeChangeListener(&everyone);
            owned.reset();
            stack.releaseAudioDevice();
        }
