        // Reopening a project whose stack opens with a group used to come back
        // silent. The pattern track is looked up positionally, and when a group
        // sits at the top of the stack the track in that position is its bus -
        // so the reopen put an instrument on the bus, renamed it after that
        // instrument, and the sum of every member feeding the bus was replaced
        // by a synth with nothing to play.
        //
        // This has to render: reading the plugin list back says an instrument
        // is there, only the render says the project went quiet. It therefore
        // sits above "gestures and persistence", which closes the audio device.
        //
        // Self-contained, and it puts the stack back the way it found it - the
        // scenarios below depend on what the ones above built.
        {
            const auto entryState = session.projectSnapshot();
            juce::TemporaryFile entryProject(".rhinoedit");

            require(session.addAudioTrack().wasOk(), "A clean track for the reload check");
            require(session.moveTrack(session.trackCount() - 1, 1).wasOk(), "carried up under the pattern track");
            require(session.importAudioAt(source.getFile(), 1, 0.0).wasOk(), "with audio on it");
            // The pattern track goes in too, so the reopened document still
            // finds everything it looks for and fails on nothing but this.
            require(session.groupTracks({0, 1}, "Reload check").wasOk(), "and both are given a bus");
            const auto band = session.trackGroups().front();
            require(band.busTrack == 0 && band.trackCount == 2,
                    "which now opens the document, with the two of them under it");
            const auto busName = session.trackName(band.busTrack);

            auto* member = te::getAudioTracks(*session.edit)[band.lastTrack()]->getClips().getLast();
            require(member != nullptr, "The lower member holds the audio");
            const auto memberID = member->itemID;

            // Everything outside the band is silenced, so the render window
            // carries the bus and nothing else.
            for (int track = 0; track < session.trackCount(); ++track)
                if (track < band.busTrack || track > band.lastTrack())
                    session.setTrackMuted(track, true);
            const auto before = renderClipRms(*member);
            require(before > 0.01f, "and is audible through the bus while the project is open");

            const auto savedBusFirst = session.projectSnapshot();
            juce::TemporaryFile busFirstProject(".rhinoedit");
            require(session.restoreProject(savedBusFirst, busFirstProject.getFile()).wasOk(),
                    "The project can be reopened");

            const auto reopened = session.trackGroups();
            require(reopened.size() == 1 && reopened.front().busTrack == 0,
                    "The group still opens the stack after the reopen");
            member = session.findAudioClip(memberID);
            require(member != nullptr, "The member's audio survives the reopen");
            require(renderClipRms(*member) > before * 0.5f,
                    "and is still heard, which is the whole of what reopening into silence broke");

            require(session.patternInstrumentForTrack(0) == nullptr,
                    "The bus runs no instrument: reopening must not mistake it for the pattern track");
            require(session.trackName(0) == busName,
                    "so it keeps its own name rather than being renamed after one");

            // The other half of the same mistake, and the one a document made
            // entirely of samples runs into: reopening used to give the
            // pattern track an instrument whenever it could not find one,
            // which is every track that plays audio rather than notes.
            session.newProject();
            require(session.importAudioAt(source.getFile(), 0, 0.0).wasOk(),
                    "A new project's track takes a sample");
            require(session.patternInstrumentForTrack(0) == nullptr,
                    "which leaves it running no instrument, because a sample is not one");
            const auto sampledName = session.trackName(0);
            // The starter pattern clip is still on this track and sorts last,
            // so the sample is looked for by what it is rather than by place.
            te::WaveAudioClip* sampled = nullptr;
            for (auto* clip : te::getAudioTracks(*session.edit)[0]->getClips())
                if (auto* wave = dynamic_cast<te::WaveAudioClip*>(clip)) sampled = wave;
            require(sampled != nullptr, "The sample is on the track");
            const auto sampledID = sampled->itemID;
            const auto sampledBefore = renderClipRms(*sampled);
            require(sampledBefore > 0.01f, "and is audible");

            const auto savedSampled = session.projectSnapshot();
            juce::TemporaryFile sampledProject(".rhinoedit");
            require(session.restoreProject(savedSampled, sampledProject.getFile()).wasOk(),
                    "That project can be reopened");
            sampled = session.findAudioClip(sampledID);
            require(sampled != nullptr, "The sample survives the reopen");
            require(renderClipRms(*sampled) > sampledBefore * 0.5f,
                    "and is still audible, rather than replaced by an invented instrument");
            require(session.patternInstrumentForTrack(0) == nullptr,
                    "The track still runs no instrument");
            require(session.trackName(0) == sampledName,
                    "and still carries its own name rather than one taken from an instrument");

            require(session.restoreProject(entryState, entryProject.getFile()).wasOk(),
                    "The stack this scenario borrowed is put back for the ones below");
            view.sync();
            view.resized();
        }
