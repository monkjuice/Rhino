        // What the grid shows and how it answers, after the second pass the
        // user asked for: the ruler in bars and beats, black keys' rows shaded,
        // notes shaded by velocity, no pitch bar at the right edge, snapping
        // with Alt to free it, a key clicked for its notes, and the preview
        // switch. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for the grid");
            juce::ValueTree loud, quiet, other, high;
            stack.beginNoteGesture("Grid fixture");
            require(stack.addNote(0.0, 60, 2.0, &loud, 127).wasOk() && stack.addNote(4.0, 61, 1.0, &quiet, 20).wasOk()
                        && stack.addNote(8.0, 60, 1.0, &other, 100).wasOk() && stack.addNote(12.0, 64, 1.0, &high, 100).wasOk(),
                    "The grid fixture's notes go down");
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
            const auto rowFor = [&grid](int pitch) { return grid.lowestVisiblePitch + grid.visiblePitchRows() - 1 - pitch; };
            const auto pixel = [](const juce::Image& image, juce::Point<float> at)
            {
                return image.getPixelAt(juce::roundToInt(at.x), juce::roundToInt(at.y));
            };
            const auto left = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
            const auto eventAt = [&grid](juce::Point<float> down, juce::Point<float> at, juce::ModifierKeys mods, int clicks = 1)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                        &grid, &grid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(),
                                        clicks, at != down);
            };
            const auto click = [&](juce::Point<float> at, juce::ModifierKeys mods)
            {
                grid.mouseDown(eventAt(at, at, mods));
                grid.mouseUp(eventAt(at, at, mods));
            };
            const auto selectionIs = [&grid](std::vector<juce::ValueTree> expected)
            {
                auto actual = grid.selectedStates();
                if (actual.size() != expected.size()) return false;
                for (const auto& state : expected)
                    if (std::find(actual.begin(), actual.end(), state) == actual.end()) return false;
                return true;
            };
            const auto noteAt = [&stack](double step, int pitch)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.pitch == pitch && std::abs(note.startSteps - step) < 0.0001)
                        return note;
                return Session::EditorNote {};
            };

            // The ruler counts bars and beats of the signature, in its own note
            // value: a quarter in 4/4, an eighth in 6/8.
            require(grid.stepsPerBeat() == 4 && grid.stepsPerBar() == 16, "In 4/4 a beat is four sixteenth steps");
            require(stack.setTimeSignature(6, 8).wasOk() && grid.stepsPerBeat() == 2 && grid.stepsPerBar() == 12,
                    "In 6/8 a beat is two of them and a bar twelve");
            require(stack.setTimeSignature(4, 4).wasOk(), "Back to 4/4");
            grid.changeListenerCallback(nullptr);
            auto shown = picture();
            const auto tickBottom = StepGrid::headerHeight - 2.0f;
            require(pixel(shown, {grid.xForStep(4.0), tickBottom}) == palette::border
                        && pixel(shown, {grid.xForStep(0.0), StepGrid::headerHeight - 7.0f}) == juce::Colour(0xff78818a),
                    "The ruler ticks every beat, and a bar's tick stands taller");
            const auto labelled = [&shown](float fromX)
            {
                int lit = 0;
                for (int y = 3; y < 19; ++y)
                    for (int x = juce::roundToInt(fromX) + 3; x < juce::roundToInt(fromX) + 26; ++x)
                        if (shown.getPixelAt(x, y).getBrightness() > 0.3f) ++lit;
                return lit;
            };
            require(labelled(grid.xForStep(0.0)) > 0 && labelled(grid.xForStep(4.0)) > 0 && labelled(grid.xForStep(2.0)) == 0,
                    "and names the bar and its beats, but not the steps between them");
            // Zoomed in until a beat is wider than half the view, the steps are
            // named too, so the ruler never stands empty.
            grid.stepZoom = 8.0;
            grid.stepScroll = 0.0;
            grid.syncHorizontalScroll();
            shown = picture();
            require(labelled(grid.xForStep(1.0)) > 0, "Zoomed in, the ruler names the steps within a beat");
            grid.stepZoom = 1.0;
            grid.syncHorizontalScroll();
            shown = picture();

            // A black key's row is darker than a white key's on the same beat,
            // and both shade by beat.
            const auto cellCentre = [&grid, &rowFor](int step, int pitch) { return grid.cell(step, rowFor(pitch)).getCentre(); };
            require(pixel(shown, cellCentre(2, 61)) == palette::noteCellBlackKey && pixel(shown, cellCentre(2, 62)) == palette::noteCell
                        && pixel(shown, cellCentre(6, 61)) == palette::noteCellBlackKeyAlternate
                        && pixel(shown, cellCentre(6, 62)) == palette::noteCellAlternate,
                    "A black key's row is a step darker than a white key's, on either shade of beat");

            // A note's shade follows its velocity: full at 127, sinking towards
            // the cells as it gets quieter.
            const auto loudShade = pixel(shown, grid.boundsFor(*grid.noteForState(loud)).getCentre());
            const auto quietShade = pixel(shown, grid.boundsFor(*grid.noteForState(quiet)).getCentre());
            require(loudShade == palette::noteBody && quietShade.getBrightness() < loudShade.getBrightness() - 0.1f,
                    "A note at 127 wears the note colour and a quiet one a darker shade of it");
            grid.setSelectedStates({quiet});
            const auto selectedQuiet = pixel(picture(), grid.boundsFor(*grid.noteForState(quiet)).getCentre());
            require(grid.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0))
                        && noteAt(4.0, 61).velocity == 30,
                    "Ctrl+Up raises the quiet note");
            shown = picture();
            require(pixel(shown, grid.boundsFor(*grid.noteForState(quiet)).getCentre()).getBrightness()
                        > selectedQuiet.getBrightness(),
                    "and its shade brightens with it");
            stack.undo();
            grid.changeListenerCallback(nullptr);
            grid.clearSelection();

            // The pitch bar that looked like a scrollbar at the right edge is
            // gone: the last cell is one colour to the panel's edge.
            shown = picture();
            bool barFree = true;
            for (int pitch = grid.lowestVisiblePitch; pitch < grid.lowestVisiblePitch + grid.visiblePitchRows(); ++pitch)
            {
                const auto y = grid.cell(15, rowFor(pitch)).getCentreY();
                barFree = barFree && pixel(shown, {static_cast<float>(grid.getWidth()) - 4.0f, y})
                                         == pixel(shown, {static_cast<float>(grid.getWidth()) - 20.0f, y});
            }
            require(barFree, "Nothing is drawn over the right edge of the rows");

            // The keyboard: narrow keys, and the names in a column of their
            // own left of them, the octaves' Cs only however tall the rows.
            // A taller panel shows more rows rather than taller ones, so the
            // rows are zoomed.
            grid.setSize(1000, 600);
            require(grid.rowHeight() == StepGrid::baseRowHeight, "A taller panel leaves the rows their height");
            grid.zoomPitchAt(StepGrid::baseRowHeight / 16.0, StepGrid::headerHeight + grid.rowAreaHeight() * 0.5f);
            require(grid.rowHeight() >= 15.0f, "Tall rows, which used to name every key");
            shown = picture();
            const auto rowY = [&grid, &rowFor](int pitch) { return grid.cell(0, rowFor(pitch)).getCentreY(); };
            const auto namedAt = [&shown](float y)
            {
                int lit = 0;
                for (int x = 0; x < juce::roundToInt(StepGrid::keyLabelWidth); ++x)
                    for (int dy = -3; dy <= 3; ++dy)
                        if (shown.getPixelAt(x, juce::roundToInt(y) + dy).getBrightness() > 0.3f) ++lit;
                return lit;
            };
            require(namedAt(rowY(60)) > 0 && namedAt(rowY(62)) == 0 && namedAt(rowY(59)) == 0,
                    "Only the octave's C is named, in the column left of the keys");
            require(pixel(shown, {StepGrid::keyLabelWidth + 10.0f, rowY(62)}) == juce::Colour(0xffcdd3c8)
                        && pixel(shown, {6.0f, rowY(62)}) == palette::sideSurface,
                    "and the keys begin past the column");
            grid.pitchZoom = 1.0;
            grid.manualPitchScroll = false;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            shown = picture();

            // Snapped, a note's edge lands on a step's line; with Alt it follows
            // the pointer. Alt and a double-click put a note off the grid.
            const auto otherBounds = grid.boundsFor(*grid.noteForState(other));
            const juce::Point<float> rightEdge {otherBounds.getRight() - 1.0f, otherBounds.getCentreY()};
            const juce::Point<float> nearlyHalf {grid.xForStep(9.4), rightEdge.y};
            grid.mouseDown(eventAt(rightEdge, rightEdge, left));
            grid.mouseDrag(eventAt(rightEdge, nearlyHalf, left));
            grid.mouseUp(eventAt(rightEdge, nearlyHalf, left));
            require(std::abs(noteAt(8.0, 60).lengthSteps - 1.0) < 0.0001, "A snapped right edge short of half a step stays on its line");
            const juce::Point<float> pastHalf {grid.xForStep(9.6), rightEdge.y};
            grid.mouseDown(eventAt(rightEdge, rightEdge, left));
            grid.mouseDrag(eventAt(rightEdge, pastHalf, left));
            grid.mouseUp(eventAt(rightEdge, pastHalf, left));
            require(std::abs(noteAt(8.0, 60).lengthSteps - 2.0) < 0.0001, "and past half a step it lands on the next");
            stack.undo();
            grid.changeListenerCallback(nullptr);
            const auto altLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
            const juce::Point<float> offGridPoint {grid.xForStep(13.5) + 1.0f, grid.cell(13, rowFor(62)).getCentreY()};
            grid.mouseDown(eventAt(offGridPoint, offGridPoint, altLeft, 2));
            grid.mouseUp(eventAt(offGridPoint, offGridPoint, altLeft, 2));
            require(noteAt(13.5, 62).state.isValid() && std::abs(noteAt(13.5, 62).lengthSteps - 1.0) < 0.0001,
                    "Alt and a double-click put a note where the pointer is, a step long");
            stack.undo();
            grid.changeListenerCallback(nullptr);

            // Ctrl+E chops every selected note on the grid, whatever its
            // pitch. With Ctrl still down, a press and a drag up adds a part to
            // each note and down takes one away, while the mouse moving alone
            // changes nothing; the wheel never chops, it zooms and ends the
            // chop. The whole chop is one undo step.
            const auto noteCount = [&stack] { return stack.editorNotes().size(); };
            grid.setSelectedStates({loud, quiet});
            require(grid.keyPressed(juce::KeyPress('E', juce::ModifierKeys::commandModifier, 0)) && grid.subdivisionActive,
                    "Ctrl+E chops the selected notes, on two pitches");
            require(stack.hasNote(0, 60) && stack.hasNote(1, 60) && stack.hasNote(4, 61) && noteCount() == 5
                        && grid.selectedStates().size() == 3 && grid.subdivisionCount == 2,
                    "on the grid: a two-step note in two, a one-step note left whole");
            const auto hover = [&](float y)
            {
                const juce::Point<float> at {grid.xForStep(10.0), y};
                grid.mouseMove(eventAt(at, at, juce::ModifierKeys(juce::ModifierKeys::commandModifier)));
            };
            hover(150.0f);
            hover(150.0f - 3.0f * StepGrid::chopPixelsPerPart);
            require(grid.selectedStates().size() == 3 && noteCount() == 5 && grid.subdivisionActive,
                    "The mouse moving with no button down chops nothing further");
            const auto ctrlLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
            const juce::Point<float> chopFrom {grid.xForStep(10.0), 150.0f};
            grid.mouseDown(eventAt(chopFrom, chopFrom, ctrlLeft));
            require(grid.subdivisionActive && grid.gesture == StepGrid::Gesture::chop && grid.selectedStates().size() == 3,
                    "A press with Ctrl down keeps the chop open, and moves and selects nothing");
            grid.mouseDrag(eventAt(chopFrom, chopFrom.translated(0.0f, -2.0f * StepGrid::chopPixelsPerPart), ctrlLeft));
            require(grid.selectedStates().size() == 7 && noteCount() == 9 && grid.subdivisionCount == 4,
                    "Dragging up two parts' worth adds two parts to every chopped note");
            grid.mouseDrag(eventAt(chopFrom, chopFrom.translated(0.0f, -StepGrid::chopPixelsPerPart), ctrlLeft));
            require(grid.selectedStates().size() == 5 && grid.subdivisionCount == 3, "and dragging back down takes one away");
            grid.mouseUp(eventAt(chopFrom, chopFrom.translated(0.0f, -StepGrid::chopPixelsPerPart), ctrlLeft));
            require(grid.subdivisionActive && grid.gesture == StepGrid::Gesture::none,
                    "Letting go of the button leaves the chop open while Ctrl is down");
            const auto chopZoom = grid.stepZoom;
            juce::MouseWheelDetails chopNotch;
            chopNotch.deltaY = 0.5f;
            grid.mouseWheelMove(eventAt({grid.xForStep(10.0), 150.0f}, {grid.xForStep(10.0), 150.0f},
                                        juce::ModifierKeys(juce::ModifierKeys::commandModifier)),
                                chopNotch);
            require(!grid.subdivisionActive && grid.selectedStates().size() == 5 && grid.stepZoom > chopZoom,
                    "The wheel with Ctrl zooms and ends the chop, chopping nothing further");
            stack.undo();
            grid.changeListenerCallback(nullptr);
            require(noteCount() == 4 && std::abs(noteAt(0.0, 60).lengthSteps - 2.0) < 0.0001
                        && std::abs(noteAt(4.0, 61).lengthSteps - 1.0) < 0.0001,
                    "One undo takes the whole chop back");
            grid.stepZoom = 1.0;
            grid.syncHorizontalScroll();
            grid.clearSelection();

            // A key clicked selects every note of its pitch in the clip; Shift
            // adds a key's notes, Ctrl takes them out when all are in, and a
            // drag on the keys selects nothing.
            const auto keyOf = [&grid, &rowFor](int pitch)
            {
                return juce::Point<float>(StepGrid::labelWidth * 0.5f, grid.cell(0, rowFor(pitch)).getCentreY());
            };
            click(keyOf(60), left);
            require(selectionIs({loud, other}), "Clicking a key selects every note of its pitch");
            click(keyOf(64), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
            require(selectionIs({loud, other, high}), "Shift and a key add its notes");
            click(keyOf(60), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier));
            require(selectionIs({high}), "Ctrl and a key whose notes are all selected take them out");
            const auto keyFrom = keyOf(61);
            const auto keyTo = keyFrom.translated(0.0f, grid.rowHeight() * 2.0f);
            grid.mouseDown(eventAt(keyFrom, keyFrom, left));
            grid.mouseDrag(eventAt(keyFrom, keyTo, left));
            grid.mouseUp(eventAt(keyFrom, keyTo, left));
            require(selectionIs({high}), "Dragging the keys scrolls them and selects nothing");
            grid.changeListenerCallback(nullptr);

            // A marquee is gone when the button comes up: the release repaints
            // where it stood, which nothing else did, so it stayed drawn until
            // something happened to paint over it. A cached image is told of
            // every repaint, so one standing in logs them; the grid is made
            // visible first, since a hidden component repaints nothing. With
            // the release's repaint taken out, this check fails.
            {
                struct RepaintLog final : juce::CachedComponentImage
                {
                    juce::RectangleList<int> areas;
                    void paint(juce::Graphics&) override {}
                    bool invalidateAll() override { areas.add({0, 0, 1 << 16, 1 << 16}); return false; }
                    bool invalidate(const juce::Rectangle<int>& area) override { areas.add(area); return false; }
                    void releaseResources() override {}
                };
                const auto boxFrom = grid.cell(1, rowFor(62)).getCentre();
                const auto boxTo = grid.cell(6, rowFor(58)).getCentre();
                grid.mouseDown(eventAt(boxFrom, boxFrom, left));
                grid.mouseDrag(eventAt(boxFrom, boxTo, left));
                const auto box = grid.selectionBox.getSmallestIntegerContainer();
                require(grid.gesture == StepGrid::Gesture::select && !box.isEmpty(), "A drag across empty cells draws a marquee");
                grid.setVisible(true);
                auto* repaintLog = new RepaintLog();
                grid.setCachedComponentImage(repaintLog);
                // Installing it repaints everything; only the release counts.
                repaintLog->areas.clear();
                grid.mouseUp(eventAt(boxFrom, boxTo, left));
                const auto repainted = repaintLog->areas;
                grid.setCachedComponentImage(nullptr);
                grid.setVisible(false);
                require(grid.selectionBox.isEmpty() && repainted.containsRectangle(box),
                        "and letting go repaints where it stood, so it disappears");
                grid.clearSelection();
            }

            // The preview: off until switched on, and then a key sounds while it
            // is held and a note while it is carried, to each pitch it reaches.
            require(!stack.noteAuditionEnabled() && !grid.previewButton.getToggleState(), "The preview starts off");
            click(keyOf(60), left);
            require(stack.auditionedPitch() < 0, "and a key then sounds nothing");
            grid.previewButton.onClick();
            require(stack.noteAuditionEnabled() && grid.previewButton.getToggleState(), "The headphones switch it on");
            // The whole editor as it now stands, to look at.
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_NOTE_GRID_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picture(), *stream);
            }
            grid.mouseDown(eventAt(keyOf(60), keyOf(60), left));
            require(stack.auditionedPitch() == 60, "A key sounds while it is held");
            grid.mouseUp(eventAt(keyOf(60), keyOf(60), left));
            require(stack.auditionedPitch() < 0, "and stops when it is let go");
            const auto from = grid.boundsFor(*grid.noteForState(other)).getCentre();
            const auto up = from.translated(0.0f, -grid.rowHeight());
            grid.mouseDown(eventAt(from, from, left));
            require(stack.auditionedPitch() == 60, "A note sounds when it is pressed");
            grid.mouseDrag(eventAt(from, up, left));
            require(stack.auditionedPitch() == 61, "and again at each pitch it is carried to");
            grid.mouseUp(eventAt(from, up, left));
            require(stack.auditionedPitch() < 0, "and stops when it is put down");
            stack.undo();
            grid.changeListenerCallback(nullptr);
            grid.previewButton.onClick();
            require(!stack.noteAuditionEnabled() && !grid.previewButton.getToggleState(), "The headphones switch it off again");

            owned.reset();
            stack.releaseAudioDevice();
        }
