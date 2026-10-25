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
            // The foot of the stack is above the returns.
            const auto foot = doc.firstReturnTrack();
            require(doc.pasteTracks(copied, -1, &pasted).wasOk() && pasted == std::vector<int> {foot}
                        && doc.trackName(foot) == "Lead" && doc.trackAutomations(foot).size() == 1,
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
            focusCard(doc.firstReturnTrack() - 1);
            cards.keyPressed(command('V'));
            require(doc.trackCount() == starter + 2 && doc.trackName(doc.firstReturnTrack() - 1) == cutName
                        && !te::getAudioTracks(*doc.edit)[doc.firstReturnTrack() - 1]->getClips().isEmpty(),
                    "and pasting puts it down again, clips and all");

            focusCard(0);
            cards.toggleTrackSelection(1);
            cards.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
            require(doc.trackCount() == starter, "Delete removes every selected card in one go");
            doc.undo();
            require(doc.trackCount() == starter + 2, "and one undo brings them all back");
            doc.releaseAudioDevice();
        }

        // Into another document: copied in one, pasted after opening another,
        // as Live carries a copy from one Set into the next. The copy finds
        // its audio from the other document's folder, keeps its clips and its
        // automation on their beats at the other tempo, and keeps no tie to a
        // track it did not bring.
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("Rhino track clipboard " + juce::String(juce::Time::getHighResolutionTicks()));
            const auto first = folder.getChildFile("First"), second = folder.getChildFile("Second");
            require(first.createDirectory().wasOk() && second.createDirectory().wasOk(), "Two project folders");
            const auto loop = first.getChildFile("Loop.wav");
            require(source.getFile().copyFileTo(loop), "and a sample beside the first");

            // Written to disk and opened from it, as the app opens one: the
            // engine names a file relative to a document that is not on disk
            // yet as though the document were a folder.
            const auto openFrom = [](Session& into, const juce::ValueTree& state, const juce::File& file)
            {
                const auto xml = state.createXml();
                return xml != nullptr && xml->writeTo(file) && into.restoreProject(state, file).wasOk();
            };

            auto document = std::make_unique<Session>();
            auto& doc = *document;
            // The second document is the starter stack at 90 BPM; the first is
            // the same at 120, opened from its file so that audio imported into
            // it is named relative to it.
            doc.setTempo(90.0);
            const auto slower = doc.projectSnapshot();
            doc.setTempo(120.0);
            require(openFrom(doc, doc.projectSnapshot(), first.getChildFile("First.rhinoedit")),
                    "The first document opens from its file");
            const auto vocoderSlots = [&doc](int track)
            {
                std::vector<int> found;
                for (const auto& slot : doc.deviceSlots(track))
                    if (slot.type == VocoderDevice::xmlTypeName)
                        found.push_back(slot.pluginIndex);
                return found;
            };

            // [MIDI, Audio, MIDI, Audio]: a bar on the first track from beat 4,
            // the sample on the second from the same beat, and on the second
            // two vocoders, one played by the first track and one by the third.
            require(doc.createClip(0, 2.0).wasOk(), "A clip on the MIDI track at beat 4");
            require(doc.importAudioAt(loop, 1, 2.0).wasOk(), "the sample on the audio track at the same beat");
            auto* sample = te::getAudioTracks(*doc.edit)[1]->getClips().getFirst();
            require(sample != nullptr && !juce::File::isAbsolutePath(sample->state[te::IDs::source].toString()),
                    "named relative to the document");
            require(doc.addAudioEffect(Session::AudioEffect::Reverb, 0).wasOk(), "A reverb on the MIDI track");
            auto reverb = -1;
            for (const auto& slot : doc.deviceSlots(0))
                if (slot.type == te::ReverbPlugin::xmlTypeName)
                    reverb = slot.pluginIndex;
            const Session::DeviceTarget lane {0, reverb, 0};
            require(reverb > 0 && doc.showTrackAutomation(lane, false).wasOk()
                        && doc.setTrackAutomationPoints(lane, {{2.0, 0.0f}, {4.0, 1.0f}}).wasOk(),
                    "with a lane rising from beat 4 to beat 8");
            require(doc.addDevice("RhinoVocoder", 1).wasOk() && doc.addDevice("RhinoVocoder", 1).wasOk()
                        && vocoderSlots(1).size() == 2,
                    "Two vocoders on the audio track");
            require(doc.setDeviceSidechainSource(1, vocoderSlots(1)[0], 0).wasOk()
                        && doc.setDeviceSidechainSource(1, vocoderSlots(1)[1], 2).wasOk(),
                    "played by the MIDI track that is copied and by one that is not");
            const auto copied = doc.copyTracks({0, 1});
            require(copied.tracks.size() == 2, "The first two tracks are copied");

            require(openFrom(doc, slower, second.getChildFile("Second.rhinoedit")) && doc.tempo() == 90.0,
                    "and the second document opens in its place, at 90 BPM");
            const auto starter = doc.trackCount();
            std::vector<int> arrived;
            require(doc.pasteTracks(copied, 0, &arrived).wasOk() && arrived == std::vector<int> {1, 2}
                        && doc.trackCount() == starter + 2,
                    "The copies paste into it, under its first track");

            // The one clip a lane shows: a starter MIDI track also carries the
            // hidden, empty Pattern 1.
            const auto clipOf = [&doc](int track) -> te::Clip*
            {
                for (auto* clip : te::getAudioTracks(*doc.edit)[track]->getClips())
                    if (doc.shouldShowClipInArrangement(*clip))
                        return clip;
                return nullptr;
            };
            auto* bar = clipOf(1);
            require(bar != nullptr && close(doc.secondsToBeats(bar->getPosition().getStart().inSeconds()), 4.0)
                        && close(doc.secondsToBeats(bar->getPosition().getEnd().inSeconds()), 8.0),
                    "The MIDI clip keeps its beats at the new tempo, as a tempo change would keep them");
            auto* pastedSample = dynamic_cast<te::WaveAudioClip*>(clipOf(2));
            require(pastedSample != nullptr, "The sample comes along");
            require(pastedSample->getSourceFileReference().getFile() == loop,
                    ("and is still found, from the other document's folder: "
                     + pastedSample->state[te::IDs::source].toString() + " is "
                     + pastedSample->getSourceFileReference().getFile().getFullPathName()).toRawUTF8());
            require(close(doc.secondsToBeats(pastedSample->getPosition().getStart().inSeconds()), 4.0)
                        && close(pastedSample->getPosition().getLength().inSeconds(), 1.0),
                    "and starts on its beat, still a second long, as an unwarped clip is after a tempo change");
            const auto lanes = doc.trackAutomations(1);
            require(lanes.size() == 1 && lanes.front().points.size() == 2
                        && close(doc.secondsToBeats(lanes.front().points[0].timeSeconds), 4.0)
                        && close(doc.secondsToBeats(lanes.front().points[1].timeSeconds), 8.0),
                    "The lane keeps its beats too");
            const auto vocoders = vocoderSlots(2);
            require(vocoders.size() == 2 && doc.deviceSidechainSource(2, vocoders[0]) == 1,
                    "A vocoder played by a copied track is played by its copy");
            require(doc.deviceSidechainSource(2, vocoders[1]) == -1,
                    "and one played by a track left behind is played by nothing, not by whatever has its id here");
            doc.undo();
            require(doc.trackCount() == starter, "One undo takes the paste back");
            doc.releaseAudioDevice();
            document.reset();
            folder.deleteRecursively();
        }
