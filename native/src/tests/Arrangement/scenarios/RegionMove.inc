        // One selection, carried whole. A span swept across part of one clip
        // and the whole of another is one rectangle; dragging any clip header
        // inside it carries everything in it - the clips inside whole, and of
        // the clips crossing its edges the part inside, cut there; a row whose
        // material may not land where it is going refuses the whole move and
        // says so before the drop; and the arrow keys nudge the same material.
        // The sweep that begins in the MIDI clip makes it the primary clip and
        // shows the part swept in the note editor. The fixtures live out past
        // thirty seconds, on lanes the region scenario left clean.
        {
            view.gridSettings.mode = GridMode::fixed;
            view.gridSettings.fixedDivision = GridDivision::sixteenth;
            view.gridSettings.triplet = false;
            const auto previousViewStart = view.viewStart;
            const auto previousViewSpan = view.viewSpan;
            // The region scenario's autoscroll test leaves the row stack
            // scrolled up, with the first lane's lower row a pixel above the
            // lanes' top edge, where a press is not a lane press at all. The
            // view here is 246 px tall, two lanes of 62 px, so the stack
            // scrolls; it is put back to the top and restored at the end.
            const auto previousTrackScroll = view.trackScroll;
            view.trackScroll = 0.0;
            view.updateScroll();
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
            const auto endOf = [](te::Clip* clip) { return clip != nullptr ? clip->getPosition().time.getEnd().inSeconds() : -1.0; };
            const auto offsetOf = [](te::Clip* clip) { return clip != nullptr ? clip->getPosition().offset.inSeconds() : -1.0; };
            const auto lowerRow = [&view](int track) { return view.lane(track).getBottom() - 8.0f; };
            require(session.trackCount() == 4 && session.trackType(0) == Session::TrackType::midi
                        && session.trackType(1) == Session::TrackType::audio
                        && session.trackType(2) == Session::TrackType::midi
                        && session.trackType(3) == Session::TrackType::audio,
                    "The starter stack alternates MIDI and audio lanes");
            for (int track = 0; track < 4; ++track)
                for (auto* candidate : te::getAudioTracks(*session.edit)[track]->getClips())
                    require(candidate->getPosition().time.getEnd().inSeconds() <= 30.0
                                || candidate->getPosition().time.getStart().inSeconds() >= 40.0,
                            "The lanes are clear where the move fixtures go");
            const std::array<int, 4> startingClips {clipsOn(0), clipsOn(1), clipsOn(2), clipsOn(3)};
            // Read before the fixtures, because making a MIDI clip points the
            // note editor at it.
            const auto patternBefore = session.hasPatternClip() ? session.pattern().itemID : te::EditItemID();

            // Two bars of MIDI on the first lane, and under it an audio clip
            // the span will cut and one it will take whole.
            te::EditItemID midiID;
            require(session.createClip(0, 32.0, &midiID).wasOk(), "A MIDI clip to cut a piece from");
            require(session.editClip(midiID, {32.0, 36.0, 0.0}, ClipGesture::trimRight).wasOk(), "stretched to two bars");
            require(session.importAudioAt(source.getFile(), 1, 32.0).wasOk(), "An audio clip the span will cut");
            require(session.importAudioAt(source.getFile(), 1, 34.0).wasOk(), "and one it takes whole");
            view.sync();
            require(close(endOf(session.findClip(midiID)), 36.0), "The MIDI clip is two bars long");
            require(clipAt(1, 32.0) != nullptr && clipAt(1, 34.0) != nullptr, "The audio clips are where they were asked for");
            const auto audioPartID = clipAt(1, 32.0)->itemID;
            const auto audioWholeID = clipAt(1, 34.0)->itemID;
            // The view is set once the fixtures exist: the scroll range is
            // bounded by the song's end, and the song reaches past them only
            // now. Twenty-four seconds from fourteen keeps them on screen.
            view.viewStart = 14.0;
            view.viewSpan = 24.0;
            view.sync();
            view.resized();
            require(view.xFor(36.0) < static_cast<float>(view.getWidth()) - 14.0f
                        && close(view.viewStart, 14.0),
                    ("The fixtures are on screen: " + juce::String(view.viewStart) + " + " + juce::String(view.viewSpan)
                     + " across " + juce::String(view.getWidth()) + " x " + juce::String(view.getHeight())).toRawUTF8());

            // Three notes in the MIDI clip, a step long each, one before the
            // span the sweep below selects, one inside it and one after it. A
            // step is a sixteenth, an eighth of a second here. The note editor
            // is then put back on the clip it was showing.
            require(patternBefore != te::EditItemID() && patternBefore != midiID,
                    "The note editor shows another clip before the sweep");
            require(session.selectPatternClip(midiID).wasOk() && session.editorStepCount() == 32,
                    "The two-bar MIDI clip is thirty-two steps");
            juce::ValueTree noteBefore, noteInside, noteAfter;
            require(session.addNote(1.0, 60, 1.0, &noteBefore).wasOk() && session.addNote(8.0, 62, 1.0, &noteInside).wasOk()
                        && session.addNote(28.0, 64, 1.0, &noteAfter).wasOk(),
                    "Notes either side of the span and one inside it");
            require(session.selectPatternClip(patternBefore).wasOk(), "The note editor goes back to the clip it showed");
            // An editor the shell is hiding, as it hides the note editor while
            // the Device View shows, still on the other clip's length.
            juce::Component pane;
            auto hiddenEditor = std::make_unique<StepGrid>(session);
            hiddenEditor->setSize(1000, 250);
            hiddenEditor->changeListenerCallback(nullptr);
            pane.addChildComponent(*hiddenEditor);
            require(isHiddenInShell(*hiddenEditor) && hiddenEditor->visibleStepCount != 32,
                    "The hidden editor was laid out for a clip of another length");
            te::EditItemID sweptClip;
            auto sweptStart = -1.0, sweptEnd = -1.0;
            view.clipSpanSwept = [&](te::EditItemID clip, double start, double end)
            {
                sweptClip = clip;
                sweptStart = start;
                sweptEnd = end;
            };

            // The sweep: from half a second into the MIDI clip to the end of
            // the whole audio clip, across both lanes.
            drag({view.xFor(32.55), lowerRow(0)}, {view.xFor(34.9), lowerRow(1)});
            require(close(view.timeSelection.start, 32.5) && close(view.timeSelection.end, 35.0)
                        && view.timeSelection.firstTrack == 0 && view.timeSelection.lastTrack == 1,
                    ("A span swept across two lanes is one rectangle; got " + juce::String(view.timeSelection.active ? 1 : 0)
                     + " " + juce::String(view.timeSelection.start, 4) + " to " + juce::String(view.timeSelection.end, 4)
                     + " rows " + juce::String(view.timeSelection.firstTrack) + " to " + juce::String(view.timeSelection.lastTrack)
                     + "; lane 0 at " + juce::String(view.lane(0).getY()) + " to " + juce::String(view.lane(0).getBottom())
                     + ", pressed at y " + juce::String(lowerRow(0))).toRawUTF8());
            require(view.isSelected(midiID) && view.isSelected(audioPartID) && view.isSelected(audioWholeID),
                    "and every clip it runs across is selected, in part or whole");
            // The clip the sweep began in is the one the lower pane shows, as
            // in Live, where selecting part of a clip shows that clip.
            require(view.selectedClipID() == midiID, "The clip the sweep began in is the primary one");
            require(session.hasPatternClip() && session.pattern().itemID == midiID,
                    "A sweep across part of a MIDI clip points the note editor at it");
            require(sweptClip == midiID && close(sweptStart, 32.5) && close(sweptEnd, 35.0),
                    "and reports the span it selected, for the editor to show");
            view.clipSpanSwept = nullptr;
            // What the shell does with the report: the note editor shows that
            // part of the clip, steps four to twenty-four of thirty-two.
            {
                auto editor = std::make_unique<StepGrid>(session);
                editor->setSize(1000, 250);
                editor->changeListenerCallback(nullptr);
                editor->showTimeSpan(sweptStart, sweptEnd);
                require(editor->stepSelection.active && close(editor->stepSelection.start, 4.0)
                            && close(editor->stepSelection.end, 24.0) && close(editor->insertStep, 4.0),
                        "The note editor's region is the span swept, in its own steps, its start marked");
                require(close(editor->stepScroll, 4.0) && close(editor->visibleStepSpan(), 20.0),
                        "and the editor is framed on exactly that part of the clip");
                require(editor->selectedStates() == std::vector<juce::ValueTree> {noteInside},
                        "The note inside the span is selected, and the notes either side of it are not");
                editor->showTimeSpan(33.0, 33.125);
                require(close(editor->stepSelection.start, 8.0) && close(editor->stepSelection.end, 9.0)
                            && close(editor->visibleStepSpan(), 4.0) && close(editor->stepScroll, 6.5),
                        "A span narrower than the deepest view is centred in it");
                // A hidden editor catches up with the new clip before it is
                // framed, so showing it keeps the frame rather than opening
                // the new clip at its default zoom.
                hiddenEditor->showTimeSpan(sweptStart, sweptEnd);
                hiddenEditor->setVisible(true);
                require(!isHiddenInShell(*hiddenEditor) && hiddenEditor->visibleStepCount == 32
                            && close(hiddenEditor->stepScroll, 4.0) && close(hiddenEditor->visibleStepSpan(), 20.0),
                        "An editor shown after it was framed hidden keeps the frame");
                require(close(hiddenEditor->insertStep, 4.0),
                        "and the insert line the span put down, which the catch-up takes for this clip's own");
                pane.removeChildComponent(hiddenEditor.get());
                hiddenEditor.reset();
            }

            // Grabbed by the whole audio clip's header and carried two rows
            // down and a beat right: the selection previews where it would
            // land, the clips preview in pieces, and no row refuses.
            const juce::Point<float> grab {view.xFor(34.5), clipRowY(1)};
            const juce::Point<float> twoDownOneBeatRight {view.xFor(35.0), clipRowY(3)};
            view.mouseDown(event(grab, grab, false));
            require(view.isSelected(midiID) && view.isSelected(audioPartID) && view.selectedClipID() == audioWholeID
                        && close(view.timeSelection.start, 32.5) && close(view.timeSelection.end, 35.0),
                    "A press on a clip header inside the selection keeps the selection and makes that clip primary");
            view.mouseDrag(event(grab, twoDownOneBeatRight, true));
            {
                const auto shown = view.displayedTimeSelection();
                require(close(shown.start, 33.0) && close(shown.end, 35.5) && shown.firstTrack == 2 && shown.lastTrack == 3,
                        "The selection previews where the drag would put it");
                const Arrangement::ClipView* midiView = nullptr;
                for (const auto& candidate : view.clips)
                    if (candidate.id == midiID) midiView = &candidate;
                require(midiView != nullptr, "The MIDI clip is on the timeline's list");
                const auto pieces = view.displayedPieces(*midiView);
                require(pieces.count == 3, "A clip crossing both edges previews as three pieces");
                require(!pieces.items[0].carried && pieces.items[0].track == 0
                            && close(pieces.items[0].position.start, 32.0) && close(pieces.items[0].position.end, 32.5),
                        "The part before the selection stays where it is");
                require(pieces.items[1].carried && pieces.items[1].track == 2
                            && close(pieces.items[1].position.start, 33.0) && close(pieces.items[1].position.end, 35.5)
                            && close(pieces.items[1].position.offset, 0.5),
                        "The part inside travels with the selection, a window half a second into its sequence");
                require(!pieces.items[2].carried && pieces.items[2].track == 0
                            && close(pieces.items[2].position.start, 35.0) && close(pieces.items[2].position.end, 36.0),
                        "and the part after it stays too");
                require(!view.previewRowRefused(0) && !view.previewRowRefused(1),
                        "MIDI onto a MIDI lane and audio onto an audio lane refuse nothing");
            }
            view.mouseUp(event(grab, twoDownOneBeatRight, true));
            require(clipAt(0, 32.0) != nullptr && clipAt(0, 32.0)->itemID == midiID && close(endOf(clipAt(0, 32.0)), 32.5),
                    "The MIDI clip keeps the part before the selection, under its own id");
            require(clipAt(0, 35.0) != nullptr && close(endOf(clipAt(0, 35.0)), 36.0) && clipAt(0, 32.5) == nullptr,
                    "and the part after it, with the part inside gone from the lane");
            require(clipAt(2, 33.0) != nullptr && dynamic_cast<te::MidiClip*>(clipAt(2, 33.0)) != nullptr
                        && close(endOf(clipAt(2, 33.0)), 35.5) && close(offsetOf(clipAt(2, 33.0)), 0.5),
                    "The part inside landed two lanes down and a beat on, a window half a second into its sequence");
            require(clipAt(1, 32.0) != nullptr && clipAt(1, 32.0)->itemID == audioPartID && close(endOf(clipAt(1, 32.0)), 32.5)
                        && clipAt(1, 34.0) == nullptr,
                    "The cut audio clip keeps what was outside the selection and the whole one has left its lane");
            require(clipAt(3, 33.0) != nullptr && close(endOf(clipAt(3, 33.0)), 33.5) && close(offsetOf(clipAt(3, 33.0)), 0.5),
                    "The cut audio clip's inside part landed with the selection");
            require(clipAt(3, 34.5) != nullptr && clipAt(3, 34.5)->itemID == audioWholeID && close(endOf(clipAt(3, 34.5)), 35.5),
                    "and the whole audio clip landed under its own id");
            require(close(view.timeSelection.start, 33.0) && close(view.timeSelection.end, 35.5)
                        && view.timeSelection.firstTrack == 2 && view.timeSelection.lastTrack == 3,
                    "The selection goes with its material");
            require(view.selectedClipID() == audioWholeID && view.isSelected(audioWholeID),
                    "and the grabbed clip is still the primary one, inside it");
            require(session.trackHasInstrument(2), "A MIDI piece brings its lane's instrument along, as a moved clip does");
            session.undo();
            view.sync();
            require(close(endOf(session.findClip(midiID)), 36.0) && clipAt(0, 35.0) == nullptr
                        && clipAt(1, 32.0) != nullptr && close(endOf(clipAt(1, 32.0)), 33.0)
                        && clipAt(1, 34.0) != nullptr && clipAt(1, 34.0)->itemID == audioWholeID
                        && clipAt(2, 33.0) == nullptr && clipAt(3, 33.0) == nullptr && clipAt(3, 34.5) == nullptr,
                    "One undo puts every piece back together and takes the move away");

            // One row down instead: MIDI would land on the audio lane and
            // audio on the MIDI lane. Both rows say so while the drag is held,
            // and the drop is refused whole with nothing cut.
            {
                auto savedStatus = view.status;
                juce::String reported;
                view.status = [&reported](juce::String text) { reported = std::move(text); };
                drag({view.xFor(32.55), lowerRow(0)}, {view.xFor(34.9), lowerRow(1)});
                const juce::Point<float> oneDown {view.xFor(34.5), clipRowY(2)};
                view.mouseDown(event(grab, grab, false));
                view.mouseDrag(event(grab, oneDown, true));
                require(view.previewRowRefused(0) && view.previewRowRefused(1),
                        "A row whose material may not land where the drag would put it says so before the drop");
                reported.clear();
                view.mouseUp(event(grab, oneDown, true));
                require(reported.isNotEmpty(), "The refused drop says why");
                require(close(endOf(session.findClip(midiID)), 36.0) && clipAt(0, 32.5) == nullptr
                            && clipAt(1, 32.0) != nullptr && close(endOf(clipAt(1, 32.0)), 33.0)
                            && clipAt(1, 34.0) != nullptr && clipAt(2, 32.5) == nullptr,
                        "and nothing is cut or moved");
                require(close(view.timeSelection.start, 32.5) && close(view.timeSelection.end, 35.0)
                            && view.timeSelection.firstTrack == 0 && view.timeSelection.lastTrack == 1,
                        "The selection stays where it was");
                view.status = std::move(savedStatus);
            }

            // The arrow keys nudge the same material by a step of the grid:
            // the span, cut at its edges, moves a sixteenth on, replacing what
            // it covers, and the selection follows it. One undo takes it back.
            require(view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)), "The arrows nudge a selection");
            require(clipAt(0, 32.625) != nullptr && close(endOf(clipAt(0, 32.625)), 35.125)
                        && clipAt(0, 32.0) != nullptr && close(endOf(clipAt(0, 32.0)), 32.5)
                        && clipAt(0, 35.125) != nullptr && close(endOf(clipAt(0, 35.125)), 36.0),
                    "A nudged span is cut at its edges and moved a sixteenth on, over what it covered");
            require(clipAt(1, 32.625) != nullptr && close(endOf(clipAt(1, 32.625)), 33.125)
                        && clipAt(1, 34.125) != nullptr && clipAt(1, 34.125)->itemID == audioWholeID,
                    "on every lane it spans");
            require(close(view.timeSelection.start, 32.625) && close(view.timeSelection.end, 35.125),
                    "and the selection moves with it");
            session.undo();
            view.sync();
            require(close(endOf(session.findClip(midiID)), 36.0) && clipAt(0, 32.625) == nullptr
                        && clipAt(1, 34.0) != nullptr && clipAt(1, 34.0)->itemID == audioWholeID,
                    "One undo takes the nudge back whole");

            // The model's own guarantees, in a document of its own.
            {
                auto document = std::make_unique<Session>();
                auto& doc = *document;
                using Spans = std::vector<std::pair<double, double>>;
                const auto visibleClips = [&doc](int track)
                {
                    Spans spans;
                    for (auto* clip : te::getAudioTracks(*doc.edit)[track]->getClips())
                        if (doc.shouldShowClipInArrangement(*clip))
                            spans.emplace_back(clip->getPosition().time.getStart().inSeconds(),
                                               clip->getPosition().time.getEnd().inSeconds());
                    std::sort(spans.begin(), spans.end());
                    return spans;
                };
                const auto sameSpans = [&close](const Spans& a, const Spans& b)
                {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); ++i)
                        if (!close(a[i].first, b[i].first) || !close(a[i].second, b[i].second)) return false;
                    return true;
                };
                te::EditItemID midi;
                require(doc.createClip(0, 0.0, &midi).wasOk() && doc.importAudioAt(source.getFile(), 1, 0.0).wasOk(),
                        "A MIDI clip and an audio clip to carry");
                const auto lanes = doc.trackCount();
                // Past the last lane: the lanes the rows need are made, of the
                // kinds they want, in the same undo step as the move.
                std::vector<te::EditItemID> moved;
                require(doc.moveClipRegion(0.0, 1.0, 0, 1, 10.0, lanes, &moved).wasOk(),
                        "A span carried past the last lane moves");
                require(doc.trackCount() == lanes + 2 && doc.trackType(lanes) == Session::TrackType::midi
                            && doc.trackType(lanes + 1) == Session::TrackType::audio,
                        "and makes the lanes its rows need, of the kinds they want");
                require(moved.size() == 2 && sameSpans(visibleClips(lanes), {{10.0, 11.0}})
                            && sameSpans(visibleClips(lanes + 1), {{10.0, 11.0}}),
                        "The part inside the span lands on each new lane");
                require(sameSpans(visibleClips(0), {{1.0, 2.0}}) && visibleClips(1).empty(),
                        "and what was outside the span stays behind");
                doc.undo();
                require(doc.trackCount() == lanes && sameSpans(visibleClips(0), {{0.0, 2.0}})
                            && sameSpans(visibleClips(1), {{0.0, 1.0}}),
                        "One undo takes the lanes and the move back together");
                // A row that may not land refuses the whole move before anything is cut.
                require(doc.moveClipRegion(0.0, 1.0, 0, 1, 0.0, 1).failed()
                            && sameSpans(visibleClips(0), {{0.0, 2.0}}) && sameSpans(visibleClips(1), {{0.0, 1.0}}),
                        "A row whose material may not land refuses the move, and nothing is cut");
                // A move by less than its own width overlaps its source: the
                // piece lands, what it covers gives way, and nothing overlaps.
                require(doc.moveClipRegion(0.25, 0.75, 1, 1, 0.5, 1).wasOk()
                            && sameSpans(visibleClips(1), {{0.0, 0.25}, {0.5, 1.0}}),
                        "A span moved by less than its width replaces what it covers and leaves the rest");
                for (auto* piece : te::getAudioTracks(*doc.edit)[1]->getClips())
                    if (close(piece->getPosition().time.getStart().inSeconds(), 0.5))
                        require(close(piece->getPosition().offset.inSeconds(), 0.25), "and the piece keeps its place in the source");
                doc.undo();
                // An empty row inside the span empties the lane it lands on, as a paste does.
                te::EditItemID later;
                require(doc.createClip(2, 10.0, &later).wasOk() && sameSpans(visibleClips(2), {{10.0, 12.0}}),
                        "A clip where an empty row will land");
                require(doc.moveClipRegion(0.0, 1.0, 1, 2, 10.0, 1).wasOk() && sameSpans(visibleClips(1), {{10.0, 11.0}})
                            && sameSpans(visibleClips(2), {{11.0, 12.0}}),
                        "An empty row inside a carried span empties the lane it lands on, as a paste does");
                // A span with nothing in it moves nothing and succeeds.
                require(doc.moveClipRegion(20.0, 21.0, 0, 3, 30.0, 0).wasOk() && doc.trackCount() == lanes,
                        "A span holding nothing moves nothing");
                doc.releaseAudioDevice();
            }

            // Everything this scenario made lives past thirty seconds.
            for (int track = 0; track < 4; ++track)
            {
                std::vector<te::EditItemID> made;
                for (auto* candidate : te::getAudioTracks(*session.edit)[track]->getClips())
                    if (candidate->getPosition().time.getStart().inSeconds() > 30.0)
                        made.push_back(candidate->itemID);
                for (const auto made_id : made) session.deleteClip(made_id);
            }
            require(session.selectPatternClip(patternBefore).wasOk(), "The note editor goes back to the clip it showed");
            view.setSelection({});
            view.clearTimeSelection();
            view.viewStart = previousViewStart;
            view.viewSpan = previousViewSpan;
            view.trackScroll = previousTrackScroll;
            view.sync();
            view.resized();
            require(clipsOn(0) == startingClips[0] && clipsOn(1) == startingClips[1]
                        && clipsOn(2) == startingClips[2] && clipsOn(3) == startingClips[3],
                    "The move scenario leaves the arrangement as it found it");
            require(session.trackCount() == 4, "The move scenario creates no tracks");
        }
