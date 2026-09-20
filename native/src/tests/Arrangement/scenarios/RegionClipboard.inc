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
            // A paste leaves the region describing the rectangle it pasted,
            // which is a span of the timeline rather than one read off clips.
            // Taking hold of the pasted clip is what makes it one again, so the
            // highlight follows the drag instead of staying where it was.
            {
                auto* justPasted = clipAt(1, 8.0);
                require(justPasted != nullptr, "The pasted clip is there to take hold of");
                const auto pastedID = justPasted->itemID;
                view.mouseDown(event({view.xFor(8.5), audioY}, {view.xFor(8.5), audioY}, false));
                view.mouseDrag(event({view.xFor(8.5), audioY}, {view.xFor(11.5), audioY}, true));
                require(close(view.displayedTimeSelection().start, 11.0),
                        "The region follows a clip dragged straight after it was pasted");
                view.mouseUp(event({view.xFor(8.5), audioY}, {view.xFor(11.5), audioY}, true));
                require(close(session.findClip(pastedID)->getPosition().time.getStart().inSeconds(), 11.0)
                        && close(view.timeSelection.start, 11.0),
                        "And is still on it after the drop");
                require(session.editClip(pastedID, {8.0, 9.0, 0.0}, ClipGesture::move).wasOk(),
                        "Can put the pasted clip back");
                view.sync();
            }

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
            // A drag previews rather than editing, so the highlight has to
            // preview with it: waiting for the drop left it sitting at the
            // position the clip was being carried away from.
            view.mouseDown(event({view.xFor(10.5), audioY}, {view.xFor(10.5), audioY}, false));
            view.mouseDrag(event({view.xFor(10.5), audioY}, {view.xFor(14.5), audioY}, true));
            require(close(view.displayedTimeSelection().start, 14.0)
                    && close(view.displayedTimeSelection().end, 15.0),
                    "The region previews with a clip drag instead of waiting for the drop");
            require(close(view.timeSelection.start, 10.0),
                    "What the commands read is still where the edit actually has the clip");
            view.mouseUp(event({view.xFor(10.5), audioY}, {view.xFor(14.5), audioY}, true));
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

            // Two clips may never share ground on one track. The clip that
            // arrives wins the span it lands on, and the one underneath is
            // split at its edges so the parts either side survive.
            const auto beat = 60.0 / session.tempo();
            const auto placeAt = [&](double startBeats, double lengthBeats)
            {
                require(session.importAudioAt(source.getFile(), 1, startBeats * beat + 40.0).wasOk(),
                        "Can place an overlap fixture");
                view.sync();
                auto* placed = clipAt(1, startBeats * beat + 40.0);
                require(placed != nullptr, "The overlap fixture is where it was asked for");
                require(session.editClip(placed->itemID, {startBeats * beat + 40.0,
                                                          (startBeats + lengthBeats) * beat + 40.0, 0.0},
                                         ClipGesture::trimRight).wasOk(), "Can size an overlap fixture");
                view.sync();
                return placed->itemID;
            };
            const auto clearTrackOne = [&]
            {
                std::vector<te::EditItemID> made;
                for (auto* candidate : te::getAudioTracks(*session.edit)[1]->getClips())
                    if (candidate->getPosition().time.getStart().inSeconds() > 39.0)
                        made.push_back(candidate->itemID);
                for (const auto doomed : made) session.deleteClip(doomed);
                view.sync();
            };
            const auto beatsOf = [&](double seconds) { return (seconds - 40.0) / beat; };
            const auto clipStartsInBeats = [&]
            {
                std::vector<double> starts;
                for (auto* candidate : te::getAudioTracks(*session.edit)[1]->getClips())
                    if (candidate->getPosition().time.getStart().inSeconds() > 39.0)
                        starts.push_back(beatsOf(candidate->getPosition().time.getStart().inSeconds()));
                std::sort(starts.begin(), starts.end());
                return starts;
            };
            const auto moveTo = [&](te::EditItemID target, double startBeats, double lengthBeats)
            {
                require(session.editClip(target, {startBeats * beat + 40.0,
                                                  (startBeats + lengthBeats) * beat + 40.0, 0.0},
                                         ClipGesture::move).wasOk(), "Can carry a clip onto another");
                view.sync();
            };

            // One beat dropped on the second beat of an eight-beat clip: the
            // beat before it stands alone, and the six after it stay together.
            clearTrackOne();
            juce::ignoreUnused(placeAt(0.0, 8.0));
            const auto oneBeat = placeAt(12.0, 1.0);
            moveTo(oneBeat, 1.0, 1.0);
            require(clipStartsInBeats() == std::vector<double>({0.0, 1.0, 2.0}),
                    "A clip dropped inside another splits it either side of itself");
            require(close(clipAt(1, 40.0)->getPosition().time.getEnd().inSeconds(), beat + 40.0),
                    "The part before the dropped clip ends where it begins");
            require(close(clipAt(1, 2.0 * beat + 40.0)->getPosition().time.getEnd().inSeconds(), 8.0 * beat + 40.0),
                    "The part after it stays one clip to the original end");

            // Three beats landing on the last two of a five-beat clip: three
            // beats of the original survive, and the newcomer keeps all three.
            clearTrackOne();
            juce::ignoreUnused(placeAt(0.0, 5.0));
            const auto threeBeats = placeAt(12.0, 3.0);
            moveTo(threeBeats, 3.0, 3.0);
            require(clipStartsInBeats() == std::vector<double>({0.0, 3.0}),
                    "A clip overlapping the tail of another trims it rather than splitting it");
            require(close(clipAt(1, 40.0)->getPosition().time.getEnd().inSeconds(), 3.0 * beat + 40.0),
                    "The clip underneath is trimmed back to where the newcomer starts");
            require(close(session.findClip(threeBeats)->getPosition().time.getEnd().inSeconds(), 6.0 * beat + 40.0),
                    "The clip that arrived keeps its whole length");

            // Two beats in the middle of an eight-beat clip: a hole is cut and
            // filled, leaving three clips.
            clearTrackOne();
            juce::ignoreUnused(placeAt(0.0, 8.0));
            const auto twoBeats = placeAt(12.0, 2.0);
            moveTo(twoBeats, 3.0, 2.0);
            require(clipStartsInBeats() == std::vector<double>({0.0, 3.0, 5.0}),
                    "A clip dropped in the middle of another leaves one clip either side of it");
            clearTrackOne();

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
