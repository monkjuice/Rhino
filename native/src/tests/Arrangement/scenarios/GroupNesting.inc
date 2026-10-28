        // Groups nest: a bus can be inside another group, up to maxGroupLevels
        // deep, feeding the bus of the group around it. A document of its own,
        // so the stacks built here disturb nothing after them.
        {
            auto document = std::make_unique<Session>();
            auto& nest = *document;
            auto cards = std::make_unique<Arrangement>(nest);
            auto& nested = *cards;
            nested.setSize(1200, 700);
            const auto trackAt = [&nest](int track) { return te::getAudioTracks(*nest.edit)[track]; };
            const auto destinationOf = [&trackAt](int track) { return trackAt(track)->getOutput().getDestinationTrack(); };
            const auto layOut = [&nested]
            {
                nested.sync();
                nested.resized();
            };

            scenario("nested groups: a group goes inside a new one whole");
            // [MIDI, MIDI, Audio, Audio] grouped at the top: [Inner, M, M, A, A].
            require(nest.groupTracks({0, 1}, "Inner").wasOk(), "A group to nest");
            const auto inner = nest.trackGroupBusId(0);
            // Its bus and the loose track under it: [Outer, Inner, M, M, A, A].
            require(nest.groupTracks({0, 3}, "Outer").wasOk(), "A group's bus and a loose track group together");
            const auto outer = nest.trackGroupBusId(0);
            auto groups = nest.trackGroups();
            require(groups.size() == 2 && groups[0].id == outer && groups[0].level == 1 && groups[0].trackCount == 4,
                    "The new group opens the stack and holds everything chosen");
            require(groups[1].id == inner && groups[1].busTrack == 1 && groups[1].parent == outer
                        && groups[1].level == 2 && groups[1].trackCount == 2,
                    "with the old group a level down inside it, its members still its own");
            require(nest.trackGroupBusId(1) == inner && nest.trackGroupId(1) == outer,
                    "A nested bus is its own group's bus and a member of the group around it");
            require(destinationOf(2) == trackAt(1) && destinationOf(3) == trackAt(1),
                    "The inner members feed the inner bus");
            require(destinationOf(1) == trackAt(0) && destinationOf(4) == trackAt(0),
                    "which feeds the outer bus, as the track grouped with it does");
            require(destinationOf(0) == nullptr && destinationOf(5) == nullptr,
                    "and the outer bus goes to the main output, as the track below both groups does");

            scenario("nested groups: each level steps the cards further in");
            layOut();
            require(nested.trackIndent(0) == 0.0f && nested.trackIndent(1) == nested.groupIndent
                        && nested.trackIndent(4) == nested.groupIndent,
                    "The outer bus starts at the edge, the inner bus where the outer group's members do");
            require(nested.trackIndent(2) == nested.groupIndent + nested.nestedGroupIndent,
                    "and the inner group's cards a step further in");
            require(nested.groupContaining(2)->id == inner && nested.groupContaining(4)->id == outer,
                    "A card answers to the innermost group it is in");

            scenario("nested groups: folding any group around a card hides it");
            require(nest.setTrackGroupCollapsed(outer, true).wasOk(), "The outer group folds");
            layOut();
            for (int track = 1; track <= 4; ++track)
                require(nested.lane(track).getHeight() == 0.0f, "taking everything inside it, the inner group too");
            require(nested.lane(0).getHeight() > 0.0f && nested.lane(5).getHeight() > 0.0f, "and nothing else");
            {
                int added = -1;
                require(nest.addTrack(Session::TrackType::audio, 2, &added).wasOk() && added == 5
                            && nest.trackGroupId(5) == 0 && destinationOf(5) == nullptr,
                        "A track added under a card the fold hides lands below the whole folded group, outside it");
                require(nest.removeAudioTrack(5).wasOk(), "and can go again");
            }
            require(nest.setTrackGroupCollapsed(outer, false).wasOk() && nest.setTrackGroupCollapsed(inner, true).wasOk(),
                    "The outer group opens and the inner one folds");
            layOut();
            require(nested.lane(2).getHeight() == 0.0f && nested.lane(3).getHeight() == 0.0f
                        && nested.lane(1).getHeight() > 0.0f && nested.lane(4).getHeight() > 0.0f,
                    "Folding the inner group hides its members only");
            {
                int added = -1;
                require(nest.addTrack(Session::TrackType::audio, 1, &added).wasOk() && added == 4
                            && nest.trackGroupId(4) == outer && destinationOf(4) == trackAt(0),
                        "A track added under the folded inner bus lands below it, still in the outer group");
                require(nest.removeAudioTrack(4).wasOk(), "and can go again");
            }
            require(nest.setTrackGroupCollapsed(inner, false).wasOk(), "The inner group opens again");

            scenario("nested groups: a track joins the innermost group it lands in");
            {
                int added = -1;
                require(nest.addTrack(Session::TrackType::midi, 3, &added).wasOk() && added == 4
                            && nest.trackGroupId(4) == inner && destinationOf(4) == trackAt(1),
                        "Added under the inner group's last member, a track joins the inner group");
                require(nest.removeAudioTrack(4).wasOk(), "and can go again");
            }
            require(nest.moveTrack(5, 3).wasOk() && nest.trackGroupId(3) == inner && destinationOf(3) == trackAt(1),
                    "A loose track carried between the inner group's members joins the inner group");
            require(nest.moveTrack(3, 5).wasOk() && nest.trackGroupId(5) == 0 && destinationOf(5) == nullptr
                        && nest.trackGroups()[1].trackCount == 2 && nest.trackGroups()[0].trackCount == 4,
                    "and carried back out below everything, it leaves both without taking anything along");

            scenario("nested groups: ungrouping hands the members to the group around it");
            require(nest.ungroupTracks(inner).wasOk(), "The inner group can be taken away");
            groups = nest.trackGroups();
            require(groups.size() == 1 && groups[0].trackCount == 3, "leaving the outer group holding its members");
            require(nest.trackGroupId(1) == outer && nest.trackGroupId(2) == outer, "Its members join the outer group");
            require(destinationOf(1) == trackAt(0) && destinationOf(2) == trackAt(0), "and feed the outer bus");
            nest.undo();
            groups = nest.trackGroups();
            require(groups.size() == 2 && groups[1].id == inner && groups[1].parent == outer
                        && destinationOf(2) == trackAt(1) && destinationOf(1) == trackAt(0),
                    "and one undo nests it again, routing and all");

            scenario("nested groups: a group steps out one level at a time");
            require(nest.removeTrackFromGroup(1).wasOk(), "The inner bus can leave the outer group");
            groups = nest.trackGroups();
            require(groups.size() == 2 && groups[0].trackCount == 1 && groups[1].busTrack == 2
                        && groups[1].parent == 0 && groups[1].level == 1 && groups[1].trackCount == 2,
                    "taking its whole group below what stays in the outer one, to the top of the stack");
            require(destinationOf(2) == nullptr && destinationOf(3) == trackAt(2),
                    "where its bus feeds the main output and its members still feed it");
            nest.undo();
            require(nest.trackGroups()[1].busTrack == 1 && nest.trackGroups()[1].parent == outer
                        && destinationOf(1) == trackAt(0),
                    "One undo puts it back inside");

            scenario("nested groups: adding to a group inside another keeps it there");
            require(nest.addTracksToGroup({5}, inner).wasOk(), "A loose track joins the inner group");
            groups = nest.trackGroups();
            require(groups[1].trackCount == 3 && groups[0].trackCount == 5 && nest.trackGroupId(4) == inner
                        && destinationOf(4) == trackAt(1) && nest.trackGroupId(5) == outer,
                    "at the end of its run, the inner group still inside the outer one");
            require(nest.addTracksToGroup({0}, inner).failed() && nest.addTracksToGroup({1}, inner).failed(),
                    "Neither a group nor the group around it can go inside it");
            require(nest.trackGroups().size() == 2 && nest.trackGroups()[0].busTrack == 0, "and refusing moves nothing");
            {
                std::vector<int> copies;
                require(nest.duplicateTracks({1}, &copies).wasOk() && copies.size() == 4
                            && nest.trackGroupId(copies.front()) == outer,
                        "Duplicating the inner group lands a copy beside it, inside the outer group");
                groups = nest.trackGroups();
                require(groups.size() == 3 && groups[1].trackCount == 3 && groups[2].busTrack == copies.front()
                            && groups[2].parent == outer && groups[2].level == 2 && groups[2].trackCount == 3,
                        "a group of its own a level down, rather than inside the group it copies");
                nest.undo();
                require(nest.trackGroups().size() == 2, "and one undo takes the copy away");
            }

            scenario("nested groups: deleting a bus deletes everything inside it");
            require(nest.addTrack(Session::TrackType::audio).wasOk(), "A loose track to keep");
            {
                const auto whole = nest.trackCount();
                require(nest.removeTracks({0}).wasOk() && nest.trackCount() == whole - 6 && nest.trackGroups().empty(),
                        "Delete on the outer bus takes the group inside it too");
                nest.undo();
                groups = nest.trackGroups();
                require(nest.trackCount() == whole && groups.size() == 2 && groups[1].parent == outer
                            && destinationOf(2) == trackAt(1) && destinationOf(1) == trackAt(0),
                        "and one undo brings both back, nested and routed");
            }

            scenario("nested groups: no deeper than the limit");
            // Wrapping the group at the top in a new one, until the inner
            // group is the tenth level.
            for (int level = 3; level <= maxGroupLevels; ++level)
                require(nest.groupTracks({0}).wasOk(), "A whole stack of groups can go inside a new one");
            groups = nest.trackGroups();
            const auto deepest = groups.back();
            require(groups.size() == static_cast<size_t>(maxGroupLevels) && deepest.id == inner
                        && deepest.level == maxGroupLevels,
                    "Ten groups stand one inside another");
            require(nest.groupTracks({0}).failed(), "An eleventh around them all is refused");
            require(nest.groupTracks({deepest.firstTrack}).failed(), "and so is one inside the deepest");
            require(nest.trackGroups().size() == static_cast<size_t>(maxGroupLevels), "leaving the ten as they were");
            {
                auto* at = trackAt(deepest.firstTrack);
                auto buses = 0;
                while (auto* next = at->getOutput().getDestinationTrack())
                {
                    at = next;
                    ++buses;
                }
                require(buses == maxGroupLevels && at == trackAt(0),
                        "The deepest member's audio passes every bus in turn, out through the outermost");
            }
            {
                int added = -1;
                require(nest.addTrack(Session::TrackType::audio, deepest.lastTrack(), &added).wasOk()
                            && nest.trackGroupId(added) == inner,
                        "A track still goes inside the deepest group: only groups count as levels");
                require(nest.removeAudioTrack(added).wasOk(), "and can go again");
            }
            {
                const auto copied = nest.copyTracks({deepest.busTrack});
                require(nest.pasteTracks(copied, deepest.firstTrack).failed()
                            && nest.trackGroups().size() == static_cast<size_t>(maxGroupLevels),
                        "A group pasted inside the deepest is refused, and nothing lands");
                std::vector<int> pasted;
                require(nest.pasteTracks(copied, nest.firstReturnTrack() - 1, &pasted).wasOk()
                            && nest.isGroupBusTrack(pasted.front()) && nest.trackGroupId(pasted.front()) == 0,
                        "while the same copy pastes at the foot of the stack");
                require(nest.moveTrack(pasted.front(), deepest.lastTrack()).failed()
                            && nest.trackGroupId(pasted.front()) == 0,
                        "and its bus carried into the deepest group is refused too");
            }
            layOut();
            {
                const auto card = deepest.firstTrack;
                require(nested.trackIndent(card) == nested.groupIndent
                                                        + static_cast<float>(maxGroupLevels - 1) * nested.nestedGroupIndent,
                        "The deepest card steps in once for every group around it");
                require(nested.trackNameBounds(card).getWidth() > 0, "and still has room for its name");
                require(static_cast<float>(nested.pan[static_cast<size_t>(card)]->getRight())
                            <= nested.trackIndent(card) + nested.cardControlsRight(),
                        "with its controls clear of the name");
            }
            cards.reset();
            nest.releaseAudioDevice();
        }

        // Dragging a bus's card carries its whole group, as copying or
        // deleting one does. It used to move the bus alone and leave the
        // members behind, cut loose from it.
        {
            auto document = std::make_unique<Session>();
            auto& dragged = *document;
            auto board = std::make_unique<Arrangement>(dragged);
            board->setSize(1200, 700);
            const auto rowAt = [&dragged](int track) { return te::getAudioTracks(*dragged.edit)[track]; };
            const auto feedOf = [&rowAt](int track) { return rowAt(track)->getOutput().getDestinationTrack(); };

            scenario("nested groups: dragging a bus carries its group");
            // [MIDI, MIDI, Audio, Audio] grouped at the top: [Band, M, M, A, A].
            require(dragged.groupTracks({0, 1}, "Band").wasOk(), "A group to drag");
            const auto band = dragged.trackGroupBusId(0);
            int landed = -1;
            require(dragged.moveTrack(0, 4, &landed).wasOk() && landed == 2,
                    "Carried down onto the last track, the bus lands above it by the size of its group");
            auto groups = dragged.trackGroups();
            require(groups.size() == 1 && groups[0].busTrack == 2 && groups[0].trackCount == 2,
                    "with both its members under it");
            require(feedOf(3) == rowAt(2) && feedOf(4) == rowAt(2) && dragged.trackGroupId(0) == 0 && feedOf(0) == nullptr,
                    "still feeding it, and the tracks it passed still outside");
            dragged.undo();
            require(dragged.trackGroups()[0].busTrack == 0 && feedOf(1) == rowAt(0) && feedOf(2) == rowAt(0),
                    "One undo carries the whole group back");
            require(dragged.moveTrack(0, 2, &landed).wasOk() && landed == 0 && dragged.trackGroups()[0].busTrack == 0,
                    "Dropped onto one of its own members, it stays where it is");

            scenario("nested groups: a card dragged down onto a folded group lands below all of it");
            require(dragged.moveTrack(3, 0, &landed).wasOk() && landed == 0 && dragged.trackGroupId(0) == 0,
                    "A loose track carried up above the group stays outside it");
            require(dragged.setTrackGroupCollapsed(band, true).wasOk(), "The group folds");
            require(dragged.moveTrack(0, 1, &landed).wasOk() && landed == 3 && dragged.trackGroupId(3) == 0
                        && dragged.trackGroups()[0].busTrack == 0 && dragged.trackGroups()[0].trackCount == 2,
                    "Carried down onto the folded bus, the track lands below the whole group, outside it");
            require(dragged.setTrackGroupCollapsed(band, false).wasOk(), "The group opens again");

            scenario("nested groups: dragging a bus into a group nests its group there");
            // [Band, M, M, Two, A, A]
            require(dragged.groupTracks({3, 4}, "Two").wasOk(), "A second group below the first");
            const auto two = dragged.trackGroupBusId(3);
            require(dragged.moveTrack(3, 2, &landed).wasOk() && landed == 2,
                    "Its bus is carried up between the first group's members");
            groups = dragged.trackGroups();
            require(groups.size() == 2 && groups[0].trackCount == 5 && groups[1].id == two && groups[1].busTrack == 2
                        && groups[1].parent == band && groups[1].level == 2 && groups[1].trackCount == 2,
                    "which nests its whole group inside the first");
            require(feedOf(2) == rowAt(0) && feedOf(3) == rowAt(2) && feedOf(4) == rowAt(2) && feedOf(5) == rowAt(0),
                    "its members feeding its bus, and its bus and the member below it feeding the outer one");
            dragged.undo();
            groups = dragged.trackGroups();
            require(groups.size() == 2 && groups[1].busTrack == 3 && groups[1].parent == 0 && feedOf(3) == nullptr,
                    "One undo takes it back out");

            scenario("nested groups: the card drag carries the group too");
            board->sync();
            board->resized();
            const auto pointerAt = [&board](juce::Point<float> point)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                        board.get(), board.get(), juce::Time::getCurrentTime(), point,
                                        juce::Time::getCurrentTime(), 1, false);
            };
            const auto cardCentre = [&board](int track) { return juce::Point<float>(90.0f, board->lane(track).getCentreY()); };
            require(board->beginCardGesture(pointerAt(cardCentre(3))), "The second group's bus card can be taken");
            board->dragCardGesture(pointerAt(cardCentre(4)));
            require(board->moveDestination == 3, "Over one of its own members it has nowhere new to go, so no drop line");
            board->dragCardGesture(pointerAt(cardCentre(1)));
            require(board->moveDestination == 1, "Over the first group's member it would land there");
            board->endCardGesture();
            groups = dragged.trackGroups();
            require(groups.size() == 2 && groups[1].id == two && groups[1].busTrack == 1 && groups[1].parent == band
                        && groups[1].trackCount == 2,
                    "Dropped, the whole group lands inside the first one");
            require(board->selectedTrack == 1, "and the selection follows its bus");
            board.reset();
            dragged.releaseAudioDevice();
        }
