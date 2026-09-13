        StepGrid selectionGrid(session);
        selectionGrid.setSize(1000, 250);
        selectionGrid.changeListenerCallback(nullptr);
        const auto defaultCellWidth = selectionGrid.cell(1, 0).getWidth();
        session.setEditorStepCount(32);
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.horizontalScroll.isVisible()
                && close(selectionGrid.visibleStepSpan(), static_cast<double>(Session::defaultSteps))
                && std::abs(selectionGrid.cell(1, 0).getWidth() - defaultCellWidth) < 0.1f,
                "Fine editor grids zoom horizontally instead of shrinking cells");
        session.setEditorStepCount(Session::defaultSteps);
        selectionGrid.changeListenerCallback(nullptr);
        require(close(selectionGrid.playheadXForTime(0.5), 54.0),
                "Note editor playhead starts at the selected clip even after bar one");
        require(close(selectionGrid.playheadXForTime(1.5), 527.0),
                "Note editor playhead follows selected clip-local beats");
        const auto selectedGridNotes = selectionGrid.notes;
        require(session.pattern().getSequence().getNumNotes() == 4, "Selected browser synth clip owns its preset notes");
        require(selectedGridNotes.count() == 4, "Selected browser synth clip populates the note editor");
        require(!session.isPatternDrums(), "Selected browser synth clip uses the note editor");
        require(session.selectPatternClip(patternID).wasOk(), "Can switch the editor back to the first pattern clip");
        require(session.selectPatternClip(view.selected).wasOk(), "Can switch the editor back to the non-first MIDI clip");
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.notes == selectedGridNotes, "Switching clips preserves the selected clip's note editor pattern");
        require(!session.isPatternDrums(), "Switching back restores the selected clip's instrument mode");
        const auto selectedClipNotes = session.pattern().getSequence().getNumNotes();
        session.setNote(1, 48, true);
        require(session.pattern().getSequence().getNumNotes() == selectedClipNotes + 1,
                "Note editor writes into the selected non-first MIDI clip");
        selectionGrid.changeListenerCallback(nullptr);
        const auto gridEvent = [&selectionGrid](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                                juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                mods, 1.0f, 0, 0, 0, 0,
                &selectionGrid, &selectionGrid, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto rulerPoint = juce::Point<float>(selectionGrid.cell(8, 0).getCentreX(), 13.0f);
        selectionGrid.mouseDown(gridEvent(rulerPoint, rulerPoint, false));
        require(close(selectionGrid.playheadXForTime(session.edit->getTransport().getPosition().inSeconds()), rulerPoint.x)
                    && close(selectionGrid.playhead, rulerPoint.x),
                "Clicking the note-editor ruler seeks within the selected clip");
        const auto editedClipRange = session.pattern().getPosition().time;
        require(selectionGrid.keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 'l')),
                "Ctrl-L is handled by the focused note editor");
        require(session.hasManualLoopRange()
                && close(session.edit->getTransport().getLoopRange().getStart().inSeconds(), editedClipRange.getStart().inSeconds())
                && close(session.edit->getTransport().getLoopRange().getEnd().inSeconds(), editedClipRange.getEnd().inSeconds())
                && session.edit->getTransport().looping,
                "Ctrl-L loops the entire edited MIDI clip");
        session.clearManualLoopRange();
        const auto loopStartPoint = juce::Point<float>(selectionGrid.cell(4, 0).getX(), 13.0f);
        const auto loopEndPoint = juce::Point<float>(selectionGrid.cell(12, 0).getX(), 13.0f);
        selectionGrid.mouseDown(gridEvent(loopStartPoint, loopStartPoint, false));
        selectionGrid.mouseDrag(gridEvent(loopStartPoint, loopEndPoint, true));
        selectionGrid.mouseUp(gridEvent(loopStartPoint, loopEndPoint, true));
        const auto expectedLoopStart = juce::jmap(4.0 / session.editorStepCount(),
                                                  editedClipRange.getStart().inSeconds(), editedClipRange.getEnd().inSeconds());
        const auto expectedLoopEnd = juce::jmap(12.0 / session.editorStepCount(),
                                                editedClipRange.getStart().inSeconds(), editedClipRange.getEnd().inSeconds());
        require(close(session.edit->getTransport().getLoopRange().getStart().inSeconds(), expectedLoopStart)
                && close(session.edit->getTransport().getLoopRange().getEnd().inSeconds(), expectedLoopEnd),
                "Dragging the note-editor ruler draws a snapped loop inside the clip");
        selectionGrid.mouseDown(gridEvent(loopStartPoint, loopStartPoint, false,
            juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)));
        require(!session.hasManualLoopRange(), "Right-clicking the note-editor ruler clears its manual loop");
        const auto rowForPitch = [&selectionGrid](int pitch)
        {
            return selectionGrid.lowestVisiblePitch + Session::pitches - 1 - pitch;
        };
        const auto resizeCell = selectionGrid.cell(1, rowForPitch(48));
        const juce::Point<float> resizeHandle(resizeCell.getRight() - 3.0f, resizeCell.getCentreY());
        const auto resizeTarget = selectionGrid.cell(4, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(resizeHandle, resizeHandle, false));
        selectionGrid.mouseDrag(gridEvent(resizeHandle, resizeTarget, true));
        selectionGrid.mouseUp(gridEvent(resizeHandle, resizeTarget, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(session.noteLengthSteps(1, 48) == 4
                && selectionGrid.noteLengths[static_cast<size_t>(selectionGrid.indexForCell(1, 48))] == 4,
                "Dragging a note edge resizes its musical length in the editor");
        session.setNote(1, 49, true);
        require(session.resizeNote(1, 49, 4).wasOk(), "Can create a sustained note for drag testing");
        selectionGrid.changeListenerCallback(nullptr);
        const auto sustainedFrom = selectionGrid.cell(2, rowForPitch(49)).getCentre();
        const auto sustainedTo = selectionGrid.cell(3, rowForPitch(49)).getCentre();
        selectionGrid.mouseDown(gridEvent(sustainedFrom, sustainedFrom, false));
        selectionGrid.mouseDrag(gridEvent(sustainedFrom, sustainedTo, true));
        selectionGrid.mouseUp(gridEvent(sustainedFrom, sustainedTo, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(!session.hasNote(1, 49) && session.hasNote(2, 49) && session.noteLengthSteps(2, 49) == 4,
                "Dragging from the body of a sustained note follows the pointer and preserves its length");
        juce::ValueTree halfNoteState;
        require(session.addNote(6.0, 49, 0.5, &halfNoteState).wasOk(),
                "Can create a half-cell note for fractional drag testing");
        selectionGrid.changeListenerCallback(nullptr);
        const auto* halfNote = selectionGrid.noteForState(halfNoteState);
        require(halfNote != nullptr, "The half-cell note appears in the editor");
        const auto halfNoteFrom = selectionGrid.boundsFor(*halfNote).getCentre();
        const auto halfNoteRight = halfNoteFrom.translated(selectionGrid.cellWidth() * 0.5f, 0.0f);
        selectionGrid.mouseDown(gridEvent(halfNoteFrom, halfNoteFrom, false));
        selectionGrid.mouseDrag(gridEvent(halfNoteFrom, halfNoteRight, true));
        selectionGrid.mouseUp(gridEvent(halfNoteFrom, halfNoteRight, true));
        selectionGrid.changeListenerCallback(nullptr);
        const auto* movedHalfNote = selectionGrid.noteForState(halfNoteState);
        require(movedHalfNote != nullptr && close(movedHalfNote->start, 6.5),
                "A half-cell note can be dragged into the second half of the same cell");
        const auto halfNoteLeft = halfNoteRight.translated(-selectionGrid.cellWidth() * 0.5f, 0.0f);
        selectionGrid.mouseDown(gridEvent(halfNoteRight, halfNoteRight, false));
        selectionGrid.mouseDrag(gridEvent(halfNoteRight, halfNoteLeft, true));
        selectionGrid.mouseUp(gridEvent(halfNoteRight, halfNoteLeft, true));
        selectionGrid.changeListenerCallback(nullptr);
        movedHalfNote = selectionGrid.noteForState(halfNoteState);
        require(movedHalfNote != nullptr && close(movedHalfNote->start, 6.0),
                "A half-cell note can be dragged back from the second half of a cell");
        require(session.removeNotes({halfNoteState}), "Can remove the fractional drag fixture");
        selectionGrid.changeListenerCallback(nullptr);
        selectionGrid.selectedNotes.reset();
        selectionGrid.selectedNotes.set(static_cast<size_t>(selectionGrid.indexForCell(2, 49)));
        const auto resolutionBeforeDivision = session.editorStepResolution();
        require(selectionGrid.keyPressed(juce::KeyPress('E', juce::ModifierKeys::ctrlModifier, 'e')),
                "Ctrl-E splits selected sustained notes");
        require(session.hasNote(2, 49) && session.hasNote(3, 49) && session.hasNote(4, 49) && session.hasNote(5, 49),
                "Split notes occupy their original grid cells");
        require(session.editorStepResolution() == resolutionBeforeDivision,
                "Dividing a note does not change the editor grid resolution");
        for (int division = 0; division < 5; ++division)
            require(selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::upKey,
                                                             juce::ModifierKeys::ctrlModifier, 0)),
                    "Ctrl-Up increases the live note division count");
        require(selectionGrid.subdivisionCount == 9 && selectionGrid.selectedNoteStates.size() == 9,
                "A four-cell note can be redistributed into nine independently selected retriggers");
        auto startsInFirstCell = 0;
        for (const auto& note : selectionGrid.visibleNotes)
            if (note.pitch == 49 && note.start >= 2.0 - 0.0001 && note.start < 3.0 - 0.0001)
                ++startsInFirstCell;
        require(startsInFirstCell > 1,
                "Multiple retriggers remain visible inside one unchanged editor cell");
        const auto splitPoint = selectionGrid.cell(3, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(splitPoint, splitPoint, false));
        selectionGrid.mouseUp(gridEvent(splitPoint, splitPoint, false));
        selectionGrid.changeListenerCallback(nullptr);
        require(!session.hasNote(1, 48) && !session.hasNote(3, 48),
                "Clicking inside a sustained note erases the complete note body");
        session.undo();
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        const auto moveFrom = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto moveTo = selectionGrid.cell(2, rowForPitch(48)).getCentre();
        selectionGrid.mouseDown(gridEvent(moveFrom, moveFrom, false));
        selectionGrid.mouseDrag(gridEvent(moveFrom, moveTo, true));
        selectionGrid.mouseUp(gridEvent(moveFrom, moveTo, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(!session.hasNote(1, 48) && session.hasNote(2, 48),
                "Dragging a note body moves it without requiring a modifier");
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        const auto commandLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(4, rowForPitch(55)).getCentre(),
                                          selectionGrid.cell(4, rowForPitch(55)).getCentre(), false, commandLeft));
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(8, rowForPitch(60)).getCentre(),
                                          selectionGrid.cell(8, rowForPitch(60)).getCentre(), false, commandLeft));
        require(selectionGrid.selectedNotes.count() == 2, "Command-click selects multiple note cells");
        require(selectionGrid.selectedVelocityPercent() == 79,
                "The velocity footer reports a shared selected-note percentage");
        require(selectionGrid.keyPressed(juce::KeyPress('V', juce::ModifierKeys(), 'v')),
                "V starts selected-note velocity adjustment");
        juce::MouseWheelDetails velocityWheel;
        velocityWheel.deltaY = 1.0f;
        selectionGrid.mouseWheelMove(gridEvent(selectionGrid.cell(4, rowForPitch(55)).getCentre(),
                                               selectionGrid.cell(4, rowForPitch(55)).getCentre(), false),
                                     velocityWheel);
        require(selectionGrid.selectedVelocityPercent() == 80,
                "The wheel adjusts every selected note while V is held");
        require(selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::upKey)),
                "Up adjusts every selected note while V is held");
        require(selectionGrid.selectedVelocityPercent() == 81,
                "A selected group retains a visible shared velocity after adjustment");
        selectionGrid.finishVelocityAdjustment();
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.selectedVelocityPercent() == 79,
                "One undo restores the complete velocity gesture");
        const auto velocityStates = selectionGrid.selectedStates();
        session.beginNoteGesture("Test mixed velocities");
        require(session.adjustNoteVelocities({velocityStates.front()}, -1),
                "A note in the selected group can receive a distinct velocity");
        session.endNoteGesture();
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.selectedVelocityPercent() == -1,
                "The velocity footer reports a mixed selection");
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        const auto groupFrom = selectionGrid.cell(4, rowForPitch(55)).getCentre();
        const auto groupTo = selectionGrid.cell(6, rowForPitch(55)).getCentre();
        selectionGrid.mouseDown(gridEvent(groupFrom, groupFrom, false));
        selectionGrid.mouseDrag(gridEvent(groupFrom, groupTo, true));
        selectionGrid.mouseUp(gridEvent(groupFrom, groupTo, true));
        selectionGrid.changeListenerCallback(nullptr);
        require(!session.hasNote(4, 55) && !session.hasNote(8, 60)
                && session.hasNote(6, 55) && session.hasNote(10, 60)
                && selectionGrid.selectedNotes.count() == 2,
                "Dragging a selected note moves the complete selected shape and keeps it selected");
        session.undo();
        selectionGrid.changeListenerCallback(nullptr);
        require(session.hasNote(4, 55) && session.hasNote(8, 60),
                "Undo restores a grouped note drag in one step");
        selectionGrid.selectedNotes.reset();
        selectionGrid.selectedNotes.set(static_cast<size_t>(selectionGrid.indexForCell(4, 55)));
        selectionGrid.selectedNotes.set(static_cast<size_t>(selectionGrid.indexForCell(8, 60)));
        const auto copiedStates = selectionGrid.selectedStates();
        session.beginNoteGesture("Set copied note velocity");
        require(session.adjustNoteVelocities({copiedStates.front()}, -10),
                "A source note can have a distinct velocity before copying");
        session.endNoteGesture();
        selectionGrid.changeListenerCallback(nullptr);
        require(selectionGrid.selectedVelocityPercent() == -1,
                "The copied selection contains distinct velocities");
        require(selectionGrid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 'c')),
                "Command-C copies selected notes");
        require(selectionGrid.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 'v')),
                "Command-V pastes selected notes");
        require(session.hasNote(0, 55) && session.hasNote(4, 60),
                "Paste places the selected shape at the first empty grid position");
        require(selectionGrid.selectedVelocityPercent() == -1,
                "Paste preserves each copied note's velocity");
        require(selectionGrid.keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 'z')),
                "Command-Z undoes a note-editor edit while the grid is focused");
        require(!session.hasNote(0, 55) && !session.hasNote(4, 60),
                "Note-editor undo removes the pasted note shape");
        require(selectionGrid.keyPressed(juce::KeyPress('Y', juce::ModifierKeys::commandModifier, 'y')),
                "Command-Y redoes a note-editor edit while the grid is focused");
        require(session.hasNote(0, 55) && session.hasNote(4, 60),
                "Note-editor redo restores the pasted note shape");
        selectionGrid.selectedNotes.reset();
        selectionGrid.selectedNotes.set(static_cast<size_t>(selectionGrid.indexForCell(0, 55)));
        selectionGrid.selectedNotes.set(static_cast<size_t>(selectionGrid.indexForCell(4, 60)));
        require(selectionGrid.selectedNotes.count() == 2, "Pasted notes become the active selection");
        require(selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)),
                "Delete removes selected notes");
        require(!session.hasNote(0, 55) && !session.hasNote(4, 60), "Deleted pasted notes are removed from the clip");
        session.undo();
        require(session.hasNote(0, 55) && session.hasNote(4, 60), "Undo restores notes removed by multi-selection delete");
        selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(selectionGrid.selectedNotes.none(), "Escape clears note selection");
        const auto ctrlLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(4, rowForPitch(55)).getCentre(),
                                          selectionGrid.cell(4, rowForPitch(55)).getCentre(), false, ctrlLeft));
        selectionGrid.mouseDown(gridEvent(selectionGrid.cell(8, rowForPitch(60)).getCentre(),
                                          selectionGrid.cell(8, rowForPitch(60)).getCentre(), false, ctrlLeft));
        require(selectionGrid.selectedNotes.count() == 2, "Ctrl-click selects multiple note cells on Windows");
        require(selectionGrid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::ctrlModifier, 'c')),
                "Ctrl-C copies selected notes");
        require(selectionGrid.keyPressed(juce::KeyPress('V', juce::ModifierKeys::ctrlModifier, 'v')),
                "Ctrl-V pastes selected notes");
        selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        require(selectionGrid.keyPressed(juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 'a')),
                "Ctrl-A selects every note in the editor on Windows");
        require(selectionGrid.selectedNotes.count() == selectionGrid.notes.count(),
                "Ctrl-A selects all currently visible editor notes");
        require(selectionGrid.keyPressed(juce::KeyPress('C', juce::ModifierKeys::ctrlModifier, 'c')),
                "Ctrl-C copies every note after Ctrl-A");
        selectionGrid.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        const auto sourceCell = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto targetCell = selectionGrid.cell(1, rowForPitch(50)).getCentre();
        const auto shiftLeft = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        selectionGrid.mouseDown(gridEvent(sourceCell, sourceCell, false, shiftLeft));
        selectionGrid.mouseDrag(gridEvent(sourceCell, targetCell, true, shiftLeft));
        selectionGrid.mouseUp(gridEvent(sourceCell, targetCell, true, shiftLeft));
        require(!session.hasNote(1, 48) && session.hasNote(1, 50),
                "Shift-dragging an existing note vertically changes its pitch");
        session.undo();
        require(session.hasNote(1, 48) && !session.hasNote(1, 50),
                "Undo restores note pitch after a grid drag");
        selectionGrid.changeListenerCallback(nullptr);
        const auto eraseCellA = selectionGrid.cell(1, rowForPitch(48)).getCentre();
        const auto eraseCellB = selectionGrid.cell(3, rowForPitch(48)).getCentre();
        const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
        selectionGrid.mouseDown(gridEvent(eraseCellA, eraseCellA, false, right));
        selectionGrid.mouseDrag(gridEvent(eraseCellA, eraseCellB, true, right));
        selectionGrid.mouseUp(gridEvent(eraseCellA, eraseCellB, true, right));
        require(!session.hasNote(1, 48) && !session.hasNote(2, 48) && !session.hasNote(3, 48),
                "Right-dragging from an existing note erases notes under the cursor");
        session.undo();
        require(session.hasNote(1, 48), "Undo restores notes erased by a grid stroke");
        session.undo();
        const auto audio2DevicesAfterSound = session.deviceSlots(2).size();
        require(view.applyBrowserDrop("theta-browser:effect:ThetaBloom", 2).wasOk(), "Theta Bloom browser row creates an audio effect");
        require(session.deviceSlots(2).size() == audio2DevicesAfterSound + 1, "Dropped Theta Bloom appears on the target track");
        session.undo();
        require(view.applyBrowserDrop("theta-browser:instrument:Drums", 2).wasOk(), "Instrument browser rows can be dropped onto non-first tracks");
        require(session.deviceSlots(2).size() == audio2DevicesAfterSound + 1, "Dropped instrument appears on the target track");
        session.undo();
        const auto instrumentClipCount = te::getAudioTracks(*session.edit)[2]->getClips().size();
        view.itemDropped({"theta-browser:instrument:FourOsc", nullptr,
                          {static_cast<int>(view.xFor(1.0)), static_cast<int>(view.lane(2).getCentreY())}});
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == instrumentClipCount + 1,
                "Dragging an instrument onto the arrangement creates an editable MIDI clip");
        require(session.pattern().itemID == te::getAudioTracks(*session.edit)[2]->getClips().getLast()->itemID,
                "Dropped instrument clip becomes the note editor target");
        session.undo();
        require(session.removeAudioTrack(2).wasOk(), "Can remove the extra audio track");
        require(session.trackCount() == 2, "Removing extra audio track restores starter track count");
        view.filesDropped(dropped, static_cast<int>(view.xFor(1.0)), view.getHeight() - 3);
        require(session.trackCount() == 3, "Dropping audio below the lanes creates a new audio track");
        require(te::getAudioTracks(*session.edit)[2]->getClips().size() == 1, "Drop-created track receives the audio clip");
        session.undo();
        session.undo();
        require(session.trackCount() == 2, "Undo removes the drop-created audio track");
