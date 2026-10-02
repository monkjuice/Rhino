        // Recording, as far as it can be checked without a microphone plugged
        // in. What a track records is decided by what the track already is, the
        // arm flag lives on the track, and the count-in is a metronome setting
        // saved with the project: all three are answerable here. Whether an
        // input actually arrives is the one part that needs hardware, and it is
        // deliberately not asserted - setTrackArmed reports that separately
        // from whether the track could be armed at all.
        //
        // Self-contained: it borrows the stack, and restores the document it
        // found at the end, so the scenarios below inherit what they would
        // have inherited anyway.
        {
            view.sync();
            view.resized();
            const auto entryState = session.projectSnapshot();
            juce::TemporaryFile entryProject(".rhinoedit");

            scenario("recording: what a track records follows what it is");
            require(session.trackHasInstrument(0), "The pattern track runs an instrument");
            require(session.trackRecordInput(0) == Session::RecordInput::midi,
                    "so arming it would record MIDI");
            require(session.addAudioTrack().wasOk(), "A track with no instrument can be added");
            const auto audioTrack = session.trackCount() - 1;
            require(session.trackRecordInput(audioTrack) == Session::RecordInput::audio,
                    "and a track without an instrument records audio");
            require(session.trackRecordInput(session.masterTrackIndex()) == Session::RecordInput::none,
                    "The main row records nothing");
            require(session.trackRecordInput(-1) == Session::RecordInput::none,
                    "and neither does a track that does not exist");

            scenario("recording: arming is a property of the track");
            require(!session.anyTrackArmed(), "Nothing is armed to begin with");
            // Whether an input turns up depends on the machine, so which way
            // it went is not asserted - only that arming answers one way or
            // the other rather than failing silently. That the track took the
            // arm does not depend on the machine, so that is checked outright.
            const auto armed = session.setTrackArmed(audioTrack, true);
            require(armed.wasOk() || armed.getErrorMessage().isNotEmpty(),
                    "Arming says whether the input behind the track is there");
            require(session.isTrackArmed(audioTrack), "The track takes the arm");
            require(session.anyTrackArmed(), "and the session reports one armed track");
            require(session.setTrackArmed(session.masterTrackIndex(), true).failed(),
                    "The main row refuses to be armed");
            require(!session.isTrackArmed(session.masterTrackIndex()), "and stays unarmed");

            scenario("recording: a group bus records nothing of its own");
            require(session.groupTracks({audioTrack}, "Record bus").wasOk(), "The armed track can be grouped");
            const auto bus = session.trackGroups().back().busTrack;
            require(session.isGroupBusTrack(bus), "which gives it a bus");
            require(session.trackRecordInput(bus) == Session::RecordInput::none, "A bus records nothing");
            require(session.setTrackArmed(bus, true).failed(), "so it refuses to be armed");
            require(!session.isTrackArmed(bus), "and stays unarmed");
            // The arm travelled with the track when the group carried it.
            const auto member = session.trackGroups().back().firstTrack;
            require(session.isTrackArmed(member), "The member kept the arm it was given");

            scenario("recording: the card offers a dot only where there is one to press");
            view.sync();
            view.resized();
            require(view.arm.size() == static_cast<size_t>(session.trackCount()),
                    "Every card has its own record dot");
            require(view.arm[static_cast<size_t>(member)]->getToggleState(),
                    "The armed card shows it");
            require(!view.arm[static_cast<size_t>(bus)]->isVisible(),
                    "A bus is given no dot rather than one that refuses");
            // A card inside a group is pushed right, controls and all, so the
            // column it has to stay inside moves with it.
            require(view.arm[static_cast<size_t>(member)]->getRight()
                        <= static_cast<int>(view.trackIndent(member) + view.cardControlsWidth),
                    "and the dot stays in the controls column with the rest");

            scenario("recording: pressing record with nothing armed says so");
            juce::ignoreUnused(session.setTrackArmed(member, false));
            require(!session.isTrackArmed(member) && !session.anyTrackArmed(), "Nothing is armed");
            require(session.toggleRecording().failed(), "and recording refuses rather than rolling silently");
            require(!session.isRecording() && !session.isCountingIn(), "Nothing started");

            scenario("recording: monitoring belongs to the track, and defaults to its kind");
            // Monitoring an armed audio track through the machine's own
            // speakers is a feedback loop, so an audio track is off until it
            // is asked for. A MIDI input cannot feed back and an armed
            // instrument track you could not hear would be useless, so a MIDI
            // track follows the arm instead.
            require(session.trackRecordInput(0) == Session::RecordInput::midi
                        && session.trackRecordInput(member) == Session::RecordInput::audio,
                    "One track of each kind, so both defaults can be read off at once");
            require(session.trackMonitoring(member) == Session::InputMonitoring::off,
                    "An audio track starts off");
            require(session.trackMonitoring(0) == Session::InputMonitoring::automatic,
                    "while a MIDI track starts on Auto");
            require(session.trackMonitoring(session.masterTrackIndex()) == Session::InputMonitoring::off,
                    "and the main row, which takes no input, answers off rather than guessing");
            require(Session::inputMonitoringName(Session::InputMonitoring::on) == "On"
                        && Session::inputMonitoringName(Session::InputMonitoring::automatic) == "Auto"
                        && Session::inputMonitoringName(Session::InputMonitoring::off) == "Off",
                    "The three are named the way the card spells them");

            scenario("recording: each track answers for itself");
            juce::ignoreUnused(session.setTrackMonitoring(member, Session::InputMonitoring::on));
            require(session.trackMonitoring(member) == Session::InputMonitoring::on,
                    "One track can be told to monitor at all times");
            require(session.trackMonitoring(0) == Session::InputMonitoring::automatic,
                    "without moving the one beside it");
            juce::ignoreUnused(session.setTrackMonitoring(0, Session::InputMonitoring::off));
            require(session.trackMonitoring(0) == Session::InputMonitoring::off,
                    "and a MIDI track can be taken off its default too");
            require(session.setTrackMonitoring(bus, Session::InputMonitoring::on).failed(),
                    "A group bus has no input of its own, so it refuses");
            require(session.setTrackMonitoring(session.masterTrackIndex(), Session::InputMonitoring::on).failed(),
                    "and so does the main row");

            scenario("recording: monitoring is saved with the project, on the track that holds it");
            {
                const auto monitored = session.projectSnapshot();
                juce::TemporaryFile monitoredProject(".rhinoedit");
                require(session.restoreProject(monitored, monitoredProject.getFile()).wasOk(),
                        "The project reopens");
                require(session.trackMonitoring(member) == Session::InputMonitoring::on
                            && session.trackMonitoring(0) == Session::InputMonitoring::off,
                        "with both answers back on the tracks that held them");
            }
            juce::ignoreUnused(session.setTrackMonitoring(member, Session::InputMonitoring::off));
            juce::ignoreUnused(session.setTrackMonitoring(0, Session::InputMonitoring::automatic));
            require(session.trackMonitoring(member) == Session::InputMonitoring::off
                        && session.trackMonitoring(0) == Session::InputMonitoring::automatic,
                    "and both go back to their defaults");

            scenario("recording: a track names the audio input it records from");
            require(session.trackAudioInput(member) == Session::audioInputDefaultToken(),
                    "A new audio track takes the default input");
            require(session.trackAudioInputName(member) == "Default In", "which is what its card says");
            {
                const auto audioChoices = session.audioInputChoices();
                require(audioChoices.size() >= 2, "The default and None are always offered");
                require(audioChoices.front().token == Session::audioInputDefaultToken(),
                        "The default comes first");
                require(audioChoices.back().token == Session::audioInputNoneToken(), "and None last");
                require(std::all_of(audioChoices.begin() + 1, audioChoices.end() - 1,
                                    [](const auto& choice) { return choice.token.startsWith("device:"); }),
                        "with the machine's own inputs between them");
            }
            require(session.setTrackAudioInput(member, Session::audioInputNoneToken()).wasOk(),
                    "A track can be told to record from nothing");
            require(session.trackAudioInputName(member) == "None", "and says so");
            require(session.setTrackAudioInput(0, Session::audioInputNoneToken()).failed(),
                    "A MIDI track has no audio input to choose");
            require(session.trackAudioInput(0) == Session::audioInputDefaultToken(),
                    "and is left alone by the refusal");
            require(session.setTrackAudioInput(bus, Session::audioInputNoneToken()).failed(),
                    "and neither has a group bus");
            {
                const auto routed = session.projectSnapshot();
                juce::TemporaryFile routedProject(".rhinoedit");
                require(session.restoreProject(routed, routedProject.getFile()).wasOk(), "The project reopens");
                require(session.trackAudioInputName(member) == "None",
                        "with the track still recording from nothing");
            }
            require(session.setTrackAudioInput(member, Session::audioInputDefaultToken()).wasOk(),
                    "and it can be put back on the default");
            require(session.trackAudioInputName(member) == "Default In", "which is the default again");

            scenario("recording: the count-in is one to four bars, or off");
            session.setCountInBars(2);
            require(session.countInBars() == 2, "Two bars can be chosen");
            session.setCountInBars(Session::maximumCountInBars + 3);
            require(session.countInBars() == Session::maximumCountInBars,
                    "and anything longer is clamped to the longest");
            session.setCountInBars(-1);
            require(session.countInBars() == 0, "while below zero is off");
            require(session.countInBarsRemaining() == 0, "Nothing is counting");

            scenario("recording: arming and the count-in are saved with the project");
            session.setCountInBars(3);
            juce::ignoreUnused(session.setTrackArmed(member, true));
            require(session.isTrackArmed(member) && session.countInBars() == 3, "Both are set");
            const auto saved = session.projectSnapshot();
            juce::TemporaryFile savedProject(".rhinoedit");
            require(session.restoreProject(saved, savedProject.getFile()).wasOk(), "The project reopens");
            require(session.countInBars() == 3, "The count-in comes back with it");
            require(session.isTrackArmed(member), "and so does the arm, on the track that held it");
            require(session.recordingDirectory().getParentDirectory() == savedProject.getFile().getParentDirectory(),
                    "Recordings are written beside the project once it has one");

            scenario("recording: two armed tracks share the input rather than taking it from each other");
            require(session.addAudioTrack().wasOk(), "One audio track for the pair");
            const auto pairFirst = session.trackCount() - 1;
            require(session.addAudioTrack().wasOk(), "and a second beside it");
            const auto pairSecond = session.trackCount() - 1;
            juce::ignoreUnused(session.setTrackArmed(pairFirst, true));
            juce::ignoreUnused(session.setTrackArmed(pairSecond, true));
            require(session.isTrackArmed(pairFirst) && session.isTrackArmed(pairSecond),
                    "Both take the arm");
            // Two tracks on one input cannot hold two monitor modes at once -
            // the engine carries the mode on the device - so the device takes
            // the strongest either of them asked for. Neither track's own
            // setting is rewritten by that: the cards keep saying what they
            // were told, which is what makes the rule explainable.
            juce::ignoreUnused(session.setTrackMonitoring(pairFirst, Session::InputMonitoring::on));
            require(session.trackMonitoring(pairFirst) == Session::InputMonitoring::on
                        && session.trackMonitoring(pairSecond) == Session::InputMonitoring::off,
                    "One of a pair sharing an input can monitor without moving the other's setting");
            juce::ignoreUnused(session.setTrackMonitoring(pairFirst, Session::InputMonitoring::off));
            // Both audio tracks record from the one input, so both have to be
            // destinations of it at once. The engine's setTarget can either add
            // a destination or move the input onto one, clearing the rest; the
            // second reading would leave only the track armed last recording
            // anything. Whether there is an input to be a destination of is the
            // machine's business, so this is checked only where one turned up.
            if (auto* context = session.edit->getCurrentPlaybackContext())
            {
                const auto armedTracks = te::getAudioTracks(*session.edit);
                for (auto* input : context->getAllInputs())
                {
                    if (input == nullptr || dynamic_cast<te::WaveInputDevice*>(&input->getInputDevice()) == nullptr)
                        continue;
                    const auto targets = input->getTargets();
                    if (targets.isEmpty())
                        continue;
                    require(targets.contains(armedTracks[pairFirst]->itemID)
                                && targets.contains(armedTracks[pairSecond]->itemID),
                            "The audio input records into both armed tracks, not only the one armed last");
                }
            }

            scenario("recording: monitoring at all times routes the input without arming anything");
            // On means you hear the input whether or not a take would capture
            // it, so the track needs the input routed to it while unarmed -
            // and still must not record when the transport rolls.
            for (int track = 0; track < session.trackCount(); ++track)
                juce::ignoreUnused(session.setTrackArmed(track, false));
            require(!session.anyTrackArmed(), "Nothing is left armed");
            juce::ignoreUnused(session.setTrackMonitoring(pairFirst, Session::InputMonitoring::on));
            require(!session.anyTrackArmed(), "Monitoring does not arm the track");
            require(session.toggleRecording().failed(),
                    "and recording still refuses, because monitoring is not arming");
            juce::ignoreUnused(session.setTrackMonitoring(pairFirst, Session::InputMonitoring::off));

            scenario("recording: putting the session back");
            require(session.restoreProject(entryState, entryProject.getFile()).wasOk(),
                    "The stack this scenario borrowed is put back for the ones below");
            require(!session.anyTrackArmed(), "with nothing left armed");
            require(session.countInBars() == 0, "and no count-in");
            view.sync();
            view.resized();
        }
