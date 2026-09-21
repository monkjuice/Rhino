        // The vocoder's carrier, and the MIDI input a track listens to.
        //
        // Both are routing rather than sound, so both are answerable here: what
        // the engine does with the routing is its business, and what Rhino
        // claims about it is what this checks. The DSP is measured separately,
        // in VocoderTest.cpp, where it needs no Edit at all.
        //
        // Self-contained: it borrows the stack and restores the document it
        // found at the end, so the scenarios below inherit what they would
        // have inherited anyway.
        {
            view.sync();
            view.resized();
            const auto entryState = session.projectSnapshot();
            juce::TemporaryFile entryProject(".rhinoedit");

            scenario("vocoder: the carrier is another track's audio");
            require(session.addTrack(Session::TrackType::midi).wasOk(), "A track for the synth");
            const auto carrierTrack = session.trackCount() - 1;
            require(session.addDevice("RhinoWave", carrierTrack).wasOk(), "which runs one");
            require(session.addAudioTrack().wasOk(), "and an audio track for the voice");
            const auto voiceTrack = session.trackCount() - 1;
            require(session.addDevice("RhinoVocoder", voiceTrack).wasOk(), "which takes the vocoder");

            auto vocoderSlot = -1;
            for (const auto& slot : session.deviceSlots(voiceTrack))
                if (slot.type == VocoderDevice::xmlTypeName)
                    vocoderSlot = slot.pluginIndex;
            require(vocoderSlot >= 0, "The vocoder is in the voice track's chain");

            scenario("vocoder: nothing is routed until a source is chosen");
            require(session.deviceSidechainSource(voiceTrack, vocoderSlot) == -1,
                    "A vocoder just added has no carrier");
            const auto sources = session.deviceSidechainSources(voiceTrack, vocoderSlot);
            require(!sources.empty(), "Every other track is offered as one");
            require(std::none_of(sources.begin(), sources.end(),
                                 [voiceTrack](const auto& source) { return source.track == voiceTrack; }),
                    "except the track the device is on: a track cannot play itself");
            require(session.deviceSidechainSources(voiceTrack, vocoderSlot + 100).empty(),
                    "and a slot that holds nothing offers none");

            scenario("vocoder: choosing a source wires it up");
            require(session.setDeviceSidechainSource(voiceTrack, vocoderSlot, carrierTrack).wasOk(),
                    "The synth track can be made the carrier");
            require(session.deviceSidechainSource(voiceTrack, vocoderSlot) == carrierTrack,
                    "and the device reports it");
            auto* vocoderPlugin = session.devicePlugin(voiceTrack, vocoderSlot);
            require(vocoderPlugin != nullptr && vocoderPlugin->canSidechain(),
                    "The device declares more inputs than its main bus, which is what offers the sidechain");
            require(vocoderPlugin->getNumWires() == 4,
                    "with a wire per input channel: two from its own track and two from the carrier");
            require(session.setDeviceSidechainSource(voiceTrack, vocoderSlot, voiceTrack).failed(),
                    "A track still cannot be made to play itself");
            require(session.deviceSidechainSource(voiceTrack, vocoderSlot) == carrierTrack,
                    "and the refusal leaves the working routing alone");

            scenario("vocoder: a device that takes no sidechain offers none");
            require(session.addDevice("Reverb", voiceTrack).wasOk(), "A reverb goes on the same track");
            auto reverbSlot = -1;
            for (const auto& slot : session.deviceSlots(voiceTrack))
                if (slot.name == "Reverb")
                    reverbSlot = slot.pluginIndex;
            require(reverbSlot >= 0, "and is in the chain");
            require(session.deviceSidechainSources(voiceTrack, reverbSlot).empty(),
                    "but has nowhere to put another track's audio");
            require(session.setDeviceSidechainSource(voiceTrack, reverbSlot, carrierTrack).failed(),
                    "so it refuses one");

            scenario("vocoder: the carrier is saved with the project");
            const auto wired = session.projectSnapshot();
            juce::TemporaryFile wiredProject(".rhinoedit");
            require(session.restoreProject(wired, wiredProject.getFile()).wasOk(), "The project reopens");
            auto reopenedSlot = -1;
            for (const auto& slot : session.deviceSlots(voiceTrack))
                if (slot.type == VocoderDevice::xmlTypeName)
                    reopenedSlot = slot.pluginIndex;
            require(reopenedSlot >= 0, "with the vocoder still on the voice track");
            require(session.deviceSidechainSource(voiceTrack, reopenedSlot) == carrierTrack,
                    "still played by the synth track");
            require(session.clearDeviceSidechainSource(voiceTrack, reopenedSlot).wasOk(),
                    "The carrier can be taken away again");
            require(session.deviceSidechainSource(voiceTrack, reopenedSlot) == -1,
                    "and the device says it has none");

            scenario("vocoder: removing the carrier track takes the routing with it");
            require(session.setDeviceSidechainSource(voiceTrack, reopenedSlot, carrierTrack).wasOk(),
                    "The synth track is the carrier again");
            require(session.addTrack(Session::TrackType::midi).wasOk(), "A spare track, to delete a different one");
            const auto spareTrack = session.trackCount() - 1;
            require(session.removeAudioTrack(spareTrack).wasOk(), "Removing a track that nothing points at");
            require(session.deviceSidechainSource(voiceTrack, reopenedSlot) == carrierTrack,
                    "leaves the routing alone");
            require(session.removeAudioTrack(carrierTrack).wasOk(), "Removing the carrier track itself");
            require(session.deviceSidechainSource(voiceTrack - 1, reopenedSlot) == -1,
                    "clears the device that was pointing at it, rather than leaving it routed to nothing");
            session.undo();
            require(session.deviceSidechainSource(voiceTrack, reopenedSlot) == carrierTrack,
                    "and undo brings the track and the routing back together");

            scenario("midi input: a track chooses which input plays it");
            require(session.trackMidiInput(carrierTrack) == Session::midiInputAllInsToken(),
                    "A new track listens to All Ins");
            require(session.trackMidiInputName(carrierTrack) == "All Ins", "which is what its card says");
            const auto choices = session.midiInputChoices();
            require(choices.size() >= 3, "All Ins, the typing keyboard and None are always offered");
            require(choices.front().token == Session::midiInputAllInsToken(), "All Ins comes first");
            require(choices.back().token == Session::midiInputNoneToken(), "and None last");
            require(std::any_of(choices.begin(), choices.end(), [](const auto& choice)
                                { return choice.token == Session::midiInputKeyboardToken(); }),
                    "with the typing keyboard between them, whatever hardware is plugged in");

            scenario("midi input: the choice belongs to the track");
            require(session.setTrackMidiInput(carrierTrack, Session::midiInputNoneToken()).wasOk(),
                    "A track can be told to listen to nothing");
            require(session.trackMidiInputName(carrierTrack) == "None", "and says so");
            require(session.setTrackMidiInput(voiceTrack, Session::midiInputKeyboardToken()).failed(),
                    "An audio track has no MIDI input to choose");
            require(session.trackMidiInput(voiceTrack) == Session::midiInputAllInsToken(),
                    "and is left alone by the refusal");

            scenario("midi input: hasHardwareMidiInput agrees with what the chooser offers");
            require(session.hasHardwareMidiInput()
                        == std::any_of(choices.begin(), choices.end(), [](const auto& choice)
                                       { return choice.token.startsWith("device:"); }),
                    "A keyboard you can play is exactly a chooser entry that is not one of the virtual devices");

            scenario("midi input: asking for the typing keyboard waits for its device rather than failing");
            // The engine makes the virtual device on a timer, so it does not
            // exist when the call that asked for it returns. Arming has to
            // wait for it; before it did, choosing Computer Keyboard reported
            // that there was no MIDI input and then quietly worked next time.
            juce::ignoreUnused(session.setTrackArmed(carrierTrack, true));
            const auto choseKeyboard = session.setTrackMidiInput(carrierTrack, Session::midiInputKeyboardToken());
            require(!choseKeyboard.getErrorMessage().contains("no MIDI input to record from"),
                    "so it does not complain about the device it has just asked for");
            require(session.trackMidiInputName(carrierTrack) == "Computer Keyboard",
                    "and the card says so at once");
            juce::ignoreUnused(session.setTrackArmed(carrierTrack, false));
            juce::ignoreUnused(session.setTrackMidiInput(carrierTrack, Session::midiInputNoneToken()));

            scenario("midi input: arming a track nothing can play says so");
            // The most confusing state the app has: a track that is armed,
            // correct, and silent. On a machine with no keyboard plugged in
            // the typing keyboard is the only thing that can play it.
            {
                juce::String armMessage;
                auto savedStatus = view.status;
                view.status = [&armMessage](juce::String text) { armMessage = std::move(text); };
                view.typingKeyboardEnabled = [] { return false; };
                view.sync();
                view.arm[static_cast<size_t>(carrierTrack)]->onClick();
                if (!session.hasHardwareMidiInput())
                    require(armMessage.contains("no MIDI keyboard is plugged in"),
                            "Arming a MIDI track with nothing to play it explains itself");
                view.typingKeyboardEnabled = [] { return true; };
                armMessage.clear();
                view.arm[static_cast<size_t>(carrierTrack)]->onClick();
                view.arm[static_cast<size_t>(carrierTrack)]->onClick();
                require(armMessage.contains("press Record or F9"),
                        "and with the typing keyboard on it is the ordinary message again");
                juce::ignoreUnused(session.setTrackArmed(carrierTrack, false));
                view.typingKeyboardEnabled = nullptr;
                view.status = std::move(savedStatus);
            }

            scenario("midi input: it is saved with the project, and follows the track");
            const auto routed = session.projectSnapshot();
            juce::TemporaryFile routedProject(".rhinoedit");
            require(session.restoreProject(routed, routedProject.getFile()).wasOk(), "The project reopens");
            require(session.trackMidiInputName(carrierTrack) == "None", "with the track still listening to nothing");
            require(session.setTrackMidiInput(carrierTrack, Session::midiInputAllInsToken()).wasOk(),
                    "and can be put back on All Ins");
            require(session.trackMidiInputName(carrierTrack) == "All Ins", "which is the default again");

            scenario("midi input: the card carries the chooser on a MIDI track only");
            view.sync();
            view.resized();
            require(view.midiInput.size() == static_cast<size_t>(session.trackCount()),
                    "Every card has one");
            require(view.midiInput[static_cast<size_t>(carrierTrack)]->getButtonText().startsWith("All Ins"),
                    "and the MIDI track's shows what it listens to");
            require(!view.midiInput[static_cast<size_t>(voiceTrack)]->isVisible(),
                    "while an audio track is given none rather than one with nothing in it");
            require(view.midiInput[static_cast<size_t>(carrierTrack)]->getRight()
                        <= static_cast<int>(view.trackIndent(carrierTrack) + view.cardControlsWidth),
                    "It stays inside the controls column with the rest");

            scenario("vocoder: putting the session back");
            require(session.restoreProject(entryState, entryProject.getFile()).wasOk(),
                    "The stack this scenario borrowed is put back for the ones below");
            view.sync();
            view.resized();
        }
