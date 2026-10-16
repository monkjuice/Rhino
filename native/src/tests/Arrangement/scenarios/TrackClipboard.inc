        // A track card's cut, copy, paste, duplicate and delete move whole
        // tracks - devices, clips and automation, with ids of their own - and
        // a bus moves with its whole group. Documents of their own, so the
        // stacks built here disturb nothing after them.
        {
            auto document = std::make_unique<Session>();
            auto& doc = *document;
            const auto trackAt = [&doc](int track) { return te::getAudioTracks(*doc.edit)[track]; };
            const auto keyOf = [&trackAt](int track, int slot)
            {
                return trackAt(track)->pluginList[slot]->state.getProperty("rhinoDeviceKey").toString();
            };
            const auto starter = doc.trackCount();

            te::EditItemID clipID;
            require(doc.createClip(0, 0.0, &clipID).wasOk(), "A clip for the copy to carry");
            require(doc.addAudioEffect(Session::AudioEffect::Reverb, 0).wasOk(), "and a device");
            auto reverb = -1;
            for (const auto& slot : doc.deviceSlots(0))
                if (slot.type == te::ReverbPlugin::xmlTypeName)
                    reverb = slot.pluginIndex;
            require(reverb > 0, "The device is on the chain");
            const Session::DeviceTarget lane {0, reverb, 0};
            require(doc.showTrackAutomation(lane, false).wasOk()
                        && doc.setTrackAutomationPoints(lane, {{0.0, 0.0f}, {1.0, 1.0f}}).wasOk(),
                    "and an automation lane drawn on it");
            require(doc.setTrackName(0, "Lead").wasOk(), "and a name");

            const auto copied = doc.copyTracks({0});
            require(copied.tracks.size() == 1, "Copying a card takes its one track");
            std::vector<int> pasted;
            require(doc.pasteTracks(copied, 0, &pasted).wasOk() && pasted == std::vector<int> {1}
                        && doc.trackCount() == starter + 1,
                    "Pasting lands the copy directly under the card it is pasted on");
            require(doc.trackName(1) == "Lead" && doc.trackType(1) == Session::TrackType::midi
                        && doc.trackColour(1) == doc.trackColour(0),
                    "The copy keeps the original's kind, name and colour");
            require(trackAt(1)->itemID != trackAt(0)->itemID, "but is a track of its own");
            const auto copiedClips = trackAt(1)->getClips();
            require(!copiedClips.isEmpty() && copiedClips.size() == trackAt(0)->getClips().size(),
                    "It carries the original's clips");
            for (auto* clip : copiedClips)
                require(clip->itemID != clipID && doc.findClip(clip->itemID) == clip, "as clips of its own");
            const auto lanes = doc.trackAutomations(1);
            require(lanes.size() == 1 && lanes.front().active() && lanes.front().target.track == 1
                        && lanes.front().target.slot == reverb,
                    "and its automation, driving the copy's own device");
            require(keyOf(1, reverb).isNotEmpty() && keyOf(1, reverb) != keyOf(0, reverb),
                    "by a key of its own, so taking over a knob on one leaves the other alone");
            doc.undo();
            require(doc.trackCount() == starter && doc.trackName(0) == "Lead", "One undo takes the paste back");

            // The clipboard holds values, so the original going takes nothing
            // from it.
            require(doc.removeTracks({0}).wasOk() && doc.trackCount() == starter - 1, "The original can be deleted");
            require(doc.pasteTracks(copied, -1, &pasted).wasOk() && pasted == std::vector<int> {starter - 1}
                        && doc.trackName(starter - 1) == "Lead" && doc.trackAutomations(starter - 1).size() == 1,
                    "and the copy still pastes, at the foot of the stack when no card is named");
            require(doc.removeTracks({doc.masterTrackIndex()}).failed(), "The main row cannot be deleted");
            std::vector<int> everything;
            for (int track = 0; track < doc.trackCount(); ++track)
                everything.push_back(track);
            require(doc.removeTracks(everything).failed() && doc.trackCount() == starter,
                    "and neither can every track at once");
            doc.releaseAudioDevice();
        }

        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            const auto trackAt = [&stack](int track) { return te::getAudioTracks(*stack.edit)[track]; };
            const auto destinationOf = [&trackAt](int track) { return trackAt(track)->getOutput().getDestinationTrack(); };

            // [MIDI, Audio, MIDI, Audio] grouped at the top: [Bus, M, A, M, A].
            require(stack.groupTracks({0, 1}, "Band").wasOk(), "A group to copy");
            const auto band = stack.trackGroupBusId(0);
            std::vector<int> made;
            require(stack.duplicateTracks({0}, &made).wasOk() && made == std::vector<int> {3, 4, 5},
                    "Duplicating a bus duplicates its whole group, directly under it");
            auto groups = stack.trackGroups();
            require(groups.size() == 2 && groups[1].busTrack == 3 && groups[1].trackCount == 2 && groups[1].id != band,
                    "as a second group with an id of its own");
            require(destinationOf(4) == trackAt(3) && destinationOf(5) == trackAt(3) && destinationOf(1) == trackAt(0),
                    "whose members feed its own bus, while the first group's still feed theirs");

            // [Bus, M, A, Bus, M, A, M, A]
            require(stack.duplicateTracks({1}, &made).wasOk() && made == std::vector<int> {2}
                        && stack.trackGroupId(2) == band && destinationOf(2) == trackAt(0),
                    "A member duplicated alone lands under itself, inside its group");

            // [Bus, M, M, A, Bus, M, A, M, A]: a group cannot go inside another.
            const auto whole = stack.copyTracks({0});
            require(whole.tracks.size() == 4, "Copying a bus copies its members");
            require(stack.pasteTracks(whole, 1, &made).wasOk() && made.front() == 4 && stack.isGroupBusTrack(4)
                        && stack.trackGroups().size() == 3 && stack.trackGroups()[0].trackCount == 3,
                    "A group pasted onto a member lands below the whole group, not inside it");

            const auto before = stack.trackCount();
            require(stack.removeTracks({4}).wasOk() && stack.trackCount() == before - 4
                        && stack.trackGroups().size() == 2,
                    "Deleting a bus deletes its group with it");
            stack.undo();
            groups = stack.trackGroups();
            require(stack.trackCount() == before && groups.size() == 3 && groups[1].busTrack == 4
                        && groups[1].trackCount == 3 && destinationOf(5) == trackAt(4),
                    "and one undo brings the whole group back, routed into its bus");
            stack.releaseAudioDevice();
        }

        // From the cards: the shortcuts act on whole tracks while a card has
        // the focus, and a copy of either kind replaces the other.
        {
            auto document = std::make_unique<Session>();
            auto& doc = *document;
            Arrangement cards(doc);
            cards.setSize(1200, 500);
            cards.sync();
            cards.fit();
            const auto command = [](int key) { return juce::KeyPress(key, juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0); };
            const auto focusCard = [&cards](int track)
            {
                cards.setSelection({});
                cards.selectTrack(track);
                cards.focus = Arrangement::Focus::track;
            };
            const auto starter = doc.trackCount();

            te::EditItemID clipID;
            require(doc.createClip(0, 0.0, &clipID).wasOk(), "A clip to copy");
            cards.setSelection({clipID}, clipID);
            cards.keyPressed(command('C'));
            require(!cards.clipboard.isEmpty() && cards.trackClipboard.isEmpty(), "Ctrl+C on a clip copies a region");

            focusCard(1);
            const auto copiedName = doc.trackName(1);
            cards.keyPressed(command('C'));
            require(cards.clipboard.isEmpty() && cards.trackClipboard.tracks.size() == 1,
                    "With a card focused, Ctrl+C copies the track instead");
            cards.keyPressed(command('V'));
            require(doc.trackCount() == starter + 1 && doc.trackName(2) == copiedName && cards.selectedTrack == 2
                        && cards.focus == Arrangement::Focus::track,
                    "Ctrl+V pastes it under the card, and the copy becomes the selected card");
            cards.keyPressed(command('V'));
            require(doc.trackCount() == starter + 2 && cards.selectedTrack == 3, "so a second paste lands under the first");
            cards.keyPressed(command('D'));
            require(doc.trackCount() == starter + 3 && cards.selectedTrack == 4 && doc.trackName(4) == copiedName,
                    "Ctrl+D duplicates the card under itself");
            cards.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(doc.trackCount() == starter + 2 && cards.selectedTrack == 4 && cards.focus == Arrangement::Focus::track,
                    "Delete removes the card and selects the one that moves up");

            focusCard(0);
            const auto cutName = doc.trackName(0);
            cards.keyPressed(command('X'));
            require(doc.trackCount() == starter + 1 && cards.trackClipboard.tracks.size() == 1,
                    "Ctrl+X takes the card off the stack and onto the clipboard");
            focusCard(doc.trackCount() - 1);
            cards.keyPressed(command('V'));
            require(doc.trackCount() == starter + 2 && doc.trackName(doc.trackCount() - 1) == cutName
                        && !te::getAudioTracks(*doc.edit).getLast()->getClips().isEmpty(),
                    "and pasting puts it down again, clips and all");

            focusCard(0);
            cards.toggleTrackSelection(1);
            cards.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(doc.trackCount() == starter, "Delete removes every selected card in one go");
            doc.undo();
            require(doc.trackCount() == starter + 2, "and one undo brings them all back");
            doc.releaseAudioDevice();
        }
