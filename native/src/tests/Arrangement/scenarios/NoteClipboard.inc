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

            // A region read off the notes is not drawn: the notes already show
            // what is selected, and a band across every pitch row on top of
            // them reads as a marquee still being dragged.
            clipboardGrid.setSelectedStates({noteAt(0.0, 48)});
            require(clipboardGrid.stepSelection.active && clipboardGrid.regionFromNotes,
                    "Selecting notes sets the region but marks it as theirs");
            clipboardGrid.setStepSelection(0.0, 4.0);
            require(!clipboardGrid.regionFromNotes, "A span asked for by itself is not a note selection");

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
            require(clipboardGrid.selectedStates().empty(), "Escape clears the note selection");
        }
