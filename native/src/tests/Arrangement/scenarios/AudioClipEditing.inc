        // Start asynchronous waveform scanning after the deterministic frame check.
        require(session.importAudio(source.getFile()).wasOk(), "Import audio");
        auto* clip = te::getAudioTracks(*session.edit)[1]->getClips()[0];
        const auto id = clip->itemID;
        view.sync();
        view.fit();
        view.selected = id;
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.splitSelectedAtPlayhead();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Split creates a second audio clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo split restores one audio clip");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Original clip remains after undo split");
        view.sync();
        view.fit();
        view.selected = id;
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.125), "Right arrow nudges selected audio by one sixteenth");
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "Undo restores nudge");
        view.snapSize.setSelectedId(2, juce::dontSendNotification);
        view.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.25), "Right arrow follows the selected one-eighth snap grid");
        session.undo();
        view.snapSize.setSelectedId(1, juce::dontSendNotification);
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
        const auto originalColour = clip->getColour();
        view.keyPressed(juce::KeyPress('C'));
        require(clip->getColour() != originalColour, "C cycles the selected clip colour label");
        session.undo();
        const auto audioTrackSlots = static_cast<int>(session.deviceSlots(1).size());
        const auto clipSlots = session.clipPluginCount(id);
        view.sync();
        view.fit();
        view.itemDropped({"theta-browser:effect:ThetaSpace", nullptr,
                          {static_cast<int>(view.xFor(0.25)), static_cast<int>(view.lane(1).getCentreY())}});
        require(static_cast<int>(session.deviceSlots(1).size()) == audioTrackSlots,
                "Dropping an audio effect on an audio clip leaves the track Device View unchanged");
        require(session.clipPluginCount(id) == clipSlots + 1,
                "Dropping an audio effect on an audio clip inserts clip-local FX");
        session.undo();

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
        require(session.setClipAutomationRamp(id, {0, 0, 0}, 0.0, 0.75,
                                              automationParameters.front().minimum, automationParameters.front().maximum).wasOk(),
                "Can add automation before exercising clip gestures");
        view.sync();
        view.fit();
        drag({view.xFor(0.5), view.lane(1).getCentreY()}, {view.xFor(0.75), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(id)->getPosition().time.getStart().inSeconds(), 0.25),
                "Automation overlays do not block a clip move gesture");
        session.undo();
        session.undo();
        view.sync();
        view.fit();
        view.selected = id;
        view.duplicateSelected();
        auto* snapDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto snapDuplicateID = snapDuplicate->itemID;
        require(session.editClip(snapDuplicateID, {1.35, 2.35, snapDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move duplicate away before testing clip-edge snap");
        view.sync();
        view.selected = snapDuplicateID;
        drag({view.xFor(1.85), view.lane(1).getCentreY()}, {view.xFor(1.46), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(snapDuplicateID)->getPosition().time.getStart().inSeconds(), originalEnd),
                "Dragging a clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        require(session.editClip(id, {0.0, 0.5, 0.0}, ClipGesture::trimRight).wasOk(),
                "Can trim the source audio clip before testing trimmed edge snap");
        view.sync();
        view.selected = id;
        view.duplicateSelected();
        auto* trimmedDuplicate = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
        const auto trimmedDuplicateID = trimmedDuplicate->itemID;
        require(close(trimmedDuplicate->getPosition().time.getStart().inSeconds(), 0.5),
                "Duplicate places trimmed audio clips directly after the visible clip end");
        require(session.editClip(trimmedDuplicateID, {1.35, 1.85, trimmedDuplicate->getPosition().offset.inSeconds()}, ClipGesture::move).wasOk(),
                "Can move trimmed duplicate away before testing edge snap");
        view.sync();
        view.selected = trimmedDuplicateID;
        drag({view.xFor(1.6), view.lane(1).getCentreY()}, {view.xFor(0.77), view.lane(1).getCentreY()});
        require(close(session.findAudioClip(trimmedDuplicateID)->getPosition().time.getStart().inSeconds(), 0.5),
                "Dragging a trimmed clip near a neighbor edge snaps it flush instead of overlapping");
        session.undo();
        session.undo();
        session.undo();
        session.undo();
        view.selected = id;
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
        view.mouseDown(event({40, 180}, {40, 180}, false));
        require(view.selectedTrack == 1 && notifiedTrack == 1, "Clicking the audio lane selects and broadcasts the audio track");
        view.selected = patternID;
        drag({240, 82}, {301, 82});
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.25), "Pointer drag moves MIDI pattern clips");
        session.undo();
        require(close(session.pattern().getPosition().time.getStart().inSeconds(), 0.0), "Undo restores MIDI pattern move");
        view.sync();
        view.selected = patternID;
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
        view.itemDropped({"theta-browser:preset:MinimalKit", nullptr, {static_cast<int>(view.xFor(2.0)), 82}});
        auto* patternTrack = te::getAudioTracks(*session.edit)[0];
        const auto blockedClipCount = patternTrack->getClips().size();
        view.selected = patternID;
        view.duplicateSelected();
        require(patternTrack->getClips().size() == blockedClipCount + 1,
                "Duplicate creates a MIDI copy when the adjacent space is occupied");
        auto* gapCopy = patternTrack->getClips().getLast();
        require(close(gapCopy->getPosition().time.getStart().inSeconds(), 4.0),
                "Duplicate lands after the blocking clip when it cannot fit beside the source");
        view.sync();
        const auto clickX = static_cast<float>(view.xFor(gapCopy->getPosition().time.getStart().inSeconds() + 0.1));
        const auto clickY = view.lane(0).getCentreY();
        view.mouseDown(event({clickX, clickY}, {clickX, clickY}, false));
        view.mouseUp(event({clickX, clickY}, {clickX, clickY}, false));
        require(close(gapCopy->getPosition().time.getStart().inSeconds(), 4.0),
                "Clicking a gap-placed duplicate does not move it to the track end");
        session.undo();
        session.undo();
        view.selected = id;
        juce::StringArray dropped;
        dropped.add(source.getFile().getFullPathName());
        view.filesDropped(dropped, static_cast<int>(view.xFor(1.0)), 180);
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 2, "Dropping an audio file on the audio lane imports a clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Undo restores dropped audio import");
        require(session.trackCount() == 2, "Starter session has pattern and one audio track");
