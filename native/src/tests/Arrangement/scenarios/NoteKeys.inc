        // The keyboard down the editor's left is the keys and nothing else,
        // however far the grid is zoomed and scrolled, and it lights and names
        // the key under the pointer. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for the keys");
            stack.setEditorStepCount(64);
            stack.beginNoteGesture("Keys fixture");
            for (int step = 0; step < 60; step += 3)
                require(stack.addNote(step, 52 + step % 10, 2.5).wasOk(), "The keys fixture's notes go down");
            stack.endNoteGesture();
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);

            const auto picture = [&grid]
            {
                juce::Image image(juce::Image::RGB, grid.getWidth(), grid.getHeight(), true);
                juce::Graphics g(image);
                grid.paintEntireComponent(g, false);
                return image;
            };
            // Everything left of the grid from the top of the panel to the foot
            // of the rows: the loop button and the keys.
            const auto keysDiffer = [&grid](const juce::Image& a, const juce::Image& b)
            {
                int differing = 0;
                const auto bottom = static_cast<int>(StepGrid::headerHeight + grid.rowAreaHeight());
                for (int y = 0; y < bottom; ++y)
                    for (int x = 0; x < static_cast<int>(StepGrid::labelWidth); ++x)
                        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) ++differing;
                return differing;
            };
            require(grid.horizontalScroll.isVisible(), "The keys fixture is wider than the view");
            grid.scrollBarMoved(&grid.horizontalScroll, 8.0);
            const auto atWholeStep = picture();
            // Zoomed in until one step is wider than the keys, and scrolled half
            // a step along, so the step at the left edge starts left of them.
            // Its cells, notes and number were painted over the whole keyboard.
            grid.stepZoom = 16.0;
            grid.stepScroll = 20.5;
            grid.syncHorizontalScroll();
            require(grid.cellWidth() > StepGrid::labelWidth && close(grid.stepScroll, 20.5),
                    "One step is wider than the keys and the view starts half-way through one");
            require(keysDiffer(atWholeStep, picture()) == 0,
                    "A step scrolled part-way into view paints nothing over the keys or the loop button");
            grid.stepZoom = 4.0;
            grid.stepScroll = 0.0;
            grid.syncHorizontalScroll();

            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto eventAt = [&grid](juce::Point<float> down, juce::Point<float> at, juce::ModifierKeys mods)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        1, at != down);
            };
            const auto move = [&](juce::Point<float> at) { grid.mouseMove(eventAt(at, at, juce::ModifierKeys())); };
            const auto rowFor = [&grid](int pitch) { return grid.lowestVisiblePitch + grid.visiblePitchRows() - 1 - pitch; };
            // How many pixels down the middle of a key wear the note colour,
            // and how tall the lit run at the key's left edge stands.
            const auto litAcross = [&grid, &rowFor](const juce::Image& image, int pitch)
            {
                const auto y = static_cast<int>(StepGrid::headerHeight + (static_cast<float>(rowFor(pitch)) + 0.5f) * grid.rowHeight());
                int lit = 0;
                for (int x = 0; x < static_cast<int>(StepGrid::labelWidth); ++x)
                    if (image.getPixelAt(x, y) == palette::noteBody) ++lit;
                return lit;
            };
            const auto litDown = [](const juce::Image& image)
            {
                int lit = 0;
                for (int y = 0; y < image.getHeight(); ++y)
                    if (image.getPixelAt(2, y) == palette::noteBody) ++lit;
                return lit;
            };
            require(litDown(picture()) == 0, "No key is lit before the pointer arrives");

            // A cell names its key, and so does the key itself.
            const auto pitch = grid.lowestVisiblePitch + 4;
            move(grid.cell(9, rowFor(pitch)).getCentre());
            require(grid.hoveredPitch == pitch, "Pointing at a cell names its key");
            const auto pointed = picture();
            require(litAcross(pointed, pitch) > 30 && litAcross(pointed, pitch + 2) == 0,
                    "and that key alone is lit in the note colour");
            move({StepGrid::labelWidth * 0.5f, grid.cell(0, rowFor(pitch + 2)).getCentreY()});
            require(grid.hoveredPitch == pitch + 2, "Pointing at a key names it too");
            move({grid.xForStep(4.0), StepGrid::headerHeight * 0.5f});
            require(grid.hoveredPitch < 0 && litDown(picture()) == 0, "The ruler names no key");

            // A note in hand names the key it is carried to.
            const auto carried = grid.visibleNotes.front().state;
            const auto startPitch = grid.visibleNotes.front().pitch;
            const auto from = grid.boundsFor(grid.visibleNotes.front()).getCentre();
            const auto to = from.translated(0.0f, -2.0f * grid.rowHeight());
            grid.mouseDown(eventAt(from, from, left));
            grid.mouseDrag(eventAt(from, to, left));
            const auto pitchNow = [&stack, &carried]
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == carried) return note.pitch;
                return -1;
            };
            require(pitchNow() == startPitch + 2 && grid.hoveredPitch == startPitch + 2,
                    "A note carried up two rows names the key it has reached");
            grid.mouseUp(eventAt(from, to, left));
            stack.undo();
            grid.changeListenerCallback(nullptr);

            // A marker in the velocity lane names its note's key, the other
            // way round from a note lighting its marker.
            const auto* first = grid.laneNoteFor(carried);
            require(first != nullptr, "The carried note is back with its marker");
            move({grid.xForStep(first->start) + 1.0f, grid.yForVelocity(static_cast<float>(first->velocity))});
            require(grid.hoveredPitch == first->pitch, "Pointing at a marker names its note's key");
            grid.mouseExit(eventAt({0.0f, 0.0f}, {0.0f, 0.0f}, juce::ModifierKeys()));
            require(grid.hoveredPitch < 0, "and leaving the editor puts the key out");

            // Rows squeezed too thin to carry names: the key under the pointer
            // is still named, on a tag taller than its row.
            grid.zoomPitchAt(8.0, StepGrid::headerHeight + grid.rowAreaHeight() * 0.5f);
            require(grid.rowHeight() < StepGrid::octaveLabelRowHeight, "The rows are too thin for the keyboard's names");
            const auto thin = grid.lowestVisiblePitch + 6;
            move(grid.cell(3, rowFor(thin)).getCentre());
            require(grid.hoveredPitch == thin && litDown(picture()) >= static_cast<int>(StepGrid::keyTagHeight) - 1,
                    "and the key under the pointer is named on a tag taller than its row");
            // Both at once, zoomed and scrolled part-way into a step, to look at.
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_NOTE_KEYS_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                grid.stepZoom = 16.0;
                grid.stepScroll = 20.5;
                grid.syncHorizontalScroll();
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picture(), *stream);
            }

            owned.reset();
            stack.releaseAudioDevice();
        }
