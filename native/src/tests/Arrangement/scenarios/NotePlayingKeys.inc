        // The keys the clip's track is playing light on the editor's keyboard:
        // whatever reaches the head of the track's chain - the clip under the
        // playhead, a keyboard played into the track, the preview. The head
        // of the chain is played by hand here, a block at a time, as the
        // song's graph plays it. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk() && stack.selectPatternClip(opened).wasOk(),
                    "A clip of its own for the keys");
            auto owned = std::make_unique<StepGrid>(stack);
            auto& grid = *owned;
            grid.setSize(1000, 300);
            grid.changeListenerCallback(nullptr);
            const auto headOf = [&stack](int track)
            {
                return dynamic_cast<DjTrackInjectorDevice*>(te::getAudioTracks(*stack.edit)[track]->pluginList[0]);
            };
            auto* head = headOf(0);
            require(head != nullptr, "The clip's track begins with its input");
            head->baseClassInitialise({{}, 48000.0, 512});
            juce::AudioBuffer<float> buffer(2, 512);
            const auto play = [&buffer](DjTrackInjectorDevice& device, std::initializer_list<juce::MidiMessage> messages,
                                        bool rendering = false)
            {
                te::MidiMessageArray midi;
                auto at = 0.001;
                for (const auto& message : messages)
                    midi.addMidiMessage(message, at += 0.001, te::MidiMessageArray::MPESourceID {});
                buffer.clear();
                device.applyToBuffer(te::PluginRenderContext(&buffer, 0, 512, &midi, 0.0, {}, false, false, rendering, false));
            };
            const auto on = [](int key) { return juce::MidiMessage::noteOn(1, key, static_cast<juce::uint8>(100)); };
            const auto off = [](int key) { return juce::MidiMessage::noteOff(1, key); };
            const auto picture = [&grid]
            {
                juce::Image image(juce::Image::RGB, grid.getWidth(), grid.getHeight(), true);
                juce::Graphics g(image);
                grid.paintEntireComponent(g, false);
                return image;
            };
            // How many pixels across the middle of a key wear the sounding
            // colour, in the keys' own column right of the names.
            const auto litAcross = [&grid](const juce::Image& image, int key)
            {
                const auto y = static_cast<int>(grid.cell(0, grid.rowOfPitch(key)).getCentreY());
                int lit = 0;
                for (int x = static_cast<int>(StepGrid::keyLabelWidth); x < static_cast<int>(StepGrid::labelWidth); ++x)
                    if (image.getPixelAt(x, y) == palette::keySounding) ++lit;
                return lit;
            };
            constexpr int white = 52, black = 54;
            require(grid.rowOfPitch(white) >= 0 && grid.rowOfPitch(white) < grid.visiblePitchRows()
                        && grid.rowOfPitch(black) >= 0 && grid.rowOfPitch(black) < grid.visiblePitchRows(),
                    "Both keys are on screen");

            grid.updateSoundingKeys();
            require(grid.soundingKeys.none() && litAcross(picture(), white) == 0, "No key is lit before anything plays");
            play(*head, {on(white)});
            grid.updateSoundingKeys();
            require(grid.soundingKeys.test(white) && grid.soundingKeys.count() == 1, "A key the track is given lights");
            const auto whiteLit = litAcross(picture(), white);
            require(whiteLit > 15 && litAcross(picture(), white + 1) == 0,
                    ("in the playhead's blue across the key, and that key alone: " + juce::String(whiteLit)).toRawUTF8());
            play(*head, {off(white)});
            grid.updateSoundingKeys();
            require(grid.soundingKeys.none() && litAcross(picture(), white) == 0, "and goes out when it is let go");

            play(*head, {on(black)});
            grid.updateSoundingKeys();
            const auto blackLit = litAcross(picture(), black);
            require(blackLit > 5 && blackLit < whiteLit, "A black key lights across its own width only");
            play(*head, {off(black)});

            play(*head, {on(57), off(57)});
            grid.updateSoundingKeys();
            require(grid.soundingKeys.test(57), "A note struck and let go inside one block still lights for a frame");
            grid.updateSoundingKeys();
            require(!grid.soundingKeys.test(57), "and for one frame only");
            play(*head, {on(59)});
            play(*head, {juce::MidiMessage::noteOn(1, 59, static_cast<juce::uint8>(0))});
            grid.updateSoundingKeys();
            grid.updateSoundingKeys();
            require(!grid.soundingKeys.test(59), "A note-on at velocity 0 lets the key go, as MIDI says it does");

            play(*head, {on(white), on(black)});
            grid.updateSoundingKeys();
            require(grid.soundingKeys.count() == 2, "Two keys held light together");
            stack.releasePlayingNotes();
            grid.updateSoundingKeys();
            require(grid.soundingKeys.none(), "A stop's panic lets every key go");
            play(*head, {on(white)});
            play(*head, {juce::MidiMessage::allNotesOff(1)});
            grid.updateSoundingKeys();
            grid.updateSoundingKeys();
            require(grid.soundingKeys.none(), "and so does all-notes-off");

            play(*head, {on(white), off(white)}, true);
            grid.updateSoundingKeys();
            require(grid.soundingKeys.none(), "A render plays the song behind the loading screen and lights nothing");

            auto* other = headOf(2);
            require(other != nullptr && stack.trackType(2) == Session::TrackType::midi, "Another MIDI track");
            other->baseClassInitialise({{}, 48000.0, 512});
            play(*other, {on(white)});
            grid.updateSoundingKeys();
            require(grid.soundingKeys.none(), "Another track's notes are not the open clip's");
            other->baseClassDeinitialise();

            play(*head, {on(57), off(57)});
            grid.visibilityChanged();
            grid.updateSoundingKeys();
            require(!grid.soundingKeys.test(57), "A key struck while the editor was hidden is not lit when it shows");

            // To look at: a white and a black key held, the clip looping.
            if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_NOTE_PLAYING_KEYS_SNAPSHOT", {});
                shot.isNotEmpty())
            {
                require(stack.setPatternLooping(true).wasOk(), "The clip loops for the picture");
                play(*head, {on(white), on(black)});
                grid.updateSoundingKeys();
                juce::File(shot).deleteFile();
                if (auto stream = juce::File(shot).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream(picture(), *stream);
            }

            head->baseClassDeinitialise();
            owned.reset();
            stack.releaseAudioDevice();
        }
