        // Cutting the tail of a clip onto the track below leaves a clip that
        // begins part-way into its own sequence: the notes stay exactly where
        // they were and the clip's offset moves instead. Everything that shows
        // or edits those notes has to read that window, because what plays is
        // the window - the arrangement's mini notes and the step grid both used
        // to read the sequence from its own zero and show a clip full of notes
        // it never sounded.
        {
            require(session.addTrack(Session::TrackType::midi).wasOk(), "Can add a track for the window fixture");
            const auto windowSource = session.trackCount() - 1;
            require(session.addTrack(Session::TrackType::midi).wasOk(), "Can add the track the cut lands on");
            const auto windowTarget = session.trackCount() - 1;

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

            // The arrangement draws from the same window. The pasted clip runs
            // from two seconds to four, and the note it plays fills it.
            view.sync();
            const auto* pastedView = static_cast<const Arrangement::ClipView*>(nullptr);
            for (const auto& onScreen : view.clips)
                if (onScreen.id == windowPasted.front()) pastedView = &onScreen;
            require(pastedView != nullptr, "The pasted clip is on screen");
            auto drawnStart = -1000.0, drawnEnd = -1000.0;
            for (const auto& note : pastedView->midiNotes)
                if (note.pitch == 60) { drawnStart = note.start; drawnEnd = note.end; }
            require(close(drawnStart, 2.0) && close(drawnEnd, 4.0),
                    "The arrangement draws the sustained note across the clip it fills");
        }
