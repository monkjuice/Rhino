        // A keyboard's velocity curve, with no keyboard: a made-up port name, so
        // nothing real is taken from the test engine, and the runner never
        // starts the keyboards, so no port is opened. What the port's own MIDI
        // callback does is driven through receiveKeyboardMidi instead.
        {
            scenario("keyboard curves: a curve takes the port from the engine");
            const juce::String testPort {"Rhino Test Keyboard Port"};
            require(session.velocityCurve(testPort) == VelocityCurve::linear, "A keyboard starts on Linear");
            require(!session.isCurvedMidiPort(testPort), "and is the engine's to read");
            require(session.keyboardPortState(testPort) == Session::KeyboardPortState::engine, "which is what it reports");
            session.setVelocityCurve(testPort, VelocityCurve::soft);
            require(session.velocityCurve(testPort) == VelocityCurve::soft, "A curve is kept");
            require(session.isCurvedMidiPort(testPort), "and Rhino reads the port for it");
            require(session.keyboardPortNames().contains(testPort), "A keyboard with a curve is listed, plugged in or not");
            require(session.keyboardPortState(testPort) == Session::KeyboardPortState::absent,
                    "and says it is not being read yet");

            scenario("keyboard curves: a track that names the port takes its stand-in");
            require(session.addTrack(Session::TrackType::midi).wasOk(), "A MIDI track for the keyboard");
            const auto keysTrack = session.firstReturnTrack() - 1;
            require(session.setTrackMidiInput(keysTrack, Session::midiInputDeviceToken(testPort)).wasOk(),
                    "It listens to the curved keyboard by the keyboard's own name");
            require(session.trackMidiInputName(keysTrack) == testPort, "and its card says so");
            require(session.midiInputDeviceForTrack(keysTrack) == session.curvedInputDevice(testPort),
                    "It resolves to the device the curved notes are delivered to, never to the port itself");
            // The engine makes that device on a timer, so it may not exist yet;
            // arming waits for it rather than reporting a missing input.
            const auto armed = session.setTrackArmed(keysTrack, true);
            require(!armed.getErrorMessage().contains("no MIDI input to record from"),
                    "Arming does not complain about the device on its way");
            juce::ignoreUnused(session.setTrackArmed(keysTrack, false));

            scenario("keyboard curves: a note is bent before anything hears it");
            const auto before = session.lastKeyboardNote(testPort).notes;
            session.receiveKeyboardMidi(testPort, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(40)));
            auto note = session.lastKeyboardNote(testPort);
            require(note.notes == before + 1, "The note is counted");
            require(note.struck == 40 && note.played == applyVelocityCurve(VelocityCurve::soft, 40) && note.played > 40,
                    "Struck at 40, it plays louder through Soft");
            session.receiveKeyboardMidi(testPort, juce::MidiMessage::noteOff(1, 60));
            require(session.lastKeyboardNote(testPort).notes == note.notes, "A release is not a note played");
            session.setVelocityCurve(testPort, VelocityCurve::softest);
            require(session.isCurvedMidiPort(testPort), "Another curve keeps the port Rhino's");
            session.receiveKeyboardMidi(testPort, juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(40)));
            note = session.lastKeyboardNote(testPort);
            require(note.played == applyVelocityCurve(VelocityCurve::softest, 40), "and takes effect on the next note");
            session.receiveKeyboardMidi(testPort, juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(0)));
            require(session.lastKeyboardNote(testPort).notes == note.notes, "A note-on at zero is a release, and stays one");

            scenario("keyboard curves: the preferences open on the tab asked for");
            {
                // On the heap: the audio tab's device chooser is large.
                auto preferences = std::make_unique<PreferencesPanel>(session, PreferencesPanel::Tab::midi);
                preferences->setSize(PreferencesPanel::defaultWidth, PreferencesPanel::defaultHeight);
                const auto shown = [&preferences](auto* kind)
                {
                    using Kind = std::remove_pointer_t<decltype(kind)>;
                    for (auto* child : preferences->getChildren())
                        if (dynamic_cast<Kind*>(child) != nullptr)
                            return child->isVisible();
                    return false;
                };
                require(preferences->shownTab() == PreferencesPanel::Tab::midi
                            && shown(static_cast<MidiPreferencesPanel*>(nullptr))
                            && !shown(static_cast<juce::AudioDeviceSelectorComponent*>(nullptr)),
                        "Opened on MIDI, it shows the keyboards and not the audio device");
                // In the app's own look, the one BrowserDrops made for the runner.
                preferences->setLookAndFeel(&theme);
                const auto picture = preferences->createComponentSnapshot(preferences->getLocalBounds());
                preferences->setLookAndFeel(nullptr);
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_PREFERENCES_SNAPSHOT", {});
                    shot.isNotEmpty())
                {
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(picture, *stream);
                }
                PreferencesPanel::Tab told = PreferencesPanel::Tab::midi;
                preferences->onTabShown = [&told](PreferencesPanel::Tab tab) { told = tab; };
                preferences->showTab(PreferencesPanel::Tab::audio);
                require(shown(static_cast<juce::AudioDeviceSelectorComponent*>(nullptr))
                            && !shown(static_cast<MidiPreferencesPanel*>(nullptr)),
                        "Audio settings is the Audio tab of the same window");
                require(told == PreferencesPanel::Tab::audio, "and the shell is told which tab was last looked at");
            }

            scenario("keyboard curves: a controller's port takes no curve");
            // Reserved by the Push scenario before this one.
            const juce::String controllerPort {"Rhino Test Controller Port"};
            require(session.isReservedMidiPort(controllerPort), "The Push scenario's port is still reserved");
            session.setVelocityCurve(controllerPort, VelocityCurve::hard);
            require(session.velocityCurve(controllerPort) == VelocityCurve::linear,
                    "What a controller plays is already shaped, so its port is refused a curve");
            require(!session.isCurvedMidiPort(controllerPort), "and stays the controller's");

            scenario("keyboard curves: Linear hands the port back");
            session.setVelocityCurve(testPort, VelocityCurve::linear);
            require(session.velocityCurve(testPort) == VelocityCurve::linear, "The curve is gone");
            require(!session.isCurvedMidiPort(testPort), "and the port is the engine's again");
            require(session.keyboardPortState(testPort) == Session::KeyboardPortState::engine, "which it reports");
            require(!session.keyboardPortNames().contains(testPort), "A port the machine lacks is no longer listed");
            require(session.midiInputDeviceForTrack(keysTrack) == nullptr,
                    "and the track, naming a port the machine lacks, resolves to nothing rather than to the stand-in");
            require(session.removeAudioTrack(keysTrack).wasOk(), "The keyboard's track goes");
        }
