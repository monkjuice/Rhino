        Theme theme;
        Arrangement view(session);
        require(std::abs(session.beatsPerBar() - 4.0) < 0.0001, "Default meter is four quarter-note beats per bar");
        view.setLookAndFeel(&theme);
        view.setSize(1000, 246);
        view.fit();
        view.sync();
        require(view.clips.empty(), "Fresh startup hides the internal empty Pattern 1 placeholder");
        // A new document is one empty track and the main row, nothing else.
        require(session.trackCount() == 1 && session.trackName(0) == "Track 1",
                "A new document opens with a single track called Track 1");
        require(!session.trackHasInstrument(0) && session.patternInstrument() == nullptr,
                "The starter track runs no instrument until one is dropped on it");
        require(session.deviceSlots(0).empty(), "The starter track shows no devices of its own");
        require(view.rows.size() == 1, "The arrangement draws one lane for a new document");
        require(session.removeAudioTrack(0).failed(), "The last remaining track cannot be removed");
        session.setNote(0, 48, true);
        view.sync();
        require(view.clips.size() == 1, "Drawing the first note makes the startup pattern clip visible");
        session.undo();
        view.sync();
        require(view.clips.empty(), "Undoing the first note hides the empty startup pattern again");
        int notifiedTrack = -1;
        view.trackSelected = [&notifiedTrack](int track) { notifiedTrack = track; };
        require(view.isInterestedInDragSource({"theta-browser:preset:HouseKit", nullptr, {160, 82}}),
                "Arrangement accepts browser drag payloads");
        require(view.applyBrowserDrop("theta-browser:preset:HouseKit", 0).wasOk(), "Browser drum drop loads a kit");
        require(session.isPatternDrums(), "Dropped drum kit enables the drum editor");
        const auto patternDevices = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("theta-browser:effect:Reverb", 0).wasOk(), "Audio FX can be dropped on the pattern clip track");
        require(session.deviceSlots(0).size() == patternDevices + 1, "Dropped Reverb appears in the pattern Device View");
        const auto midiEffectSlots = session.deviceSlots(0).size();
        require(view.applyBrowserDrop("theta-browser:midi-effect:ThetaArp", 0).wasOk(), "MIDI FX can be dropped on the pattern track");
        require(session.deviceSlots(0).size() == midiEffectSlots + 1, "Dropped MIDI FX appears in the pattern Device View");
        const auto midiDropDevices = session.deviceSlots(0).size();
        view.itemDropped({"theta-browser:effect:Delay", nullptr, {static_cast<int>(view.xFor(0.25)), 82}});
        require(session.deviceSlots(0).size() == midiDropDevices + 1,
                "Dropping audio FX on a MIDI clip falls back to the track Device View");
        const auto patternClipsBeforeDrop = te::getAudioTracks(*session.edit)[0]->getClips().size();
        view.itemDropped({"theta-browser:preset:BreakKit", nullptr, {static_cast<int>(view.xFor(1.0)), 82}});
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == patternClipsBeforeDrop + 1,
                "Dragging a browser preset to the arrangement adds a pattern clip instead of replacing the first one");
        require(session.edit->getTransport().getLoopRange().getEnd().inSeconds() >= 3.0,
                "Loop range follows added clips on the first track");
        session.undo();
        const auto effectSlots = static_cast<int>(session.deviceSlots(1).size());
        require(view.applyBrowserDrop("theta-browser:effect:Delay", 1).wasOk(), "Browser effect drop inserts on the target audio track");
        require(static_cast<int>(session.deviceSlots(1).size()) == effectSlots + 1, "Dropped effect appears in the target audio Device View");
