        // The jog wheel's face is painted once and a turn of its marker
        // repaints the marker alone, where it was and where it is, so a frame
        // painted over the last matches a full paint at 100, 125 and 200 per
        // cent. Every opaque control paints all of itself. A scrub lands
        // where the drag took it, however many steps came between two ticks,
        // and the keys a set is played on act as the hand comes down.
        {
            auto wheel = std::make_unique<DjJogWheel>();
            wheel->setBounds(0, 0, 190, 190);
            wheel->setVisible(true);
            wheel->setTrackName("A tune");
            auto& damage = recordDamage(*wheel);
            for (const auto scale : {1.0f, 1.25f, 2.0f})
            {
                const auto size = juce::roundToInt(190 * scale);
                juce::Image frame(juce::Image::RGB, size, size, true, juce::SoftwareImageType());
                renderRegion(*wheel, frame, juce::RectangleList<int>(wheel->getLocalBounds()), scale);
                const auto face = wheel->face;
                for (const auto [seconds, playing] : std::initializer_list<std::pair<double, bool>> {
                         {0.0, false}, {0.03, true}, {0.031, true}, {0.6, true}, {0.9, true}, {1.79, true}, {1.81, true},
                         {2.4, false}, {0.2, false}})
                {
                    damage.region.clear();
                    wheel->setPosition(seconds, playing);
                    for (const auto& area : damage.region)
                        require(area.getWidth() <= 10 && area.getHeight() <= 10, "A turn of the marker repaints the marker, not the platter");
                    renderRegion(*wheel, frame, damage.region, scale);
                    juce::Image expected(juce::Image::RGB, size, size, true, juce::SoftwareImageType());
                    renderRegion(*wheel, expected, juce::RectangleList<int>(wheel->getLocalBounds()), scale);
                    const auto difference = sameImages(frame, expected);
                    require(difference.isEmpty(), ("The jog wheel at " + juce::String(scale, 2) + ", " + juce::String(seconds, 3)
                                                   + " s: pixel " + difference).toRawUTF8());
                }
                require(wheel->face == face, "and the face painted once is the face drawn on every frame");
            }
            {
                const auto face = wheel->face;
                wheel->setTrackName("Another tune");
                juce::Image frame(juce::Image::RGB, 190, 190, true, juce::SoftwareImageType());
                renderRegion(*wheel, frame, juce::RectangleList<int>(wheel->getLocalBounds()), 1.0f);
                require(!(wheel->face == face), "A new name paints the face again");
            }
            wheel->setCachedComponentImage(nullptr);

            // In a window at 125 or 150 per cent most wheels stand between two
            // device pixels. The face is painted that far into its picture and
            // drawn back as far, so it lands whole on the window's grid: every
            // pixel of it on the window is a pixel of the picture, as the
            // vectors it stands in for were drawn, never a blend of two.
            {
                juce::Component window;
                window.setBounds(0, 0, 220, 220);
                window.addAndMakeVisible(*wheel);
                wheel->setTopLeftPosition(13, 7);
                for (const auto scale : {1.25f, 1.5f})
                {
                    const auto size = juce::roundToInt(220 * scale);
                    juce::Image canvas(juce::Image::RGB, size, size, true, juce::SoftwareImageType());
                    {
                        juce::Graphics g(canvas);
                        g.addTransform(juce::AffineTransform::scale(scale));
                        window.paintEntireComponent(g, false);
                    }
                    const auto origin = wheel->getPosition().toFloat() * scale;
                    require(origin.x != std::floor(origin.x) && origin.y != std::floor(origin.y), "The wheel stands between device pixels");
                    const juce::Point<int> corner(static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)));
                    const auto inside = juce::Rectangle<float>(origin.x, origin.y, static_cast<float>(wheel->getWidth()) * scale,
                                                               static_cast<float>(wheel->getHeight()) * scale)
                                            .getSmallestIntegerContainer().reduced(1);
                    const auto marker = (wheel->markerBox(wheel->markerAngle).expanded(2.0f) * scale + origin).getSmallestIntegerContainer();
                    int blended = 0;
                    juce::Point<int> first;
                    for (int y = inside.getY(); y < inside.getBottom(); ++y)
                        for (int x = inside.getX(); x < inside.getRight(); ++x)
                            if (!marker.contains(x, y) && canvas.getPixelAt(x, y) != wheel->face.getPixelAt(x - corner.x, y - corner.y))
                                if (blended++ == 0)
                                    first = {x, y};
                    require(blended == 0, ("The jog wheel's face at " + juce::String(scale, 2) + " in a window: " + juce::String(blended)
                                           + " pixels are not the face's own, the first at " + first.toString()).toRawUTF8());
                }
                window.removeChildComponent(wheel.get());
                wheel->setTopLeftPosition(0, 0);
            }

            // Opaque: each paints every pixel of its bounds.
            const auto coversItself = [](juce::Component& component)
            {
                juce::Image canvas(juce::Image::RGB, component.getWidth(), component.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics g(canvas);
                    g.fillAll(juce::Colour(0xffff00ff));
                    component.paint(g);
                }
                for (int y = 0; y < canvas.getHeight(); ++y)
                    for (int x = 0; x < canvas.getWidth(); ++x)
                        if (canvas.getPixelAt(x, y) == juce::Colour(0xffff00ff))
                            return false;
                return true;
            };
            DjFader fader(true, palette::volume);
            fader.setBounds(0, 0, 28, 160);
            fader.setTicks(9);
            DjMeter meter;
            meter.setBounds(0, 0, 8, 120);
            meter.feed(0.5f);
            require(fader.isOpaque() && coversItself(fader), "A fader is opaque and paints all of itself");
            require(meter.isOpaque() && coversItself(meter), "A meter is opaque and paints all of itself");
            require(wheel->isOpaque() && coversItself(*wheel), "The jog wheel is opaque and paints all of itself");
        }

        // A scrub of a standing deck: the steps of one drag arrive faster than
        // the view reads where the deck is, and each landed from that stale
        // place, so all but the last between two reads were lost.
        {
            auto& console = *djView->decks[0];
            auto& wheel = console.jog;
            require(wheel.isVisible() && !wheel.getBounds().isEmpty() && wheel.isVinylMode(), "The console shows its jog wheel");
            booth.djSeek(0, 1.0);
            booth.djProcessOffline(512);
            djView->timerCallback();
            require(!booth.djDeckState(0).isPlaying() && std::abs(booth.djDeckState(0).positionSeconds - 1.0) < 0.01,
                    "The file deck stands at a second in");
            const auto centre = wheel.getLocalBounds().toFloat().getCentre();
            const auto radius = static_cast<float>(wheel.getWidth()) * 0.2f;
            const auto at = [&](float angle) { return centre + juce::Point<float>(std::sin(angle) * radius, -std::cos(angle) * radius); };
            const std::array<float, 5> angles {0.2f, 0.6f, 1.0f, 1.4f, 1.8f};
            wheel.mouseDown(mouseAt(wheel, at(angles[0]), at(angles[0]), 10));
            for (size_t i = 1; i < angles.size(); ++i)
                wheel.mouseDrag(mouseAt(wheel, at(angles[i]), at(angles[0]), 10 + static_cast<int>(i)));
            wheel.mouseUp(mouseAt(wheel, at(angles.back()), at(angles[0]), 20));
            booth.djProcessOffline(512);
            const auto travelled = static_cast<double>(angles.back() - angles.front()) / juce::MathConstants<double>::twoPi * 1.8;
            const auto landed = booth.djDeckState(0).positionSeconds;
            require(std::abs(landed - (1.0 + travelled)) < 0.01,
                    ("A scrub lands where the whole drag took it: " + juce::String(landed, 3) + " s, not "
                     + juce::String(1.0 + travelled, 3)).toRawUTF8());
            booth.djSeek(0, 0.0);
            booth.djProcessOffline(512);
            djView->timerCallback();

            // The keys a set is played on act on the press; the ones that open
            // a menu, and the switches, on the release.
            for (auto* key : {&console.play, &console.loopIn, &console.loopOut, &console.reloop, &console.loopHalve,
                              &console.loopDouble, &console.beatLoop, &console.jumpBack, &console.jumpForward, &console.searchBack,
                              &console.searchForward, &console.reverse, &console.syncKey, &console.master, &console.tempoReset,
                              &console.cue})
                require(key->getTriggeredOnMouseDown(), ("A performance key acts on the press: " + key->getName()).toRawUTF8());
            for (auto& pad : console.hotCues)
                require(pad->getTriggeredOnMouseDown(), "A hot cue acts on the press");
            for (auto* key : {&console.jumpSize, &console.tempoRange, &console.vinyl, &console.quantise, &console.live,
                              &console.edit, &console.reload, &console.eject})
                require(!key->getTriggeredOnMouseDown(), ("A key that opens a menu or switches acts on the release: " + key->getName()).toRawUTF8());
            require(djView->mixer.fxTap.getTriggeredOnMouseDown() && djView->mixer.fxOn.getTriggeredOnMouseDown(),
                    "The mixer's TAP and effect switch act on the press");
        }

        // Where a console's controls stand, worked out apart from it.
        {
            const auto inside = [](const djconsole::ConsoleLayout& layout, juce::Rectangle<int> bounds)
            {
                for (const auto& area : {layout.source, layout.live, layout.edit, layout.reload, layout.eject, layout.display, layout.loopIn,
                                         layout.loopOut, layout.reloop, layout.beatLoop, layout.cue, layout.play, layout.grid, layout.sync,
                                         layout.tempo, layout.tempoReset, layout.jog, layout.vinyl, layout.brake})
                    if (!area.isEmpty() && !bounds.contains(area)) return false;
                return true;
            };
            djconsole::ConsoleSpace tall;
            tall.bounds = {0, 0, 420, 700};
            tall.gridShown = true;
            const auto roomy = djconsole::layOutConsole(tall);
            require(roomy.showsJog && !roomy.jog.isEmpty() && inside(roomy, tall.bounds), "A tall console shows its jog wheel, everything inside it");
            require(roomy.grid.getRight() <= roomy.cue.getX() && !roomy.grid.isEmpty(), "and its clips grid beside the big keys");
            require(roomy.display.getHeight() <= 220, "and gives its screen no more than 220 px");
            djconsole::ConsoleSpace narrow;
            narrow.bounds = {0, 0, 220, 400};
            const auto squeezed = djconsole::layOutConsole(narrow);
            require(!squeezed.showsJog && squeezed.jog.isEmpty() && squeezed.vinyl.isEmpty() && squeezed.brake.isEmpty()
                        && inside(squeezed, narrow.bounds),
                    "A narrow console drops its jog wheel, and its two controls, before anything else");
        }
