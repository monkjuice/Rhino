        {
            // A kit is a Drum Rack's pads in a file. Dropping one makes
            // the track's instrument a rack playing it and names the track.
            //
            // A kit is an instrument, so it goes on a MIDI lane. The lane is
            // looked up by kind rather than by index, because the scenarios
            // above this one reshape the stack and reopen the document.
            const auto kitDrop = [] (const char* kit)
            {
                return "rhino-browser:drumkit:" + ContentLibrary::file("Drums/Kits/" + juce::String(kit) + ".rdk")
                                                      .getFullPathName();
            };
            auto kitTrack = -1, kitAudioTrack = -1;
            for (int track = 0; track < session.trackCount(); ++track)
            {
                if (session.isGroupBusTrack(track)) continue;
                auto& found = session.trackType(track) == Session::TrackType::midi ? kitTrack : kitAudioTrack;
                if (found < 0) found = track;
            }
            require(kitTrack >= 0 && kitAudioTrack >= 0, "The stack holds a lane of each kind");
            require(view.applyBrowserDrop(kitDrop("Break Kit"), kitAudioTrack).failed(),
                    "An audio track refuses a drum kit, which is an instrument");
            require(view.applyBrowserDrop(kitDrop("Break Kit"), kitTrack).wasOk(),
                    "A drum kit can be dropped on a MIDI track");
            require(session.trackHasInstrument(kitTrack), "The kit puts an instrument on the track");
            require(session.trackName(kitTrack) == "Break Kit", "The track takes the kit's name");
            int instrumentCount = 0;
            juce::String instrumentType;
            for (const auto& slot : session.deviceSlots(kitTrack))
                if (slot.kind == Session::DeviceKind::Instrument)
                {
                    ++instrumentCount;
                    instrumentType = slot.type;
                }
            require(instrumentCount == 1 && instrumentType == DrumRackDevice::xmlTypeName,
                    "A kit is a Drum Rack, not a second instrument beside one");
            auto* kitDevice = dynamic_cast<DrumRackDevice*>(session.patternInstrumentForTrack(kitTrack));
            require(kitDevice != nullptr, "The track runs a Drum Rack");
            const auto breakSnare = kitDevice->pad(53).sound;
            require(breakSnare.has_value() && breakSnare->name == "Snare"
                        && std::abs(breakSnare->settings.tune + 1.07f) < 0.005f,
                    "The dropped kit fills the pads: Break Kit tunes its snare down a semitone");
            require(view.applyBrowserDrop(kitDrop("Clap Kit"), kitTrack).wasOk(), "Kits can be swapped");
            require(session.patternInstrumentForTrack(kitTrack) == kitDevice,
                    "Swapping a kit reuses the same Drum Rack");
            require(kitDevice->pad(53).sound.has_value() && kitDevice->pad(53).sound->name == "Clap",
                    "and Clap Kit puts its clap on the snare's note");
            require(session.trackName(kitTrack) == "Clap Kit", "The track is renamed to the new kit");
            const auto savedKit = session.projectSnapshot();
            juce::TemporaryFile kitProject(".rhinoedit");
            require(session.restoreProject(savedKit, kitProject.getFile()).wasOk(), "Reopen a project using a kit");
            auto* restoredKit = dynamic_cast<DrumRackDevice*>(session.patternInstrumentForTrack(kitTrack));
            require(restoredKit != nullptr && restoredKit->kit().pads[53].has_value()
                        && restoredKit->kit().pads[53]->sample.contains("HandClap"),
                    "The kit's pads survive reopen");
        }
