        // No track is special for being first. Where the note editor falls back
        // to, what a track plays, and whether a document reopens all follow the
        // tracks themselves, wherever the stack has put them. A document of its
        // own, so nothing here disturbs the scenarios that share `session`.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            const auto midiClipsOnAudioTracks = [&stack]
            {
                auto count = 0;
                const auto tracks = te::getAudioTracks(*stack.edit);
                for (int track = 0; track < tracks.size(); ++track)
                    if (stack.trackType(track) == Session::TrackType::audio)
                        for (auto* clip : tracks[track]->getClips())
                            if (dynamic_cast<te::MidiClip*>(clip) != nullptr)
                                ++count;
                return count;
            };

            // Deleting the first track takes the editor's clip with it. The
            // editor moves to the next MIDI track rather than planting a MIDI
            // clip on the audio track that is now first.
            require(stack.removeAudioTrack(0).wasOk(), "The first track can be removed");
            require(stack.trackType(0) == Session::TrackType::audio, "An audio track is now first");
            require(midiClipsOnAudioTracks() == 0, "Removing the first track puts no MIDI clip on an audio track");
            require(stack.hasPatternClip() && stack.pattern().getClipTrack() == te::getAudioTracks(*stack.edit)[1],
                    "The note editor moves to the first MIDI track");

            // A track's instrument is its own. Reordered so that a track with no
            // clip is first, the stack still saves and reopens, and every track
            // comes back playing what it played.
            require(stack.addTrack(Session::TrackType::midi).wasOk(), "A MIDI track can be added");
            require(stack.addInstrument(Session::Instrument::Drums, 1).wasOk()
                    && stack.addInstrument(Session::Instrument::FourOsc, 3).wasOk(),
                    "Two MIDI tracks take two different instruments");
            require(stack.moveTrack(3, 0).wasOk(), "The new MIDI track moves to the top");
            juce::TemporaryFile reordered(".rhinoedit");
            require(stack.restoreProject(stack.projectSnapshot(), reordered.getFile()).wasOk(),
                    "A stack whose first track holds no clip still reopens");
            require(stack.patternInstrumentForTrack(0) != nullptr
                    && stack.patternInstrumentForTrack(0)->getPluginType() == te::FourOscPlugin::xmlTypeName
                    && stack.patternInstrumentForTrack(2) != nullptr
                    && stack.patternInstrumentForTrack(2)->getPluginType() == DrumDevice::xmlTypeName,
                    "Reopening gives every track back its own instrument, whichever is first");
            require(midiClipsOnAudioTracks() == 0, "Reopening puts no MIDI clip on an audio track");

            // The first track's instrument is a device like any other: shown,
            // and taken off when asked.
            const auto firstSlots = stack.deviceSlots(0);
            require(firstSlots.size() == 1 && firstSlots.front().removable,
                    "The first track's instrument is shown as removable");
            require(stack.deleteDevice(0, firstSlots.front().pluginIndex).wasOk(),
                    "and can be taken off like any other track's");

            // A document with no MIDI track has nothing for the note editor to
            // open, and every note edit says so rather than reaching for it.
            require(stack.removeAudioTrack(2).wasOk() && stack.removeAudioTrack(0).wasOk(),
                    "Both MIDI tracks can be removed");
            require(stack.trackType(0) == Session::TrackType::audio && stack.trackType(1) == Session::TrackType::audio,
                    "Only audio tracks are left");
            require(!stack.hasPatternClip(), "With no MIDI track the note editor has no clip");
            require(midiClipsOnAudioTracks() == 0, "and no MIDI clip was made on an audio track for it");
            require(stack.addNote(0.0, Session::lowestNote, 1.0).failed() && stack.editorNotes().empty(),
                    "Note edits refuse rather than reaching for a clip that is not there");
            {
                auto editor = std::make_unique<StepGrid>(stack);
                editor->setSize(800, 300);
                const auto picture = editor->createComponentSnapshot(editor->getLocalBounds());
                require(picture.isValid(), "The note editor still paints with no clip to show");
            }
            // A MIDI track and a clip on it give the editor something again.
            require(stack.addTrack(Session::TrackType::midi).wasOk() && stack.createClip(2, 0.0).wasOk(),
                    "A new MIDI track takes a clip");
            require(stack.hasPatternClip(), "and the note editor opens it");
            stack.releaseAudioDevice();
        }
