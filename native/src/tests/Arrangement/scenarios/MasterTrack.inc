        // The main track is addressed one past the last audio track. It carries
        // effects and the main level, and refuses everything that makes sound.
        const auto mainTrack = session.masterTrackIndex();
        require(mainTrack == session.trackCount(), "The main track sits past the audio tracks");
        require(session.isMasterTrack(mainTrack) && !session.isMasterTrack(0), "Only that index is the main track");
        require(session.trackName(mainTrack) == "Main", "The main track is named Main");

        scenario("main track: level and pan");
        session.setMasterVolumeDb(-6.0f);
        require(std::abs(session.masterVolumeDb() + 6.0f) < 0.05f, "The main level can be set");
        session.setMasterPan(-0.25f);
        require(std::abs(session.masterPan() + 0.25f) < 0.02f, "The main pan can be set");
        view.sync();
        require(std::abs(view.masterVolume.getValue() + 6.0) < 0.05, "The arrangement's main row shows the level");
        require(std::abs(view.masterPan.getValue() + 0.25) < 0.02, "The arrangement's main row shows the pan");
        view.masterVolume.setValue(-2.0, juce::sendNotificationSync);
        require(std::abs(session.masterVolumeDb() + 2.0f) < 0.05f, "Dragging the main fader moves the main level");

        scenario("main track: effects only");
        const auto mainDevices = session.deviceSlots(mainTrack).size();
        require(session.addAudioEffect(Session::AudioEffect::Reverb, mainTrack).wasOk(),
                "An audio effect can be added to the main track");
        require(session.deviceSlots(mainTrack).size() == mainDevices + 1,
                "The effect appears in the main track's chain");
        require(session.addInstrument(Session::Instrument::FourOsc, mainTrack).failed(),
                "The main track refuses instruments");
        require(session.addMidiEffect(Session::MidiEffect::ThetaArp, mainTrack).failed(),
                "The main track refuses MIDI effects");
        require(session.createClip(mainTrack, 0.0).failed(), "The main track takes no clips");
        require(session.importBuiltInSample(Session::BuiltInSample::Whistle, mainTrack, 0.0).failed(),
                "The main track takes no audio");
        require(view.applyBrowserDrop("theta-browser:preset:HouseKit", mainTrack).failed(),
                "A pattern cannot be dropped on the main track");
        require(view.applyBrowserDrop("theta-browser:instrument:ThetaWave", mainTrack).failed(),
                "An instrument cannot be dropped on the main track");
        require(view.applyBrowserDrop("theta-browser:drumkit:HouseKit", mainTrack).failed(),
                "A drum kit cannot be dropped on the main track");
        require(view.applyBrowserDrop("theta-browser:effect:Delay", mainTrack).wasOk(),
                "An effect can be dropped on the main track");

        scenario("main track: selection and persistence");
        view.selectTrack(mainTrack);
        require(view.isMasterSelected(), "The main row can be selected");
        require(!view.masterLane().isEmpty() && view.masterLane().getY() > view.lane(0).getY(),
                "The main row sits below the first lane");
        {
            const auto savedMain = session.projectSnapshot();
            juce::TemporaryFile mainProject(".thetaedit");
            require(session.restoreProject(savedMain, mainProject.getFile()).wasOk(), "Reopen a project with main effects");
            require(std::abs(session.masterVolumeDb() + 2.0f) < 0.05f, "The main level survives reopen");
            require(session.deviceSlots(session.masterTrackIndex()).size() == mainDevices + 2,
                    "Main track effects survive reopen");
        }
