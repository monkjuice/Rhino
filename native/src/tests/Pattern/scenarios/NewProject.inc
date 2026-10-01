        // File > New project throws the open document away for a starter one.
        // The session has just been through a save and a reopen, so everything
        // checked here was true of the restored project and must not survive.
        require(session.projectFile != juce::File{}, "A restored project has a file behind it");
        require(session.isPatternDrums(), "The restored project runs the drum instrument");
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1,
                "The restored project has audio on its second lane");
        session.newProject();
        require(session.projectFile == juce::File{}, "A new project is untitled");
        require(!session.hasUnsavedChanges(), "A new project is clean");
        require(session.trackCount() == 4, "A new project has four tracks");
        // A track is named for what it is and nothing else: the card prints the
        // row's one-based position beside the name itself, so a number in the
        // name was the same figure twice on every card.
        require(session.trackName(0) == "MIDI" && session.trackName(1) == "Audio"
                && session.trackName(2) == "MIDI" && session.trackName(3) == "Audio",
                "A new project's tracks are named for what they are, MIDI and audio alternating");
        require(session.trackType(0) == Session::TrackType::midi
                && session.trackType(1) == Session::TrackType::audio
                && session.trackType(2) == Session::TrackType::midi
                && session.trackType(3) == Session::TrackType::audio,
                "A new project alternates MIDI and audio");
        {
            // A stack nobody has coloured yet still has to read as four
            // separate tracks, and the clips drawn on it take these colours.
            juce::StringArray starterColours;
            for (int track = 0; track < session.trackCount(); ++track)
            {
                require(!session.trackColour(track).isTransparent(),
                        "Every starter track is given a colour of its own");
                starterColours.addIfNotAlreadyThere(session.trackColour(track).toString());
            }
            require(starterColours.size() == session.trackCount(),
                    "and no two of them are the same colour");
        }
        require(te::getAudioTracks(*session.edit)[1]->getClips().isEmpty(),
                "A new project does not inherit the audio the last one held");
        require(!session.isPatternDrums(), "A new project does not inherit the drum instrument");
        require(session.utility != nullptr, "A new project has its pattern track Utility device");
        require(session.audioUtility != nullptr, "A new project's second track carries an audio Utility");
        require(std::abs(session.tempo() - 120.0) < 0.001, "A new project is back at 120 BPM");
        require(session.editorStepCount() == Session::defaultSteps, "A new project uses the default pattern length");
        require(session.pattern().getSequence().getNumNotes() == 0, "A new project's pattern is empty");
        require(session.trackAutomations(0).empty(), "A new project carries no automation lanes");
        require(!session.edit->getUndoManager().canUndo(), "A new project has nothing to undo");
        // The type a starter track was created as is the type it behaves as,
        // with nothing dropped on it yet: the MIDI lanes are playable at once
        // and the audio lanes send anyone drawing a clip to one that is.
        require(session.createClip(2, 0.0).wasOk(), "A starter MIDI track takes a clip straight away");
        require(session.createClip(3, 0.0).failed(), "A starter audio track refuses one");
        session.undo();
        // The starter Pattern 1 is a placeholder: empty, and hidden from the
        // arrangement until it is told where it goes. Double-clicking the first
        // bar of the starter track used to be refused as though a clip were in
        // the way, which made the first track of a document behave differently
        // from every track added after it.
        require(session.addInstrument(Session::Instrument::FourOsc, 0).wasOk(),
                "Can drop an instrument on the starter track");
        const auto starterClips = te::getAudioTracks(*session.edit)[0]->getClips().size();
        require(!session.shouldShowClipInArrangement(session.pattern()),
                "The starter clip is hidden until it is placed");
        require(session.createClip(0, 0.0).wasOk(), "The first bar of the starter track accepts a clip");
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == starterClips,
                "Placing it carries the hidden starter clip rather than adding a second one");
        require(session.shouldShowClipInArrangement(session.pattern()),
                "The placed clip is drawn in the arrangement");
        require(session.createClip(0, 0.0).failed(), "A clip that is really there still refuses a second one");
        // The swapped-in edit has to be the one the session now edits, not a
        // detached copy: notes must land in it and dirty the document.
        require(session.addNote(0.0, Session::lowestNote, 1.0).wasOk(), "Draw a note into a new project");
        require(session.pattern().getSequence().getNumNotes() == 1, "The note lands in the new project's pattern");
        require(session.hasUnsavedChanges(), "Editing a new project dirties it");
        session.releaseAudioDevice();
