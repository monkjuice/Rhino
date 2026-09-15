        }
        auto& sequence = session.pattern().getSequence();
        require(sequence.getNumNotes() == 0, "New pattern must be empty");
        session.beginNoteGesture();
        session.setNote(0, 48, true);
        session.setNote(4, 55, true);
        session.setNote(4, 55, true);
        session.setNote(16, 48, true);
        session.setNote(0, 128, true);
        session.endNoteGesture();
        require(sequence.getNumNotes() == 2, "Idempotent draw and grid bounds");
        session.undo();
        require(sequence.getNumNotes() == 0, "One gesture must undo together");
        session.redo();
        require(sequence.getNumNotes() == 2, "Redo restores the gesture");
        session.setEditorStepCount(32);
        session.beginNoteGesture("Draw 32-step note");
        session.setNote(1, 50, true);
        session.setNote(32, 50, true);
        session.endNoteGesture();
        require(session.editorStepCount() == 32 && session.hasNote(1, 50), "Editor can draw notes on a 32-step grid");
        auto* thirtySecondNote = static_cast<te::MidiNote*>(nullptr);
        for (auto* note : sequence.getNotes())
            if (note->getNoteNumber() == 50)
                thirtySecondNote = note;
        require(thirtySecondNote != nullptr && std::abs(thirtySecondNote->getStartBeat().inBeats() - 0.125) < 0.0001,
                "32-step grid places notes halfway between 16th steps");
        const auto originalGridClip = session.pattern().itemID;
        require(session.insertPatternPreset(Session::PatternPreset::HouseKit, 0, 2.0).wasOk(),
                "Can add another MIDI clip while testing clip-local grid resolution");
        const auto newGridClip = te::getAudioTracks(*session.edit)[0]->getClips().getLast()->itemID;
        require(session.selectPatternClip(newGridClip).wasOk() && session.editorStepCount() == Session::defaultSteps,
                "Newly selected MIDI clips keep the default editor grid");
        require(session.selectPatternClip(originalGridClip).wasOk() && session.editorStepCount() == 32,
                "Switching back restores the source clip's editor grid");
        session.undo();
        require(session.selectPatternClip(originalGridClip).wasOk(), "Original grid clip remains editable after undoing the extra clip");
        // The track's instrument is replaced when it changes, so these read
        // whichever 4OSC is on the pattern track right now.
        const auto patternSynth = [&session]
        {
            auto* plugin = dynamic_cast<te::FourOscPlugin*>(session.patternInstrument());
            if (plugin == nullptr) throw std::runtime_error("Pattern track is not running 4OSC");
            return plugin;
        };
        session.setEditorStepCount(Session::defaultSteps);
        session.applyPatternPreset(Session::PatternPreset::HouseKit);
        require(sequence.getNumNotes() == 10, "Drum kit preset loads notes");
        require(session.isPatternDrums(), "Drum kit preset switches the track to the drum instrument");
        require(dynamic_cast<te::FourOscPlugin*>(session.patternInstrument()) == nullptr,
                "Switching instrument replaces the previous one rather than leaving it dormant");
        session.panicReset(false);
        require(!session.edit->getTransport().isPlaying(), "Panic reset stops playback");
        session.undo();
        require(sequence.getNumNotes() == 3, "Undo restores notes before preset load");
        require(!session.isPatternDrums(), "Undo restores the synth instrument");

        session.redo();
        require(sequence.getNumNotes() == 10, "Redo restores preset load");
        require(session.isPatternDrums(), "Redo restores the drum instrument");
        session.clearPattern();
        require(sequence.getNumNotes() == 0, "Clear notes");
        session.undo();
        require(sequence.getNumNotes() == 10, "Undo clear");

        // A clip is written in beats and stored in seconds, so a tempo change
        // has to rewrite the seconds and leave the beats alone. Whatever the
        // clip happens to be worth here, it must come out the far side worth
        // the same: it used to be snapped back to a single bar instead.
        const auto beatsBeforeTempo = session.patternLengthBeats();
        session.setTempo(90.0);
        require(std::abs(session.patternLengthBeats() - beatsBeforeTempo) < 0.0001,
                "A tempo change keeps every beat of the clip");
        require(std::abs(session.pattern().getPosition().time.getLength().inSeconds()
                             - beatsBeforeTempo * 60.0 / 90.0) < 0.0001,
                "The clip's seconds follow the new tempo");
        require(session.hasNote(4, 53), "Tempo change preserves note beats");
        session.undo();
        require(std::abs(session.tempo() - 120.0) < 0.001, "Undo tempo");
        session.redo();
        require(std::abs(session.tempo() - 90.0) < 0.001, "Redo tempo");
        session.applyPatternPreset(Session::PatternPreset::ClapKit);
        require(session.isPatternDrums() && session.hasNote(4, 56) && session.hasNote(12, 56),
                "Clap kit preset loads clap notes into the drum editor");
        require(session.addAudioTrack().wasOk(), "Can add a target track for moved MIDI");
        const auto drumClipID = session.pattern().itemID;
        require(session.duplicateClip(drumClipID).wasOk(), "Can duplicate a drum MIDI clip");
        auto* drumDuplicate = te::getAudioTracks(*session.edit)[0]->getClips().getLast();
        const auto drumDuplicateID = drumDuplicate->itemID;
        const auto duplicateLength = drumDuplicate->getPosition().time.getLength().inSeconds();
        require(session.editClip(drumDuplicateID, {2.0, 2.0 + duplicateLength, drumDuplicate->getPosition().offset.inSeconds()},
                                 ClipGesture::move, 2).wasOk(),
                "Can move a duplicated drum MIDI clip to another track");
        const auto movedDrumSlots = session.deviceSlots(2);
        auto movedDrumsEnabled = false, movedSynthDisabled = true, movedWaveDisabled = true;
        for (const auto& slot : movedDrumSlots)
        {
            if (slot.type == DrumDevice::xmlTypeName) movedDrumsEnabled = slot.enabled;
            if (slot.type == te::FourOscPlugin::xmlTypeName) movedSynthDisabled = !slot.enabled;
            if (slot.type == ThetaWaveDevice::xmlTypeName) movedWaveDisabled = !slot.enabled;
        }
        require(movedDrumsEnabled && movedSynthDisabled && movedWaveDisabled,
                "Moving duplicated drum MIDI to another track enables the drum instrument there");
        session.undo();
        session.undo();
        session.undo();
        session.applyPatternPreset(Session::PatternPreset::ArpRun);
        require(!session.isPatternDrums() && session.hasNote(0, 48) && session.hasNote(0, 60),
                "Arp run preset loads a held synth chord");
        session.applyPatternPreset(Session::PatternPreset::ChordPad);
        require(!session.isPatternDrums() && session.hasNote(0, 48) && session.hasNote(0, 64) && session.hasNote(8, 65),
                "Chord pad preset loads sustained synth chords");
        require(patternSynth()->ampRelease->getCurrentValue() > 0.5f && patternSynth()->ampRelease->getCurrentValue() < 1.2f
                && patternSynth()->chorusMix->getCurrentValue() > 0.2f && patternSynth()->reverbMix->getCurrentValue() < 0.15f,
                "Chord pad preset shapes the 4OSC patch for controlled sustain");
        session.applyPatternPreset(Session::PatternPreset::SubBass);
        require(!session.isPatternDrums() && session.hasNote(0, 36) && session.hasNote(12, 34),
                "Sub bass preset loads a low mono bass line");
        require(patternSynth()->ampRelease->getCurrentValue() < 0.4f && patternSynth()->chorusMix->getCurrentValue() == 0.0f
                && static_cast<int>(patternSynth()->state.getProperty("voices")) == 1,
                "Sub bass preset shapes the 4OSC patch for clean mono low end");
        auto synthMacros = session.deviceParameters(0, 0);
        require(synthMacros.size() == 6 && synthMacros[0].name == "Attack" && synthMacros[1].name == "Decay"
                && synthMacros[2].name == "Sustain" && synthMacros[3].name == "Release",
                "4OSC rack exposes musical envelope macros first");
        require(synthMacros[0].minimum == 0.001f && synthMacros[0].maximum == 6.0f,
                "4OSC Attack rack range is 1 to 6000 ms");
        require(session.setDeviceParameter(0, 0, 0, 60.0f).wasOk()
                    && patternSynth()->ampAttack->getCurrentValue() == 6.0f,
                "4OSC Attack rack edits are capped at 6000 ms");
        require(session.setDeviceParameter(0, 0, 0, 1.0f).wasOk()
                    && patternSynth()->ampAttack->getCurrentValue() == 1.0f,
                "4OSC Attack leaves the cap when set lower");
        const auto previousAttack = patternSynth()->ampAttack->getCurrentValue();
        require(session.setDeviceParameter(0, 0, 0, previousAttack + 0.05f).wasOk(), "4OSC Attack macro can be edited from the rack");
        require(patternSynth()->ampAttack->getCurrentValue() > previousAttack, "4OSC Attack macro writes to the synth envelope");
        const auto manualAttack = patternSynth()->ampAttack->getCurrentValue();
        const auto attackMinimum = synthMacros[0].minimum;
        const auto attackMaximum = std::min(synthMacros[0].maximum, attackMinimum + 1.0f);
        require(session.trackAutomations(0).empty(), "A track starts with no automation lanes");
        require(session.showTrackAutomation({0, 0, 0}, false).wasOk(),
                "Right-clicking a knob can reveal an automation lane");
        require(session.trackAutomations(0).size() == 1 && !session.trackAutomations(0).front().active(),
                "A revealed lane exists but drives nothing until it is drawn");
        require(session.trackAutomationState({0, 0, 0}).visible && !session.trackAutomationState({0, 0, 0}).ownLane
                && !session.trackAutomationState({0, 0, 0}).active,
                "A revealed lane reports itself as shown on the track and inactive");
        require(std::abs(session.trackAutomations(0).front().restingValue - manualAttack) < 0.02f,
                "An undrawn lane rests at the knob's current value");
        require(!session.deviceParameters(0, 0)[0].automated,
                "Revealing a lane does not make the parameter automated");
        require(session.setTrackAutomationPoints({0, 0, 0}, {{0.0, attackMinimum}, {1.0, attackMaximum}}).wasOk(),
                "An automation lane stores a curve spanning the timeline");
        require(session.trackAutomations(0).front().active() && session.trackAutomations(0).front().points.size() == 2,
                "A drawn lane becomes active");
        const auto decayMinimum = synthMacros[1].minimum;
        const auto decayMaximum = std::min(synthMacros[1].maximum, decayMinimum + 1.0f);
        require(session.showTrackAutomation({0, 0, 1}, true).wasOk()
                && session.setTrackAutomationPoints({0, 0, 1}, {{0.0, decayMinimum}, {4.0, decayMaximum}}).wasOk(),
                "A track can automate a second parameter on its own lane");
        require(session.trackAutomationState({0, 0, 1}).ownLane,
                "\"Show automation on new lane\" is remembered per lane");
        require(session.setTrackAutomationPoints({0, 0, 2}, {{0.0, synthMacros[2].minimum},
                                                             {2.0, synthMacros[2].maximum}}).wasOk(),
                "Drawing a curve reveals a lane that was never shown");
        require(session.trackAutomations(0).size() == 3, "A track holds several automation lanes at once");
        session.beginNoteGesture("Draw note after automation");
        session.setNote(15, 80, true);
        session.endNoteGesture();
        require(session.hasNote(15, 80), "Notes can be drawn after automation");
        session.undo();
        require(!session.hasNote(15, 80) && session.trackAutomations(0).size() == 3,
                "Undo after automation removes the new note without deleting automation");
        synthMacros = session.deviceParameters(0, 0);
        require(synthMacros[0].automated && synthMacros[1].automated && synthMacros[2].automated
                && !synthMacros[0].automationOverridden && !synthMacros[1].automationOverridden && !synthMacros[2].automationOverridden,
                "Automated parameters report following state to device panels");
        session.applyTrackAutomationAt(0.5);
        require(std::abs(patternSynth()->ampAttack->getCurrentValue() - (attackMinimum + attackMaximum) * 0.5f) < 0.02f,
                "Track automation applies interpolated parameter values during playback");
        require(std::abs(patternSynth()->ampDecay->getCurrentValue()
                         - (decayMinimum + (decayMaximum - decayMinimum) * 0.125f)) < 0.02f,
                "Multiple automation lanes apply together");
        session.applyTrackAutomationAt(1.5);
        require(std::abs(patternSynth()->ampAttack->getCurrentValue() - attackMaximum) < 0.02f,
                "Automation holds its last value past the end of its curve");
        session.applyTrackAutomationAt(0.5);
        const auto overrideAttack = juce::jlimit(attackMinimum, attackMaximum, attackMinimum + 0.2f);
        require(session.setDeviceParameter(0, 0, 0, overrideAttack).wasOk(),
                "Manual parameter edits can override active automation");
        session.applyTrackAutomationAt(0.75);
        require(std::abs(patternSynth()->ampAttack->getCurrentValue() - overrideAttack) < 0.02f,
                "Manual override wins over automation until automation is re-enabled");
        synthMacros = session.deviceParameters(0, 0);
        require(synthMacros[0].automated && synthMacros[0].automationOverridden,
                "Device panels can show manual automation override state");
        require(session.toggleParameterAutomationOverride(0, 0, 0).wasOk(),
                "Automation indicators can return a parameter to automation");
        session.applyTrackAutomationAt(0.75);
        require(std::abs(patternSynth()->ampAttack->getCurrentValue()
                         - (attackMinimum + (attackMaximum - attackMinimum) * 0.75f)) < 0.02f,
                "Re-enabled automation drives the parameter again");
        const auto sweptAttack = patternSynth()->ampAttack->getCurrentValue();
        session.beginOfflineAutomation();
        require(patternSynth()->ampAttack->getCurve().getNumPoints() == 2
                && patternSynth()->ampDecay->getCurve().getNumPoints() == 2,
                "An export mirrors every active lane into the engine's own automation curves");
        require(std::abs(patternSynth()->ampAttack->getCurve().getValueAt(
                             tracktion::core::TimePosition::fromSeconds(0.75), 0.0f)
                         - (attackMinimum + (attackMaximum - attackMinimum) * 0.75f)) < 0.02f,
                "A mirrored curve carries the lane's shape, so the render hears the sweep");
        session.endOfflineAutomation();
        require(patternSynth()->ampAttack->getCurve().getNumPoints() == 0
                && patternSynth()->ampDecay->getCurve().getNumPoints() == 0,
                "Finishing an export leaves the engine curves as it found them");
        require(std::abs(patternSynth()->ampAttack->getCurrentValue() - sweptAttack) < 0.0001f,
                "Finishing an export hands the parameters back to the playback sweep");
        require(session.clearTrackAutomationPoints({0, 0, 1}).wasOk(),
                "A curve can be deleted without losing its lane");
        require(session.trackAutomations(0).size() == 3 && !session.trackAutomations(0)[1].active()
                && session.deviceParameters(0, 0)[0].automated && !session.deviceParameters(0, 0)[1].automated
                && session.deviceParameters(0, 0)[2].automated,
                "Deleting one curve leaves its lane shown and the other lanes driving");
        require(session.hideTrackAutomation({0, 0, 1}).wasOk() && session.trackAutomations(0).size() == 2,
                "A lane can be hidden again from the knob menu");
        session.applyPatternPreset(Session::PatternPreset::ReeseBass);
        require(!session.isPatternDrums() && session.hasNote(0, 36) && session.hasNote(8, 39) && session.hasNote(12, 41),
                "Reese bass preset loads sustained electronic bass notes");
        require(session.noteLengthSteps(0, 36) == 8 && session.resizeNote(8, 39, 2).wasOk()
                && session.noteLengthSteps(8, 39) == 2,
                "Pattern notes expose and edit musical step lengths");
        require(session.resizeNote(8, 39, 0.5).wasOk(), "Half-grid note resize succeeds");
        auto halfGridLengthIsStored = false;
        for (auto* note : session.pattern().getSequence().getNotes())
            halfGridLengthIsStored = halfGridLengthIsStored || (note->getNoteNumber() == 39
                && std::abs(note->getStartBeat().inBeats() - 2.0) < 0.0001
                && std::abs(note->getLengthBeats().inBeats() - 0.125) < 0.0001);
        require(halfGridLengthIsStored, "Pattern notes support half-grid lengths");
        const auto longClipStart = session.pattern().getPosition().time.getStart().inSeconds();
        require(session.editClip(session.pattern().itemID, {longClipStart, longClipStart + 4.0, 0.0}, ClipGesture::trimRight).wasOk(),
                "MIDI clips can be extended to multi-bar parts");
        require(session.editorStepCount() == 24,
                "A one-and-a-half-bar MIDI clip exposes its exact sixteenth-note editor cells");
        session.setNote(3, 55, true);
        require(session.hasNote(3, 55) && !session.hasNote(7, 55),
                "Drawing into an extended MIDI clip keeps the clicked editor step");
        require(session.editClip(session.pattern().itemID, {longClipStart, longClipStart + 30.0, 0.0}, ClipGesture::trimRight).wasOk(),
                "MIDI clips can be extended to long sustained parts");
        require(session.pattern().getPosition().time.getLength().inSeconds() > 29.9,
                "Extended MIDI clip keeps the requested duration");
        require(session.fillNoteToClipEnd(0, 36).wasOk()
                && session.noteLengthSteps(0, 36) == juce::roundToInt(session.patternLengthBeats()
                                                                       * session.editorStepResolution() / 4.0),
                "A MIDI note can sustain to the end of an extended clip");
        require(session.patternLengthBeats() > 44.9,
                "Selected MIDI clip exposes extended length for fill operations");
        require(patternSynth()->chorusMix->getCurrentValue() > 0.1f && patternSynth()->oscParams[0]->detune->getCurrentValue() > 0.05f
                && static_cast<int>(patternSynth()->state.getProperty("voices")) == 1,
                "Reese bass preset shapes the 4OSC patch for detuned bass movement");
        session.applyPatternPreset(Session::PatternPreset::SirenLead);
        require(!session.isPatternDrums() && session.hasNote(4, 72) && session.hasNote(10, 48),
                "Siren lead preset loads a rising and falling synth line");
        session.applyPatternPreset(Session::PatternPreset::WavePad);
        require(session.patternInstrumentKind() == Session::Instrument::ThetaWave,
                "Wave pad preset enables Theta Wave");
        require(!session.isPatternDrums() && session.hasNote(0, 48) && session.hasNote(8, 62),
                "Wave pad preset loads sustained wavetable chords");
        session.applyPatternPreset(Session::PatternPreset::WaveBass);
        require(session.patternInstrumentKind() == Session::Instrument::ThetaWave && session.hasNote(0, 36) && session.hasNote(12, 39),
                "Wave bass preset keeps Theta Wave active with low notes");
        session.applyPatternPreset(Session::PatternPreset::WavePluck);
        require(session.patternInstrumentKind() == Session::Instrument::ThetaWave && session.hasNote(0, 60) && session.hasNote(12, 74),
                "Wave pluck preset keeps Theta Wave active with bright notes");

        // Swapping a track's instrument is a device change, not a clip change:
        // the pattern stays put and the instrument under it is replaced. This
        // runs last and leaves the track on the instrument it found.
        {
            const auto notesBeforeSwap = session.pattern().getSequence().getNumNotes();
            require(notesBeforeSwap > 0, "The pattern has notes to preserve");
            require(session.addInstrument(Session::Instrument::FourOsc, 0).wasOk(),
                    "The pattern track accepts a different instrument");
            require(session.patternInstrumentKind() == Session::Instrument::FourOsc,
                    "Dropping an instrument switches the track to it");
            const auto patternInstruments = [&session]
            {
                int count = 0;
                for (const auto& slot : session.deviceSlots(0))
                    if (slot.kind == Session::DeviceKind::Instrument) ++count;
                return count;
            };
            require(patternInstruments() == 1,
                    "The instrument it replaced is gone rather than left dormant");
            require(session.pattern().getSequence().getNumNotes() == notesBeforeSwap,
                    "Switching instrument leaves the pattern notes untouched");
            require(session.addInstrument(Session::Instrument::ThetaWave, 0).wasOk(),
                    "The track accepts the previous instrument back");
            require(session.patternInstrumentKind() == Session::Instrument::ThetaWave,
                    "Switching back restores Theta Wave");
            require(session.pattern().getSequence().getNumNotes() == notesBeforeSwap,
                    "Notes survive switching back as well");
        }
