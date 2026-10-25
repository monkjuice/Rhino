        // The note editor reads a region exactly as the arrangement does, in
        // steps instead of seconds, and the same four commands act on it.
        {
            // On the heap deliberately. Every scenario shares one stack frame,
            // and a StepGrid carries about 200 KB of row-addressed note caches
            // - two more of them on a 1 MB stack is a stack overflow in the
            // middle of the deepest paint the suite does.
            auto grids = std::make_unique<StepGrid>(session);
            auto& clipboardGrid = *grids;
            clipboardGrid.setSize(1000, 250);
            clipboardGrid.changeListenerCallback(nullptr);

            std::vector<juce::ValueTree> previousNotes;
            for (const auto& note : session.editorNotes()) previousNotes.push_back(note.state);
            session.beginNoteGesture("Clipboard fixture");
            session.removeNotes(previousNotes);
            session.endNoteGesture();
            session.setEditorStepCount(16);
            require(session.editorStepCount() == 16, "The clipboard fixture is one bar of sixteen steps");
            session.beginNoteGesture("Clipboard fixture notes");
            require(session.addNote(0.0, 48, 1.0).wasOk(), "Can place the first fixture note");
            require(session.addNote(4.0, 50, 1.0).wasOk(), "Can place the second fixture note");
            require(session.addNote(8.0, 48, 1.0).wasOk(), "Can place the third fixture note");
            session.endNoteGesture();
            clipboardGrid.changeListenerCallback(nullptr);

            const auto noteAt = [&session](double step, int pitch)
            {
                for (const auto& note : session.editorNotes())
                    if (note.pitch == pitch && std::abs(note.startSteps - step) < 0.0001)
                        return note.state;
                return juce::ValueTree();
            };
            const auto noteCount = [&session] { return session.editorNotes().size(); };
            const auto command = [&clipboardGrid](int key)
            {
                return clipboardGrid.keyPressed(juce::KeyPress(key, juce::ModifierKeys::commandModifier, 0));
            };

            // Selecting every note makes the region the whole clip, not the
            // distance between the first and last note: a bar whose last note
            // stops early is still a bar, and its copy belongs at step sixteen.
            require(command('A'), "Ctrl+A selects every note");
            require(close(clipboardGrid.stepSelection.start, 0.0) && close(clipboardGrid.stepSelection.end, 16.0),
                    "Selecting every note makes the region the whole clip");

            require(command('D'), "Ctrl+D duplicates the selection");
            require(session.editorStepCount() == 32, "Duplicating a whole bar lengthens the clip to two");
            require(noteAt(16.0, 48).isValid() && noteAt(20.0, 50).isValid() && noteAt(24.0, 48).isValid(),
                    "Ctrl+D places the copy directly after the end of the selection");
            require(noteCount() == 6, "Ctrl+D copies the selection once");
            require(close(clipboardGrid.stepSelection.start, 16.0) && close(clipboardGrid.stepSelection.end, 32.0),
                    "Ctrl+D carries the region onto the copy it made");

            require(command('D'), "A second Ctrl+D is accepted");
            require(session.editorStepCount() == 48, "Holding Ctrl+D keeps extending the pattern");
            require(noteAt(32.0, 48).isValid() && noteAt(36.0, 50).isValid(),
                    "The second copy lands after the first");

            // Paste replaces what it lands on rather than hunting for the
            // first free space, which is what it used to do.
            const auto displaced = noteAt(8.0, 48);
            require(displaced.isValid(), "There is a note to be replaced");
            clipboardGrid.setSelectedStates({noteAt(0.0, 48)});
            require(command('C'), "Ctrl+C copies the selected note");
            clipboardGrid.setStepInsertPoint(8.0);
            require(command('V'), "Ctrl+V pastes at the insert point");
            require(noteAt(8.0, 48).isValid(), "The paste lands on the step it was asked for");
            require(noteAt(8.0, 48) != displaced, "The note that was there has been replaced");
            require(noteCount() == 9, "Replacing a note does not leave the old one behind");

            // Cut takes its region away and keeps what it took.
            clipboardGrid.setSelectedStates({noteAt(4.0, 50)});
            require(command('X'), "Ctrl+X cuts the selected note");
            require(!noteAt(4.0, 50).isValid(), "Ctrl+X removes what it took");
            require(noteCount() == 8, "Ctrl+X removes exactly the selection");
            clipboardGrid.setStepInsertPoint(12.0);
            require(command('V'), "What was cut can be pasted somewhere else");
            require(noteAt(12.0, 50).isValid(), "The cut note comes back where it was asked for");

            // Selecting notes sets the region and leaves the insert line where
            // it was: the notes already show what is selected, and the line
            // says only where a paste would land.
            clipboardGrid.setStepInsertPoint(12.0);
            clipboardGrid.setSelectedStates({noteAt(0.0, 48)});
            require(clipboardGrid.stepSelection.active && close(clipboardGrid.stepSelection.start, 0.0)
                        && close(clipboardGrid.insertStep, 12.0),
                    "Selecting notes sets the region and leaves the insert line alone");

            // An explicit span draws its insert point and nothing else. The
            // marquee and the notes it catches are already two ways of showing
            // a selection; a band behind either of them read as a third.
            clipboardGrid.setStepSelection(44.0, 48.0);
            clipboardGrid.stepScroll = 32.0;
            const auto emptySpanPicture = clipboardGrid.createComponentSnapshot(clipboardGrid.getLocalBounds());
            const auto spanX = [&clipboardGrid](double step)
            {
                return static_cast<int>(clipboardGrid.labelWidth
                    + static_cast<float>(step - clipboardGrid.stepScroll) * clipboardGrid.cellWidth());
            };
            int bandPixels = 0;
            for (int y = static_cast<int>(clipboardGrid.headerHeight) - 3;
                 y <= static_cast<int>(clipboardGrid.headerHeight) + 3; ++y)
                for (int x = spanX(44.0) + 8; x < spanX(48.0) - 8; ++x)
                    if (isMidiNotePixel(emptySpanPicture.getPixelAt(x, y))) ++bandPixels;
            require(bandPixels == 0, "A step span draws no band across the lanes it covers");

            // Escape drops the region as well as the selection, so the next
            // paste is not steered by a span nobody can see any more.
            clipboardGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            require(!clipboardGrid.stepSelection.active, "Escape clears the region");
            require(clipboardGrid.insertStep < 0.0, "and the insert line");
            require(clipboardGrid.selectedStates().empty(), "Escape clears the note selection");

            // Copy and paste with nothing clicked in between. A copy carries
            // the insert line to the end of what it copied, so the paste lies
            // beside its source. It used to land on the source and replace it
            // with itself: nothing showed, and dragging the "copy" away left
            // a hole where the source had been, which read as a cut.
            {
                std::vector<juce::ValueTree> everything;
                for (const auto& note : session.editorNotes()) everything.push_back(note.state);
                session.beginNoteGesture("Paste fixture");
                session.removeNotes(everything);
                session.endNoteGesture();
                juce::ValueTree copied, other;
                session.beginNoteGesture("Paste fixture notes");
                require(session.addNote(2.0, 60, 2.0, &copied).wasOk() && session.addNote(0.0, 64, 1.0, &other).wasOk(),
                        "Two notes for the paste fixture");
                session.endNoteGesture();
                clipboardGrid.changeListenerCallback(nullptr);

                clipboardGrid.setSelectedStates({copied});
                require(command('C'), "Ctrl+C copies the note");
                require(close(clipboardGrid.insertStep, 4.0), "and carries the insert line to the end of what it copied");
                require(command('V'), "Ctrl+V straight after it pastes");
                require(noteAt(2.0, 60) == copied, "The source is untouched: a paste never lands on what was copied");
                require(noteAt(4.0, 60).isValid() && noteCount() == 3, "The copy lies beside it");
                require(close(clipboardGrid.insertStep, 6.0), "and the line moves past the copy");
                require(command('V') && noteAt(6.0, 60).isValid() && noteCount() == 4,
                        "so a second paste lies beside the first rather than writing over it");

                // The line is not the region, which every change in the
                // document reads off the selected notes again.
                session.sendSynchronousChangeMessage();
                require(close(clipboardGrid.insertStep, 8.0), "The line survives a change elsewhere in the document");

                // Duplicate takes its own copy and leaves the clipboard alone,
                // as the arrangement's does.
                clipboardGrid.setSelectedStates({other});
                require(command('D') && noteAt(1.0, 64).isValid(), "Ctrl+D duplicates the other note");
                clipboardGrid.setStepInsertPoint(12.0);
                require(command('V') && noteAt(12.0, 60).isValid() && !noteAt(12.0, 64).isValid(),
                        "and Ctrl+V still pastes what was copied before it");

                // With no line - after Escape - a paste goes just after the
                // selected notes, never on top of them.
                clipboardGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                clipboardGrid.setSelectedStates({noteAt(4.0, 60)});
                const auto before = noteCount();
                require(command('V') && noteAt(4.0, 60).isValid() && noteAt(6.0, 60).isValid() && noteCount() == before,
                        "With no line a paste lands after the selection, replacing what is there and nothing it copied from");
                clipboardGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            }
        }
