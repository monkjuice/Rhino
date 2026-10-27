        // Automation runs the length of the timeline, so these checks use the
        // row stack rather than clip bounds. Track 0 is the pattern track and
        // always has a device at slot 1, behind the DJ booth's hidden input at
        // slot 0.
        view.sync();
        view.fit();
        const auto laneParameters = session.deviceParameters(0, 1);
        require(!laneParameters.empty(), "Pattern device exposes a parameter to automate");
        const Session::DeviceTarget laneTarget {0, 1, 0};
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
            // A return's controls are pinned with it, in a container of their own.
            for (size_t track = 0; track < view.mute.size(); ++track)
            {
                const auto* container = view.isPinnedTrack(static_cast<int>(track)) ? &view.returnHeaders : &view.laneHeaders;
                require(view.mute[track]->getParentComponent() == container
                            && view.volume[track]->getParentComponent() == container,
                        "Track header controls are cropped to the lane viewport");
            }
            if (view.rowsHeight > static_cast<float>(view.laneHeaders.getHeight()))
                require(view.mute[static_cast<size_t>(view.firstPinnedTrack() - 1)]->getBottom() > view.laneHeaders.getHeight(),
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
        require(view.automationFoldBounds(0).isEmpty(),
                "A lane drawn over the track's own clips leaves nothing under the card to fold");

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
        require(session.deviceParameters(0, 1)[0].automated,
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

        // A track's own row shows the lane drawn over it that its card's
        // chooser picked, and edits it under the clip names with no mode to
        // switch on. None leaves the row to its clips.
        {
            const auto trackRow = view.trackRowIndex[0];
            const auto curve = view.automationArea(trackRow);
            const juce::Point<float> body {view.xFor(0.5), curve.getCentreY()};
            require(view.shownAutomation(0) == nullptr && !view.automationHoverAt(body, false).valid(),
                    "A lane on its own row is not also editable over the track");
            require(view.automationChooser[0]->getButtonText().startsWith("None"), "and the card's chooser says None");
            require(view.automationChooser[0]->getRight() - view.trackInput[0]->getX()
                            <= 2 * view.cardControlWidth + view.cardControlGap
                        && view.automationChooser[0]->getX() >= view.trackInput[0]->getRight()
                        && view.automationChooser[0]->getY() == view.trackInput[0]->getY(),
                    "The chooser sits right of the input chooser, on its line and inside the controls");

            require(laneParameters.size() > 1, "The pattern device has a second parameter to draw over the track");
            const Session::DeviceTarget overTarget {0, 1, 1};
            require(session.showTrackAutomation(overTarget, false).wasOk(), "Reveal a lane over the track");
            view.sync();
            const auto* shown = view.shownAutomation(0);
            require(shown != nullptr && shown->target.parameter == overTarget.parameter,
                    "A lane revealed over the track is the one its row shows");
            require(view.automationChooser[0]->getButtonText().startsWith(shown->parameterName),
                    "and the card's chooser names it");
            const auto clipBand = Arrangement::clipBandOf(view.lane(0));
            const auto clipNames = clipBand.getY() + view.clipHeaderHeight(clipBand.getHeight());
            require(!curve.isEmpty() && curve.getY() >= clipNames, "The curve runs under the strip the clip names are on");
            const auto hover = view.automationHoverAt(body, false);
            require(hover.valid() && view.trackLanes[0][static_cast<size_t>(hover.automation)].target.parameter
                                         == overTarget.parameter,
                    "The track's own row edits the lane it shows, with no mode to switch on");
            require(!view.automationHoverAt({view.xFor(0.5), view.lane(0).getY() + 8.0f}, false).valid(),
                    "The strip along the clips' tops stays the clips'");

            view.showAutomationOnTrack(0, {});
            view.sync();
            require(view.shownAutomation(0) == nullptr && !view.automationHoverAt(body, false).valid(),
                    "None leaves the row to its clips");
            require(view.automationChooser[0]->getButtonText().startsWith("None")
                        && session.trackAutomationState(overTarget).visible,
                    "and keeps the lane, which goes on playing");
            view.showAutomationOnTrack(0, overTarget);
            view.sync();
            require(view.shownAutomation(0) != nullptr, "Choosing the lane again shows it");
            // Painted over the clips, so a render is the coverage it can have;
            // the PNG is only written when somebody asks to look at it.
            {
                require(session.setTrackAutomationPoints(overTarget, {{0.0, laneParameters[1].minimum},
                                                                      {8.0, laneParameters[1].maximum}}).wasOk(),
                        "Draw a ramp over the track");
                view.sync();
                const auto band = view.lane(0).getSmallestIntegerContainer().withX(0).withWidth(view.getWidth());
                const auto picture = view.createComponentSnapshot(band);
                require(picture.getWidth() == band.getWidth(), "A track's row renders with the curve it shows");
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_AUTOMATION_TRACK_SNAPSHOT", {});
                    shot.isNotEmpty())
                {
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(picture, *stream);
                }
            }
            require(session.setTrackShownAutomation(0, laneTarget).failed(),
                    "A lane on a row of its own is not the track's row to show");
            require(session.hideTrackAutomation(overTarget).wasOk(), "Tidy the lane over the track away");
            view.sync();
            require(view.shownAutomation(0) == nullptr, "A hidden lane leaves the row to its clips");
        }

        // A drag moves a level by how far the pointer travels rather than to
        // where it is, so a lane a few dozen pixels tall still sweeps its range
        // in fine steps, and Shift steps a tenth as far again. A short lane
        // used to step a thirtieth of the range per pixel and hold a node to
        // its neighbours' line across a sixth of it.
        {
            const auto minimum = laneParameters.front().minimum;
            const auto span = laneParameters.front().maximum - minimum;
            const auto middle = minimum + span * 0.5f;
            const auto laneBox = view.rowBounds(1);
            const auto nodeTime = view.snapped(view.timeAt(laneBox.getCentreX()), false);
            const auto curveEnd = std::max(4.0, view.timeAt(laneBox.getRight()) + 2.0);
            const auto travel = std::max(view.automationArea(1).getHeight(), view.automationDragTravel);
            require(travel > view.automationArea(1).getHeight(), "A lane row is shorter than a full drag");
            const auto nodeEvent = [&view](juce::Point<float> down, juce::Point<float> point, bool dragged, int mods)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | mods), 1.0f, 0, 0, 0, 0,
                    &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
            };
            // Draws a flat curve with a node in the middle, carries that node
            // through the given offsets and answers the level it was put down at.
            const auto carry = [&](std::initializer_list<juce::Point<float>> offsets, int mods)
            {
                require(session.setTrackAutomationPoints(laneTarget, {{0.0, middle}, {nodeTime, middle},
                                                                      {curveEnd, middle}}).wasOk(),
                        "Draw a flat curve to carry a node on");
                view.sync();
                const juce::Point<float> from {view.xFor(nodeTime),
                                               view.automationYFor(1, view.trackLanes[0].front(), middle)};
                view.mouseDown(nodeEvent(from, from, false, mods));
                auto to = from;
                for (const auto offset : offsets)
                {
                    to = from + offset;
                    view.mouseDrag(nodeEvent(from, to, true, mods));
                }
                view.mouseUp(nodeEvent(from, to, true, mods));
                view.sync();
                // Held in a local: a range-for over a member of the returned
                // vector would read it after the vector had gone.
                const auto lanes = session.trackAutomations(0);
                for (const auto& point : lanes.front().points)
                    if (point.timeSeconds > 0.0 && point.timeSeconds < curveEnd)
                        return point.value;
                return -1.0f;
            };
            const auto levelMatches = [span](float a, float b) { return std::abs(a - b) < span * 0.0005f; };
            require(levelMatches(carry({{0.0f, -10.0f}}, 0), middle + span * 10.0f / travel),
                    "Ten pixels up moves a node ten steps of a full drag, not ten pixels of a short lane");
            require(levelMatches(carry({{0.0f, -10.0f}}, juce::ModifierKeys::shiftModifier),
                          middle + span * 10.0f / (travel * view.automationFineDivisor)),
                    "Shift moves it a tenth as far");
            require(levelMatches(carry({{0.0f, -1.5f * travel}, {0.0f, -1.5f * travel + 5.0f}}, 0),
                          minimum + span - span * 5.0f / travel),
                    "A node carried past the top comes back down as soon as the pointer turns");
            require(levelMatches(carry({{4.0f, -1.0f}}, 0), middle), "A pixel off its neighbours' line, a node snaps onto it");
            require(levelMatches(carry({{4.0f, -1.0f}}, juce::ModifierKeys::altModifier), middle + span / travel),
                    "and Alt places it freely");
        }

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
            // The returns pinned over the main row take their slice too, and
            // a lane under them is covered, so the stack is given all the room
            // it needs to be clicked on.
            view.setSize(view.getWidth(), 900 + static_cast<int>(view.footHeight + view.pinnedRowsHeight));
            require(session.setTrackAutomationPoints(laneTarget, {{0.0, laneParameters.front().minimum},
                                                                  {4.0, laneParameters.front().maximum}}).wasOk(),
                    "Redraw a curve for the delete-routing checks");
            require(session.addAudioTrack().wasOk(), "The delete-routing checks need a track of their own");
            const auto spareTrack = session.firstReturnTrack() - 1;
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
            const auto lowerTrack = session.firstReturnTrack() - 1;
            const auto upperTrack = lowerTrack - 1;
            // A fresh track's one automatable device is its Utility strip,
            // found by what it is: the DJ booth's hidden ends stand ahead of
            // it in the chain.
            const auto stripSlot = [&session](int track)
            {
                auto* onTrack = te::getAudioTracks(*session.edit)[track];
                for (int i = 0; i < onTrack->pluginList.size(); ++i)
                    if (dynamic_cast<UtilityDevice*>(onTrack->pluginList[i]) != nullptr)
                        return i;
                return -1;
            };
            const auto strip = stripSlot(lowerTrack);
            require(strip >= 0 && strip == stripSlot(upperTrack), "Two fresh tracks, each with its Utility strip in the same place");
            const auto lowerParameters = session.deviceParameters(lowerTrack, strip);
            require(!lowerParameters.empty(), "A fresh track exposes a parameter to automate");
            const Session::DeviceTarget lowerTarget {lowerTrack, strip, 0};
            require(session.setTrackAutomationPoints(lowerTarget,
                        {{0.0, lowerParameters.front().minimum},
                         {2.0, lowerParameters.front().maximum}}).wasOk(),
                    "Draw a curve on the lower of the two");
            require(session.removeAudioTrack(upperTrack).wasOk(), "Remove the track above it");
            const Session::DeviceTarget renumbered {upperTrack, strip, 0};
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

        // The arrow in the card's bottom-right corner folds the lanes under a
        // track away and brings them back, keeping them either way.
        {
            const auto card = view.lane(0);
            const auto fold = view.automationFoldBounds(0);
            require(!fold.isEmpty(), "A card with a lane on a row of its own carries the lanes' arrow");
            require(fold.getX() > view.cardControlsWidth + view.cardDividerWidth && fold.getRight() <= view.headerWidth
                        && fold.getY() > card.getCentreY() && fold.getBottom() <= card.getBottom(),
                    "in the bottom-right corner of the card's name side");
            require(view.cardResizeEdgeAt({fold.getCentreX(), fold.getBottom()}) < 0,
                    "clear of the bottom edge the row is resized from");
            for (int track = 1; track < session.trackCount(); ++track)
                if (view.trackLanes[static_cast<size_t>(track)].empty())
                    require(view.automationFoldBounds(track).isEmpty(), "A card with no lanes has no arrow");

            // Painted, not composed, so a render is the coverage it can have.
            // The PNGs are only written when somebody asks to look at them.
            const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_AUTOMATION_FOLD_SNAPSHOT", {});
            const auto picture = [&view, &shot, &require](const juce::String& suffix)
            {
                const auto band = juce::Rectangle<int>(0, static_cast<int>(view.lane(0).getY()) - 4,
                                                       view.getWidth(), 160);
                const auto image = view.createComponentSnapshot(band);
                require(image.getWidth() == band.getWidth(), "The card renders with its lanes' arrow");
                if (shot.isEmpty())
                    return;
                const auto file = juce::File(shot).getSiblingFile(juce::File(shot).getFileNameWithoutExtension()
                                                                  + suffix + ".png");
                file.deleteFile();
                if (auto stream = file.createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(image, *stream);
            };
            picture("-open");

            const auto laneRowHeight = view.rowBounds(1).getHeight();
            view.mouseDown(laneEvent(fold.getCentre(), fold.getCentre(), false));
            view.mouseUp(laneEvent(fold.getCentre(), fold.getCentre(), false));
            view.sync();
            require(session.isTrackAutomationFolded(0) && view.rowBounds(1).getHeight() == 0.0f,
                    "Clicking the arrow folds the lane rows under the track away");
            require(std::abs(view.lane(1).getY() - view.lane(0).getBottom()) < 0.001f,
                    "and the next track closes up under the card");
            require(view.trackLanes[0].size() == 1 && session.trackAutomationState(laneTarget).visible,
                    "Folded lanes are kept, not hidden");
            require(!view.automationFoldBounds(0).isEmpty(), "The arrow stays to bring them back");
            picture("-folded");

            view.mouseDown(laneEvent(fold.getCentre(), fold.getCentre(), false));
            view.mouseUp(laneEvent(fold.getCentre(), fold.getCentre(), false));
            view.sync();
            require(!session.isTrackAutomationFolded(0) && std::abs(view.rowBounds(1).getHeight() - laneRowHeight) < 0.001f,
                    "and clicking it again brings them back at their height");

            require(session.setTrackAutomationFolded(0, true).wasOk() && session.showTrackAutomation(laneTarget, true).wasOk()
                        && !session.isTrackAutomationFolded(0),
                    "Revealing a lane on a row of its own unfolds the lanes, so the lane asked for is seen");

            require(session.setTrackFolded(0, true).wasOk(), "The card folds to its one line");
            view.sync();
            const auto shortFold = view.automationFoldBounds(0);
            require(!shortFold.isEmpty() && static_cast<float>(view.trackNameBounds(0).getRight()) <= shortFold.getX(),
                    "On a row folded to its one line the arrow keeps its corner and the name stops short of it");
            picture("-short");
            require(session.setTrackFolded(0, false).wasOk(), "The card unfolds again");
            view.sync();
        }
        require(session.hideTrackAutomation(laneTarget).wasOk(), "A lane can be hidden again");
        view.sync();
        require(view.rows.size() == rowsWithoutLanes && view.trackLanes[0].empty(),
                "Hiding a lane removes its row");
        require(session.showTrackAutomation({session.masterTrackIndex(), 0, 0}, false).failed(),
                "The main row refuses automation lanes it cannot show");

        // A card's fader and pan are automated as a knob is: a right-click on
        // either opens the knob's menu for a lane on the track's own fader.
        {
            const auto volumeLane = session.trackMixerTarget(0, Session::MixerControl::volume);
            const auto panLane = session.trackMixerTarget(0, Session::MixerControl::pan);
            require(volumeLane.isValid() && panLane.isValid() && volumeLane.slot == panLane.slot
                        && volumeLane.parameter != panLane.parameter
                        && session.devicePlugin(0, volumeLane.slot) == session.trackVolumePlugin(0),
                    "A track's fader and pan are two parameters of its own fader, which a lane can name");
            require(!session.trackMixerTarget(session.masterTrackIndex(), Session::MixerControl::volume).isValid(),
                    "The main row's level has no lane yet");

            juce::String said;
            auto savedStatus = view.status;
            view.status = [&said](juce::String text) { said = std::move(text); };

            // The bar jumps to wherever it is pressed, so a right-click near
            // its left end would have dropped the level to the bottom.
            auto& fader = *view.volume[0];
            const auto levelBefore = session.trackMixer(0).volumeDb;
            const juce::Point<float> nearLeft {2.0f, static_cast<float>(fader.getHeight()) * 0.5f};
            const juce::MouseEvent rightPress(juce::Desktop::getInstance().getMainMouseSource(), nearLeft,
                juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f, 0, 0, 0, 0,
                &fader, &fader, juce::Time::getCurrentTime(), nearLeft, juce::Time::getCurrentTime(), 1, false);
            auto openMenu = std::move(fader.onRightClick);
            bool asked = false;
            fader.onRightClick = [&asked] { asked = true; };
            fader.mouseDown(rightPress);
            fader.mouseUp(rightPress);
            fader.onRightClick = std::move(openMenu);
            require(asked && std::abs(session.trackMixer(0).volumeDb - levelBefore) < 0.001f,
                    "A right-click on a card's fader asks for its menu and leaves the level where it was");

            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::showOnTrack);
            view.sync();
            const auto* shown = view.shownAutomation(0);
            require(session.trackAutomationState(volumeLane).visible && shown != nullptr
                        && shown->target.slot == volumeLane.slot && shown->target.parameter == volumeLane.parameter,
                    "Show automation puts the fader's lane over the track, where its row shows it");
            require(shown->deviceName == "Mixer" && shown->parameterName == "Volume",
                    "The lane reads Mixer: Volume, after Live");
            require(session.automationValueText(volumeLane, shown->restingValue).contains("dB"),
                    "A point on the fader's lane reads in decibels");
            require(view.automatedMixers.empty(), "A lane never drawn on drives nothing, so no bar follows it");

            require(session.setTrackAutomationPoints(volumeLane, {{0.0, 0.2f}, {8.0, 0.9f}}).wasOk(),
                    "Draw a fade-in on the fader");
            view.sync();
            auto* faderPlugin = session.trackVolumePlugin(0);
            require(faderPlugin->volParam->getCurve().getNumPoints() == 2,
                    "The engine plays the fader's lane from the fader's own curve");
            require(view.automatedMixers == std::vector<int> {0}, "The card's bars follow a lane that drives its fader");

            // Grabbing the bar takes it from the lane, as a touched knob is
            // taken, or the curve would pull it back under the hand.
            said.clear();
            view.beginMixerGesture(0, Session::MixerControl::volume);
            require(session.deviceParameter(0, volumeLane.slot, volumeLane.parameter)->automationOverridden
                        && faderPlugin->volParam->getCurve().getNumPoints() == 0,
                    "Grabbing an automated fader stops its lane driving it");
            require(said.contains("re-enable"), "and says how to give it back");
            require(session.setTrackVolumeDb(0, -12.0f).wasOk(), "Set the fader by hand");
            session.endTrackVolumeGesture(0);

            // The bar reads the fader back on the frame clock, whatever moved it.
            fader.setValue(0.0, juce::dontSendNotification);
            view.followMixerAutomation();
            require(std::abs(fader.getValue() + 12.0) < 0.05, "An automated fader's bar follows the fader");

            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::reenable);
            require(!session.deviceParameter(0, volumeLane.slot, volumeLane.parameter)->automationOverridden
                        && faderPlugin->volParam->getCurve().getNumPoints() == 2,
                    "Re-enable automation hands the fader back to its lane");
            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::reenable);
            require(!session.deviceParameter(0, volumeLane.slot, volumeLane.parameter)->automationOverridden,
                    "and choosing it again does not take it away");
            // A double-click or an encoder sets the level with no drag.
            require(session.setTrackVolumeDb(0, 0.0f).wasOk()
                        && session.deviceParameter(0, volumeLane.slot, volumeLane.parameter)->automationOverridden,
                    "Setting an automated fader with no drag takes it from its lane too");
            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::reenable);

            // Pan, on a row of its own.
            const auto rowsBefore = view.rows.size();
            view.chooseMixerAutomation(0, Session::MixerControl::pan, Arrangement::MixerAutomationChoice::showOnLane);
            view.sync();
            require(view.rows.size() == rowsBefore + 1 && view.rows[1].track == 0 && view.rows[1].automation >= 0,
                    "Show automation on new lane gives the pan a row of its own under the track");
            const auto* panRow = view.automationFor(view.rows[1]);
            require(panRow != nullptr && panRow->deviceName == "Mixer" && panRow->parameterName == "Pan",
                    "and the row is the pan's");
            require(session.setTrackPan(0, 0.25f).wasOk()
                        && !session.deviceParameter(0, panLane.slot, panLane.parameter)->automationOverridden,
                    "Moving a pan no lane drives overrides nothing");

            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::remove);
            require(!session.trackAutomationState(volumeLane).active && session.trackAutomationState(volumeLane).visible,
                    "Delete automation clears the fader's curve and keeps its lane");
            view.chooseMixerAutomation(0, Session::MixerControl::volume, Arrangement::MixerAutomationChoice::hide);
            view.chooseMixerAutomation(0, Session::MixerControl::pan, Arrangement::MixerAutomationChoice::hide);
            view.sync();
            require(view.trackLanes[0].empty() && view.rows.size() == rowsWithoutLanes && view.automatedMixers.empty(),
                    "Hide automation takes both lanes away");
            require(session.setTrackPan(0, 0.0f).wasOk() && session.setTrackVolumeDb(0, levelBefore).wasOk(),
                    "Put the mixer back as it was");
            view.status = std::move(savedStatus);
        }
