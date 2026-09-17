        // The session view and the arrangement are two presentations of one
        // project. These checks exist to catch either view growing its own copy
        // of a track's state.
        view.sync();
        view.resized();
        launcher.resized();
        require(session.trackCount() >= 2, "The project has tracks to mix");
        for (int track = 0; track < session.trackCount(); ++track)
            require(session.trackVolumePlugin(track) != nullptr, "Every track has a fader");

        scenario("shared mixer: volume reaches both views");
        require(session.setTrackVolumeDb(1, -8.0f).wasOk(), "Track volume can be set");
        require(std::abs(session.trackMixer(1).volumeDb + 8.0f) < 0.05f, "The mixer reports the volume it was given");
        view.sync();
        launcher.syncTrackControls();
        require(std::abs(view.volume[1]->getValue() + 8.0) < 0.05, "The arrangement header shows the new volume");
        require(std::abs(launcher.volume[1]->getValue() + 8.0) < 0.05, "The session view strip shows the same volume");

        scenario("shared mixer: pan reaches both views");
        require(session.setTrackPan(1, -0.5f).wasOk(), "Track pan can be set");
        view.sync();
        launcher.syncTrackControls();
        require(std::abs(view.pan[1]->getValue() + 0.5) < 0.02, "The arrangement header shows the new pan");
        require(std::abs(launcher.pan[1]->getValue() + 0.5) < 0.02, "The session view strip shows the same pan");

        scenario("shared mixer: edits from either view are the same edit");
        launcher.volume[1]->setValue(-3.0, juce::sendNotificationSync);
        require(std::abs(session.trackMixer(1).volumeDb + 3.0f) < 0.05f, "Dragging the session fader moves the track");
        view.sync();
        require(std::abs(view.volume[1]->getValue() + 3.0) < 0.05, "The arrangement follows a session-view edit");
        view.volume[1]->setValue(-1.0, juce::sendNotificationSync);
        require(std::abs(session.trackMixer(1).volumeDb + 1.0f) < 0.05f, "Dragging the arrangement fader moves the track");
        launcher.syncTrackControls();
        require(std::abs(launcher.volume[1]->getValue() + 1.0) < 0.05, "The session view follows an arrangement edit");

        scenario("shared mixer: mute and solo are one state");
        session.setTrackMuted(1, true);
        view.sync();
        launcher.syncTrackControls();
        require(session.trackMixer(1).muted, "The track is muted");
        require(view.mute[1]->getToggleState(), "The arrangement mute button is lit");
        require(launcher.mute[1]->getToggleState(), "The session view mute button is lit");
        session.setTrackMuted(1, false);
        session.setTrackSoloed(1, true);
        view.sync();
        launcher.syncTrackControls();
        require(session.trackMixer(1).soloed && view.solo[1]->getToggleState() && launcher.solo[1]->getToggleState(),
                "Solo is one state seen in both views");
        session.setTrackSoloed(1, false);

        scenario("shared mixer: new tracks and reopened projects get a fader");
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

        scenario("shared mixer: main output");
        session.setMasterVolumeDb(-4.0f);
        require(std::abs(session.masterVolumeDb() + 4.0f) < 0.05f, "The main output level can be set");
        session.setMasterVolumeDb(0.0f);

        scenario("shared mixer: handing tracks back to the arrangement");
        // A launched slot clip makes its track ignore timeline clips. Whether
        // the engine has raised that flag depends on the audio thread, so this
        // checks the way back rather than the way in, which is what would
        // otherwise leave arrangement tracks silent for good.
        session.returnToArrangement();
        require(!session.anyTrackPlayingSlots(), "No track overrides the arrangement after handing them back");
        require(session.launchSlot(0, 0).wasOk(), "A clip can be launched again afterwards");
        session.stopAllSlots();
        session.returnToArrangement();
        require(!session.anyTrackPlayingSlots(), "Handing back is repeatable");
        session.stop();
