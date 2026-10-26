        // The tools panel down the note editor's left, Live's Pitch and Time
        // Utilities: Reverse, Legato and Humanize on the selected notes or,
        // with none selected, on the whole clip, each one undo step; and the
        // scale whose rows the editor lights, moved here from the shell's
        // strip. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk()
                        && stack.editorStepCount() == 16,
                    "A one-bar clip of its own for the note tools");
            // a and d share a pitch, so Legato has a neighbour to stop at.
            juce::ValueTree a, b, c, d;
            stack.beginNoteGesture("Tools fixture");
            require(stack.addNote(0.0, 60, 1.0, &a).wasOk() && stack.addNote(2.0, 62, 2.0, &b).wasOk()
                        && stack.addNote(8.0, 64, 1.0, &c).wasOk() && stack.addNote(12.0, 60, 1.0, &d).wasOk(),
                    "The tools fixture's notes go down");
            stack.endNoteGesture();
            const auto noteOf = [&stack](const juce::ValueTree& state)
            {
                for (const auto& note : stack.editorNotes())
                    if (note.state == state) return note;
                return Session::EditorNote {};
            };
            const auto at = [&](const juce::ValueTree& state, double start, double length)
            {
                const auto note = noteOf(state);
                return close(note.startSteps, start) && close(note.lengthSteps, length);
            };
            const auto fixtureIntact = [&]
            {
                return stack.editorNotes().size() == 4 && at(a, 0.0, 1.0) && at(b, 2.0, 2.0) && at(c, 8.0, 1.0)
                    && at(d, 12.0, 1.0);
            };
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(900, StepGrid::openingHeight);
            grid.changeListenerCallback(nullptr);
            juce::String said;
            grid.status = [&said](juce::String message) { said = std::move(message); };
            auto panelOwned = std::make_unique<NoteToolsPanel>(stack, grid);
            auto& panel = *panelOwned;
            panel.setSize(NoteToolsPanel::width, StepGrid::openingHeight);
            const auto undoOnce = [&]
            {
                stack.undo();
                grid.changeListenerCallback(nullptr);
            };

            // The panel holds every control inside itself, the Humanize
            // amount on the row beside its button.
            for (auto* child : panel.getChildren())
                require(!child->getBounds().isEmpty() && panel.getLocalBounds().contains(child->getBounds()),
                        "Every control of the tools panel stands inside it");
            require(panel.humanizeAmount.getY() == panel.humanizeButton.getY()
                        && panel.humanizeAmount.getRight() < panel.humanizeButton.getX(),
                    "The Humanize amount stands beside its button");

            // The scale: a root and a scale light their rows, and the switch
            // puts the light out. D Dorian has C and not C sharp; D major has
            // C sharp and not C.
            const auto pixelOnRow = [&grid](int pitch)
            {
                juce::Image image(juce::Image::ARGB, grid.getWidth(), grid.getHeight(), true, juce::SoftwareImageType());
                juce::Graphics g(image);
                grid.paintEntireComponent(g, false);
                return image.getPixelAt(juce::roundToInt(grid.cell(6, grid.rowOfPitch(pitch)).getCentreX()),
                                        juce::roundToInt(grid.cell(6, grid.rowOfPitch(pitch)).getCentreY()));
            };
            require(grid.rowOfPitch(60) >= 0 && grid.rowOfPitch(62) < grid.visiblePitchRows() && grid.rowOfPitch(62) >= 0
                        && grid.rowOfPitch(60) < grid.visiblePitchRows(),
                    "The scale's rows are on screen");
            const auto plainC = pixelOnRow(60), plainCSharp = pixelOnRow(61), plainD = pixelOnRow(62);
            require(!grid.scaleShown, "No scale is lit until one is chosen");
            panel.rootBox.setSelectedId(3, juce::sendNotificationSync);
            panel.scaleBox.setSelectedId(static_cast<int>(MusicalScale::Dorian) + 1, juce::sendNotificationSync);
            require(grid.scaleShown && grid.scaleRoot == 2 && grid.scaleKind == MusicalScale::Dorian
                        && panel.highlightButton.getToggleState(),
                    "Choosing a root and a scale in the panel lights it in the editor");
            require(pixelOnRow(62) != plainD && pixelOnRow(60) != plainC && pixelOnRow(61) == plainCSharp,
                    "D Dorian lights D and C and leaves C sharp dark");
            panel.scaleBox.setSelectedId(static_cast<int>(MusicalScale::Major) + 1, juce::sendNotificationSync);
            require(pixelOnRow(61) != plainCSharp && pixelOnRow(60) == plainC, "D major lights C sharp and not C");
            // The panel beside the editor as the shell lays them out, to look at.
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_NOTE_TOOLS_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                grid.setSelectedStates({b, c});
                // In the shell's look, which the runner does not install.
                Theme look;
                panel.setLookAndFeel(&look);
                grid.setLookAndFeel(&look);
                juce::Image picture(juce::Image::ARGB, panel.getWidth() + grid.getWidth(), grid.getHeight(), true,
                                    juce::SoftwareImageType());
                {
                    juce::Graphics g(picture);
                    panel.paintEntireComponent(g, false);
                    g.setOrigin(panel.getWidth(), 0);
                    grid.paintEntireComponent(g, false);
                }
                panel.setLookAndFeel(nullptr);
                grid.setLookAndFeel(nullptr);
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picture, *stream);
                grid.clearSelection();
            }
            panel.highlightButton.setToggleState(false, juce::dontSendNotification);
            panel.highlightButton.onClick();
            require(!grid.scaleShown && pixelOnRow(62) == plainD, "The Highlight switch puts the scale out");
            require(panel.scaleBox.getNumItems() == musicalScaleCount - 1,
                    "Every scale Rhino Tune knows is offered but Chromatic, which would light every row");

            // Reverse, with nothing selected: the whole clip mirrored, so the
            // last note comes first.
            require(grid.selectedStates().empty(), "Nothing is selected");
            panel.reverseButton.onClick();
            require(at(a, 15.0, 1.0) && at(b, 12.0, 2.0) && at(c, 7.0, 1.0) && at(d, 3.0, 1.0),
                    "Reverse with nothing selected mirrors the whole clip");
            require(said == "Reversed every note in the clip", "and says it reversed the clip");
            undoOnce();
            require(fixtureIntact(), "One undo takes the whole reverse back");

            // With a selection, across the selection's own span; the rest stays.
            grid.setSelectedStates({a, b, c});
            require(grid.reverseNotes(), "Reverse the selected notes");
            require(at(a, 8.0, 1.0) && at(b, 5.0, 2.0) && at(c, 0.0, 1.0) && at(d, 12.0, 1.0),
                    "Reverse mirrors the selected notes across their own span and leaves the rest");
            require(grid.selectedStates().size() == 3 && said == "Reversed 3 notes", "and keeps them selected");
            undoOnce();
            require(fixtureIntact(), "in one undo step");

            // Legato with nothing selected: each note reaches the next start,
            // the last the clip's end, and a note stops at the next one on its
            // own pitch.
            grid.clearSelection();
            panel.legatoButton.onClick();
            require(at(a, 0.0, 2.0) && at(b, 2.0, 6.0) && at(c, 8.0, 4.0) && at(d, 12.0, 4.0),
                    "Legato with nothing selected joins every note to the next and the last to the clip's end");
            require(said == "Made every note in the clip legato", "and says so");
            undoOnce();
            require(fixtureIntact(), "One undo takes Legato back");
            grid.setSelectedStates({b, c});
            panel.legatoButton.onClick();
            require(at(b, 2.0, 6.0) && at(c, 8.0, 8.0) && at(a, 0.0, 1.0) && at(d, 12.0, 1.0),
                    "Legato on a selection joins only the selected notes to each other");
            require(said == "Made 2 notes legato", "and counts them");
            undoOnce();
            grid.setSelectedStates({a});
            require(grid.legatoNotes() && at(a, 0.0, 12.0),
                    "A note with nothing selected after it stops at the next note on its own pitch, not the clip's end");
            undoOnce();
            require(fixtureIntact(), "and that comes back too");

            // Humanize: every start moved by at most a quarter of a step at
            // 100%, none before the clip, lengths kept, one undo step, and the
            // same seed the same drift.
            grid.clearSelection();
            require(stack.humanizeNotes({}, 1.0, 7).wasOk(), "Humanize the whole clip");
            std::vector<double> drifted;
            auto moved = 0;
            for (const auto& [state, start, length] : {std::tuple(a, 0.0, 1.0), std::tuple(b, 2.0, 2.0),
                                                       std::tuple(c, 8.0, 1.0), std::tuple(d, 12.0, 1.0)})
            {
                const auto note = noteOf(state);
                require(note.startSteps >= 0.0 && std::abs(note.startSteps - start) <= Session::humanizeReachSteps + 1.0e-6
                            && close(note.lengthSteps, length),
                        "Humanize moves a start by at most a quarter of a step, never before the clip, and keeps its length");
                moved += close(note.startSteps, start) ? 0 : 1;
                drifted.push_back(note.startSteps);
            }
            require(moved >= 3, "Humanize at 100% moves the notes");
            undoOnce();
            require(fixtureIntact(), "One undo takes Humanize back");
            require(stack.humanizeNotes({}, 1.0, 7).wasOk() && close(noteOf(a).startSteps, drifted[0])
                        && close(noteOf(d).startSteps, drifted[3]),
                    "The same seed drifts the notes the same way");
            undoOnce();
            // Two notes back to back on one pitch never overlap or swap,
            // whatever the dice say. The drifts pile up rather than being
            // undone, because a throw that moves neither note leaves no undo
            // step to take back.
            juce::ValueTree first, second;
            stack.beginNoteGesture("Touching notes");
            require(stack.addNote(4.0, 67, 1.0, &first).wasOk() && stack.addNote(5.0, 67, 1.0, &second).wasOk(),
                    "Two notes touching on one pitch");
            stack.endNoteGesture();
            for (juce::int64 seed = 1; seed <= 24; ++seed)
            {
                stack.beginNoteGesture("Humanize");
                require(stack.humanizeNotes({first, second}, 1.0, seed).wasOk(), "Humanize the touching notes");
                stack.endNoteGesture();
                require(noteOf(first).startSteps >= 0.0
                            && noteOf(first).startSteps + noteOf(first).lengthSteps <= noteOf(second).startSteps + 1.0e-6,
                        "Humanize never makes two notes on one pitch overlap or swap");
            }
            stack.beginNoteGesture("Touching notes gone");
            require(stack.removeNotes({first, second}), "The touching notes go again");
            stack.endNoteGesture();
            grid.changeListenerCallback(nullptr);
            require(fixtureIntact(), "and Humanize on them touched nothing else");
            // Through the panel, at its own amount; at 0% it refuses.
            grid.setSelectedStates({c});
            panel.humanizeAmount.setValue(0.0, juce::dontSendNotification);
            panel.humanizeButton.onClick();
            require(fixtureIntact() && said.startsWith("Turn the Humanize amount up"),
                    "Humanize at 0% moves nothing and says why");
            panel.humanizeAmount.setValue(100.0, juce::dontSendNotification);
            panel.humanizeButton.onClick();
            require(at(a, 0.0, 1.0) && at(b, 2.0, 2.0) && at(d, 12.0, 1.0)
                        && std::abs(noteOf(c).startSteps - 8.0) <= Session::humanizeReachSteps + 1.0e-6
                        && said == "Humanized 1 note",
                    "The panel humanizes the selected note alone, by its own amount");
            undoOnce();
            require(fixtureIntact(), "in one undo step");

            // Reverse refuses to land a note on one that stays put.
            juce::ValueTree inTheWay;
            stack.beginNoteGesture("In the way");
            require(stack.addNote(8.0, 60, 1.0, &inTheWay).wasOk(), "A note where a reversed one would land");
            stack.endNoteGesture();
            grid.changeListenerCallback(nullptr);
            grid.setSelectedStates({a, c});
            require(!grid.reverseNotes() && at(a, 0.0, 1.0) && at(c, 8.0, 1.0) && said.startsWith("Reversing would land"),
                    "Reverse refuses to land a note on one that is not selected, and moves nothing");

            panelOwned.reset();
            owned.reset();
            stack.releaseAudioDevice();
        }
