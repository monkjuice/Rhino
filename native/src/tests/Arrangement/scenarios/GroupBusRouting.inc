        // Reading the routing back proves what was written; rendering proves
        // what is heard. This scenario sits here, directly after track
        // management, because "gestures and persistence" closes the audio
        // device further down the run and an offline render never returns once
        // it has: anything that renders has to come before that point.
        //
        // The check is self-contained - a clean track at the bottom, grouped,
        // rendered, ungrouped and removed - so it leaves the stack exactly as
        // it found it for the scenarios that follow.
        {
            require(session.addAudioTrack().wasOk(), "A clean track for the routing check");
            const auto fresh = session.trackCount() - 1;
            require(session.importAudioAt(source.getFile(), fresh, 0.0).wasOk(), "Audio can go on it");
            auto* routed = te::getAudioTracks(*session.edit)[fresh]->getClips().getLast();
            require(routed != nullptr, "The track holds that clip");

            // Everything else is silenced, so the render window carries this
            // track and nothing else.
            std::vector<bool> wasMuted;
            for (int track = 0; track < session.trackCount(); ++track)
            {
                wasMuted.push_back(session.trackMixer(track).muted);
                if (track != fresh)
                    session.setTrackMuted(track, true);
            }
            const auto loose = renderClipRms(*routed);
            require(loose > 0.01f, "An ungrouped track is audible at the output");

            require(session.groupTracks({fresh}, "Bus check").wasOk(), "The track is given a bus of its own");
            const auto checked = session.trackGroups().front();
            require(checked.busTrack == fresh && checked.firstTrack == fresh + 1,
                    "which opens where the track was, with the track under it");
            routed = session.findAudioClip(routed->itemID);
            require(routed != nullptr, "The clip survives the grouping");
            session.setTrackMuted(checked.busTrack, false);
            const auto heard = renderClipRms(*routed);
            require(heard > 0.01f, "and it is still audible once its audio goes through a bus");

            // The discriminating step: if the member were still wired straight
            // to the main output, muting the bus would change nothing at all.
            session.setTrackMuted(checked.busTrack, true);
            require(renderClipRms(*routed) < heard * 0.05f,
                    "Muting the bus silences the track, which is what says the audio passes through it");
            session.setTrackMuted(checked.busTrack, false);
            require(renderClipRms(*routed) > heard * 0.5f, "and unmuting the bus brings it back");

            require(session.ungroupTracks(checked.id).wasOk(), "The check group can be taken away");
            routed = session.findAudioClip(routed->itemID);
            require(routed != nullptr && renderClipRms(*routed) > heard * 0.5f,
                    "and the track plays straight to the main output again");

            session.deleteClip(routed->itemID);
            for (int track = 0; track < static_cast<int>(wasMuted.size()) && track < session.trackCount(); ++track)
                session.setTrackMuted(track, wasMuted[static_cast<size_t>(track)]);
            require(session.removeAudioTrack(session.trackCount() - 1).wasOk(),
                    "and the track this scenario added is removed again");
        }
