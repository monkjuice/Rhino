        // What a drag costs the rest of the interface. Inside a knob or fader
        // drag the session tells only the rack, or nobody, and tells everyone
        // once when the drag ends; a panel the shell has switched off waits
        // and catches up when it is shown. A document of its own.
        {
            struct Count final : juce::ChangeListener
            {
                int heard = 0;
                void changeListenerCallback(juce::ChangeBroadcaster*) override { ++heard; }
            };
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            require(stack.addDevice("FourOsc", 0).wasOk(), "A synth to turn a knob on");
            const auto synth = stack.deviceSlots(0).front().pluginIndex;
            Count everyone, rack;
            stack.addChangeListener(&everyone);
            stack.deviceParameterValues.addChangeListener(&rack);

            require(stack.beginDeviceParameterGesture(0, synth, 0).wasOk(), "A knob drag begins");
            for (int step = 0; step < 5; ++step)
                require(stack.setDeviceParameter(0, synth, 0, step % 2 == 0 ? 0.3f : 0.6f).wasOk(), "and moves");
            require(everyone.heard == 0 && rack.heard == 5,
                    "Inside a knob drag only the rack hears each step");
            require(stack.endDeviceParameterGesture(0, synth, 0).wasOk() && everyone.heard == 1,
                    "and everyone hears once, when it ends");

            everyone.heard = 0;
            stack.beginTrackVolumeGesture(0);
            for (int step = 0; step < 5; ++step)
                require(stack.setTrackVolumeDb(0, step % 2 == 0 ? -3.0f : -6.0f).wasOk(), "A fader drag moves");
            require(everyone.heard == 0, "Inside a fader drag nobody is told of each pixel");
            stack.endTrackVolumeGesture(0);
            require(everyone.heard == 1 && std::abs(stack.trackMixer(0).volumeDb + 3.0f) < 0.01f,
                    "and everyone hears once at its end, with the level it was left at");

            everyone.heard = 0;
            require(stack.setTrackVolumeDb(0, -2.0f).wasOk() && everyone.heard == 1,
                    "Outside a drag a fader change is told at once");
            stack.removeChangeListener(&everyone);
            stack.deviceParameterValues.removeChangeListener(&rack);

            // A panel the shell has switched off does not rebuild, and is
            // current again the moment it is shown.
            juce::Component shell;
            auto launcher = std::make_unique<SessionView>(stack);
            shell.addChildComponent(*launcher);
            launcher->setBounds(0, 0, 900, 500);
            const auto columnsBefore = launcher->mute.size();
            require(stack.addTrack(Session::TrackType::midi).wasOk(), "A track is added while the panel is hidden");
            require(launcher->mute.size() == columnsBefore, "A hidden panel does not rebuild for it");
            launcher->setVisible(true);
            require(static_cast<int>(launcher->mute.size()) == stack.trackCount(),
                    "and is current again as soon as it is shown");
            launcher.reset();
            stack.releaseAudioDevice();
        }
