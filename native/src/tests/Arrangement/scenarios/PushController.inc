        // The Ableton Push, with no Push: the controller is never started, so
        // no port is opened, and what it would send and play is caught by its
        // two sinks instead. Two tracks are added for it and removed again.
        {
            scenario("push: a controller's ports are kept from the engine");
            // A made-up name: reserving a real port would disable it in the
            // test engine's device list.
            const juce::String testPort {"Rhino Test Controller Port"};
            require(!session.isReservedMidiPort(testPort), "Nothing is reserved until a controller asks");
            session.reserveMidiPorts({testPort});
            require(session.isReservedMidiPort(testPort), "A controller's port is reserved once it asks");
            for (const auto& choice : session.midiInputChoices())
                require(choice.name != testPort, "and no card offers it");

            require(session.addTrack(Session::TrackType::midi).wasOk(), "A MIDI track for the Push");
            const auto pushTrack = session.firstReturnTrack() - 1;
            require(session.addDevice("FourOsc", pushTrack).wasOk(), "with an instrument on it");
            int instrument = -1;
            for (const auto& slot : session.deviceSlots(pushTrack))
                if (slot.kind == DeviceKind::Instrument)
                    instrument = slot.pluginIndex;
            require(instrument > 0, "The instrument is on the chain");

            scenario("push: its input is offered while it is connected");
            const auto offersPush = [&session]
            {
                const auto choices = session.midiInputChoices();
                return std::any_of(choices.begin(), choices.end(), [](const auto& choice)
                                   { return choice.token == Session::midiInputPushToken() && choice.name == "Push"; });
            };
            require(!offersPush(), "No Push input without a Push");
            session.setControllerConnected(true);
            require(offersPush(), "A connected Push is offered on the cards");
            require(session.hasHardwareMidiInput(), "and counts as a keyboard");
            require(session.setTrackMidiInput(pushTrack, Session::midiInputPushToken()).wasOk(),
                    "A track can listen to the Push alone");
            require(session.trackMidiInputName(pushTrack) == "Push", "and its card says so");
            session.setControllerConnected(false);
            require(offersPush(), "Unplugged, a track that names it still shows it");
            require(session.setTrackMidiInput(pushTrack, Session::midiInputAllInsToken()).wasOk(), "Back on All Ins");
            require(!offersPush(), "and with nothing naming it, it is gone");

            scenario("push: a gesture closes even when its device has gone");
            {
                // A finger can rest on an encoder for as long as it likes, and
                // the device can be deleted under it. The depth used to stay
                // counted, and every later change was told to the rack alone.
                require(session.addDevice("FourOsc", pushTrack).wasOk(), "A second instrument replaces the first");
                int doomed = -1;
                for (const auto& slot : session.deviceSlots(pushTrack))
                    if (slot.kind == DeviceKind::Instrument)
                        doomed = slot.pluginIndex;
                require(session.beginDeviceParameterGesture(pushTrack, doomed, 0).wasOk(), "A knob is taken hold of");
                require(session.parameterGestureDepth == 1, "and the gesture is counted");
                require(session.deleteDevice(pushTrack, doomed).wasOk(), "The device goes under the held knob");
                juce::ignoreUnused(session.endDeviceParameterGesture(pushTrack, doomed, 0));
                require(session.parameterGestureDepth == 0, "Letting go closes the gesture all the same");
                session.beginTrackVolumeGesture(pushTrack);
                require(session.mixerGestureDepth == 1, "A fader is taken hold of");
                session.endTrackVolumeGesture(session.trackCount() + 5);
                require(session.mixerGestureDepth == 0, "and let go of, whatever became of its track");
                require(session.addDevice("FourOsc", pushTrack).wasOk(), "The instrument goes back");
                for (const auto& slot : session.deviceSlots(pushTrack))
                    if (slot.kind == DeviceKind::Instrument)
                        instrument = slot.pluginIndex;
            }

            std::vector<juce::MidiMessage> sent, played;
            auto controller = std::make_unique<PushController>(session);
            controller->sendToPush = [&sent](const juce::MidiMessage& message) { sent.push_back(message); };
            controller->playNote = [&played](const juce::MidiMessage& message) { played.push_back(message); };
            auto playPresses = 0;
            controller->play = [&playPresses] { ++playPresses; };
            const auto press = [&controller](int button)
            {
                controller->receive(juce::MidiMessage::controllerEvent(1, button, 127));
                controller->receive(juce::MidiMessage::controllerEvent(1, button, 0));
                controller->flush();
            };
            const auto turn = [&controller](int encoder, int delta)
            {
                controller->receive(juce::MidiMessage::controllerEvent(1, encoder, delta & 0x7f));
                controller->flush();
            };
            const auto touch = [&controller](int note, bool on)
            {
                controller->receive(on ? juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(127))
                                 : juce::MidiMessage::noteOff(1, note));
                controller->flush();
            };
            const auto pad = [&controller](int index, int velocity)
            {
                controller->receive(velocity > 0 ? juce::MidiMessage::noteOn(1, push::padNoteFirst + index, static_cast<juce::uint8>(velocity))
                                           : juce::MidiMessage::noteOff(1, push::padNoteFirst + index));
                controller->flush();
            };
            const auto displayShows = [&sent](const char* text)
            {
                for (const auto& message : sent)
                    if (message.isSysEx())
                    {
                        // Raw bytes, not UTF-8: a sysex starts with F0.
                        const std::string bytes(reinterpret_cast<const char*>(message.getRawData()),
                                                static_cast<size_t>(message.getRawDataSize()));
                        if (bytes.find(text) != std::string::npos)
                            return true;
                    }
                return false;
            };

            scenario("push: the display shows the selected device's controls");
            controller->setSelectedTrack(pushTrack);
            controller->flush();
            require(controller->selectedPlugin == instrument, "On a new track the Push takes its instrument");
            require(displayShows("Attack") && displayShows("Release"), "and shows its controls by name");
            require(std::any_of(sent.begin(), sent.end(), [](const auto& message)
                                { return message.isNoteOn() && message.getNoteNumber() == push::padNoteFirst; }),
                    "The pads are lit");
            sent.clear();
            controller->greet();
            require(std::any_of(sent.begin(), sent.end(), [](const auto& message)
                                {
                                    const auto expected = push::padSensitivitySysex();
                                    return message.isSysEx() && message.getRawDataSize() == static_cast<int>(expected.size())
                                        && std::equal(expected.begin(), expected.end(), message.getRawData());
                                }),
                    "Connecting sets Live's pad sensitivity, so the pads reach full velocity as they do in Live");

            scenario("push: an encoder turn is one undo step");
            {
                // On Rhino FM, a device on Rhino's own SDK: its controls follow
                // an undo at once. A Tracktion plugin's controls - 4OSC's -
                // keep the value they had until something reads their state
                // again, which would make this check about the engine rather
                // than about the Push.
                require(session.addDevice("RhinoFM", pushTrack).wasOk(), "Rhino FM takes the instrument's place");
                for (const auto& slot : session.deviceSlots(pushTrack))
                    if (slot.kind == DeviceKind::Instrument)
                        instrument = slot.pluginIndex;
                auto encoder = -1;
                for (int i = 0; i < push::rowButtons && encoder < 0; ++i)
                    if (const auto control = session.deviceParameter(pushTrack, instrument, i);
                        control && !control->discrete && !control->toggle && control->choices.isEmpty()
                        && control->value < control->maximum)
                        encoder = i;
                require(encoder >= 0, "One of its first eight controls is continuous, with room to rise");
                const auto before = session.deviceParameter(pushTrack, instrument, encoder)->value;
                session.edit->getUndoManager().beginNewTransaction();
                touch(encoder, true);
                require(session.parameterGestureDepth == 0, "A touch alone takes nothing off its lane");
                turn(push::cc::trackEncoderFirst + encoder, 10);
                require(controller->selectedPlugin == instrument, "The Push follows the new instrument");
                turn(push::cc::trackEncoderFirst + encoder, 10);
                require(session.parameterGestureDepth == 1, "The first turn opens the gesture");
                touch(encoder, false);
                require(session.parameterGestureDepth == 0, "and letting go closes it");
                const auto after = session.deviceParameter(pushTrack, instrument, encoder)->value;
                require(after > before, "Turning clockwise raises the control");
                session.undo();
                require(std::abs(session.deviceParameter(pushTrack, instrument, encoder)->value - before) < 1.0e-4f,
                        "and one undo takes the whole turn back");
            }

            scenario("push: buttons");
            {
                const auto click = session.clickTrackEnabled();
                press(push::cc::metronome);
                require(session.clickTrackEnabled() != click, "Metronome toggles the click");
                press(push::cc::metronome);
                require(session.clickTrackEnabled() == click, "and back");
                press(push::cc::play);
                require(playPresses == 1, "Play asks the shell, which knows what it means in each view");
                for (const auto& message : played)
                    require(!message.isController(), "No button or encoder reaches a track");
            }

            scenario("push: mixer mode");
            {
                press(push::cc::volume);
                require(controller->encoderMode() == PushController::EncoderMode::volume, "Volume puts the faders on the encoders");
                const auto encoder = pushTrack % push::rowButtons;
                const auto before = session.trackMixer(pushTrack).volumeDb;
                turn(push::cc::trackEncoderFirst + encoder, -4);
                touch(encoder, false);
                require(std::abs(session.trackMixer(pushTrack).volumeDb - (before - 2.0f)) < 0.01f,
                        "Four steps down is two decibels");
                require(session.mixerGestureDepth == 0, "and letting go closes the fader's gesture");
                juce::ignoreUnused(session.setTrackVolumeDb(pushTrack, before));
                const auto main = session.masterVolumeDb();
                turn(push::cc::masterEncoder, 2);
                touch(push::touchMasterEncoder, false);
                require(std::abs(session.masterVolumeDb() - std::min(Session::maximumVolumeDb, main + 1.0f)) < 0.01f,
                        "The master encoder is the main level");
                session.setMasterVolumeDb(main);
                press(push::cc::device);
                require(controller->encoderMode() == PushController::EncoderMode::device, "Device puts the controls back");
            }

            scenario("push: pads play in key, and let go of what they played");
            {
                played.clear();
                pad(0, 100);
                require(played.size() == 1 && played[0].isNoteOn() && played[0].getNoteNumber() == 48
                            && played[0].getVelocity() == 100,
                        "The bottom-left pad plays the root, C3, at its own velocity");
                press(push::cc::scales);
                turn(push::cc::trackEncoderFirst, 6);
                require(controller->noteLayout().root == 1, "The scale page's first encoder moves the root");
                pad(0, 0);
                require(played.size() == 2 && played[1].isNoteOff() && played[1].getNoteNumber() == 48,
                        "A pad let go releases the note it played, not the one it plays now");
                pad(0, 90);
                require(played.back().getNoteNumber() == 49, "Pressed again, it plays the new root");
                pad(0, 0);
                turn(push::cc::trackEncoderFirst, -6);
                press(push::cc::scales);
                require(controller->noteLayout().root == 0, "The root goes back");
                pad(8, 64);
                require(played.back().getNoteNumber() == 53, "A row up is a fourth up");
                pad(8, 0);
                press(push::cc::accent);
                pad(1, 20);
                require(played.back().getVelocity() == 127, "Accent plays every pad at full velocity");
                pad(1, 0);
                press(push::cc::accent);
                controller->receive(juce::MidiMessage::pitchWheel(1, 9000));
                require(played.back().isPitchWheel() && played.back().getPitchWheelValue() == 9000,
                        "The touch strip bends the pitch");
                for (const auto& message : played)
                    require(!message.isController(), "Nothing a pad does is a controller message");
            }

            scenario("push: a Drum Rack's bank on the bottom-left four by four");
            {
                require(session.addTrack(Session::TrackType::midi).wasOk(), "A track for the drums");
                const auto drumTrack = session.firstReturnTrack() - 1;
                require(session.addDevice("Drums", drumTrack).wasOk(), "with a Drum Rack on it");
                int drums = -1;
                for (const auto& slot : session.deviceSlots(drumTrack))
                    if (slot.deviceId == "Drums")
                        drums = slot.pluginIndex;
                auto* rack = dynamic_cast<DrumRackDevice*>(session.devicePlugin(drumTrack, drums));
                require(rack != nullptr, "The rack is there to read");
                controller->setSelectedTrack(drumTrack);
                controller->flush();
                played.clear();
                pad(0, 100);
                pad(0, 0);
                require(!played.empty() && played.front().getNoteNumber() == rack->firstShownNote(),
                        "The bottom-left pad is the first pad of the bank the rack shows");
                const auto first = rack->firstShownNote();
                press(push::cc::octaveUp);
                require(rack->firstShownNote() == std::min(first + DrumRackDevice::bankSize, DrumRackDevice::lastFirstNote),
                        "Octave up moves the rack's own bank");
                pad(27, 100);
                pad(27, 0);
                require(played.back().getNoteNumber() == rack->firstShownNote() + 15, "and the top-right pad follows it");
                played.clear();
                pad(4, 100);
                require(played.empty(), "A pad outside the four by four plays nothing");
                pad(4, 0);
                juce::ignoreUnused(session.showDrumBank(drumTrack, drums, first));
                controller->setSelectedTrack(pushTrack);
                controller->flush();
                controller.reset();
                require(session.removeAudioTrack(drumTrack).wasOk(), "The drum track goes");
            }
            require(session.removeAudioTrack(pushTrack).wasOk(), "The Push's track goes");
        }
