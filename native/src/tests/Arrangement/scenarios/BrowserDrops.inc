        Theme theme;
        Arrangement view(session);
        require(std::abs(session.beatsPerBar() - 4.0) < 0.0001, "Default meter is four quarter-note beats per bar");
        view.setLookAndFeel(&theme);
        view.setSize(1000, 246);
        view.fit();
        view.sync();
        require(view.clips.empty(), "Fresh startup hides the internal empty Pattern 1 placeholder");
        // A new document is four empty tracks and the main row, nothing else:
        // MIDI and audio alternating, so there is somewhere to play and
        // somewhere to drop a file without building the stack first.
        require(session.trackCount() == 4, "A new document opens with four tracks");
        // A track is named for what it is and nothing else: the card prints the
        // row's one-based position beside the name itself, so a number in the
        // name was the same figure twice on every card.
        require(session.trackName(0) == "MIDI" && session.trackName(1) == "Audio"
                && session.trackName(2) == "MIDI" && session.trackName(3) == "Audio",
                "and each is named for what it is, MIDI and audio alternating");
        require(!session.trackHasInstrument(0) && session.patternInstrument() == nullptr,
                "The starter track runs no instrument until one is dropped on it");
        require(session.deviceSlots(0).empty(), "The starter track shows no devices of its own");
        require(view.rows.size() == 4, "The arrangement draws a lane for each of them");
        {
            // The stack this run works on has to survive, so the rule that a
            // document keeps at least one track is read off a document of its
            // own rather than by emptying this one.
            Session lastTrack;
            while (lastTrack.trackCount() > 1)
                require(lastTrack.removeAudioTrack(lastTrack.trackCount() - 1).wasOk(),
                        "A starter track can be removed");
            require(lastTrack.removeAudioTrack(0).failed(), "The last remaining track cannot be removed");
        }
        session.setNote(0, 48, true);
        view.sync();
        require(view.clips.size() == 1, "Drawing the first note makes the startup pattern clip visible");
        session.undo();
        view.sync();
        require(view.clips.empty(), "Undoing the first note hides the empty startup pattern again");
        int notifiedTrack = -1;
        view.trackSelected = [&notifiedTrack](int track) { notifiedTrack = track; };
        require(view.isInterestedInDragSource({"rhino-browser:preset:HouseKit", nullptr, {160, 82}}),
                "Arrangement accepts browser drag payloads");
        require(view.applyBrowserDrop("rhino-browser:preset:HouseKit", 0).wasOk(), "Browser drum drop loads a kit");
        require(session.isPatternDrums(), "Dropped drum kit enables the drum editor");
        const auto patternDevices = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("rhino-browser:effect:Reverb", 0).wasOk(), "Audio FX can be dropped on the pattern clip track");
        require(session.deviceSlots(0).size() == patternDevices + 1, "Dropped Reverb appears in the pattern Device View");
        const auto midiEffectSlots = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("rhino-browser:midi-effect:RhinoArp", 0).wasOk(), "MIDI FX can be dropped on the pattern track");
        require(session.deviceSlots(0).size() == midiEffectSlots + 1, "Dropped MIDI FX appears in the pattern Device View");
        const auto midiDropDevices = session.deviceSlots(0).size();
        view.itemDropped({"rhino-browser:effect:Delay", nullptr, {static_cast<int>(view.xFor(0.25)), 82}});
        require(session.deviceSlots(0).size() == midiDropDevices + 1,
                "Dropping audio FX on a MIDI clip falls back to the track Device View");
        const auto patternClipsBeforeDrop = te::getAudioTracks(*session.edit)[0]->getClips().size();
        view.itemDropped({"rhino-browser:preset:BreakKit", nullptr, {static_cast<int>(view.xFor(1.0)), 82}});
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == patternClipsBeforeDrop + 1,
                "Dragging a browser preset to the arrangement adds a pattern clip instead of replacing the first one");
        require(session.edit->getTransport().getLoopRange().getEnd().inSeconds() >= 3.0,
                "Loop range follows added clips on the first track");
        session.undo();
        const auto effectSlots = static_cast<int>(session.deviceSlots(1).size());
        require(view.applyBrowserDrop("rhino-browser:effect:Delay", 1).wasOk(), "Browser effect drop inserts on the target audio track");
        require(static_cast<int>(session.deviceSlots(1).size()) == effectSlots + 1, "Dropped effect appears in the target audio Device View");

        // A preset dragged out of the browser adds the device it is for,
        // already set, wherever that device may go: the main track takes an
        // effect's preset and refuses an instrument's. Both drops are undone,
        // since later scenarios count these tracks' devices.
        {
            const auto hall = ContentLibrary::file("Presets/RhinoSpace/Vast Hall.rnd");
            const auto hallDrop = "rhino-browser:device-preset:" + hall.getFullPathName();
            require(view.isInterestedInDragSource({hallDrop, nullptr, {160, 82}}), "The arrangement takes a dragged preset");
            const auto before = static_cast<int>(session.deviceSlots(1).size());
            require(view.applyBrowserDrop(hallDrop, 1).wasOk(), "A Rhino Space preset drops on an audio track");
            const auto after = session.deviceSlots(1);
            require(static_cast<int>(after.size()) == before + 1 && after.back().deviceId == "RhinoSpace"
                        && std::abs(session.deviceParameters(1, after.back().pluginIndex)[1].value - 0.9f) < 1.0e-6f,
                    "and adds Rhino Space set as Vast Hall says, Size 90%");
            require(view.applyBrowserDrop(hallDrop, session.masterTrackIndex()).wasOk(),
                    "An effect's preset drops on the main track");
            const auto bass = ContentLibrary::file("Presets/RhinoFM/Hard Bass.rnd");
            require(view.applyBrowserDrop("rhino-browser:device-preset:" + bass.getFullPathName(),
                                          session.masterTrackIndex()).failed(),
                    "but an instrument's preset does not");
            session.undo();
            session.undo();
            require(static_cast<int>(session.deviceSlots(1).size()) == before, "Each preset drop is one undo step");
        }
