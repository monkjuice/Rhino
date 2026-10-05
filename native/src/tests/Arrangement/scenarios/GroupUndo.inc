        // Undo puts a group back exactly: who is in it and where their audio
        // goes, along with where the tracks sit. Membership and routing are
        // written in the same undo step as the move, group or delete that
        // decided them, so one Ctrl+Z restores all three. A document of its
        // own, so the stack shapes here disturb nothing after them.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            const auto destinationOf = [&stack](int track)
            {
                return te::getAudioTracks(*stack.edit)[track]->getOutput().getDestinationTrack();
            };
            const auto trackAt = [&stack](int track) { return te::getAudioTracks(*stack.edit)[track]; };

            // [MIDI, Audio, MIDI, Audio] grouped at the top: [Bus, M, A, M, A].
            require(stack.groupTracks({0, 1}, "Band").wasOk(), "Two tracks group under a bus");
            const auto band = stack.trackGroupId(1);
            require(stack.isGroupBusTrack(0) && band != 0 && stack.trackGroupId(2) == band,
                    "The bus heads its two members");

            // Carried into the middle of the group, a track joins it; one undo
            // takes it back out, routing and all.
            auto* carried = trackAt(3);
            require(stack.moveTrack(3, 2).wasOk() && stack.trackGroupId(2) == band,
                    "A track carried into a group joins it");
            stack.undo();
            require(trackAt(3) == carried, "Undo puts the carried track back below the group");
            require(stack.trackGroupId(3) == 0 && destinationOf(3) == nullptr,
                    "and takes it back out of the group, sending its audio to the main output again");
            require(stack.trackGroups().front().trackCount == 2, "The group is its two members again");

            // Ungrouping and one undo regroup the members under the bus, and
            // their audio feeds it again.
            require(stack.ungroupTracks(band).wasOk() && !stack.isGroupBusTrack(0), "Ungroup removes the bus");
            stack.undo();
            require(stack.isGroupBusTrack(0) && stack.trackGroupId(1) == band && stack.trackGroupId(2) == band,
                    "One undo of ungroup brings the bus back with its members");
            require(destinationOf(1) == trackAt(0) && destinationOf(2) == trackAt(0),
                    "and the members feed the bus again");

            // Deleting the bus and one undo: the same.
            require(stack.removeAudioTrack(0).wasOk() && !stack.isGroupBusTrack(0), "The bus can be deleted");
            stack.undo();
            require(stack.isGroupBusTrack(0) && stack.trackGroupId(1) == band && stack.trackGroupId(2) == band
                        && destinationOf(1) == trackAt(0),
                    "One undo of deleting a bus brings its members and their routing back");

            // A track dropped directly under the bus joins the group, rather than
            // cutting every member below it loose.
            require(stack.moveTrack(4, 1).wasOk(), "A track can be dropped directly under the bus");
            require(stack.trackGroups().front().trackCount == 3 && stack.trackGroupId(1) == band
                        && destinationOf(1) == trackAt(0),
                    "A track dropped directly under a bus joins its group");
            stack.releaseAudioDevice();
        }

        // A lane made by dragging a clip below the last one, or by a paste that
        // runs off the bottom, is a lane like any other at once: it has a fader.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID made;
            require(stack.createClip(0, 0.0, &made).wasOk(), "A MIDI clip to carry");
            const auto below = stack.trackCount();
            require(stack.editClip(made, {0.0, 2.0, 0.0}, ClipGesture::move, below).wasOk()
                        && stack.trackCount() == below + 1,
                    "Dragging a clip below the last lane makes a lane for it");
            require(stack.setTrackVolumeDb(below, -5.0f).wasOk(), "and that lane has a fader at once");
            const auto copied = stack.copyClipRegion(0.0, 2.0, below, below);
            std::vector<te::EditItemID> pasted;
            require(stack.pasteClipRegion(copied, 0.0, below + 1, pasted).wasOk()
                        && stack.trackCount() == below + 2,
                    "A paste below the last lane makes a lane for it");
            require(stack.setTrackVolumeDb(below + 1, -5.0f).wasOk(), "and that lane has a fader at once too");
            stack.releaseAudioDevice();
        }
