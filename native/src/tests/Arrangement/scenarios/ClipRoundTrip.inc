        // Session clips and arrangement clips are separate objects, as in Live.
        // What must work is the route between them, in both directions.
        view.sync();
        const auto arrangementClipsOn = [&session](int track)
        {
            int count = 0;
            for (auto* clip : te::getAudioTracks(*session.edit)[track]->getClips())
                if (session.shouldShowClipInArrangement(*clip)) ++count;
            return count;
        };

        scenario("clip round trip: slot to arrangement");
        require(session.insertPatternPresetInSlot(Session::PatternPreset::AcidSteps, 0, 2).wasOk(),
                "Put a known clip in a slot");
        const auto beforeCopy = arrangementClipsOn(0);
        require(session.copySlotClipToArrangement(0, 2, 4.0).wasOk(), "A slot clip can be copied to the arrangement");
        require(arrangementClipsOn(0) == beforeCopy + 1, "The arrangement gained exactly one clip");
        require(session.slotClip(0, 2).hasClip, "Copying leaves the original in its slot");
        view.sync();
        {
            const te::Clip* placed = nullptr;
            for (auto* clip : te::getAudioTracks(*session.edit)[0]->getClips())
                if (std::abs(clip->getPosition().time.getStart().inSeconds() - 4.0) < 0.001)
                    placed = clip;
            require(placed != nullptr, "The copy lands at the position it was given");
            require(!placed->isLooping(), "An arrangement copy plays once rather than repeating");
            const auto* placedMidi = dynamic_cast<const te::MidiClip*>(placed);
            require(placedMidi != nullptr, "A MIDI slot clip copies as a MIDI arrangement clip");
        }
        require(session.copySlotClipToArrangement(0, 3, 4.0).failed(), "Copying an empty slot reports a problem");

        scenario("clip round trip: arrangement to slot");
        te::EditItemID roundTripID;
        for (auto* clip : te::getAudioTracks(*session.edit)[0]->getClips())
            if (std::abs(clip->getPosition().time.getStart().inSeconds() - 4.0) < 0.001)
                roundTripID = clip->itemID;
        require(roundTripID.isValid(), "Found the clip to send back");
        const auto freeSlot = session.firstFreeSlot(0);
        require(freeSlot >= 0, "Track 1 has a free slot");
        require(session.copyClipToSlot(roundTripID).wasOk(), "An arrangement clip can be copied to a session slot");
        require(session.slotClip(0, freeSlot).hasClip, "The copy lands in the first free slot");
        require(session.findClip(roundTripID) != nullptr, "Copying leaves the original in the arrangement");
        {
            auto* slot = session.clipSlotAt(0, freeSlot);
            require(slot != nullptr && slot->getClip() != nullptr, "The slot holds the copy");
            require(slot->getClip()->isLooping(), "A slot copy repeats until it is stopped");
        }
        require(session.copyClipToSlot(te::EditItemID()).failed(), "Copying a clip that does not exist reports a problem");

        scenario("clip round trip: the copy survives a reopen");
        {
            const auto savedRoundTrip = session.projectSnapshot();
            juce::TemporaryFile roundTripProject(".thetaedit");
            require(session.restoreProject(savedRoundTrip, roundTripProject.getFile()).wasOk(), "Reopen the project");
            require(session.slotClip(0, freeSlot).hasClip, "The slot copy survives reopen");
            require(session.findClip(roundTripID) != nullptr, "The arrangement clip survives reopen");
        }
