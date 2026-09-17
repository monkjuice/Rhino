        // Changing the tempo must not edit the music. The engine anchors clips
        // to beats and rewrites their seconds itself, so what is checked here is
        // that nothing on Rhino's side fights it: the open clip used to be
        // snapped back to a single bar afterwards, with the rest of it cut off,
        // and the automation lanes are Rhino's own and still have to be moved.
        view.sync();
        {
            const auto beatsOf = [&session](double seconds)
            {
                return session.edit->tempoSequence.toBeats(
                    tracktion::core::TimePosition::fromSeconds(seconds)).inBeats();
            };
            require(session.addAudioTrack().wasOk(), "Add a track to change the tempo under");
            const auto tempoTrack = session.trackCount() - 1;
            require(session.insertPatternPreset(Session::PatternPreset::AcidSteps, tempoTrack, 0.0).wasOk(),
                    "Place a clip on it");
            auto* first = te::getAudioTracks(*session.edit)[tempoTrack]->getClips().getFirst();
            require(first != nullptr, "The clip is on the track");
            const auto firstID = first->itemID;

            // Longer than a bar, which is what the old one-bar snap destroyed,
            // and deliberately not a whole number of beats.
            const auto oneBar = session.edit->tempoSequence.toTime(
                tracktion::core::BeatPosition::fromBeats(session.beatsPerBar())).inSeconds();
            const auto longLength = oneBar * 1.375;
            require(session.editClip(firstID, {0.0, longLength, 0.0}, ClipGesture::trimRight, tempoTrack).wasOk(),
                    "A clip can be trimmed longer than one bar");
            require(close(first->getPosition().time.getEnd().inSeconds(), longLength), "It kept that length");
            require(session.insertPatternPreset(Session::PatternPreset::AcidSteps, tempoTrack, longLength).wasOk(),
                    "Place a neighbour hard against its end");

            te::EditItemID secondID;
            for (auto* onTrack : te::getAudioTracks(*session.edit)[tempoTrack]->getClips())
                if (onTrack->itemID != firstID)
                    secondID = onTrack->itemID;
            require(secondID.isValid(), "Found the neighbouring clip");

            const Session::DeviceTarget tempoLane {tempoTrack, 0, 0};
            require(!session.deviceParameters(tempoTrack, 0).empty(), "The track has a parameter to automate");
            require(session.showTrackAutomation(tempoLane, true).wasOk(), "Reveal a lane to sweep");
            require(session.setTrackAutomationPoints(tempoLane, {{0.0, 0.2f}, {longLength, 0.8f}}).wasOk(),
                    "Draw a curve that ends on the clip boundary");

            const auto firstEndBeats = beatsOf(longLength);
            const auto secondEndBeats = beatsOf(session.findClip(secondID)->getPosition().time.getEnd().inSeconds());
            require(firstEndBeats > session.beatsPerBar(), "The clip really does run past a bar");

            const auto before = session.tempo();
            session.setTempo(before + 1.0);
            require(close(session.tempo(), before + 1.0), "The tempo moved by one");

            const auto firstAfter = session.findClip(firstID)->getPosition().time;
            const auto secondAfter = session.findClip(secondID)->getPosition().time;
            require(close(beatsOf(firstAfter.getStart().inSeconds()), 0.0),
                    "A tempo nudge leaves the first clip on its downbeat");
            require(close(beatsOf(firstAfter.getEnd().inSeconds()), firstEndBeats),
                    "A tempo nudge keeps the whole of a clip longer than one bar");
            require(close(beatsOf(secondAfter.getStart().inSeconds()), firstEndBeats),
                    "The neighbour still starts where the first clip ends, with no gap opened");
            require(close(beatsOf(secondAfter.getEnd().inSeconds()), secondEndBeats),
                    "The neighbour keeps its own beat span");

            const auto sweep = session.trackAutomations(tempoTrack);
            require(!sweep.empty() && sweep.front().points.size() == 2, "The lane kept its curve");
            require(close(beatsOf(sweep.front().points.back().timeSeconds), firstEndBeats),
                    "Automation follows the clips rather than drifting out from under them");

            session.undo();
            require(close(session.tempo(), before), "Undo restores the tempo");
            require(close(session.findClip(firstID)->getPosition().time.getEnd().inSeconds(), longLength),
                    "Undo restores the clip length the tempo change rewrote");

            require(session.hideTrackAutomation(tempoLane).wasOk(), "Clean up the tempo lane");
            require(session.removeAudioTrack(tempoTrack).wasOk(), "Clean up the tempo track");
            view.sync();
        }
