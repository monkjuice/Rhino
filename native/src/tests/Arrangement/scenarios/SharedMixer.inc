        // The mixer is the session's, and the arrangement's track headers show
        // it. These checks exist to catch the arrangement growing its own copy
        // of a track's state.
        view.sync();
        view.resized();
        require(session.trackCount() >= 2, "The project has tracks to mix");
        for (int track = 0; track < session.trackCount(); ++track)
            require(session.trackVolumePlugin(track) != nullptr, "Every track has a fader");

        scenario("arrangement mixer: volume reaches the header");
        require(session.setTrackVolumeDb(1, -8.0f).wasOk(), "Track volume can be set");
        require(std::abs(session.trackMixer(1).volumeDb + 8.0f) < 0.05f, "The mixer reports the volume it was given");
        view.sync();
        require(std::abs(view.volume[1]->getValue() + 8.0) < 0.05, "The arrangement header shows the new volume");

        scenario("arrangement mixer: pan reaches the header");
        require(session.setTrackPan(1, -0.5f).wasOk(), "Track pan can be set");
        view.sync();
        require(std::abs(view.pan[1]->getValue() + 0.5) < 0.02, "The arrangement header shows the new pan");

        scenario("arrangement mixer: edits from the header and the model are the same edit");
        require(session.setTrackVolumeDb(1, -3.0f).wasOk(), "The model moves the track");
        view.sync();
        require(std::abs(view.volume[1]->getValue() + 3.0) < 0.05, "The arrangement follows a model edit");
        view.volume[1]->setValue(-1.0, juce::sendNotificationSync);
        require(std::abs(session.trackMixer(1).volumeDb + 1.0f) < 0.05f, "Dragging the arrangement fader moves the track");

        scenario("arrangement mixer: mute and solo are one state");
        session.setTrackMuted(1, true);
        view.sync();
        require(session.trackMixer(1).muted, "The track is muted");
        require(view.mute[1]->getToggleState(), "The arrangement mute button is lit");
        session.setTrackMuted(1, false);
        session.setTrackSoloed(1, true);
        view.sync();
        require(session.trackMixer(1).soloed && view.solo[1]->getToggleState(), "Solo is one state, seen in the header");
        session.setTrackSoloed(1, false);

        scenario("arrangement mixer: new tracks and reopened projects get a fader");
        require(session.addAudioTrack().wasOk(), "A track can be added");
        const auto addedTrack = session.trackCount() - 1;
        require(session.trackVolumePlugin(addedTrack) != nullptr, "A new track arrives with a fader");
        require(session.setTrackVolumeDb(addedTrack, -5.0f).wasOk(), "The new track's fader works");
        {
            const auto savedMix = session.projectSnapshot();
            juce::TemporaryFile mixProject(".rhinoedit");
            require(session.restoreProject(savedMix, mixProject.getFile()).wasOk(), "Reopen a mixed project");
            require(std::abs(session.trackMixer(addedTrack).volumeDb + 5.0f) < 0.05f, "Track volume survives reopen");
            require(std::abs(session.trackMixer(1).pan + 0.5f) < 0.02f, "Track pan survives reopen");
        }

        scenario("arrangement mixer: main output");
        session.setMasterVolumeDb(-4.0f);
        require(std::abs(session.masterVolumeDb() + 4.0f) < 0.05f, "The main output level can be set");
        session.setMasterVolumeDb(0.0f);
