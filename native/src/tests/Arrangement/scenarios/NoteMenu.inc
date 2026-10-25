        // The right button opens a menu of what can be done to what it landed
        // on and edits nothing by itself; it used to erase. Each menu is caught
        // before it reaches the screen and its items are chosen by id. A
        // document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for the menus");
            juce::ValueTree held, high, other;
            stack.beginNoteGesture("Menu fixture");
            require(stack.addNote(0.0, 60, 4.0, &held).wasOk() && stack.addNote(4.0, 64, 1.0, &high).wasOk()
                        && stack.addNote(8.0, 60, 1.0, &other).wasOk(),
                    "The menu fixture's notes go down");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            // Sixteen whole rows, so the pitch window reaches down to 50.
            grid.setSize(1000, StepGrid::openingHeight);
            grid.changeListenerCallback(nullptr);

            juce::PopupMenu shown;
            std::function<void(int)> choose;
            int menus = 0;
            grid.presentMenu = [&](juce::PopupMenu menu, std::function<void(int)> chosen)
            {
                shown = std::move(menu);
                choose = std::move(chosen);
                ++menus;
            };
            // An item of the last menu, submenus and headers included, by the
            // start of its text and whether it can be chosen.
            const auto offers = [&shown](const juce::String& start, bool enabled = true)
            {
                juce::PopupMenu::MenuItemIterator each(shown, true);
                while (each.next())
                    if (const auto& item = each.getItem(); item.text.startsWith(start) && item.isEnabled == enabled)
                        return true;
                return false;
            };
            const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
            const auto eventAt = [&grid](juce::Point<float> at, juce::ModifierKeys mods)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(),
                                        1, false);
            };
            const auto rightClickAt = [&](juce::Point<float> at)
            {
                grid.mouseDown(eventAt(at, right));
                grid.mouseUp(eventAt(at, right));
            };
            const auto on = [&grid](const juce::ValueTree& state) { return grid.boundsFor(*grid.noteForState(state)).getCentre(); };
            const auto rowFor = [&grid](int pitch) { return grid.lowestVisiblePitch + grid.visiblePitchRows() - 1 - pitch; };
            const auto count = [&stack] { return stack.editorNotes().size(); };
            const auto pitchOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note.pitch;
                return -1;
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

            // On a note: the note is taken, as a press would take it, and a menu
            // of its commands opens. Nothing is erased, in either mode.
            const auto quiet = stack.changeRevision;
            rightClickAt(on(held));
            require(menus == 1 && count() == 3 && stack.changeRevision == quiet,
                    "Right-clicking a note opens a menu and erases nothing");
            require(selectionIs({held}), "and takes the note it landed on");
            require(offers("C3") && offers("Cut") && offers("Copy") && offers("Duplicate") && offers("Delete")
                        && offers("Chop") && offers("On Grid") && offers("2 Parts") && offers("Up an Octave") && offers("Fill to Clip End"),
                    "The note's menu is named for its note and offers the note commands");
            require(offers("Paste", false), "and no paste before anything was copied");
            grid.setDrawMode(true);
            rightClickAt(on(high));
            require(menus == 2 && count() == 3 && stack.changeRevision == quiet && selectionIs({high}),
                    "In draw mode too the right button only asks");
            grid.setDrawMode(false);

            // Inside a selection it keeps the selection, so the menu acts on
            // all of it.
            grid.setSelectedStates({held, high});
            rightClickAt(on(high));
            require(selectionIs({held, high}) && offers("2 NOTES"), "Right-clicking inside a selection keeps it");
            require(offers("Chop"), "and notes on two pitches can be chopped, each where it stands");
            choose(StepGrid::octaveUpCommand);
            require(pitchOf(held) == 72 && pitchOf(high) == 76, "Transposing from the menu moves the whole selection");
            undoOnce();
            require(pitchOf(held) == 60 && pitchOf(high) == 64, "in one undo step");

            rightClickAt(on(other));
            choose(StepGrid::deleteCommand);
            require(count() == 2 && pitchOf(other) < 0, "Delete from the menu takes the note");
            undoOnce();
            require(count() == 3, "and one undo brings it back");

            rightClickAt(on(held));
            require(offers("Chop") && selectionIs({held}), "One note can be chopped");
            choose(StepGrid::chopCommand + 4);
            require(count() == 6 && grid.selectedStates().size() == 4 && stack.hasNote(0, 60) && stack.hasNote(3, 60),
                    "Chop into four parts makes four selected notes in the note's place");
            undoOnce();
            require(count() == 3 && pitchOf(held) == 60, "and one undo puts the note back whole");

            // Empty cells: the grid's menu, named for the cell, which puts a
            // note there and pastes where it was opened.
            const auto emptyCell = grid.cell(12, rowFor(62)).getCentre();
            rightClickAt(emptyCell);
            require(offers("D3, STEP 13") && offers("Add Note") && offers("Loop Clip") && offers("Select All"),
                    "Right-clicking an empty cell opens the grid's menu, named for the cell");
            choose(StepGrid::addNoteCommand);
            require(stack.hasNote(12, 62) && count() == 4, "Add Note puts a note in the cell the menu was opened on");
            undoOnce();
            grid.setSelectedStates({high});
            require(grid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0)), "Copy the high note");
            rightClickAt(grid.cell(10, rowFor(50)).getCentre());
            require(close(grid.insertStep, 10.0) && offers("Paste") && selectionIs({high}),
                    "The grid's menu keeps the selection and puts the insert line where it was opened");
            choose(StepGrid::pasteCommand);
            require(stack.hasNote(10, 64) && count() == 4,
                    "so Paste from it lands where the menu was opened, at the pitch that was copied");
            undoOnce();

            // The lane: a marker opens its note's menu, empty lane the grid's,
            // with no cell to put a note in.
            rightClickAt({grid.xForStep(4.0) + 2.0f, grid.yForVelocity(100.0f)});
            require(selectionIs({high}) && offers("Cut") && offers("E3"), "Right-clicking a marker opens its note's menu");
            rightClickAt({grid.xForStep(14.5), grid.yForVelocity(30.0f)});
            require(!offers("Add Note") && offers("Loop Clip"),
                    "Right-clicking empty lane opens the grid's menu, with no cell to add a note to");
            require(count() == 3, "and the lane erased nothing");

            // The keys open nothing.
            const auto menusBefore = menus;
            rightClickAt({StepGrid::labelWidth * 0.5f, grid.cell(0, rowFor(60)).getCentreY()});
            require(menus == menusBefore, "The keys open no menu");

            // Ctrl+E is the editor's even when it cannot chop anything: a
            // refusal fell through to the shell, where Ctrl+E splits the
            // arrangement's clip at its line.
            grid.clearSelection();
            require(grid.keyPressed(juce::KeyPress('E', juce::ModifierKeys::commandModifier, 0)) && count() == 3,
                    "Ctrl+E with nothing selected is answered by the note editor, not passed on");

            // Ctrl and the wheel zoom; they opened a chop of their own, which
            // chopped the selection for anyone reaching for the zoom.
            grid.setSelectedStates({held});
            const auto zoomBefore = grid.stepZoom;
            juce::MouseWheelDetails notch;
            notch.deltaY = 0.5f;
            grid.mouseWheelMove(eventAt(grid.cell(2, rowFor(60)).getCentre(), juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                                notch);
            require(count() == 3 && !grid.subdivisionActive && grid.stepZoom > zoomBefore,
                    "Ctrl and the wheel zoom in and chop nothing");
            // A sideways wheel travels along the clip rather than through the
            // pitches.
            juce::MouseWheelDetails sideways;
            sideways.deltaX = -0.5f;
            const auto scrollBefore = grid.stepScroll;
            const auto lowestBefore = grid.lowestVisiblePitch;
            grid.mouseWheelMove(eventAt(grid.cell(2, rowFor(60)).getCentre(), juce::ModifierKeys()), sideways);
            require(grid.stepScroll > scrollBefore && grid.lowestVisiblePitch == lowestBefore,
                    "A sideways wheel travels along the clip and leaves the pitch alone");

            // Left and Right carry the selection along the clip, Alt by a
            // sixteenth of a step.
            grid.setSelectedStates({other});
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)) && stack.hasNote(9, 60) && !stack.hasNote(8, 60),
                    "Right moves the selected note a step along the clip");
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0))
                        && !stack.hasNote(9, 60),
                    "Alt+Left moves it by a sixteenth of one");
            undoOnce();
            undoOnce();
            require(stack.hasNote(8, 60), "Each nudge is one undo step");
            grid.clearSelection();
            require(!grid.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)), "With nothing selected Right is not the editor's");

            grid.presentMenu = nullptr;
            owned.reset();
            stack.releaseAudioDevice();
        }
