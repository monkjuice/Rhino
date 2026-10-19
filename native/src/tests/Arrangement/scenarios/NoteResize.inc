        // Pointer events and the gesture clock must agree on Alt: otherwise
        // every timer tick snaps the note back between free pointer updates.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for note resizing");
            stack.setEditorStepCount(32);
            juce::ValueTree resized;
            stack.beginNoteGesture("Resize fixture");
            require(stack.addNote(4.0, 60, 2.0, &resized).wasOk()
                        && stack.addNote(1.5, 60, 1.0).wasOk() && stack.addNote(11.0, 60, 1.0).wasOk(),
                    "A resize fixture with a neighbour on either side");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto alt = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            const auto eventAt = [&grid](juce::Point<float> down, juce::Point<float> at, juce::ModifierKeys mods)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods,
                    1.0f, 0, 0, 0, 0, &grid, &grid, juce::Time::getCurrentTime(),
                    down, juce::Time::getCurrentTime(), 1, at != down);
            };
            const auto placed = [&]()
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == resized) return note;
                throw std::runtime_error("The resized note still exists");
            };
            const auto rangeIs = [&](double start, double end)
            {
                const auto note = placed();
                return close(note.startSteps, start) && close(note.startSteps + note.lengthSteps, end);
            };
            const auto handle = [&](bool fromLeft)
            {
                const auto* note = grid.noteForState(resized);
                require(note != nullptr, "The resize handle is visible");
                const auto bounds = grid.boundsFor(*note);
                return juce::Point<float>(fromLeft ? bounds.getX() + 1.0f : bounds.getRight() - 1.0f,
                                          bounds.getCentreY());
            };
            const auto heldStill = [&]
            {
                const auto before = stack.changeRevision;
                const auto note = placed();
                for (int tick = 0; tick < 4; ++tick) grid.timerCallback();
                require(rangeIs(note.startSteps, note.startSteps + note.lengthSteps),
                        "The resize clock preserves an Alt-dragged edge between pointer events");
                require(stack.changeRevision == before,
                        "A stationary resize does no repeated model, undo or UI work");
            };

            auto down = handle(false);
            auto at = juce::Point<float>(grid.xForStep(7.375), down.y);
            grid.mouseDown(eventAt(down, down, alt));
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(4.0, 7.375), "Alt resizes the right edge between grid cells");
            heldStill();
            // Smaller than a sixteenth of a cell, including subpixel travel:
            // free resizing follows each event without a hidden fine grid.
            for (int pixel = 0; pixel < 12; ++pixel)
            {
                at.x += 0.5f;
                grid.mouseDrag(eventAt(down, at, alt));
                require(rangeIs(4.0, grid.stepAtX(at.x)), "Every half-pixel of an Alt resize reaches the note");
                const auto* note = grid.noteForState(resized);
                require(note != nullptr && std::abs(grid.boundsFor(*note).getRight() - at.x) < 0.01f,
                        "The visible note edge follows the free resize immediately");
                heldStill();
            }
            const auto freeEnd = grid.stepAtX(at.x);
            grid.modifierKeysChanged(left);
            require(rangeIs(4.0, 7.0), "Releasing Alt while stationary restores snapping");
            heldStill();
            grid.modifierKeysChanged(alt);
            require(rangeIs(4.0, freeEnd), "Pressing Alt while stationary restores the free edge");
            heldStill();
            grid.mouseUp(eventAt(down, at, alt));
            require(rangeIs(4.0, freeEnd), "Releasing the pointer keeps the free length");
            stack.undo();
            require(rangeIs(4.0, 6.0), "One undo restores the entire right-edge resize");
            stack.redo();
            require(rangeIs(4.0, freeEnd), "Redo restores the exact free length");
            stack.undo();

            down = handle(true);
            at = {grid.xForStep(3.413), down.y};
            grid.mouseDown(eventAt(down, down, alt));
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(3.413, 6.0), "Alt resizes the left edge freely and keeps the right edge fixed");
            heldStill();
            grid.modifierKeysChanged(left);
            require(rangeIs(3.0, 6.0), "Left-edge snapping stays anchored to the start of the gesture");
            heldStill();
            grid.modifierKeysChanged(alt);
            require(rangeIs(3.413, 6.0), "The left edge returns to the pointer when Alt is pressed again");
            at.x = grid.xForStep(1.0);
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(2.5, 6.0), "A free left edge stops at the previous note");
            heldStill();
            at.x = grid.xForStep(3.271);
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(3.271, 6.0), "A clamped left edge immediately follows the pointer back");
            grid.mouseUp(eventAt(down, at, alt));
            stack.undo();
            require(rangeIs(4.0, 6.0), "One undo restores the entire left-edge resize");

            down = handle(false);
            at = {grid.xForStep(12.0), down.y};
            grid.mouseDown(eventAt(down, down, alt));
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(4.0, 11.0), "A free right edge stops at the next note");
            heldStill();
            at.x = grid.xForStep(7.413);
            grid.mouseDrag(eventAt(down, at, alt));
            require(rangeIs(4.0, 7.413), "A clamped right edge immediately follows the pointer back");
            grid.mouseUp(eventAt(down, at, alt));
            stack.undo();

            // The pointer can stop outside the grid while the clock carries
            // the edge across newly visible cells. Alt stays in force there.
            juce::ValueTree scrolling;
            require(stack.addNote(4.0, 61, 2.0, &scrolling).wasOk(), "An unobstructed note for autoscroll");
            resized = scrolling;
            down = handle(false);
            at = {grid.gridRight() + 12.0f, down.y};
            grid.mouseDown(eventAt(down, down, alt));
            grid.mouseDrag(eventAt(down, at, alt));
            for (int tick = 0; tick < 5; ++tick)
            {
                const auto previous = placed().lengthSteps;
                grid.timerCallback();
                require(rangeIs(4.0, grid.stepAtX(at.x)) && placed().lengthSteps > previous,
                        "Autoscroll extends the free edge smoothly on every clock tick");
            }
            grid.mouseUp(eventAt(down, at, alt));
            stack.undo();
            require(rangeIs(4.0, 6.0), "Autoscrolled resizing is still one undo step");
            require(grid.createComponentSnapshot(grid.getLocalBounds()).isValid(), "The resized editor paints");
            stack.releaseAudioDevice();
        }
