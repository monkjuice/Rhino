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

        // Adding nodes to a curve that is already there. The pointer is asked
        // what it is over before anything is clicked, and what it answers is
        // what the click has to write - otherwise the ghost drawn under the
        // pointer and the node that lands are two different promises.
        {
            const auto laneBox = view.rowBounds(1);
            const auto minimum = laneParameters.front().minimum;
            const auto maximum = laneParameters.front().maximum;
            // The ramp runs off the right of the view, so every position in
            // the lane is between its two ends and a click there adds a node
            // rather than landing on one.
            const auto rampEnd = std::max(4.0, view.timeAt(laneBox.getRight()) + 2.0);
            require(session.setTrackAutomationPoints(laneTarget, {{0.0, minimum}, {rampEnd, maximum}}).wasOk(),
                    "Draw a ramp to add nodes to");
            view.sync();
            require(view.trackLanes[0].size() == 1 && view.trackLanes[0].front().active(),
                    "The ramp is the lane's own curve");

            const auto nodeTime = view.snapped(view.timeAt(laneBox.getCentreX()), false);
            require(nodeTime > 0.001 && nodeTime < rampEnd - 0.001,
                    "The lane centre is between the ramp's two ends");
            const auto lineValue = view.trackLanes[0].front().valueAt(nodeTime);
            const auto lineY = view.automationYFor(1, view.trackLanes[0].front(), lineValue);

            // Hovering: on the line, a few pixels off it, and well away.
            const auto onLine = view.automationHoverAt({view.xFor(nodeTime), lineY + 3.0f}, false);
            require(onLine.valid() && onLine.point < 0 && onLine.onCurve,
                    "A pointer a few pixels off the line is still on it");
            require(std::abs(onLine.value - lineValue) < 0.0001f,
                    "On the line, the reading is the line's own value rather than the pointer's");
            require(std::abs(onLine.timeSeconds - nodeTime) < 0.0001,
                    "The ghost sits at the time the node would take");
            const auto offLine = view.automationHoverAt({view.xFor(nodeTime), lineY + 14.0f}, false);
            require(offLine.valid() && !offLine.onCurve && offLine.value < lineValue - 0.0001f,
                    "Further off, the reading follows the pointer instead of the line");
            require(view.automationHoverAt({view.xFor(nodeTime), laneBox.getCentreY()}, false).valid(),
                    "Anywhere in a lane row answers for the curve drawn on it");
            require(session.automationValueText(laneTarget, lineValue).isNotEmpty(),
                    "A level reads in the units the parameter's own knob prints");

            // A lane is painted, not composed, so a render is the only
            // coverage the lit curve, the ghost and the reading can have.
            // The snapshot runs every time as a paint smoke test; the PNG is
            // only written when somebody asks to look at it.
            {
                view.automationHover = onLine;
                const auto band = view.rowBounds(1).getSmallestIntegerContainer()
                    .expanded(0, 40).withX(0).withWidth(view.getWidth());
                const auto picture = view.createComponentSnapshot(band);
                require(picture.getWidth() == band.getWidth() && picture.getHeight() == band.getHeight(),
                        "The automation lane renders");
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_AUTOMATION_SNAPSHOT", {});
                    shot.isNotEmpty())
                {
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(picture, *stream);
                }
                view.automationHover = {};
            }

            // Clicking: the node lands where the ghost was, on the line.
            const auto pointsBefore = session.trackAutomations(0).front().points.size();
            const juce::Point<float> clickAt {view.xFor(nodeTime), lineY + 3.0f};
            view.mouseDown(laneEvent(clickAt, clickAt, false));
            view.mouseUp(laneEvent(clickAt, clickAt, false));
            view.sync();
            {
                const auto written = session.trackAutomations(0).front().points;
                require(written.size() == pointsBefore + 1, "Clicking the lane adds a node to its curve");
                auto added = -1;
                for (int i = 0; i < static_cast<int>(written.size()); ++i)
                    if (std::abs(written[static_cast<size_t>(i)].timeSeconds - nodeTime) < 0.001)
                        added = i;
                require(added >= 0, "The new node lands at the time the ghost was drawn at");
                require(std::abs(written[static_cast<size_t>(added)].value - lineValue) < 0.001f,
                        "A node dropped near the line snaps flush onto it rather than denting the ramp");
            }

            // The node under the pointer is the one a press picks up.
            const juce::Point<float> nodeAt {view.xFor(nodeTime),
                                             view.automationYFor(1, view.trackLanes[0].front(), lineValue)};
            const auto nodeHover = view.automationHoverAt(nodeAt, false);
            require(nodeHover.valid() && nodeHover.point >= 0,
                    "A node under the pointer is marked rather than the line it sits on");
            require(std::abs(nodeHover.value - lineValue) < 0.001f,
                    "The reading over a node is that node's own level");

            // Double-clicking it takes it out again, so a curve that can gain
            // nodes by a click can lose them by one too.
            const auto laneClicks = [&view](juce::Point<float> point, int clicks)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                    &view, &view, juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), clicks, false);
            };
            view.mouseDown(laneClicks(nodeAt, 2));
            view.mouseUp(laneClicks(nodeAt, 2));
            view.sync();
            require(session.trackAutomations(0).front().points.size() == pointsBefore,
                    "Double-clicking a node takes it out again");

            // A click far from the line puts the node where the pointer is.
            const juce::Point<float> lowClick {view.xFor(nodeTime), lineY + 14.0f};
            view.mouseDown(laneEvent(lowClick, lowClick, false));
            view.mouseUp(laneEvent(lowClick, lowClick, false));
            view.sync();
            {
                const auto written = session.trackAutomations(0).front().points;
                require(written.size() == pointsBefore + 1, "A click away from the line still adds a node");
                for (const auto& point : written)
                    if (std::abs(point.timeSeconds - nodeTime) < 0.001)
                        require(point.value < lineValue - 0.0001f,
                                "A node dropped away from the line keeps the level it was dropped at");
            }

            // The hover is addressed by row, so a rebuilt stack must not leave
            // a stale one lighting a lane that has moved or gone.
            view.buildRows();
            require(!view.automationHover.valid(), "Rebuilding the row stack drops the hover");
        }

        // A clip drag on the track's own row must still move the clip, not the
        // curve drawn over it, until automation mode is switched on.
        require(!view.automationHoverAt({view.xFor(0.5), view.lane(0).getCentreY()}, false).valid(),
                "Automation ignores the pointer on a track row in clip mode");
        view.automationButton.setToggleState(true, juce::dontSendNotification);
        require(!view.automationHoverAt({view.xFor(0.5), view.lane(0).getCentreY()}, false).valid(),
                "A lane on its own row is not also editable over the track");
        view.automationButton.setToggleState(false, juce::dontSendNotification);

        // Delete acts on the last thing selected and nothing else. It used to
        // reach for the focused automation lane whatever was selected.
        {
            const auto savedHeight = view.getHeight();
            // The clicks below land on rows by their own coordinates, so the
            // stack has to fit. Written against the panel's foot - the times,
            // the scrollbar, the two rules - rather than as a flat number,
            // because the foot takes its slice of the height before any lane
            // gets one and the figure would otherwise need revisiting every
            // time the foot changes.
            view.setSize(view.getWidth(), 682 + static_cast<int>(view.footHeight));
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
