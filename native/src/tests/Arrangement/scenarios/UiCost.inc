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
            auto booth = std::make_unique<DjView>(stack);
            shell.addChildComponent(*booth);
            booth->setBounds(0, 0, 1240, 720);
            const auto decksBefore = booth->decks.size();
            require(stack.addDjDeck().wasOk(), "A deck is added while the panel is hidden");
            require(booth->decks.size() == decksBefore, "A hidden panel does not rebuild for it");
            booth->setVisible(true);
            require(static_cast<int>(booth->decks.size()) == stack.djDeckCount(),
                    "and is current again as soon as it is shown");
            booth.reset();
            // The timeline too, which the DJ view stands in for: hidden, a
            // change leaves it stale rather than rebuilding it.
            auto timeline = std::make_unique<Arrangement>(stack);
            shell.addChildComponent(*timeline);
            timeline->setBounds(0, 0, 1240, 720);
            const auto clipsBefore = timeline->clips.size();
            require(stack.createClip(0, 4.0).wasOk(), "A clip is made while the timeline is hidden");
            require(timeline->clips.size() == clipsBefore, "A hidden timeline does not rebuild for it");
            timeline->setVisible(true);
            require(timeline->clips.size() == clipsBefore + 1, "and is current again as soon as it is shown");
            timeline.reset();
            stack.releaseAudioDevice();
        }

        // What the DJ view costs and what it draws, in a document of its own:
        // a deck playing a file - eight seconds of a beat at 120 BPM - and a
        // deck playing a MIDI track that runs no instrument, which bounces at
        // once to the silence such a track holds and carries its notes, so
        // one screen draws a waveform, the other notes, and nothing renders.
        // The view stands in a parent, as the shell holds it.
        {
            // What a component and everything inside it invalidated, as its
            // window would collect it: a component with a cached image is
            // told of every repaint inside it before its parent is.
            struct Damage final : juce::CachedComponentImage
            {
                explicit Damage(juce::Component& owner) : component(owner) {}
                void paint(juce::Graphics&) override {}
                bool invalidateAll() override { region.add(component.getLocalBounds()); return false; }
                bool invalidate(const juce::Rectangle<int>& area) override { region.add(area); return false; }
                void releaseResources() override {}
                juce::Component& component;
                juce::RectangleList<int> region;
            };
            const auto recordDamage = [](juce::Component& component) -> Damage&
            {
                auto* damage = new Damage(component);
                component.setCachedComponentImage(damage);
                return *damage;
            };
            // A component's own paint for a region at a display scale, the
            // region rounded out to whole device pixels as a window's damage
            // is.
            const auto renderRegion = [](juce::Component& component, juce::Image& target, const juce::RectangleList<int>& region,
                                         float scale)
            {
                juce::Graphics g(target);
                juce::RectangleList<int> device;
                for (const auto& area : region)
                    device.add((area.toFloat() * scale).getSmallestIntegerContainer());
                g.reduceClipRegion(device);
                g.addTransform(juce::AffineTransform::scale(scale));
                component.paint(g);
            };
            const auto sameImages = [](const juce::Image& a, const juce::Image& b)
            {
                for (int y = 0; y < a.getHeight(); ++y)
                    for (int x = 0; x < a.getWidth(); ++x)
                        if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                            return juce::String(x) + "," + juce::String(y) + " is " + a.getPixelAt(x, y).toDisplayString(true)
                                 + " where a full paint gives " + b.getPixelAt(x, y).toDisplayString(true);
                return juce::String();
            };
            const auto mouseAt = [](juce::Component& component, juce::Point<float> at, juce::Point<float> down, int eventIndex)
            {
                const auto time = juce::Time(1000000 + eventIndex);
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, &component, &component, time, down, time, 1, false);
            };
            auto document = std::make_unique<Session>();
            auto& booth = *document;
            juce::TemporaryFile beatFile(".wav");
            {
                juce::WavAudioFormat format;
                std::unique_ptr<juce::OutputStream> stream = beatFile.getFile().createOutputStream();
                auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));
                require(writer != nullptr, "A beat to play on the file deck");
                juce::AudioBuffer<float> audio(1, 48000 * 8);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    const auto seconds = i / 48000.0;
                    const auto inBeat = std::fmod(seconds, 0.5);
                    audio.setSample(0, i, static_cast<float>(0.7 * std::sin(juce::MathConstants<double>::twoPi * 55.0 * inBeat) * std::exp(-inBeat * 16.0)
                                                             + 0.05 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * seconds)));
                }
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "written");
            }
            require(booth.addDjDeck().wasOk() && booth.addDjDeck().wasOk(), "Two decks for the view");
            require(booth.loadDjDeckFile(0, beatFile.getFile()).wasOk(), "The first plays the beat");
            te::EditItemID notesClip;
            require(booth.createClip(0, 0.0, &notesClip).wasOk() && booth.selectPatternClip(notesClip).wasOk(),
                    "A clip on the first MIDI lane, which runs no instrument");
            for (int note = 0; note < 8; ++note)
                require(booth.addNote(note * 2.0, Session::lowestNote + (note % 4) * 2, 1.5).wasOk(), "with notes in it");
            require(booth.loadDjDeckTrack(1, 0).wasOk(), "The second deck plays the lane");
            for (int waited = 0; waited < 400 && (booth.djBusy() || !booth.djDeckInfo(0).loaded || !booth.djDeckInfo(1).loaded); ++waited)
            {
                juce::Thread::sleep(25);
                booth.djPoll();
            }
            require(booth.djDeckInfo(0).loaded && booth.djDeckInfo(1).loaded,
                    ("Both decks load: " + booth.djDeckInfo(0).error + booth.djDeckInfo(1).error).toRawUTF8());
            require(booth.djDeckTrack(1) != nullptr && !booth.djDeckTrack(1)->midi.empty(), "and the lane's deck carries its notes");
            booth.djProcessOffline(512);
            juce::Component frameOfView;
            frameOfView.setBounds(0, 0, 1240, 720);
            auto djView = std::make_unique<DjView>(booth);
            djView->setLookAndFeel(&theme);
            frameOfView.addAndMakeVisible(*djView);
            djView->setBounds(frameOfView.getLocalBounds());
            djView->sync();
            // The screens paint their pictures a couple a frame.
            for (int frame = 0; frame < 12; ++frame)
                djView->frame();

            scenario("what the dj view costs: a change repaints what it changed");
           #include "DjViewCost.inc"
            scenario("what the dj view costs: the screens");
           #include "DjScreens.inc"
            scenario("what the dj view costs: the jog wheel and the keys");
           #include "DjJogAndKeys.inc"
            scenario("what the dj view costs: a culled repaint matches drawing everything");
           #include "DjCulledPaint.inc"

            djView->setLookAndFeel(nullptr);
            djView.reset();
            booth.releaseAudioDevice();
        }
