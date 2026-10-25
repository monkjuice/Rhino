        // Selection and transposition across the pitch window. The grid shows
        // the rows it has room for; a pattern is routinely taller, and the
        // parts of it that are scrolled away must still be selectable and
        // still move.
        {
            // A piano clip of its own, which the clipboard and gesture scenarios
            // after this one go on using. The shared clip by now is on the lane
            // a kit was dropped on, a drum clip whose rows are its rack's
            // assigned keys, and these are about the piano roll.
            int pianoLane = -1;
            te::EditItemID pianoClip;
            require(session.addTrack(Session::TrackType::midi, -1, &pianoLane).wasOk()
                        && session.createClip(pianoLane, 0.0, &pianoClip).wasOk()
                        && session.selectPatternClip(pianoClip).wasOk() && !session.isPatternDrums(),
                    "The note editor opens a piano clip on a lane of its own");
            StepGrid transposeGrid(session);
            // Sixteen whole rows at the opening height reach the high note but
            // not the low one.
            transposeGrid.setSize(1000, StepGrid::openingHeight);
            transposeGrid.changeListenerCallback(nullptr);

            std::vector<juce::ValueTree> existingNotes;
            for (const auto& note : session.editorNotes()) existingNotes.push_back(note.state);
            session.beginNoteGesture("Selection fixture");
            session.removeNotes(existingNotes);
            require(session.addNote(0.0, 40, 1.0).wasOk(), "Can place the low fixture note");
            require(session.addNote(2.0, 62, 1.0).wasOk(), "Can place the high fixture note");
            session.endNoteGesture();
            transposeGrid.changeListenerCallback(nullptr);

            const auto pitches = [&session]
            {
                std::vector<int> result;
                for (const auto& note : session.editorNotes()) result.push_back(note.pitch);
                std::sort(result.begin(), result.end());
                return result;
            };
            require(pitches() == std::vector<int>({40, 62}), "The fixture is the whole pattern");
            require(transposeGrid.visiblePitchRows() < 62 - 40, "The fixture is taller than the pitch window");
            require(transposeGrid.visibleNotes.size() == 1, "Only one fixture note is on screen");

            const auto ctrlA = juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0);
            require(transposeGrid.keyPressed(ctrlA), "Ctrl+A selects notes");
            require(transposeGrid.selectedStates().size() == 2,
                    "Ctrl+A selects the whole clip, not just the visible rows");
            transposeGrid.changeListenerCallback(nullptr);
            require(transposeGrid.selectedStates().size() == 2,
                    "A note scrolled out of the pitch window stays selected");

            const auto arrow = [&transposeGrid](int code, bool shift)
            {
                return transposeGrid.keyPressed(juce::KeyPress(code,
                    shift ? juce::ModifierKeys(juce::ModifierKeys::shiftModifier) : juce::ModifierKeys(), 0));
            };
            require(arrow(juce::KeyPress::upKey, true), "Shift+Up is handled");
            require(pitches() == std::vector<int>({52, 74}), "Shift+Up transposes every selected note an octave");
            require(arrow(juce::KeyPress::downKey, true), "Shift+Down is handled");
            require(pitches() == std::vector<int>({40, 62}), "Shift+Down transposes back down an octave");
            require(arrow(juce::KeyPress::upKey, false), "Up is handled");
            require(pitches() == std::vector<int>({41, 63}), "Up transposes a semitone");
            require(arrow(juce::KeyPress::downKey, false), "Down is handled");
            require(pitches() == std::vector<int>({40, 62}), "Down transposes a semitone back");

            session.undo();
            require(pitches() == std::vector<int>({41, 63}), "A transpose is one undo step");

            transposeGrid.clearSelection();
            require(!arrow(juce::KeyPress::upKey, false), "Up with nothing selected is left to the rest of the app");
            require(pitches() == std::vector<int>({41, 63}), "Nothing selected transposes nothing");
        }
