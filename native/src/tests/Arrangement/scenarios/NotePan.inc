        // Holding the middle button and moving the mouse carries the note
        // editor's view, as it carries the arrangement's: along the steps by
        // the pixels travelled, through the pitch rows by the rows travelled,
        // and whatever the press landed on - a note, the keys - is left alone.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for panning");
            stack.setEditorStepCount(64);
            juce::ValueTree held;
            stack.beginNoteGesture("Pan fixture");
            require(stack.addNote(4.0, 60, 2.0, &held).wasOk(), "A note to press on");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            const auto eventAt = [&grid](juce::Point<float> down, juce::Point<float> at)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                    juce::ModifierKeys(juce::ModifierKeys::middleButtonModifier),
                    1.0f, 0, 0, 0, 0, &grid, &grid, juce::Time::getCurrentTime(),
                    down, juce::Time::getCurrentTime(), 1, at != down);
            };
            const auto pan = [&grid, &eventAt](juce::Point<float> down, juce::Point<float> to)
            {
                grid.mouseDown(eventAt(down, down));
                grid.mouseDrag(eventAt(down, to));
                grid.mouseUp(eventAt(down, to));
            };

            // Sideways. Sixty-four steps open four times deeper than the view,
            // so there is somewhere to go either way from the middle.
            require(grid.visibleStepSpan() < 64.0, "Sixty-four steps are wider than the view");
            grid.stepScroll = 16.0;
            grid.syncHorizontalScroll();
            const auto startedAt = grid.stepScroll;
            const auto pixelsPerStep = static_cast<double>(grid.cellWidth());
            const auto from = juce::Point<float>(StepGrid::labelWidth + grid.gridWidth() * 0.5f,
                                                 StepGrid::headerHeight + grid.rowAreaHeight() * 0.5f);
            pan(from, from.translated(-120.0f, 0.0f));
            require(close(grid.stepScroll, startedAt + 120.0 / pixelsPerStep),
                    "Dragging left with the middle button moves the steps right by the distance travelled");
            pan(from, from.translated(120.0f, 0.0f));
            require(close(grid.stepScroll, startedAt), "Dragging the same distance back returns to where it started");
            pan(from, from.translated(4000.0f, 0.0f));
            require(close(grid.stepScroll, 0.0), "A pan past the start of the clip stops there");

            // Up and down, a whole row at a time, keeping the pitch under the
            // pointer under it.
            const auto lowest = grid.lowestVisiblePitch;
            const auto rows = grid.rowHeight();
            const auto pitchUnder = grid.pitchAtY(from.y);
            pan(from, from.translated(0.0f, rows * 3.0f));
            require(grid.lowestVisiblePitch == lowest + 3, "Dragging down three rows brings three higher pitches into view");
            require(close(grid.pitchAtY(from.y + rows * 3.0f), pitchUnder), "The pitch under the pointer stays under it");
            require(grid.manualPitchScroll, "The pitch window stays where the pan left it");
            pan(from, from.translated(0.0f, -rows * 3.0f));
            require(grid.lowestVisiblePitch == lowest, "Dragging back up returns the rows");

            // A pan is not a note drag, whatever it started on. A middle press
            // on a note used to take the note and carry it.
            const auto* note = grid.noteForState(held);
            require(note != nullptr, "The note is on screen");
            const auto onNote = grid.boundsFor(*note).getCentre();
            const auto revision = stack.changeRevision;
            pan(onNote, onNote.translated(-30.0f, 0.0f));
            require(stack.changeRevision == revision, "Panning from a note neither moves nor edits it");
            require(grid.selectedStates().empty(), "Panning from a note does not select it");
            require(grid.gesture == StepGrid::Gesture::none, "The pan ends when the button comes up");

            // Nor is it a drag of the keys, which sideways resizes the rows.
            const auto zoom = grid.pitchZoom;
            const auto onKeys = juce::Point<float>(StepGrid::labelWidth * 0.5f, from.y);
            pan(onKeys, onKeys.translated(-40.0f, rows * 2.0f));
            require(grid.pitchZoom == zoom, "Panning from the keys leaves the row height alone");
            require(grid.lowestVisiblePitch == lowest + 2, "Panning from the keys carries the rows like anywhere else");
            require(grid.createComponentSnapshot(grid.getLocalBounds()).isValid(), "The panned editor paints");
            stack.releaseAudioDevice();
        }
