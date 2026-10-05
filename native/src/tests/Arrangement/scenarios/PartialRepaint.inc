        // A repaint of part of a panel draws exactly what it drew before the
        // painters learned to skip. The arrangement and the note editor leave
        // out everything beyond the area being repainted - the playhead
        // invalidates a strip a few pixels wide every display refresh - and a
        // skip that is too eager leaves a pixel wrong that only that strip
        // would show. Each area is painted twice under the same clip, culled
        // and drawing everything, over a colour neither panel uses, so a pixel
        // the culled paint forgets cannot pass for one it drew. Not against the
        // whole paint: JUCE anti-aliases a path's edge slightly differently
        // under a different clip, so that comparison fails without any culling.
        // A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            const auto paintArea = [](juce::Component& panel, juce::Image& canvas, juce::Rectangle<int> area)
            {
                juce::Graphics g(canvas);
                g.reduceClipRegion(area);
                g.fillAll(juce::Colour(0xffff00ff));
                panel.paintEntireComponent(g, false);
            };
            // Columns the width of a playhead strip across the whole panel,
            // rows across the whole of it, and the seams the culling reasons
            // about.
            const auto checkAreas = [&](auto& panel, const char* name, std::vector<juce::Rectangle<int>> areas)
            {
                for (int x = 0; x < panel.getWidth(); x += 23)
                    areas.push_back({x, 0, 3, panel.getHeight()});
                for (int y = 0; y < panel.getHeight(); y += 17)
                    areas.push_back({0, y, panel.getWidth(), 2});
                juce::Image everything(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Image culled(juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                for (const auto& area : areas)
                {
                    panel.cullRepaints = false;
                    paintArea(panel, everything, area);
                    panel.cullRepaints = true;
                    paintArea(panel, culled, area);
                    const juce::Image::BitmapData expected(everything, juce::Image::BitmapData::readOnly);
                    const juce::Image::BitmapData drawn(culled, juce::Image::BitmapData::readOnly);
                    for (int y = area.getY(); y < area.getBottom(); ++y)
                        for (int x = area.getX(); x < area.getRight(); ++x)
                        {
                            if (expected.getPixelColour(x, y) == drawn.getPixelColour(x, y))
                                continue;
                            const auto message = juce::String(name) + ": repainting " + area.toString()
                                + " drew pixel " + juce::Point<int>(x, y).toString() + " as "
                                + drawn.getPixelColour(x, y).toDisplayString(true) + " where drawing everything gives "
                                + expected.getPixelColour(x, y).toDisplayString(true);
                            require(false, message.toRawUTF8());
                        }
                }
            };

            // [MIDI, Audio, Bus, MIDI, Audio]: clips with held and short notes
            // on both MIDI lanes, audio on the first audio lane, a group, an
            // automation lane of its own and a loop.
            require(stack.groupTracks({2, 3}, "Band").wasOk(), "Two tracks group under a bus");
            te::EditItemID opened;
            require(stack.createClip(0, 0.0, &opened).wasOk(), "A clip on the first lane");
            require(stack.selectPatternClip(opened).wasOk() && stack.ensurePatternLengthSteps(64),
                    "opens, four bars long");
            require(stack.createClip(0, 8.0).wasOk() && stack.createClip(0, 12.0).wasOk(), "Two more after it");
            require(stack.createClip(3, 1.0).wasOk(), "A clip on the grouped MIDI lane");
            require(stack.importAudioAt(source.getFile(), 1, 3.0).wasOk(), "Audio on the first audio lane");
            require(stack.selectPatternClip(opened).wasOk(), "The first clip is the one being edited");
            juce::ValueTree held;
            for (int note = 0; note < 24; ++note)
                require(stack.addNote(note * 2.5, Session::lowestNote + (note * 5) % 16, note % 3 == 0 ? 3.75 : 0.5,
                                      note == 6 ? &held : nullptr).wasOk(), "Notes fill the clip");
            require(stack.addDevice("FourOsc", 0).wasOk(), "A synth with a knob to automate");
            const Session::DeviceTarget knob {0, stack.deviceSlots(0).front().pluginIndex, 0};
            require(stack.showTrackAutomation(knob, true).wasOk()
                        && stack.setTrackAutomationPoints(knob, {{0.0, 0.1f}, {3.0, 0.9f}, {7.0, 0.3f}}).wasOk(),
                    "and a lane of its own to draw it in");
            require(stack.setLoopRange(1.0, 5.0).wasOk(), "A loop over the first bars");

            // On the heap: the runner's frame holds every scenario's locals at
            // once, and two more panels on it overflowed the stack.
            auto arrangementPanel = std::make_unique<Arrangement>(stack);
            auto& paintedView = *arrangementPanel;
            paintedView.setSize(1100, 520);
            paintedView.sync();
            paintedView.resized();
            paintedView.selectedTrack = 3;
            paintedView.playhead = paintedView.xFor(2.4);
            // The audio clip's waveform arrives from a worker. Compare only
            // once two paints in a row agree, so the picture is not still
            // changing underneath the comparison.
            {
                auto previous = paintedView.createComponentSnapshot(paintedView.getLocalBounds());
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
                while (juce::Time::getMillisecondCounterHiRes() < deadline)
                {
                    juce::Thread::sleep(50);
                    paintedView.sync();
                    auto next = paintedView.createComponentSnapshot(paintedView.getLocalBounds());
                    bool same = true;
                    for (int y = 0; y < next.getHeight() && same; ++y)
                        for (int x = 0; x < next.getWidth() && same; ++x)
                            same = next.getPixelAt(x, y) == previous.getPixelAt(x, y);
                    if (same)
                        break;
                    previous = next;
                }
            }
            const auto cards = static_cast<int>(Arrangement::headerWidth);
            checkAreas(paintedView, "The arrangement", {
                {cards - 6, 0, 12, paintedView.getHeight()},
                {static_cast<int>(paintedView.playhead) - 1, 0, 4, paintedView.getHeight()},
                {300, 0, 160, 120},
                {cards + 1, 0, 2, 60},
            });

            auto gridPanel = std::make_unique<StepGrid>(stack);
            auto& paintedGrid = *gridPanel;
            paintedGrid.setSize(900, 320);
            stack.sendSynchronousChangeMessage();
            paintedGrid.setSelectedStates({held});
            paintedGrid.scaleHighlight = 2;
            paintedGrid.playhead = 300.0f;
            checkAreas(paintedGrid, "The note editor", {
                {0, 30, static_cast<int>(paintedGrid.labelWidth), 80},
                {static_cast<int>(paintedGrid.labelWidth) - 2, 0, 4, paintedGrid.getHeight()},
                {0, 0, paintedGrid.getWidth(), static_cast<int>(paintedGrid.headerHeight) + 2},
            });
            // Zoomed in and scrolled part of a step along, so cells straddle
            // the keys and the repainted columns cut through them.
            paintedGrid.stepZoom = 2.0;
            paintedGrid.stepScroll = 5.5;
            checkAreas(paintedGrid, "The zoomed note editor", {
                {static_cast<int>(paintedGrid.labelWidth) - 8, 0, 10, paintedGrid.getHeight()},
            });
            gridPanel.reset();
            arrangementPanel.reset();
            stack.releaseAudioDevice();
        }
