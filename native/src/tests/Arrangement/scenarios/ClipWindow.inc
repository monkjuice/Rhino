        // Cutting the tail of a clip onto the track below leaves a clip that
        // begins part-way into its own sequence: the notes stay exactly where
        // they were and the clip's offset moves instead. Everything that shows
        // or edits those notes has to read that window, because what plays is
        // the window - the arrangement's mini notes and the step grid both used
        // to read the sequence from its own zero and show a clip full of notes
        // it never sounded.
        {
            require(session.addTrack(Session::TrackType::midi).wasOk(), "Can add a track for the window fixture");
            const auto windowSource = session.firstReturnTrack() - 1;
            require(session.addTrack(Session::TrackType::midi).wasOk(), "Can add the track the cut lands on");
            const auto windowTarget = session.firstReturnTrack() - 1;

            te::EditItemID windowClip;
            require(session.createClip(windowSource, 0.0, &windowClip).wasOk(), "Can make the window fixture clip");
            require(session.selectPatternClip(windowClip).wasOk(), "The editor points at the fixture clip");
            session.setEditorStepCount(16);
            require(session.ensurePatternLengthSteps(32), "The fixture clip is two bars of sixteenths");
            require(close(session.patternOffsetBeats(), 0.0), "A clip nobody has trimmed opens at its sequence start");
            session.beginNoteGesture("Window fixture notes");
            require(session.addNote(0.0, 62, 1.0).wasOk(), "Can place the note in the bar that stays behind");
            require(session.addNote(16.0, 60, 16.0).wasOk(), "Can place the sustained note in the bar that moves");
            session.endNoteGesture();

            // Two bars at 120 BPM, so the second one runs from two seconds to
            // four: exactly the span the sustained note occupies.
            const auto tail = session.copyClipRegion(2.0, 4.0, windowSource, windowSource);
            require(!tail.isEmpty(), "The second bar of the clip copies");
            std::vector<te::EditItemID> windowPasted;
            require(session.pasteClipRegion(tail, 2.0, windowTarget, windowPasted).wasOk(),
                    "The copied bar pastes onto the track below");
            require(windowPasted.size() == 1, "Pasting one bar of one clip makes one clip");
            require(session.selectPatternClip(windowPasted.front()).wasOk(), "The editor points at the pasted clip");
            require(close(session.patternOffsetBeats(), 4.0),
                    "The pasted clip opens a bar into the sequence it carries");
            require(session.editorStepCount() == 16, "The pasted clip is one bar of sixteen steps");

            auto sustainedStart = -1000.0, sustainedLength = 0.0, strandedStart = 1000.0;
            for (const auto& note : session.editorNotes())
            {
                if (note.pitch == 60) { sustainedStart = note.startSteps; sustainedLength = note.lengthSteps; }
                if (note.pitch == 62) strandedStart = note.startSteps;
            }
            require(close(sustainedStart, 0.0) && close(sustainedLength, 16.0),
                    "The editor shows the sustained note the pasted clip plays, whole and at its start");
            require(strandedStart < 0.0,
                    "The note on the other side of the cut sits before the grid rather than inside it");
            require(session.hasNote(0, 60), "The first step of the pasted clip carries the note that plays there");
            require(session.noteLengthSteps(0, 60) == 16, "That note is read back at its full length");

            // Drawing has to land in the window as well: written at the
            // sequence start it would sit a bar before anything the clip plays.
            session.beginNoteGesture("Window fixture edit");
            require(session.addNote(4.0, 67, 2.0).wasOk(), "Can draw a note on the pasted clip");
            session.endNoteGesture();
            auto* drawn = static_cast<te::MidiNote*>(nullptr);
            for (auto* note : session.pattern().getSequence().getNotes())
                if (note->getNoteNumber() == 67) drawn = note;
            require(drawn != nullptr, "The drawn note reached the clip's sequence");
            require(close(drawn->getStartBeat().inBeats(), session.patternOffsetBeats() + 1.0),
                    "A note drawn on step four is written a beat into the window, not a beat into the sequence");
            require(session.hasNote(4, 67), "The drawn note reads back on the step it was drawn on");

            // A note that began a 64th before the window - the clip cut or
            // pasted on the beat after an early note - still plays, from the
            // clip's start. The editor read it as starting before step 0, like
            // the notes the clip never plays, and dropped it: a clip that kept
            // sounding showed an empty grid.
            {
                const auto crossingStart = session.patternOffsetBeats() - 0.046875;
                session.beginNoteGesture("Window fixture crossing note");
                auto* crossing = session.pattern().getSequence().addNote(58,
                    tracktion::core::BeatPosition::fromBeats(crossingStart),
                    tracktion::core::BeatDuration::fromBeats(3.0), 100, 0, &session.edit->getUndoManager());
                session.endNoteGesture();
                require(crossing != nullptr, "Can place a note across the window's start");
                const auto crossingState = crossing->state;
                const auto shownCrossing = [&session, &crossingState]
                {
                    for (const auto& note : session.editorNotes())
                        if (note.state == crossingState) return std::pair {note.startSteps, note.lengthSteps};
                    return std::pair {-1000.0, 0.0};
                };
                require(close(shownCrossing().first, 0.0) && close(shownCrossing().second, 11.8125),
                        "A note that began just before the window is shown from step 0, where it plays");
                auto editor = std::make_unique<StepGrid>(session);
                editor->setSize(1000, 250);
                editor->changeListenerCallback(nullptr);
                require(editor->noteForState(crossingState) != nullptr && editor->laneNoteFor(crossingState) != nullptr,
                        "The note editor draws it, and gives it a velocity marker");

                session.beginNoteGesture("Window fixture resize");
                require(session.resizeNote(crossingState, 4.0).wasOk(), "Can resize the crossing note");
                session.endNoteGesture();
                require(close(shownCrossing().first, 0.0) && close(shownCrossing().second, 4.0)
                            && close(crossing->getStartBeat().inBeats(), crossingStart),
                        "Resizing it sets the length the editor shows, and keeps where the note began");

                std::vector<juce::ValueTree> halves;
                session.beginNoteGesture("Window fixture divide");
                require(session.redistributeNotes({crossingState}, 2, halves).wasOk() && halves.size() == 2,
                        "Can divide the crossing note");
                session.endNoteGesture();
                auto halfStarts = std::vector<double> {};
                for (const auto& note : session.editorNotes())
                    if (note.pitch == 58) halfStarts.push_back(note.startSteps);
                std::sort(halfStarts.begin(), halfStarts.end());
                require(halfStarts.size() == 2 && close(halfStarts[0], 0.0) && close(halfStarts[1], 2.0),
                        "Dividing it divides the part the editor shows, so both halves play");

                editor.reset();
                session.undo();
                session.undo();
                session.undo();
                const auto afterUndo = session.editorNotes();
                require(std::none_of(afterUndo.begin(), afterUndo.end(), [](const auto& note) { return note.pitch == 58; }),
                        "Undo takes the crossing note back out");
            }

            // The arrangement draws from the same window. The pasted clip runs
            // from two seconds to four, and the note it plays fills it.
            view.sync();
            const auto* pastedView = static_cast<const Arrangement::ClipView*>(nullptr);
            for (const auto& onScreen : view.clips)
                if (onScreen.id == windowPasted.front()) pastedView = &onScreen;
            require(pastedView != nullptr, "The pasted clip is on screen");
            auto drawnStart = -1000.0, drawnEnd = -1000.0;
            // A note view is kept in the sequence's own seconds and drawn from
            // the window's origin, the clip's start less its offset.
            const auto origin = pastedView->position.start - pastedView->position.offset;
            require(pastedView->midiNotes != nullptr, "The pasted clip's notes were read");
            for (const auto& note : *pastedView->midiNotes)
                if (note.pitch == 60) { drawnStart = note.start + origin; drawnEnd = note.end + origin; }
            require(close(drawnStart, 2.0) && close(drawnEnd, 4.0),
                    "The arrangement draws the sustained note across the clip it fills");

            // A MIDI clip splits at the line exactly as an audio clip does, and
            // for the same reason the paste above works: the right-hand half is
            // a window into the sequence both halves still share, so no note
            // moves and nothing is copied.
            view.sync();
            view.setSelection({});
            view.setInsertPoint(2.0, windowSource);
            view.splitAtInsertPoint();
            auto* sourceLane = te::getAudioTracks(*session.edit)[windowSource];
            require(sourceLane->getClips().size() == 2, "The line cuts a MIDI clip in two");
            auto* rightHalf = static_cast<te::Clip*>(nullptr);
            for (auto* half : sourceLane->getClips())
                if (half != nullptr && close(half->getPosition().time.getStart().inSeconds(), 2.0))
                    rightHalf = half;
            require(rightHalf != nullptr, "The right-hand MIDI half starts at the line");
            require(session.selectPatternClip(rightHalf->itemID).wasOk(),
                    "The editor points at the right-hand half");
            require(close(session.patternOffsetBeats(), 4.0),
                    "The right-hand MIDI half opens a bar into the sequence it carries");
            require(session.hasNote(0, 60),
                    "The sustained note still plays at the start of the right-hand half");
            session.undo();
            require(te::getAudioTracks(*session.edit)[windowSource]->getClips().size() == 1,
                    "Undo restores the MIDI clip the line split");
            require(session.selectPatternClip(windowClip).wasOk(),
                    "The editor goes back to the whole clip the undo restored");
            view.sync();
        }
