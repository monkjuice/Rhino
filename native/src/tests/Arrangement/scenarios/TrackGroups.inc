        // Groups are organisational: a band over a contiguous run of tracks
        // that folds away, carries mute and solo to its members, and decides
        // membership by where a track sits rather than by a stored span.
        const auto tracksBeforeGroups = session.trackCount();
        while (session.trackCount() < 4)
            require(session.addAudioTrack().wasOk(), "Groups need four tracks to work with");
        view.setSize(1000, 620);
        view.sync();
        view.resized();
        const auto bandRow = [&view]
        {
            for (int i = 0; i < static_cast<int>(view.rows.size()); ++i)
                if (view.rows[static_cast<size_t>(i)].group >= 0)
                    return i;
            return -1;
        };
        const auto clickAt = [&view](juce::Point<float> point)
        {
            const juce::ModifierKeys leftButton(juce::ModifierKeys::leftButtonModifier);
            const juce::MouseEvent press(juce::Desktop::getInstance().getMainMouseSource(), point, leftButton,
                1.0f, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(), point,
                juce::Time::getCurrentTime(), 1, false);
            view.mouseDown(press);
            view.mouseUp(press);
        };

        scenario("track groups: grouping gathers the tracks into one run");
        require(session.trackGroups().empty(), "A document starts with no bands");
        require(session.groupTracks({1, 2}, "Drums").wasOk(), "Two cards can be grouped");
        auto bands = session.trackGroups();
        require(bands.size() == 1, "Grouping makes one band");
        require(bands[0].name == "Drums", "The band takes the name it was given");
        require(bands[0].firstTrack == 1 && bands[0].trackCount == 2,
                "The run starts where the topmost of the grouped cards already was");
        const auto bandId = bands[0].id;
        require(session.trackGroupId(1) == bandId && session.trackGroupId(2) == bandId,
                "Both tracks belong to the band");
        require(session.trackGroupId(0) == 0 && session.trackGroupId(3) == 0,
                "and nothing else does");

        scenario("track groups: the band is a row of its own");
        view.sync();
        view.resized();
        require(view.groups.size() == 1, "The arrangement reads the band from the session");
        require(bandRow() >= 0, "The band has a row in the stack");
        require(view.rows[static_cast<size_t>(bandRow())].track == 1,
                "It is the row of the first track it covers");
        require(view.rowBounds(bandRow()).getBottom() <= view.lane(1).getY() + 0.001f,
                "and it sits above that track's lane");
        require(view.cardAt({90.0f, view.rowBounds(bandRow()).getCentreY()}) < 0,
                "The band is not a card, so it cannot be carried or resized like one");
        require(view.trackAt(view.rowBounds(bandRow()).getCentreY()) < 0,
                "and it is no track, so a clip dropped on it lands nowhere");

        scenario("track groups: collapsing folds the members away");
        const auto openStackHeight = view.rowsHeight;
        require(session.setTrackGroupCollapsed(bandId, true).wasOk(), "A band can be collapsed");
        view.sync();
        view.resized();
        require(view.lane(1).getHeight() == 0.0f && view.lane(2).getHeight() == 0.0f,
                "A collapsed band leaves its members no height");
        require(view.lane(0).getHeight() > 0.0f && view.lane(3).getHeight() > 0.0f,
                "The tracks outside it keep theirs");
        require(view.rowsHeight < openStackHeight, "The stack is shorter by what the folded rows took");
        require(!view.mute[1]->isVisible() && !view.volume[1]->isVisible(),
                "A folded member's controls go with it rather than piling up under the band");
        require(view.cardAt({90.0f, view.lane(3).getCentreY()}) == 3,
                "The cards still below the band answer at their new places");

        scenario("track groups: the disclosure folds and unfolds");
        clickAt(view.groupDisclosureBounds(view.rowBounds(bandRow())).getCentre());
        require(!session.trackGroups().front().collapsed, "Clicking the disclosure expands a folded band");
        view.sync();
        view.resized();
        require(view.lane(1).getHeight() > 0.0f && view.mute[1]->isVisible(),
                "Expanding brings the members back with their controls");
        clickAt(view.groupDisclosureBounds(view.rowBounds(bandRow())).getCentre());
        require(session.trackGroups().front().collapsed, "and clicking it again folds the band back up");
        require(session.setTrackGroupCollapsed(bandId, false).wasOk(), "A band can be expanded again");
        view.sync();
        view.resized();

        scenario("track groups: mute and solo reach every member");
        session.setTrackGroupMuted(bandId, true);
        require(session.trackMixer(1).muted && session.trackMixer(2).muted,
                "Muting the band mutes every track under it");
        require(session.trackGroupMuted(bandId), "and the band reads as muted");
        session.setTrackMuted(2, false);
        require(!session.trackGroupMuted(bandId), "A band is muted only while all of its tracks are");
        session.setTrackGroupMuted(bandId, false);
        require(!session.trackMixer(1).muted && !session.trackMixer(2).muted,
                "Unmuting the band clears every one of them");

        scenario("track groups: the band is part of the document");
        {
            const std::function<juce::ValueTree(const juce::ValueTree&)> findBand =
                [&findBand](const juce::ValueTree& tree) -> juce::ValueTree
            {
                if (tree.hasType("rhinoTrackGroup")) return tree;
                for (int i = 0; i < tree.getNumChildren(); ++i)
                    if (const auto found = findBand(tree.getChild(i)); found.isValid()) return found;
                return {};
            };
            const std::function<juce::ValueTree(const juce::ValueTree&)> findMember =
                [&findMember](const juce::ValueTree& tree) -> juce::ValueTree
            {
                if (tree.hasProperty("rhinoGroup")) return tree;
                for (int i = 0; i < tree.getNumChildren(); ++i)
                    if (const auto found = findMember(tree.getChild(i)); found.isValid()) return found;
                return {};
            };
            const auto snapshot = session.projectSnapshot();
            const auto savedBand = findBand(snapshot);
            require(savedBand.isValid(), "The band is written into the saved project");
            require(savedBand.getProperty("name").toString() == "Drums", "with the name it was given");
            const auto member = findMember(snapshot);
            require(member.isValid() && static_cast<int>(member.getProperty("rhinoGroup")) == bandId,
                    "and membership travels on the track, so reordering carries it");
        }

        scenario("track groups: where a track sits decides what it is in");
        require(session.moveTrack(3, 1).wasOk(), "A loose track can be carried above the band");
        require(session.trackGroupId(1) == 0, "Landing above the band leaves it loose");
        require(session.trackGroups().front().firstTrack == 2,
                "and the band is still one run, one place further down");
        require(session.moveTrack(1, 2).wasOk(), "That track can be carried on into the band");
        require(session.trackGroupId(2) == bandId, "A track carried into the middle of a band joins it");
        require(session.trackGroups().front().trackCount == 3, "so the band now covers three tracks");
        require(session.removeTrackFromGroup(2).wasOk(), "A member can be taken out of the band");
        bands = session.trackGroups();
        require(bands.size() == 1 && bands[0].trackCount == 2,
                "Removing a track from the middle keeps the band one run rather than splitting it");
        require(session.trackGroupId(3) == 0, "and the track it let go is loose below it");

        scenario("track groups: Ctrl+G groups the selected cards");
        require(session.ungroupTracks(bandId).wasOk(), "A band can be taken away");
        require(session.trackGroups().empty(), "Ungrouping leaves no band behind");
        require(session.trackCount() == 4, "and keeps every track it held");
        view.sync();
        view.selectTrack(1);
        view.selectTrackRange(1, 2);
        const auto commandG = juce::KeyPress('G', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
        require(view.keyPressed(commandG), "Ctrl+G is answered by the arrangement");
        require(session.trackGroups().size() == 1, "and groups the cards the selection had gathered");
        require(session.trackGroups().front().trackCount == 2, "both of them");
        const auto commandShiftG = juce::KeyPress('G',
            juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);
        require(view.keyPressed(commandShiftG), "Ctrl+Shift+G is answered too");
        require(session.trackGroups().empty(), "and takes the band back off them");

        while (session.trackCount() > tracksBeforeGroups)
            require(session.removeAudioTrack(session.trackCount() - 1).wasOk(),
                    "The tracks this scenario added can be removed again");
        view.sync();
        view.resized();
