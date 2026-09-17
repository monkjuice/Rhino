        // Compare incremental frames to complete renders, including fractional
        // Windows display scales, wraparound, seeks, and hiding the playhead.
        StepGrid grid(session);
        grid.setSize(1000, 250);
        const auto checkPlayhead = [&require](juce::Component& panel, float& position, juce::Rectangle<int> area)
        {
            for (const auto scale : {1.0f, 1.25f, 2.0f})
            {
                const auto render = [&panel, scale](juce::Image& target, juce::Rectangle<int> dirty)
                {
                    juce::Graphics g(target);
                    // Native damage is an outward-rounded physical-pixel region,
                    // not an antialiased fractional clip applied over old pixels.
                    g.reduceClipRegion((dirty.toFloat() * scale).getSmallestIntegerContainer());
                    g.addTransform(juce::AffineTransform::scale(scale));
                    panel.paint(g);
                };
                juce::Image frame(juce::Image::RGB, juce::roundToInt(panel.getWidth() * scale),
                                  juce::roundToInt(panel.getHeight() * scale), true);
                position = -1;
                render(frame, panel.getLocalBounds());
                for (const auto next : {150.0f, 150.25f, 150.5f, 155.75f, 420.0f, 998.25f, 150.0f, -1.0f})
                {
                    const auto damage = playheadDamage(position, next, area);
                    movePlayhead(panel, position, next, area);
                    render(frame, damage);
                    juce::Image expected(juce::Image::RGB, frame.getWidth(), frame.getHeight(), true);
                    render(expected, panel.getLocalBounds());
                    for (int y = 0; y < frame.getHeight(); ++y)
                        for (int x = 0; x < frame.getWidth(); ++x)
                        {
                            if (frame.getPixelAt(x, y) != expected.getPixelAt(x, y))
                                std::fprintf(stderr, "%s scale %.2f next %.2f pixel %d,%d actual %s expected %s\n",
                                             panel.getTitle().toRawUTF8(), scale, next, x, y,
                                             frame.getPixelAt(x, y).toString().toRawUTF8(), expected.getPixelAt(x, y).toString().toRawUTF8());
                            require(frame.getPixelAt(x, y) == expected.getPixelAt(x, y),
                                    "Incremental playhead frame must match full render without trails or missing pixels");
                        }
                }
            }
        };
        checkPlayhead(view, view.playhead, view.getLocalBounds().withTrimmedTop(32).withTrimmedBottom(18));
        checkPlayhead(grid, grid.playhead, grid.getLocalBounds().withTrimmedTop(26));

        session.applyPatternPreset(Session::PatternPreset::AcidSteps);
        view.sync();
        auto midiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int midiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = static_cast<int>(view.lane(0).getX()) + 7; x < 620; ++x)
                if (isMidiNotePixel(midiPicture.getPixelAt(x, y))) ++midiPixels;
        require(midiPixels >= 25, "Pattern lane must draw visible MIDI notes");
        session.applyPatternPreset(Session::PatternPreset::SubBass);
        view.sync();
        auto bassMidiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int bassMidiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = static_cast<int>(view.lane(0).getX()) + 7; x < 620; ++x)
                if (isMidiNotePixel(bassMidiPicture.getPixelAt(x, y))) ++bassMidiPixels;
        require(bassMidiPixels >= 25, "Low bass MIDI clips must draw visible arrangement note bars");
        session.undo();
        view.sync();
        const auto patternID = session.pattern().itemID;
        view.selected = patternID;
        const auto patternPosition = session.pattern().getPosition();
        view.original = {patternPosition.time.getStart().inSeconds(), patternPosition.time.getEnd().inSeconds(),
                         patternPosition.offset.inSeconds()};
        view.preview = {1.0, 1.0 + patternPosition.time.getLength().inSeconds(), patternPosition.offset.inSeconds()};
        view.originalTrack = view.previewTrack = 0;
        view.dragging = true;
        auto dragPreviewPicture = view.createComponentSnapshot(view.getLocalBounds());
        int dragPreviewOldPixels = 0, dragPreviewNewPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = static_cast<int>(view.lane(0).getX()) + 7; x < 620; ++x)
            {
                if (!isMidiNotePixel(dragPreviewPicture.getPixelAt(x, y))) continue;
                if (x < view.xFor(0.8)) ++dragPreviewOldPixels;
                if (x >= view.xFor(1.0)) ++dragPreviewNewPixels;
            }
        require(dragPreviewOldPixels == 0 && dragPreviewNewPixels >= 25,
                "Dragged MIDI clip contents must follow the live preview before mouse-up");
        view.dragging = false;
        require(session.editClip(patternID, {0.0, 0.25, 0.0}, ClipGesture::trimRight).wasOk(),
                "Can create a very short MIDI clip for paint clipping");
        view.sync();
        auto shortMidiPicture = view.createComponentSnapshot(view.getLocalBounds());
        int escapedMidiPixels = 0;
        for (int y = 95; y < 122; ++y)
            for (int x = static_cast<int>(view.xFor(0.25)) + 4; x < 620; ++x)
                if (isMidiNotePixel(shortMidiPicture.getPixelAt(x, y))) ++escapedMidiPixels;
        require(escapedMidiPixels == 0, "Short MIDI clips must clip note bars to the visible clip bounds");
        session.undo();
        view.sync();
        view.selected = patternID;
        view.duplicateSelected();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 2, "Duplicate creates a second MIDI pattern clip");
        session.undo();
        require(te::getAudioTracks(*session.edit)[0]->getClips().size() == 1, "Undo duplicate restores one MIDI pattern clip");

        const auto runNativeRenderPeerTest = juce::SystemStats::getEnvironmentVariable("RHINO_NATIVE_RENDER_TEST", {}) == "1";
        if (runNativeRenderPeerTest)
        {
            // isShowing() requires a visible desktop peer. Keep this tiny navigation
            // check offscreen and remove the peer before exercising clip gestures.
            view.setTopLeftPosition(-10000, -10000);
            view.addToDesktop(0);
            view.setVisible(true);
            juce::Thread::sleep(1);
       #if JUCE_WINDOWS
            // Exercise the native damage handoff, not just software image painting.
            // Simulate a new position arriving in vblank while earlier damage is
            // already queued. Both strips must reach D2D before vblank drawing.
            auto* peer = view.getPeer();
            const auto direct2D = peer->getAvailableRenderingEngines().indexOf("Direct2D");
            require(direct2D >= 0, "Windows rendering regression requires Direct2D");
            peer->setCurrentRenderingEngine(direct2D);
            const auto hwnd = static_cast<HWND>(peer->getNativeHandle());
            UpdateWindow(hwnd);
            view.repaint(150, 32, 6, 196);
            require(GetUpdateRect(hwnd, nullptr, FALSE) != 0, "Repaint queues native window damage");
            movePlayhead(view, view.playhead, 300, view.getLocalBounds().withTrimmedTop(32).withTrimmedBottom(18));
            require(GetUpdateRect(hwnd, nullptr, FALSE) == 0, "Playhead damage must reach Direct2D before the vblank callback returns");
       #endif
        }
        session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(0.5));
        view.zoom(0.5, 0.5);
        require(view.playhead == view.xFor(0.5), "Zoom must preserve the fractional playhead position before the next vblank");
        view.fit();
        const auto spanBeforeWheelZoom = view.viewSpan;
        const auto wheelPoint = juce::Point<float>(view.xFor(0.5), 100.0f);
        const auto shiftWheelEvent = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), wheelPoint,
            juce::ModifierKeys(juce::ModifierKeys::shiftModifier), 1.0f, 0, 0, 0, 0,
            &view, &view, juce::Time::getCurrentTime(), wheelPoint, juce::Time::getCurrentTime(), 1, false);
        juce::MouseWheelDetails shiftWheel;
        shiftWheel.deltaY = 1.0f;
        view.mouseWheelMove(shiftWheelEvent, shiftWheel);
        require(view.viewSpan < spanBeforeWheelZoom, "Shift-wheel zooms the arrangement like the note editor");
        const auto spanBeforeHorizontalWheelZoom = view.viewSpan;
        shiftWheel.deltaY = 0.0f;
        shiftWheel.deltaX = -1.0f;
        view.mouseWheelMove(shiftWheelEvent, shiftWheel);
        require(view.viewSpan < spanBeforeHorizontalWheelZoom,
                "Shift-wheel zoom handles platforms that report the gesture as horizontal scrolling");
        require(close(playheadTime(session.edit->getTransport()), 0.5), "Stopped playhead follows the seek position");
        view.fit();
        require(view.playhead == view.xFor(0.5), "Fit must update the playhead immediately");
        session.stop();
        if (runNativeRenderPeerTest)
        {
            view.setVisible(false);
            view.removeFromDesktop();
            juce::Thread::sleep(1);
        }
