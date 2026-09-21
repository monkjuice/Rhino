        // Start asynchronous waveform scanning after the deterministic frame check.
        require(session.importAudio(source.getFile()).wasOk(), "Import audio");
        auto* clip = te::getAudioTracks(*session.edit)[1]->getClips()[0];
        const auto id = clip->itemID;
        view.sync();
        view.fit();
        view.setSelection({id}, id);
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.splitSelectedAtPlayhead();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Split creates a second audio clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo split restores one audio clip");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Original clip remains after undo split");
        view.sync();
        view.fit();
        view.setSelection({id}, id);
        view.gridSettings.mode = GridMode::fixed;
        view.gridSettings.fixedDivision = GridDivision::sixteenth;
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.125), "Right arrow nudges selected audio by one sixteenth");
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "Undo restores nudge");
        view.gridSettings.fixedDivision = GridDivision::eighth;
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.25), "Right arrow follows the selected one-eighth snap grid");
        session.undo();
        view.gridSettings.fixedDivision = GridDivision::sixteenth;
        view.duplicateSelected();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Duplicate creates a second audio clip");
        auto* duplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto originalEnd = clip->getPosition().time.getEnd().inSeconds();
        require(close(duplicate->getPosition().time.getStart().inSeconds(), originalEnd),
                "Duplicate places audio clips exactly after the source clip when space is available");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo duplicate restores one audio clip");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Original clip remains after undo duplicate");
        // Duplicating moves the selection onto the copy, as it does in Live, so
        // undoing it leaves the selection naming a clip that is no longer there.
        view.setSelection({id}, id);
        const auto originalColour = clip->getColour();
        view.keyPressed(juce::KeyPress('C'));
        require(clip->getColour() != originalColour, "C cycles the selected clip colour label");
        session.undo();
        const auto audioTrackSlots = static_cast<int>(session.deviceSlots(1).size());
        const auto clipSlots = session.clipPluginCount(id);
        view.sync();
        view.fit();
        view.itemDropped({"rhino-browser:effect:RhinoSpace", nullptr,
                          {static_cast<int>(view.xFor(0.25)), static_cast<int>(view.lane(1).getCentreY())}});
        require(static_cast<int>(session.deviceSlots(1).size()) == audioTrackSlots,
                "Dropping an audio effect on an audio clip leaves the track Device View unchanged");
        require(session.clipPluginCount(id) == clipSlots + 1,
                "Dropping an audio effect on an audio clip inserts clip-local FX");
        session.undo();

        // Lane heights depend on the panel size, so gestures take their y from
        // the lane rather than assuming one. A clip is two rows and only the
        // upper one is the clip itself, so a gesture that means to select or
        // carry one aims at that strip; the row below it puts the selection
        // line down instead.
        const auto clipRowY = [&view](int track) { return view.lane(track).getY() + 10.0f; };
        const auto audioLaneY = clipRowY(1);
        const auto event = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged,
                                   juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier))
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                mods, 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const auto drag = [&view, &event](juce::Point<float> down, juce::Point<float> to)
        {
            view.mouseDown(event(down, down, false));
            view.mouseDrag(event(down, to, true));
            view.mouseUp(event(down, to, true));
        };
        const auto automationParameters = session.deviceParameters(0, 0);
        require(!automationParameters.empty(), "Starter device exposes an automatable parameter");
        require(session.setTrackAutomationPoints({0, 0, 0}, {{0.0, automationParameters.front().minimum},
                                                             {0.75, automationParameters.front().maximum}}).wasOk(),
                "Can add automation before exercising clip gestures");
        view.sync();
        view.fit();
        drag({view.xFor(0.5), clipRowY(1)}, {view.xFor(0.75), clipRowY(1)});
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25),
                "Automation overlays do not block a clip move gesture");
        session.undo();
        session.undo();
        view.sync();
        view.fit();
        view.setSelection({id}, id);
        view.duplicateSelected();
        auto* snapDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto snapDuplicateID = snapDuplicate->itemID;
        require(session.editClip(snapDuplicateID, {1.35, 2.35, snapDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move duplicate away before testing clip-edge snap");
        view.sync();
        view.setSelection({snapDuplicateID}, snapDuplicateID);
        drag({view.xFor(1.85), clipRowY(1)}, {view.xFor(1.46), clipRowY(1)});
        require(close(session.findAudioClip(snapDuplicateID)->getPosition().time.getStart().inSeconds(), originalEnd),
                "Dragging a clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        require(session.editClip(id, {0.0, 0.5, 0.0}, ClipGesture::trimRight).wasOk(),
                "Can trim the source audio clip before testing trimmed edge snap");
        view.sync();
        view.setSelection({id}, id);
        view.duplicateSelected();
        auto* trimmedDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto trimmedDuplicateID = trimmedDuplicate->itemID;
        require(close(trimmedDuplicate->getPosition().time.getStart().inSeconds(), 0.5),
                "Duplicate places trimmed audio clips directly after the visible clip end");
        require(session.editClip(trimmedDuplicateID, {1.35, 1.85, trimmedDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move trimmed duplicate away before testing edge snap");
        view.sync();
        view.setSelection({trimmedDuplicateID}, trimmedDuplicateID);
        drag({view.xFor(1.6), clipRowY(1)}, {view.xFor(0.77), clipRowY(1)});
        require(close(session.findAudioClip(trimmedDuplicateID)->getPosition().time.getStart().inSeconds(), 0.5),
                "Dragging a trimmed clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        session.undo();
        view.setSelection({id}, id);
        const auto rulerY = view.rulerTop + 8.0f;
        drag({view.xFor(0.5), rulerY}, {view.xFor(1.5), rulerY});
        auto loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.5) && close(loopRange.getEnd().inSeconds(), 1.5),
                "Dragging the arrangement ruler selects the transport loop range");
        require(session.edit->getTransport().looping, "Ruler loop selection enables transport looping");
        session.refreshLoop();
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.5) && close(loopRange.getEnd().inSeconds(), 1.5),
                "Manual ruler loop selection is not overwritten by auto loop refresh");
        drag({view.xFor(1.25), rulerY}, {view.xFor(1.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 1.0) && close(loopRange.getEnd().inSeconds(), 2.0),
                "Dragging inside the ruler loop moves the range without changing its length");
        drag({view.xFor(1.0) + 1.0f, rulerY}, {view.xFor(0.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.75) && close(loopRange.getEnd().inSeconds(), 2.0),
                "Dragging the loop start edge expands the loop earlier");
        drag({view.xFor(2.0) - 1.0f, rulerY}, {view.xFor(1.75), rulerY});
        loopRange = session.edit->getTransport().getLoopRange();
        require(close(loopRange.getStart().inSeconds(), 0.75) && close(loopRange.getEnd().inSeconds(), 1.75),
                "Dragging the loop end edge trims the loop later edge");
        const auto rightClick = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
        view.mouseDown(event({view.xFor(1.0), rulerY}, {view.xFor(1.0), rulerY}, false, rightClick));
        require(!session.hasManualLoopRange(), "Right-clicking the ruler loop clears the manual loop range");
        view.mouseDown(event({view.xFor(0.25), rulerY}, {view.xFor(0.25), rulerY}, false));
        view.mouseUp(event({view.xFor(0.25), rulerY}, {view.xFor(0.25), rulerY}, false));
        require(close(playheadTime(session.edit->getTransport()), 0.25), "Clicking the ruler still seeks the playhead");
        view.mouseDown(event({40, 82}, {40, 82}, false));
        require(view.selectedTrack == 0, "Clicking the pattern lane selects the pattern track");
        view.mouseDown(event({40, audioLaneY}, {40, audioLaneY}, false));
        require(view.selectedTrack == 1 && notifiedTrack == 1, "Clicking the audio lane selects and broadcasts the audio track");
        view.setSelection({patternID}, patternID);
        // In time rather than pixels: the drag has to cross the same musical
        // distance whatever width the track headers are given. A quarter second
        // plus a little lands on 0.25 once the sixteenth grid has snapped it.
        drag({view.xFor(0.05), clipRowY(0)}, {view.xFor(0.33), clipRowY(0)});
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.25), "Pointer drag moves MIDI pattern clips");
        session.undo();
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.0), "Undo restores MIDI pattern move");
        view.sync();
        view.setSelection({patternID}, patternID);
        const auto patternEnd = session.pattern().getPosition().time.getEnd().inSeconds();
        drag({static_cast<float>(view.xFor(patternEnd) - 2.0), view.lane(0).getCentreY()},
             {static_cast<float>(view.xFor(0.75)), view.lane(0).getCentreY()});
        require(close(session.pattern().getPosition().time.getEnd().inSeconds(), 0.75), "Right trim shrinks MIDI clips");
        view.sync();
        drag({static_cast<float>(view.xFor(0.75) - 2.0), view.lane(0).getCentreY()},
             {static_cast<float>(view.xFor(patternEnd)), view.lane(0).getCentreY()});
        require(close(session.pattern().getPosition().time.getEnd().inSeconds(), patternEnd), "Right trim can expand a previously shrunken MIDI clip");
        session.undo();
        session.undo();
        view.itemDropped({"rhino-browser:preset:MinimalKit", nullptr, {static_cast<int>(view.xFor(2.0)), 82}});
        auto* patternTrack = te::getAudioTracks(*session.edit)[0];
        const auto findPatternClipAt = [&patternTrack](double start) -> te::Clip*
        {
            for (auto* candidate : patternTrack->getClips())
                if (std::abs(candidate->getPosition().time.getStart().inSeconds() - start) < 0.01)
                    return candidate;
            return nullptr;
        };
        auto* blocking = findPatternClipAt(2.0);
        require(blocking != nullptr, "The dropped preset occupies the space just after the pattern clip");
        const auto blockingID = blocking->itemID;
        const auto blockedClipCount = patternTrack->getClips().size();
        view.setSelection({patternID}, patternID);
        view.duplicateSelected();
        // Live's rule, and what replaced a hunt for the first free gap: the
        // copy lands flush against the source and replaces what was there, so
        // a duplicate can never appear bars away from the clip it came from.
        require(patternTrack->getClips().size() == blockedClipCount,
                "Duplicating over an occupied span replaces rather than adds");
        require(session.findClip(blockingID) == nullptr, "The clip that was in the way is gone");
        auto* adjacentCopy = findPatternClipAt(2.0);
        require(adjacentCopy != nullptr, "Duplicate lands directly after the source even when that space is taken");
        const auto adjacentCopyStart = adjacentCopy->getPosition().time.getStart().inSeconds();
        view.sync();
        const auto clickX = static_cast<float>(view.xFor(adjacentCopyStart + 0.1));
        const auto clickY = clipRowY(0);
        view.mouseDown(event({clickX, clickY}, {clickX, clickY}, false));
        view.mouseUp(event({clickX, clickY}, {clickX, clickY}, false));
        require(close(adjacentCopy->getPosition().time.getStart().inSeconds(), adjacentCopyStart),
                "Clicking a duplicate selects it without moving it");
        session.undo();
        session.undo();
        view.setSelection({id}, id);
        juce::StringArray dropped;
        dropped.add(source.getFile().getFullPathName());
        view.filesDropped(dropped, static_cast<int>(view.xFor(1.0)), static_cast<int>(audioLaneY));
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Dropping an audio file on the audio lane imports a clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo restores dropped audio import");
        require(session.trackCount() == 2, "The starter track plus the one the audio import created");
