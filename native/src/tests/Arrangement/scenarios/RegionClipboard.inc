        // A region is a span of time across a run of tracks, and copy, cut,
        // paste, duplicate and delete all read it the same way. The fixtures
        // live out past eight seconds so nothing here disturbs the clips the
        // scenarios around this one are working with.
        {
            view.gridSettings.mode = GridMode::fixed;
            view.gridSettings.fixedDivision = GridDivision::sixteenth;
            view.sync();
            // Wide enough that the fixtures out at twelve seconds are on
            // screen; put back at the end, because the scenarios after this one
            // place pointer gestures by time and read the answer in pixels.
            const auto previousViewStart = view.viewStart;
            const auto previousViewSpan = view.viewSpan;
            view.viewStart = 0.0;
            view.viewSpan = 24.0;
            view.resized();
            const auto clipsOn = [&session](int track)
            {
                return te::getAudioTracks(*session.edit)[track]->getClips().size();
            };
            const auto clipAt = [&session](int track, double start) -> te::Clip*
            {
                for (auto* candidate : te::getAudioTracks(*session.edit)[track]->getClips())
                    if (std::abs(candidate->getPosition().time.getStart().inSeconds() - start) < 0.01)
                        return candidate;
                return nullptr;
            };
            const auto startingAudioClips = clipsOn(1);
            const auto startingPatternClips = clipsOn(0);

            require(session.importAudioAt(source.getFile(), 1, 4.0).wasOk(), "Can place the region fixture");
            view.sync();
            auto* fixture = clipAt(1, 4.0);
            require(fixture != nullptr, "The fixture clip is where it was asked for");
            const auto fixtureID = fixture->itemID;
            require(close(fixture->getPosition().time.getEnd().inSeconds(), 5.0), "The fixture clip is one second long");

            // Dragging empty lane space selects a span of it rather than
            // clearing the selection and doing nothing.
            const auto audioY = view.lane(1).getCentreY();
            drag({view.xFor(6.0), audioY}, {view.xFor(7.0), audioY});
            require(view.timeSelection.active && view.timeSelection.isRange(),
                    "Dragging an empty lane selects a span of the timeline");
            require(close(view.timeSelection.start, 6.0) && close(view.timeSelection.end, 7.0),
                    "The dragged region snaps to the grid at both edges");
            require(view.timeSelection.firstTrack == 1 && view.timeSelection.lastTrack == 1,
                    "A drag inside one lane selects that track alone");
            drag({view.xFor(6.0), view.lane(0).getCentreY()}, {view.xFor(7.0), audioY});
            require(view.timeSelection.firstTrack == 0 && view.timeSelection.lastTrack == 1,
                    "A drag across lanes selects the run of tracks it crossed");
            drag({view.xFor(6.5), audioY}, {view.xFor(6.5), audioY});
            require(view.timeSelection.active && !view.timeSelection.isRange()
                    && close(view.timeSelection.start, 6.5),
                    "A click that never drags leaves an insert point where it landed");

            // Copy takes what is inside the region; paste puts it at the
            // insert point and moves the region onto what it pasted.
            view.setTimeSelection(4.0, 5.0, 1, 1);
            view.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0));
            require(view.clipboard.clips.size() == 1, "Ctrl+C copies what is inside the region");
            view.setInsertPoint(8.0, 1);
            view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(1, 8.0) != nullptr, "Ctrl+V lands the copy at the insert point");
            require(close(clipAt(1, 8.0)->getPosition().time.getEnd().inSeconds(), 9.0),
                    "The pasted clip keeps the length it was copied with");
            require(close(view.timeSelection.start, 8.0) && close(view.timeSelection.end, 9.0),
                    "Paste moves the region onto what it just pasted");

            // The clipboard is a snapshot, not a pointer at a clip, so it
            // outlives the thing it was taken from - which is what makes cut
            // work at all.
            session.deleteClip(fixtureID);
            view.sync();
            require(session.findClip(fixtureID) == nullptr, "The source clip is gone");
            view.setInsertPoint(10.0, 1);
            view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
            auto* survivor = clipAt(1, 10.0);
            require(survivor != nullptr, "A copy outlives the clip it was taken from");
            const auto survivorID = survivor->itemID;

            // A region cuts the clips crossing its edges instead of taking
            // them whole, and the crop carries the offset into the source.
            view.setTimeSelection(8.25, 8.75, 1, 1);
            view.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0));
            require(view.clipboard.clips.size() == 1, "Copying part of a clip copies that clip");
            require(close(view.clipboard.clips.front().end - view.clipboard.clips.front().start, 0.5),
                    "Copying part of a clip copies only the part inside the region");
            require(close(view.clipboard.clips.front().offset, 0.25),
                    "A cropped copy starts a quarter second into its source");

            // Ctrl+D puts the copy flush against the end of the selection and
            // carries the selection onto it, so holding it keeps extending.
            auto* toDuplicate = clipAt(1, 8.0);
            require(toDuplicate != nullptr, "The pasted clip is there to duplicate");
            view.setSelection({toDuplicate->itemID}, toDuplicate->itemID);
            view.setRegionFromSelectedClips();
            require(close(view.timeSelection.start, 8.0) && close(view.timeSelection.end, 9.0),
                    "Selecting a clip sets the region to that clip's span");
            view.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(1, 9.0) != nullptr, "Ctrl+D places the copy directly after the selection");
            require(close(view.timeSelection.start, 9.0) && close(view.timeSelection.end, 10.0),
                    "Ctrl+D carries the selection onto the copy it made");

            // The copy replaces what it lands on rather than sliding past it,
            // which is the behaviour this replaced: a duplicate used to hunt
            // for the first free gap and could land bars away from the source.
            view.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0));
            require(session.findClip(survivorID) == nullptr,
                    "Duplicating over an occupied span replaces what was there");
            require(clipAt(1, 10.0) != nullptr, "The duplicate still lands flush against the selection");

            // A region read off a clip belongs to that clip, so moving the
            // clip carries the highlight along instead of leaving it behind at
            // the position the clip used to have.
            auto* toMove = clipAt(1, 10.0);
            require(toMove != nullptr, "There is a clip to move");
            const auto movedID = toMove->itemID;
            view.setSelection({movedID}, movedID);
            require(close(view.timeSelection.start, 10.0), "The region starts on the clip it was read from");
            drag({view.xFor(10.5), audioY}, {view.xFor(14.5), audioY});
            require(close(session.findClip(movedID)->getPosition().time.getStart().inSeconds(), 14.0),
                    "The clip moved four seconds to the right");
            require(close(view.timeSelection.start, 14.0) && close(view.timeSelection.end, 15.0),
                    "A region read off a clip follows it when the clip is moved");
            // A region dragged out over the lanes belongs to the timeline, so
            // it stays where it was put whatever the clips under it do.
            view.setTimeSelection(17.0, 18.0, 1, 1);
            view.sync();
            require(close(view.timeSelection.start, 17.0) && close(view.timeSelection.end, 18.0),
                    "A region dragged over the lanes stays where it was put");
            require(session.editClip(movedID, {10.0, 11.0, session.findClip(movedID)->getPosition().offset.inSeconds()},
                                     ClipGesture::move).wasOk(), "Can put the moved clip back");
            view.sync();

            // Delete on a region empties it, trimming the clips that cross its
            // edges and leaving a hole in one that spans it.
            view.setTimeSelection(8.5, 9.5, 1, 1);
            view.focus = Arrangement::Focus::region;
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(close(clipAt(1, 8.0)->getPosition().time.getEnd().inSeconds(), 8.5),
                    "Deleting a region trims a clip that runs into it");
            require(clipAt(1, 9.5) != nullptr, "Deleting a region trims a clip that runs out of it");
            require(clipAt(1, 9.0) == nullptr, "Nothing is left inside the deleted region");
            view.setTimeSelection(10.25, 10.75, 1, 1);
            view.focus = Arrangement::Focus::region;
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(clipAt(1, 10.0) != nullptr && close(clipAt(1, 10.0)->getPosition().time.getEnd().inSeconds(), 10.25)
                    && clipAt(1, 10.75) != nullptr,
                    "Deleting a region inside one clip leaves the clip either side of the hole");

            // Cut empties the region and keeps what it took.
            view.setTimeSelection(10.75, 11.0, 1, 1);
            view.keyPressed(juce::KeyPress('X', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(1, 10.75) == nullptr, "Ctrl+X empties the region it took");
            require(view.clipboard.clips.size() == 1, "Ctrl+X keeps what it took");
            view.setInsertPoint(20.0, 1);
            view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(1, 20.0) != nullptr, "What was cut can be pasted somewhere else");

            // A region spanning two tracks keeps their relative order when it
            // is pasted, which is what makes a multitrack copy worth having.
            require(session.trackHasInstrument(0), "The pattern track runs an instrument");
            require(session.createClip(0, 4.0).wasOk(), "Can place a MIDI fixture clip");
            view.sync();
            require(clipAt(0, 4.0) != nullptr, "The MIDI fixture is where it was asked for");
            require(session.importAudioAt(source.getFile(), 1, 4.0).wasOk(), "Can place the audio half of the pair");
            view.sync();
            view.setTimeSelection(4.0, 5.0, 0, 1);
            view.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0));
            require(view.clipboard.clips.size() == 2, "A region across two tracks copies from both");
            view.setInsertPoint(12.0, 0);
            view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(0, 12.0) != nullptr, "The upper track's clip is pasted on the upper track");
            require(clipAt(1, 12.0) != nullptr, "The lower track's clip keeps its distance from it");
            require(dynamic_cast<te::MidiClip*>(clipAt(0, 12.0)) != nullptr, "A pasted MIDI clip is still a MIDI clip");
            require(session.findAudioClip(clipAt(1, 12.0)->itemID) != nullptr, "A pasted audio clip is still an audio clip");

            // An empty lane inside a region is part of what was copied. The
            // copy has to be the size of the rectangle, not the bounding box
            // of the clips that happened to be inside it, or pasting it would
            // neither occupy nor clear the lane that was empty.
            require(session.importAudioAt(source.getFile(), 1, 16.0).wasOk(), "Can place a clip for the empty-lane copy");
            view.sync();
            view.setTimeSelection(16.0, 20.0, 0, 1);
            view.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0));
            require(view.clipboard.clips.size() == 1, "Only the lane with a clip on it contributes a clip");
            require(close(view.clipboard.spanSeconds, 4.0) && view.clipboard.trackSpan == 1,
                    "The copy is the size of the rectangle, not of the clip inside it");
            require(session.createClip(0, 24.5).wasOk(), "Can place something for the empty lane to clear");
            view.sync();
            require(clipAt(0, 24.5) != nullptr, "The clip to be cleared is there first");
            view.setInsertPoint(24.0, 0);
            view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
            require(clipAt(0, 24.5) == nullptr,
                    "Pasting a lane that was empty empties the lane it lands on");
            require(clipAt(1, 24.0) != nullptr, "The lane that had a clip still gets one");
            require(close(view.timeSelection.start, 24.0) && close(view.timeSelection.end, 28.0)
                    && view.timeSelection.firstTrack == 0 && view.timeSelection.lastTrack == 1,
                    "The region after a paste is the whole rectangle that was pasted");

            // Everything this scenario made lives past three seconds, so the
            // clean-up is one sweep and the counts prove it worked.
            for (int track = 0; track < 2; ++track)
            {
                std::vector<te::EditItemID> made;
                for (auto* candidate : te::getAudioTracks(*session.edit)[track]->getClips())
                    if (candidate->getPosition().time.getStart().inSeconds() > 3.0)
                        made.push_back(candidate->itemID);
                for (const auto made_id : made) session.deleteClip(made_id);
            }
            view.setSelection({});
            view.clearTimeSelection();
            view.viewStart = previousViewStart;
            view.viewSpan = previousViewSpan;
            view.sync();
            view.resized();
            require(clipsOn(1) == startingAudioClips && clipsOn(0) == startingPatternClips,
                    "The region scenario leaves the arrangement as it found it");
            require(session.trackCount() == 2, "The region scenario creates no tracks");
        }
