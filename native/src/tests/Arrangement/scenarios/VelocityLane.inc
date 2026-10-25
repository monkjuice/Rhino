        // The note editor's velocity lane: where it sits, what its markers
        // stand for, and what dragging one does. A document of its own, and
        // every edit a check makes is undone before the next one.
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
                    "A clip of its own for the velocity lane");
            // A and B start together; C and D are a chord, one marker on top of
            // the other; E is below the pitch window; F is a sliver.
            juce::ValueTree a, b, c, d, e, f;
            stack.beginNoteGesture("Velocity lane fixture");
            require(stack.addNote(1.0, 60, 2.0, &a, 100).wasOk() && stack.addNote(1.0, 64, 1.0, &b, 40).wasOk()
                        && stack.addNote(4.0, 62, 1.0, &c, 100).wasOk() && stack.addNote(4.0, 67, 1.0, &d, 100).wasOk()
                        && stack.addNote(8.0, 30, 1.0, &e, 90).wasOk() && stack.addNote(12.0, 65, 0.0625, &f, 70).wasOk(),
                    "The velocity lane's fixture notes go down");
            stack.endNoteGesture();
            // Before the grid, so it outlives it: a failed check inside a drag
            // unwinds through ~StepGrid, whose end of the gesture announces.
            Count everyone;
            stack.addChangeListener(&everyone);
            // On the heap, as every scenario's StepGrid is; see tests/README.md.
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
            const auto press = [&](juce::Point<float> at, juce::ModifierKeys mods)
            {
                grid.mouseDown(laneEvent(at, at, false, mods));
            };
            const auto dragBy = [&](juce::Point<float> from, float dy, juce::ModifierKeys mods)
            {
                grid.mouseDrag(laneEvent(from, from.translated(0.0f, dy), true, mods));
            };
            const auto releaseAt = [&](juce::Point<float> from, float dy)
            {
                grid.mouseUp(laneEvent(from, from.translated(0.0f, dy), dy != 0.0f, left));
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
                return note == nullptr ? juce::Point<float>(-1.0f, -1.0f)
                                       : juce::Point<float>(grid.xForStep(note->start),
                                                            grid.yForVelocity(static_cast<float>(note->velocity)));
            };
            const auto markerAt = [&grid](juce::Point<float> point)
            {
                const auto index = grid.velocityMarkerAt(point);
                return index < 0 ? juce::ValueTree() : grid.laneNotes[static_cast<size_t>(index)].state;
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

            // Under the rows and over the footer, on the same time axis, and
            // nothing in the grid reaches into it.
            require(grid.velocityLaneHeight() == 64.0f && grid.rowAreaHeight() == 181.0f && grid.visiblePitchRows() == 14
                        && grid.rowHeight() == StepGrid::baseRowHeight,
                    "The lane takes its default height from under the rows, which keep theirs: thirteen whole and one cut short");
            require(grid.lowestVisiblePitch == 55, "The fixture's pitch window is 55 to 67, and 68 cut short above it");
            require(grid.laneDividerBounds().getY() == 207.0f && grid.velocityLaneBounds().getY() == 212.0f
                        && grid.velocityLaneBounds().getBottom() == grid.footerBounds().getY(),
                    "The divider sits under the rows and the lane runs down to the footer");
            const juce::Point<float> inLane {500.0f, 244.0f};
            require(grid.cellHit(inLane) < 0 && grid.hit(inLane) < 0 && grid.resizeHit(inLane) < 0
                        && !grid.isOverKeyboard({20.0f, 244.0f}),
                    "No cell, note, handle or key is hit in the lane");
            grid.setVelocityLaneShown(false);
            require(grid.velocityLaneHeight() == 0.0f && grid.rowAreaHeight() == 250.0f && grid.cellHit(inLane) >= 0,
                    "Hidden, the lane gives its room back to the rows");
            grid.setVelocityLaneShown(true);
            require(grid.rowAreaHeight() == 181.0f, "and shown again it takes it back");

            // A marker for every note, scrolled into the pitch window or not.
            require(grid.laneNotes.size() == 6, "Every note in the clip has a marker");
            require(grid.laneNoteFor(e) != nullptr && !grid.laneNoteFor(e)->inWindow && grid.noteForState(e) == nullptr,
                    "including a note the pitch window has scrolled past");
            require(head(a).y < head(b).y, "A louder note's marker stands higher");
            require(head(c) == head(d), "A chord's markers stand on top of one another");
            require(markerAt(head(b)) == b && markerAt(head(f)) == f, "A press on a marker's dot takes its note");
            require(markerAt({grid.xForStep(2.0), grid.yForVelocity(100.0f)}) == a,
                    "and so does a press on the bar along the note's length");
            require(!markerAt({grid.xForStep(10.5), grid.yForVelocity(64.0f)}).isValid(), "Empty lane takes nothing");

            // Hovering lights the marker a press would take, and the gutter
            // reads its value out; pointing at a note in the grid lights its
            // marker, which is how one in a stack is found.
            grid.mouseMove(laneEvent(head(b), head(b), false, juce::ModifierKeys()));
            require(grid.hoveredNoteState == b, "Hovering a marker lights it");
            require(grid.velocityGutterText() == std::pair<juce::String, juce::String>("40", "E3"),
                    "and the gutter shows its velocity and its note");
            const auto chordBody = grid.boundsFor(*grid.noteForState(c)).getCentre();
            grid.mouseMove(laneEvent(chordBody, chordBody, false, juce::ModifierKeys()));
            require(grid.hoveredNoteState == c, "Hovering a note in the grid lights its marker");
            grid.mouseExit(laneEvent(chordBody, chordBody, false, juce::ModifierKeys()));
            require(!grid.hoveredNoteState.isValid() && grid.velocityGutterText().first.isEmpty(),
                    "Leaving the editor puts the gutter's scale back");

            // One marker. Nothing moves until the press has travelled, the drag
            // writes straight to the notes and tells nobody, and its end is
            // one announcement and one undo step.
            const auto pressA = head(a);
            const auto revision = stack.changeRevision;
            everyone.heard = 0;
            press(pressA, left);
            require(selectionIs({a}) && everyone.heard == 0, "A press on a marker selects its note");
            dragBy(pressA, -2.0f, left);
            dragBy(pressA, -4.0f, left);
            require(velocityOf(a) == 100 && stack.changeRevision == revision,
                    "A press that has only just travelled changes nothing, so a marker never jumps");
            dragBy(pressA, -13.0f, left);
            require(velocityOf(a) == 121, "Dragging up raises the velocity, 126 steps over the plot's height");
            require(everyone.heard == 0 && grid.selectedVelocity() == 121,
                    "Nobody is told of each step, but the editor reads the value back at once");
            const auto drawn = stack.changeRevision;
            grid.timerCallback();
            require(stack.changeRevision == drawn, "A pointer held still writes nothing");
            dragBy(pressA, -43.0f, left);
            require(velocityOf(a) == 127, "A drag past the top stops at 127");
            dragBy(pressA, -40.0f, left);
            require(velocityOf(a) == 120, "and coming back answers at once, with no dead travel");
            releaseAt(pressA, -40.0f);
            require(everyone.heard == 1 && selectionIs({a}), "Letting go tells everyone once, and the note stays selected");
            undoOnce();
            require(velocityOf(a) == 100, "One undo takes the whole drag back");
            stack.redo();
            grid.changeListenerCallback(nullptr);
            require(velocityOf(a) == 120, "and redo puts it back");
            undoOnce();

            press(pressA, left);
            dragBy(pressA, -4.0f, left);
            dragBy(pressA, -16.0f, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
            require(velocityOf(a) == 107, "Shift during a drag is a quarter of the speed, for a fine value");
            releaseAt(pressA, -16.0f);
            undoOnce();

            // A press inside a selected group that never travels is a click,
            // and a click takes the one marker under it.
            grid.setSelectedStates({a, b});
            everyone.heard = 0;
            const auto clickRevision = stack.changeRevision;
            const auto pressB = head(b);
            press(pressB, left);
            dragBy(pressB, 2.0f, left);
            releaseAt(pressB, 2.0f);
            require(selectionIs({b}) && velocityOf(a) == 100 && velocityOf(b) == 40,
                    "A click on a marker in a group selects it alone and changes nothing");
            require(everyone.heard == 0 && stack.changeRevision == clickRevision, "and tells nobody of an edit");

            // Ctrl and Shift gather, as on a note, and carry nothing.
            const auto ctrl = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
            const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
            press(head(a), ctrl);
            releaseAt(head(a), 0.0f);
            require(selectionIs({b, a}), "Ctrl-clicking a marker adds its note");
            press(head(f), shift);
            releaseAt(head(f), 0.0f);
            press(head(a), shift);
            releaseAt(head(a), 0.0f);
            require(selectionIs({b, a, f}), "Shift adds, and never takes away");
            press(head(f), ctrl);
            releaseAt(head(f), 0.0f);
            require(selectionIs({b, a}) && velocityOf(a) == 100 && velocityOf(b) == 40 && velocityOf(f) == 70,
                    "Ctrl takes a note back out, and gathering changes no velocity");

            // A group moves by the same amount, equalises at a rail, and gets
            // its own spacing back when it is dragged back off it.
            everyone.heard = 0;
            press(pressA, left);
            dragBy(pressA, -4.0f, left);
            dragBy(pressA, -13.0f, left);
            require(velocityOf(a) == 121 && velocityOf(b) == 61, "A group's markers move together");
            dragBy(pressA, -43.0f, left);
            require(velocityOf(a) == 127 && velocityOf(b) == 127, "Pushed past the top the whole group meets there");
            dragBy(pressA, -13.0f, left);
            require(velocityOf(a) == 117 && velocityOf(b) == 57, "and dragged back it has its spacing again");
            releaseAt(pressA, -13.0f);
            require(everyone.heard == 1 && selectionIs({a, b}), "One announcement, and the group stays selected");
            undoOnce();
            require(velocityOf(a) == 100 && velocityOf(b) == 40, "One undo restores the whole group");
            press(pressA, left);
            dragBy(pressA, -4.0f, left);
            dragBy(pressA, -100.0f, left);
            releaseAt(pressA, -100.0f);
            require(grid.selectedVelocity() == 127, "Released at the top, the group shares one velocity");
            const auto top = head(a);
            press(top, left);
            dragBy(top, -4.0f, left);
            dragBy(top, -40.0f, left);
            releaseAt(top, -40.0f);
            require(selectionIs({a, b}) && grid.selectedVelocity() == 127,
                    "A group dragged on into the rail it is at was dragged, not clicked, and stays selected");
            undoOnce();
            press(pressA, left);
            dragBy(pressA, 4.0f, left);
            dragBy(pressA, 100.0f, left);
            require(velocityOf(a) == 1 && velocityOf(b) == 1, "and at the bottom it stops at 1, never a note-off");
            releaseAt(pressA, 100.0f);
            undoOnce();
            require(velocityOf(a) == 100 && velocityOf(b) == 40, "The fixture is back as it was");

            // In a stack, the selected note's marker is the one taken; with none
            // selected, the one drawn on top.
            const auto chord = head(c);
            grid.setSelectedStates({d});
            press(chord, left);
            dragBy(chord, -4.0f, left);
            dragBy(chord, -13.0f, left);
            require(velocityOf(d) == 121 && velocityOf(c) == 100, "A press on a stack takes the selected note's marker");
            releaseAt(chord, -13.0f);
            undoOnce();
            grid.clearSelection();
            press(chord, left);
            dragBy(chord, -4.0f, left);
            dragBy(chord, -13.0f, left);
            require((velocityOf(c) == 121) != (velocityOf(d) == 121) && grid.selectedStates().size() == 1
                        && velocityOf(grid.selectedStates().front()) == 121,
                    "With none selected it takes exactly one, and selects it");
            releaseAt(chord, -13.0f);
            undoOnce();

            // A note out of the pitch window is edited from its marker without
            // the window moving, and a sliver of a note still has one.
            grid.clearSelection();
            const auto pressE = head(e);
            press(pressE, left);
            dragBy(pressE, -4.0f, left);
            dragBy(pressE, -13.0f, left);
            require(velocityOf(e) == 111 && selectionIs({e}) && grid.selectedVelocity() == 111 && grid.lowestVisiblePitch == 55,
                    "A note below the pitch window is selected and edited from its marker");
            releaseAt(pressE, -13.0f);
            undoOnce();
            const auto pressF = head(f);
            press(pressF, left);
            dragBy(pressF, -4.0f, left);
            dragBy(pressF, -13.0f, left);
            require(velocityOf(f) == 91, "A sixteenth of a step still has a marker to drag");
            releaseAt(pressF, -13.0f);
            undoOnce();

            // Live's keys: Ctrl+Up and Down by 10, with Shift by 1, each an
            // ordinary edit of its own.
            grid.setSelectedStates({a, c});
            everyone.heard = 0;
            const auto ctrlKey = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
            const auto ctrlShiftKey = juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey, ctrlKey, 0))
                        && velocityOf(a) == 110 && velocityOf(c) == 110 && everyone.heard == 1,
                    "Ctrl+Up raises the selection by 10 and says so at once");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::downKey, ctrlShiftKey, 0))
                        && velocityOf(a) == 109 && velocityOf(c) == 109,
                    "Ctrl+Shift+Down lowers it by 1");
            undoOnce();
            undoOnce();
            require(velocityOf(a) == 100 && velocityOf(c) == 100, "Each key press is an undo step of its own");
            stack.beginNoteGesture("Near the top");
            require(stack.setNoteVelocities({{a, 125}, {c, 125}}), "Notes can be set near the top");
            stack.endNoteGesture();
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey, ctrlKey, 0))
                        && velocityOf(a) == 127 && velocityOf(c) == 127,
                    "Ctrl+Up stops at 127");
            undoOnce();
            undoOnce();
            grid.clearSelection();
            require(!grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey, ctrlKey, 0)),
                    "With nothing selected Ctrl+Up is not the editor's");

            // V held: one MIDI step a press or a notch, so every value is
            // reachable, and one announcement when V is let go.
            grid.setSelectedStates({a});
            everyone.heard = 0;
            require(grid.keyPressed(juce::KeyPress('V', juce::ModifierKeys(), 'v')), "V starts the velocity tool");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey)) && velocityOf(a) == 101
                        && everyone.heard == 0 && grid.selectedVelocity() == 101,
                    "Up with V held raises the selection by one MIDI step, quietly");
            juce::MouseWheelDetails notch;
            notch.deltaY = 1.0f;
            grid.mouseWheelMove(laneEvent(pressA, pressA, false, juce::ModifierKeys()), notch);
            require(velocityOf(a) == 102, "and so does a notch of the wheel");
            grid.finishVelocityAdjustment();
            require(everyone.heard == 1, "Letting go of V tells everyone once");
            undoOnce();
            require(velocityOf(a) == 100, "and one undo takes the whole hold back");

            // A drag owns its undo step: no edit from the keyboard may cut into
            // it, or take its note away.
            const auto pitchOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.pitch;
                return -1;
            };
            const auto notesBefore = stack.editorNotes().size();
            press(pressA, left);
            require(grid.keyPressed(juce::KeyPress('V', juce::ModifierKeys(), 'v')) && !grid.velocityAdjustActive,
                    "V does not start its tool in the middle of a drag");
            dragBy(pressA, -4.0f, left);
            dragBy(pressA, -13.0f, left);
            require(grid.keyPressed(juce::KeyPress('Z', ctrlKey, 'z')) && velocityOf(a) == 121,
                    "Ctrl+Z in the middle of a drag is held off");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey, ctrlKey, 0)) && velocityOf(a) == 121,
                    "and so is Ctrl+Up");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey)) && pitchOf(a) == 60,
                    "and a transpose");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)) && stack.editorNotes().size() == notesBefore,
                    "and Delete");
            releaseAt(pressA, -13.0f);
            undoOnce();
            require(velocityOf(a) == 100 && pitchOf(a) == 60, "The drag is still one undo step");

            // A second press while the first is held - another finger, a pen
            // beside the mouse - ends the first gesture, so its undo bracket
            // is closed and the booth is not told a gesture is held for ever.
            everyone.heard = 0;
            press(pressA, left);
            dragBy(pressA, -4.0f, left);
            dragBy(pressA, -13.0f, left);
            const auto elsewhere = grid.cell(10, 3).getCentre();
            press(elsewhere, left);
            require(everyone.heard == 1 && !stack.gestureHeld() && velocityOf(a) == 121,
                    "A second press ends a velocity drag that is still held, and its edit stands");
            releaseAt(elsewhere, 0.0f);
            releaseAt(pressA, -13.0f);
            require(!stack.gestureHeld() && grid.gesture == StepGrid::Gesture::none, "and nothing is left held");
            undoOnce();
            require(velocityOf(a) == 100, "The ended drag undoes as one step");

            // What it looks like. A marker wears its note's colours, dimmed for
            // a note out of the pitch window, and the playhead runs through it.
            const auto sameColour = [](juce::Colour actual, juce::Colour expected)
            {
                return std::abs(actual.getRed() - expected.getRed()) <= 2
                    && std::abs(actual.getGreen() - expected.getGreen()) <= 2
                    && std::abs(actual.getBlue() - expected.getBlue()) <= 2;
            };
            const auto pixel = [](const juce::Image& image, juce::Point<float> at)
            {
                return image.getPixelAt(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)));
            };
            grid.mouseExit(laneEvent(pressA, pressA, false, juce::ModifierKeys()));
            grid.clearSelection();
            const auto plain = grid.createComponentSnapshot(grid.getLocalBounds());
            require(sameColour(pixel(plain, head(a)), palette::noteBody), "A marker wears its note's colour");
            require(sameColour(pixel(plain, head(e)), palette::noteBody.interpolatedWith(palette::velocityLane, 0.55f)),
                    "dimmed for a note out of the pitch window");
            grid.setSelectedStates({a});
            const auto picked = grid.createComponentSnapshot(grid.getLocalBounds());
            require(sameColour(pixel(picked, head(a)), palette::noteSelected), "and the selection's colour when it is selected");
            grid.playhead = 300.0f;
            const auto playing = grid.createComponentSnapshot(grid.getLocalBounds());
            require(sameColour(pixel(playing, {300.0f, grid.velocityLaneBounds().getCentreY()}), playheadColour)
                        && sameColour(pixel(playing, {300.0f, grid.laneDividerBounds().getY() + 2.0f}), playheadColour),
                    "The playhead runs down through the divider and the lane");
            grid.playhead = -1.0f;
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_VELOCITY_LANE_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picked, *stream);
            }

            stack.removeChangeListener(&everyone);
            owned.reset();
            stack.releaseAudioDevice();
        }
