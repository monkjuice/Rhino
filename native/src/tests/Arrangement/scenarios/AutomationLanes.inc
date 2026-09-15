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
