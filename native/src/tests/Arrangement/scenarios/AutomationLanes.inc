        // Automation runs the length of the timeline, so these checks use the
        // row stack rather than clip bounds. Track 0 is the pattern track and
        // always has a device at slot 0.
        view.sync();
        view.fit();
        const auto laneParameters = session.deviceParameters(0, 0);
        require(!laneParameters.empty(), "Pattern device exposes a parameter to automate");
        const Session::DeviceTarget laneTarget {0, 0, 0};
        const auto rowsWithoutLanes = view.rows.size();
        const auto secondTrackTop = view.lane(1).getY();

        // Lane height follows the panel height, and a device rack opening below
        // the arrangement resizes it. The row stack has to reflow then, or the
        // painted lanes and the header controls drift apart.
        {
            const auto tallHeight = view.getHeight();
            require(std::abs(view.lane(0).getHeight() - view.laneHeight()) < 0.001f,
                    "Lane rows start at the current lane height");
            view.setSize(view.getWidth(), tallHeight - 120);
            require(std::abs(view.lane(0).getHeight() - view.laneHeight()) < 0.001f,
                    "Shrinking the panel reflows the lane rows rather than keeping stale heights");
            require(std::abs(view.lane(1).getY() - view.lane(0).getBottom()) < 0.001f,
                    "Reflowed lane rows stay contiguous");
            // Lanes stop shrinking before the panel does, so a short panel
            // leaves rows hanging past the pinned main row. The controls are
            // cropped at the lane viewport rather than drawn over that row.
            require(view.laneHeaders.getY() == static_cast<int>(view.lanesTop)
                        && view.laneHeaders.getBottom() <= static_cast<int>(view.masterLane().getY()),
                    "The header container spans the lanes and stops at the main row");
            for (size_t track = 0; track < view.mute.size(); ++track)
                require(view.mute[track]->getParentComponent() == &view.laneHeaders
                            && view.volume[track]->getParentComponent() == &view.laneHeaders,
                        "Track header controls are cropped to the lane viewport");
            if (view.rowsHeight > static_cast<float>(view.laneHeaders.getHeight()))
                require(view.mute.back()->getBottom() > view.laneHeaders.getHeight(),
                        "A row hanging past the lanes keeps its controls, cropped, rather than losing them");
            view.setSize(view.getWidth(), tallHeight);
            view.sync();
            view.fit();
        }

        require(session.showTrackAutomation(laneTarget, false).wasOk(), "A knob can reveal automation on its track");
        view.sync();
        require(view.rows.size() == rowsWithoutLanes,
                "Automation shown on the track itself adds no row");
        require(view.trackLanes[0].size() == 1 && !view.trackLanes[0].front().active(),
                "A revealed lane is carried by the view and starts inactive");

        require(session.showTrackAutomation(laneTarget, true).wasOk(), "The same lane can be moved to its own row");
        view.sync();
        require(view.rows.size() == rowsWithoutLanes + 1, "A lane of its own adds one row");
        require(view.rows[1].track == 0 && view.rows[1].automation == 0,
                "The lane row is stacked directly under the track it clones");
        require(view.lane(1).getY() > secondTrackTop, "A lane row pushes the tracks below it down");
        require(view.trackAt(view.rowBounds(1).getCentreY()) == 0,
                "A lane row answers for the track it clones");

        // Lifting the resting line of a never-drawn lane is what activates it.
        const auto laneRow = view.rowBounds(1);
        const auto laneEvent = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
        };
        const juce::Point<float> liftFrom {view.xFor(0.5), laneRow.getCentreY()};
        const juce::Point<float> liftTo {view.xFor(0.5), laneRow.getY() + 2.0f};
        view.mouseDown(laneEvent(liftFrom, liftFrom, false));
        view.mouseDrag(laneEvent(liftFrom, liftTo, true));
        view.mouseUp(laneEvent(liftFrom, liftTo, true));
        require(session.trackAutomationState(laneTarget).active,
                "Dragging the resting line of a lane row writes a curve");
        {
            const auto written = session.trackAutomations(0);
            require(written.size() == 1 && written.front().points.size() == 2,
                    "Lifting the resting line leaves a flat two-point curve");
            require(written.front().points.front().timeSeconds == 0.0
                        && written.front().points.back().timeSeconds > written.front().points.front().timeSeconds,
                    "The written curve spans the timeline rather than one clip");
            require(written.front().points.front().value > laneParameters.front().minimum,
                    "The lifted curve sits above the parameter minimum");
        }
        require(session.deviceParameters(0, 0)[0].automated,
                "A drawn lane reports the parameter as automated");

        // A clip drag on the track's own row must still move the clip, not the
        // curve drawn over it, until automation mode is switched on.
        require(!view.automationHitAt({view.xFor(0.5), view.lane(0).getCentreY()}).valid(),
                "Automation ignores the pointer on a track row in clip mode");
        view.automationButton.setToggleState(true, juce::dontSendNotification);
        require(!view.automationHitAt({view.xFor(0.5), view.lane(0).getCentreY()}).valid(),
                "A lane on its own row is not also editable over the track");
        view.automationButton.setToggleState(false, juce::dontSendNotification);

        // Delete acts on the last thing selected and nothing else. It used to
        // reach for the focused automation lane whatever was selected.
        {
            const auto savedHeight = view.getHeight();
            view.setSize(view.getWidth(), 700);
            require(session.setTrackAutomationPoints(laneTarget, {{0.0, laneParameters.front().minimum},
                                                                  {4.0, laneParameters.front().maximum}}).wasOk(),
                    "Redraw a curve for the delete-routing checks");
            require(session.addAudioTrack().wasOk(), "The delete-routing checks need a track of their own");
            const auto spareTrack = session.trackCount() - 1;
            const auto tracksBefore = session.trackCount();
            view.sync();
            if (view.clips.empty())
            {
                require(session.createClip(0, 6.0).wasOk(), "Create a clip for the delete-routing checks");
                view.sync();
            }

            // A clip: Delete removes the clip and leaves the curve alone.
            require(!view.clips.empty(), "The delete-routing checks need a clip on screen");
            const auto victim = view.clips.front().id;
            view.setSelection({victim}, victim);
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(session.findClip(victim) == nullptr, "Delete removes the selected clip");
            require(session.trackAutomationState(laneTarget).active,
                    "Deleting a clip leaves the focused automation curve alone");
            require(session.trackCount() == tracksBefore, "Deleting a clip leaves the tracks alone");
            session.undo();
            view.sync();

            // Empty lane space: Delete has nothing to act on.
            const auto emptyLane = view.lane(spareTrack);
            view.mouseDown(laneEvent({emptyLane.getCentreX(), emptyLane.getCentreY()},
                                     {emptyLane.getCentreX(), emptyLane.getCentreY()}, false));
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(session.trackCount() == tracksBefore && session.trackAutomationState(laneTarget).active,
                    "Delete over empty lane space removes nothing");

            // An automation lane: Delete clears its curve, not a clip or track.
            const auto laneCentreY = view.rowBounds(1).getCentreY();
            view.mouseDown(laneEvent({40.0f, laneCentreY}, {40.0f, laneCentreY}, false));
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(!session.trackAutomationState(laneTarget).active,
                    "Delete clears the selected automation curve");
            require(session.trackAutomationState(laneTarget).visible,
                    "Deleting a curve keeps its lane, which only the knob menu hides");
            require(session.trackCount() == tracksBefore, "Deleting a curve leaves the tracks alone");

            // A track header: Delete removes that track.
            const auto headerY = view.lane(spareTrack).getCentreY();
            view.mouseDown(laneEvent({40.0f, headerY}, {40.0f, headerY}, false));
            require(view.selectedTrackIndex() == spareTrack, "Clicking a track header selects that track");
            view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(session.trackCount() == tracksBefore - 1, "Delete removes the selected track");
            session.undo();
            view.sync();
            require(session.trackCount() == tracksBefore, "Undo restores a deleted track");
            require(session.removeAudioTrack(session.masterTrackIndex()).failed(),
                    "The main row is not a track that can be deleted");
            require(session.removeAudioTrack(spareTrack).wasOk(), "Tidy the spare track away");

            // A lane is addressed by where it lives, not by a stored index, so
            // renumbering the tracks above it must not orphan it.
            require(session.addAudioTrack().wasOk() && session.addAudioTrack().wasOk(),
                    "Add two tracks to renumber between");
            view.sync();
            const auto lowerTrack = session.trackCount() - 1;
            const auto upperTrack = lowerTrack - 1;
            const auto lowerParameters = session.deviceParameters(lowerTrack, 0);
            require(!lowerParameters.empty(), "A fresh track exposes a parameter to automate");
            const Session::DeviceTarget lowerTarget {lowerTrack, 0, 0};
            require(session.setTrackAutomationPoints(lowerTarget,
                        {{0.0, lowerParameters.front().minimum},
                         {2.0, lowerParameters.front().maximum}}).wasOk(),
                    "Draw a curve on the lower of the two");
            require(session.removeAudioTrack(upperTrack).wasOk(), "Remove the track above it");
            const Session::DeviceTarget renumbered {upperTrack, 0, 0};
            require(session.trackAutomationState(renumbered).active,
                    "A lane follows its track when the tracks above it are renumbered");
            require(session.trackAutomations(upperTrack).size() == 1,
                    "The renumbered track still reports its lane");
            require(session.removeAudioTrack(upperTrack).wasOk(), "Remove the track carrying the lane");
            require(!session.trackAutomationState(renumbered).visible,
                    "Deleting a track takes its automation lanes with it");
            view.sync();
            require(session.trackCount() == tracksBefore - 1,
                    "The delete-routing checks leave no tracks of their own behind");
            view.setSize(view.getWidth(), savedHeight);
            view.sync();
            view.fit();
        }

        require(session.clearTrackAutomationPoints(laneTarget).wasOk(), "A curve can be deleted from its lane");
        view.sync();
        require(view.rows.size() == rowsWithoutLanes + 1 && !view.trackLanes[0].front().active(),
                "Deleting a curve keeps the lane on screen");
        require(session.hideTrackAutomation(laneTarget).wasOk(), "A lane can be hidden again");
        view.sync();
        require(view.rows.size() == rowsWithoutLanes && view.trackLanes[0].empty(),
                "Hiding a lane removes its row");
        require(session.showTrackAutomation({session.masterTrackIndex(), 0, 0}, false).failed(),
                "The main row refuses automation lanes it cannot show");
