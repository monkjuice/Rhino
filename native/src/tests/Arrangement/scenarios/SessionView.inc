        SessionView launcher(session);
        launcher.setLookAndFeel(&theme);
        launcher.setSize(1000, 246);
        require(session.sceneCount() == Session::defaultScenes, "A new project starts with a full set of scenes");
        require(session.sceneName(0) == "Scene 1", "Scenes are named by position until renamed");
        for (int track = 0; track < session.trackCount(); ++track)
            require(session.clipSlotAt(track, session.sceneCount() - 1) != nullptr,
                    "Every track has a slot for every scene");
        require(!session.slotClip(0, 0).hasClip, "Slots start empty");

        scenario("session view: filling slots");
        require(session.insertPatternPresetInSlot(Session::PatternPreset::WarmPulse, 0, 0).wasOk(),
                "A pattern preset can be inserted into a clip slot");
        const auto filled = session.slotClip(0, 0);
        require(filled.hasClip && filled.isMidi, "The slot reports the MIDI clip it now holds");
        require(filled.name.isNotEmpty(), "Slot clips carry the preset name");
        require(!filled.playing && !filled.playQueued, "Inserting a clip does not launch it");
        {
            auto* slot = session.clipSlotAt(0, 0);
            require(slot != nullptr && slot->getClip() != nullptr, "The engine holds the slot clip");
            require(slot->getClip()->isLooping(), "Slot clips loop until they are stopped");
            auto* midi = dynamic_cast<te::MidiClip*>(slot->getClip());
            require(midi != nullptr && midi->getSequence().getNumNotes() > 0, "The preset wrote notes into the slot clip");
        }
        require(launcher.isInterestedInDragSource({"rhino-browser:preset:HouseKit", nullptr, {10, 70}}),
                "The session view accepts browser drag payloads");
        launcher.applyBrowserDrop("rhino-browser:preset:HouseKit", 1, 1);
        require(session.slotClip(1, 1).hasClip, "A browser preset dropped on a slot fills it");

        scenario("session view: cell geometry");
        {
            const auto cell = launcher.slotBounds(1, 1);
            const auto hit = launcher.hitTest(cell.getCentre());
            require(hit.region == SessionView::Region::slot && hit.track == 1 && hit.scene == 1,
                    "A point inside a cell maps back to that track and scene");
            const auto sceneHit = launcher.hitTest(launcher.sceneLaunchBounds(1).getCentre());
            require(sceneHit.region == SessionView::Region::sceneLaunch && sceneHit.scene == 1,
                    "The right-hand column launches scenes");
            const auto stopHit = launcher.hitTest(launcher.trackStopBounds(0).getCentre());
            require(stopHit.region == SessionView::Region::trackStop && stopHit.track == 0,
                    "The bottom row stops a track");
        }

        scenario("session view: launching");
        require(session.launchSlot(0, 0).wasOk(), "A filled slot can be launched");
        require(session.slotClip(0, 0).hasClip, "Launching does not disturb the clip in the slot");
        require(session.launchScene(1).wasOk(), "A scene row can be launched");
        session.stopAllSlots();
        session.stop();
        require(session.launchSlot(0, session.sceneCount() - 1).wasOk(),
                "Launching an empty slot stops the track rather than failing");
        require(session.launchSlot(0, session.sceneCount() + 5).failed(), "Launching a slot that does not exist fails");

        scenario("session view: scenes and deletion");
        const auto sceneTotal = session.sceneCount();
        require(session.addScene().wasOk(), "A scene can be added");
        require(session.sceneCount() == sceneTotal + 1, "Adding a scene grows every track's slot list");
        require(session.clipSlotAt(session.trackCount() - 1, sceneTotal) != nullptr,
                "The new scene reaches the last track");
        require(session.deleteScene(sceneTotal).wasOk(), "A scene can be deleted");
        require(session.sceneCount() == sceneTotal, "Deleting a scene shrinks the list back");
        require(session.deleteSlotClip(1, 1).wasOk(), "A slot clip can be deleted");
        require(!session.slotClip(1, 1).hasClip, "The deleted slot is empty again");
        require(session.deleteSlotClip(1, 1).failed(), "Deleting from an empty slot reports a problem");

        scenario("session view: persistence");
        {
            const auto savedSession = session.projectSnapshot();
            juce::TemporaryFile sessionProject(".rhinoedit");
            require(session.restoreProject(savedSession, sessionProject.getFile()).wasOk(), "Reopen a project with slot clips");
            require(session.sceneCount() == sceneTotal, "Scene count survives reopen");
            const auto restored = session.slotClip(0, 0);
            require(restored.hasClip && restored.isMidi, "Slot clips survive reopen");
            require(restored.name == filled.name, "Slot clip names survive reopen");
        }
