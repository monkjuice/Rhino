        // A deck's screen, frame by frame: every frame repaints exactly what
        // changed - through fractional positions, a jump, a loop, a hot cue
        // and a stop - so a frame painted over the last matches a full paint
        // at 100, 125 and 200 per cent; a move of less than a device pixel
        // paints nothing; a zoom brings its tiles in two a frame with the
        // last zoom's standing in; in a window at 125 or 150 per cent the
        // tiles and the overview land whole on device pixels; and a bounce
        // that changed only the notes keeps the overview, one that changed
        // only the sound the note tiles.
        {
            auto& screen = djView->decks[0]->display;
            auto& damage = recordDamage(screen);
            auto standing = booth.djDeckState(0);
            require(standing.lengthSeconds > 7.0 && standing.bpm > 0.0, "The file deck holds the beat, with a grid");
            const auto step = [&](auto change)
            {
                change(standing);
                damage.region.clear();
                screen.tick(standing);
            };
            for (const auto scale : {1.0f, 1.25f, 2.0f})
            {
                const auto width = juce::roundToInt(screen.getWidth() * scale), height = juce::roundToInt(screen.getHeight() * scale);
                juce::Image frame(juce::Image::RGB, width, height, true, juce::SoftwareImageType());
                renderRegion(screen, frame, juce::RectangleList<int>(screen.getLocalBounds()), scale);
                // The pictures for this scale come in over a few frames.
                for (int settle = 0; settle < 6; ++settle)
                {
                    step([](Session::DjDeckState&) {});
                    renderRegion(screen, frame, damage.region, scale);
                }
                const auto check = [&](const char* what)
                {
                    renderRegion(screen, frame, damage.region, scale);
                    juce::Image expected(juce::Image::RGB, width, height, true, juce::SoftwareImageType());
                    renderRegion(screen, expected, juce::RectangleList<int>(screen.getLocalBounds()), scale);
                    const auto difference = sameImages(frame, expected);
                    require(difference.isEmpty(), (juce::String("The screen at ") + juce::String(scale, 2) + " after " + what
                                                   + ": pixel " + difference + " (strip " + screen.waveArea().toString()
                                                   + ", overview " + screen.overviewArea().toString() + ", repainted "
                                                   + damage.region.getBounds().toString() + ")").toRawUTF8());
                };
                using Transport = Session::DjDeckState::Transport;
                step([](Session::DjDeckState& s) { s.transport = Transport::playing; s.positionSeconds = 1.0; });
                check("playing from a second in");
                for (const auto seconds : {1.013, 1.0263, 1.25, 1.2502, 1.6})
                {
                    step([seconds](Session::DjDeckState& s) { s.positionSeconds = seconds; });
                    check("a fractional move");
                }
                // Either side of the device pixel the strip stands on.
                const auto perDevicePixel = 1.0 / (screen.pixelsPerSecond() * screen.paintedScale);
                const auto standsOn = static_cast<double>(screen.scrollFor(standing.positionSeconds));
                for (const auto within : {0.2, -0.2})
                {
                    step([&](Session::DjDeckState& s) { s.positionSeconds = (standsOn + within) * perDevicePixel; });
                    require(!damage.region.intersectsRectangle(screen.waveArea()), "A move of less than a device pixel paints no strip");
                    check("a move of less than a pixel");
                }
                step([](Session::DjDeckState& s) { s.positionSeconds = 4.7; ++s.jumps; });
                check("a jump");
                step([](Session::DjDeckState& s) { s.loop = {4.0, 5.0, true}; s.positionSeconds = 4.98; });
                check("a loop set");
                step([](Session::DjDeckState& s) { s.positionSeconds = 4.02; });
                check("the loop going round");
                step([](Session::DjDeckState& s) { s.hotCueSeconds[2] = 4.5; });
                check("a hot cue set");
                step([](Session::DjDeckState& s) { s.hotCueSeconds[2] = -1.0; s.loop = {}; s.transport = Transport::stopped; });
                check("a hot cue cleared and the deck stopped");
                step([](Session::DjDeckState& s) { s.positionSeconds = 7.9; });
                check("a move near the end");
                step([](Session::DjDeckState& s) { s.positionSeconds = 4.0; });
                check("a move into the middle");
                // A zoom, where the strip shows more tiles than a frame
                // paints: two pictures a frame, the last zoom's tiles standing
                // in, scaled, until the new ones cover the strip.
                if (scale == 2.0f)
                {
                    const auto before = screen.tiles.key;
                    const auto inStrip = screen.waveArea().getCentre().toFloat();
                    juce::MouseWheelDetails wheel;
                    wheel.deltaY = 1.0f;
                    damage.region.clear();
                    screen.mouseWheelMove(mouseAt(screen, inStrip, inStrip, 3), wheel);
                    screen.tick(standing);
                    require(!(screen.tiles.key == before) && screen.tiles.pictures.size() <= 2 && !screen.standIns.pictures.empty(),
                            "A zoom paints two tiles in a frame at most, the last zoom's standing in");
                    check("a zoom, its first frame");
                    for (int more = 0; more < 6; ++more)
                    {
                        step([](Session::DjDeckState&) {});
                        check("a zoom coming in");
                    }
                    require(screen.standIns.pictures.empty(), "and lets them go once its own cover the strip");

                    // Two zooms in two frames, as the wheel spun or a splitter
                    // dragged gives: the tiles that covered the strip go on
                    // standing in while neither zoom's own do, so no column
                    // where the deck has material shows bare ground. The two
                    // tiles the first zoom painted used to take their place.
                    const auto covered = screen.tiles.key;
                    damage.region.clear();
                    for (int notch = 0; notch < 2; ++notch)
                    {
                        screen.mouseWheelMove(mouseAt(screen, inStrip, inStrip, 4 + notch), wheel);
                        screen.tick(standing);
                    }
                    require(screen.tiles.pictures.size() <= 2 && screen.standIns.key == covered,
                            "Two zooms in two frames keep the tiles that covered the strip standing in");
                    check("two zooms in two frames");
                    {
                        juce::Image drawn(juce::Image::RGB, width, height, true, juce::SoftwareImageType());
                        renderRegion(screen, drawn, juce::RectangleList<int>(screen.getLocalBounds()), scale);
                        // The middle row of the strip, which every column of a
                        // waveform covers, at whatever level.
                        const auto strip = screen.waveArea();
                        const auto middle = juce::roundToInt(static_cast<float>(strip.getY()) * scale)
                                          + static_cast<int>(std::ceil(static_cast<float>(strip.getHeight()) * scale)) / 2;
                        const auto perSecond = screen.pixelsPerSecond() * scale;
                        const auto playhead = static_cast<double>(strip.getX() + strip.getWidth() / 2) * scale;
                        const auto scroll = static_cast<double>(screen.scrollFor(standing.positionSeconds));
                        int bare = 0, firstBare = -1;
                        for (int x = juce::roundToInt(static_cast<float>(strip.getX()) * scale);
                             x < juce::roundToInt(static_cast<float>(strip.getRight()) * scale); ++x)
                        {
                            const auto seconds = (scroll + x - playhead) / perSecond;
                            if (seconds < 0.02 || seconds > standing.lengthSeconds - 0.02) continue;
                            if (drawn.getPixelAt(x, middle) == palette::displayInset)
                            {
                                if (firstBare < 0) firstBare = x;
                                ++bare;
                            }
                        }
                        require(bare == 0, ("and no column of the strip shows bare ground where the deck has material: "
                                            + juce::String(bare) + " do, the first at " + juce::String(firstBare)).toRawUTF8());
                    }
                }
                step([](Session::DjDeckState& s) { s.positionSeconds = 0.0; });
                check("back to the top");
            }
            screen.setCachedComponentImage(nullptr);

            // In a window at 125 or 150 per cent most screens stand between
            // two device pixels. The tiles and the overview are drawn in
            // device pixels from whole ones, so each lands whole on the
            // window's grid: somewhere there, every pixel it covers is its
            // own as it was painted, never a blend of two. Drawn from the
            // strip's middle and the overview's corner, both were resampled
            // half a pixel soft.
            {
                const auto inFrame = frameOfView.getLocalPoint(&screen, juce::Point<int>());
                djView->setTopLeftPosition(inFrame.x % 2 == 0 ? 1 : 0, inFrame.y % 2 == 0 ? 1 : 0);
                // Four seconds in, with the cue at the top and no hot cue or
                // loop, nothing but the playhead is drawn over the strip's
                // tiles, and nothing but the played part over the overview.
                const auto before = standing;
                standing.positionSeconds = 4.0;
                standing.cueSeconds = 0.0;
                standing.hotCueSeconds.fill(-1.0);
                standing.loop = {};
                for (const auto scale : {1.25f, 1.5f})
                {
                    const auto origin = frameOfView.getLocalPoint(&screen, juce::Point<int>()).toFloat() * scale;
                    require(origin.x != std::floor(origin.x) && origin.y != std::floor(origin.y), "The screen stands between device pixels");
                    juce::Image window(juce::Image::RGB, juce::roundToInt(static_cast<float>(frameOfView.getWidth()) * scale) + 2,
                                       juce::roundToInt(static_cast<float>(frameOfView.getHeight()) * scale) + 2, true, juce::SoftwareImageType());
                    const auto paintWindow = [&]
                    {
                        juce::Graphics g(window);
                        g.addTransform(juce::AffineTransform::scale(scale));
                        frameOfView.paintEntireComponent(g, false);
                    };
                    // The pictures for this scale come in over a few frames.
                    paintWindow();
                    for (int settle = 0; settle < 8; ++settle)
                        screen.tick(standing);
                    paintWindow();
                    require(screen.tiles.key == screen.tileKey() && screen.tiles.complete && screen.standIns.pictures.empty()
                                && screen.overview.key == screen.overviewKey() && !screen.overview.pictures.empty(),
                            "The screen's pictures for this scale are painted");
                    const juce::Image::BitmapData drawn(window, juce::Image::BitmapData::readOnly);
                    // How many of a picture's edges - a pixel unlike the one
                    // before it - show where it lands whole, within a device
                    // pixel of where the screen's own coordinates put it:
                    // every pixel of `inside` it covers, but those in `skip`,
                    // its own. -1 when it lands whole nowhere.
                    const auto edgesWhereWhole = [&](const juce::Image& picture, juce::Point<float> drawnFrom, juce::Rectangle<int> inside,
                                                     juce::Rectangle<int> skip)
                    {
                        const juce::Image::BitmapData own(picture, juce::Image::BitmapData::readOnly);
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const juce::Point<int> at(juce::roundToInt(drawnFrom.x) + dx, juce::roundToInt(drawnFrom.y) + dy);
                                const auto covered = inside.getIntersection({at.x, at.y, picture.getWidth(), picture.getHeight()});
                                auto whole = !covered.isEmpty();
                                int edges = 0;
                                for (int y = covered.getY(); whole && y < covered.getBottom(); ++y)
                                    for (int x = covered.getX(); whole && x < covered.getRight(); ++x)
                                    {
                                        if (skip.contains(x, y)) continue;
                                        const auto colour = own.getPixelColour(x - at.x, y - at.y);
                                        whole = drawn.getPixelColour(x, y) == colour;
                                        if (x > covered.getX() && colour != own.getPixelColour(x - at.x - 1, y - at.y))
                                            ++edges;
                                    }
                                if (whole) return edges;
                            }
                        return -1;
                    };
                    const auto strip = screen.waveArea();
                    const auto stripInside = (strip.toFloat() * scale + origin).getSmallestIntegerContainer().reduced(1);
                    const auto playhead = (juce::Rectangle<float>(static_cast<float>(strip.getCentreX() - 2), static_cast<float>(strip.getY()), 4.0f,
                                                                  static_cast<float>(strip.getHeight())) * scale + origin).getSmallestIntegerContainer();
                    const auto scroll = screen.scrollFor(standing.positionSeconds);
                    int tilesChecked = 0;
                    for (const auto& tile : screen.tiles.pictures)
                    {
                        const juce::Point<float> drawnFrom(origin.x + static_cast<float>(strip.getCentreX()) * scale
                                                               + static_cast<float>(tile.index * DjDeckDisplay::tileWidth - scroll),
                                                           origin.y + static_cast<float>(strip.getY()) * scale);
                        const juce::Rectangle<int> around(juce::roundToInt(drawnFrom.x), juce::roundToInt(drawnFrom.y), tile.image.getWidth(),
                                                          tile.image.getHeight());
                        if (around.getIntersection(stripInside).getWidth() < 16) continue;
                        const auto edges = edgesWhereWhole(tile.image, drawnFrom, stripInside, playhead);
                        require(edges >= 10, ("A tile of the strip at " + juce::String(scale, 2) + " lands whole on the window's device pixels: tile "
                                              + juce::String(tile.index) + " gives " + juce::String(edges)).toRawUTF8());
                        ++tilesChecked;
                    }
                    require(tilesChecked >= 2, "and every tile the strip shows is checked");
                    // The overview: the part played is dimmed over it, up to
                    // the marker.
                    const auto overview = screen.overviewArea();
                    const auto overviewInside = (overview.toFloat() * scale + origin).getSmallestIntegerContainer().reduced(1);
                    const auto marker = origin.x + static_cast<float>(overview.getX()) * scale
                                      + static_cast<float>(screen.overviewMarker(standing.positionSeconds));
                    const auto played = overviewInside.withRight(static_cast<int>(std::ceil(marker)) + 3);
                    const auto edges = edgesWhereWhole(screen.overview.pictures.front().image,
                                                       origin + overview.getPosition().toFloat() * scale, overviewInside, played);
                    require(edges >= 10, ("The overview at " + juce::String(scale, 2) + " lands whole on the window's device pixels: it gives "
                                          + juce::String(edges)).toRawUTF8());
                }
                standing = before;
                djView->setTopLeftPosition(0, 0);
            }

            // Painted at 100 per cent again, as the rest of the scenario draws.
            {
                juce::Image frame(juce::Image::RGB, screen.getWidth(), screen.getHeight(), true, juce::SoftwareImageType());
                renderRegion(screen, frame, juce::RectangleList<int>(screen.getLocalBounds()), 1.0f);
                for (int settle = 0; settle < 6; ++settle)
                    screen.tick(standing);
            }

            // What a picture shows decides whether it is painted again. The
            // lane's deck draws its cell's notes over a strip of silence the
            // length of the cell.
            auto& notesScreen = djView->decks[1]->display;
            const auto settled = [&notesScreen, &booth]
            {
                for (int frame = 0; frame < 10; ++frame)
                    notesScreen.tick(booth.djDeckState(1));
            };
            // A change to the cell is made into the deck at the next poll.
            const auto polled = [&booth, &djView]
            {
                booth.djPoll();
                djView->sync();
            };
            settled();
            require(notesScreen.drawsNotes && !notesScreen.tiles.pictures.empty() && !notesScreen.overview.pictures.empty(),
                    "The lane's screen has painted its note tiles and its overview");
            const auto overviewOfSilence = notesScreen.overview.pictures.front().image;
            const auto firstNoteTiles = notesScreen.tiles.key;
            require(booth.selectPatternClip(notesClip).wasOk() && booth.addNote(3.0, Session::lowestNote + 7, 2.0).wasOk(),
                    "A note added to the lane's cell");
            polled();
            settled();
            require(!(notesScreen.tiles.key == firstNoteTiles), "New notes are painted again");
            require(!notesScreen.overview.pictures.empty() && notesScreen.overview.pictures.front().image == overviewOfSilence,
                    "but the overview of the same silence is kept, not painted again");
            const auto noteTiles = notesScreen.tiles.pictures.front().image;
            const auto noteTileKey = notesScreen.tiles.key;
            require(booth.createClip(0, 8.0).wasOk(), "A clip on the lane's timeline, which is the arrangement's");
            polled();
            settled();
            require(notesScreen.tiles.key == noteTileKey && notesScreen.tiles.find(notesScreen.tiles.pictures.front().index) != nullptr,
                    "leaves the deck's cell, its notes keeping their tiles");
            bool keptTile = false;
            for (const auto& picture : notesScreen.tiles.pictures)
                keptTile = keptTile || picture.image == noteTiles;
            require(keptTile, "the very tiles painted before");
            require(!notesScreen.overview.pictures.empty() && notesScreen.overview.pictures.front().image == overviewOfSilence,
                    "and its overview");
            // A slower song plays the cell's bar for longer: the same notes on
            // a longer silence.
            const auto songTempo = booth.tempo();
            booth.setTempo(songTempo * 0.75);
            polled();
            settled();
            require(notesScreen.overview.pictures.empty() || !(notesScreen.overview.pictures.front().image == overviewOfSilence),
                    "A longer silence paints its overview again");
            booth.setTempo(songTempo);
            polled();
            settled();
        }

        // A jump to where the strip has not been - a hot cue far along, a beat
        // jump, a seek on the overview - keeps the strip's key, so nothing is
        // left to stand in for the tiles it lands among: every one the strip
        // shows is painted that frame, where two a frame left bare ground
        // between them. The same for the view shown again after its deck
        // moved on unseen. At the closest zoom the beat's eight seconds run
        // well past the strip, as a long track does.
        {
            auto& screen = djView->decks[0]->display;
            const auto scale = 2.0f;
            const auto width = juce::roundToInt(screen.getWidth() * scale), height = juce::roundToInt(screen.getHeight() * scale);
            // The columns of the strip that show bare ground where the deck
            // has material, along the middle row, which every column of a
            // waveform covers at whatever level.
            const auto bareColumns = [&]
            {
                juce::Image drawn(juce::Image::RGB, width, height, true, juce::SoftwareImageType());
                renderRegion(screen, drawn, juce::RectangleList<int>(screen.getLocalBounds()), scale);
                const auto strip = screen.waveArea();
                const auto middle = juce::roundToInt(static_cast<float>(strip.getY()) * scale)
                                  + static_cast<int>(std::ceil(static_cast<float>(strip.getHeight()) * scale)) / 2;
                const auto perSecond = screen.pixelsPerSecond() * scale;
                const auto playhead = static_cast<double>(strip.getX() + strip.getWidth() / 2) * scale;
                const auto scroll = static_cast<double>(screen.scrollFor(screen.state.positionSeconds));
                int bare = 0;
                for (int x = juce::roundToInt(static_cast<float>(strip.getX()) * scale);
                     x < juce::roundToInt(static_cast<float>(strip.getRight()) * scale); ++x)
                {
                    const auto seconds = (scroll + x - playhead) / perSecond;
                    if (seconds < 0.02 || seconds > screen.state.lengthSeconds - 0.02) continue;
                    if (drawn.getPixelAt(x, middle) == palette::displayInset) ++bare;
                }
                return bare;
            };
            const auto zoomBefore = screen.secondsAcross;
            screen.secondsAcross = 3.0;
            auto standing = booth.djDeckState(0);
            standing.positionSeconds = 0.5;
            // Painted at the scale once, as a window paints it, then settled.
            bareColumns();
            for (int settle = 0; settle < 10; ++settle)
                screen.tick(standing);
            require(screen.tiles.complete && screen.standIns.pictures.empty() && bareColumns() == 0,
                    "The strip at the closest zoom is painted where the deck stands");
            const auto tilesShown = screen.visibleTiles(static_cast<double>(screen.scrollFor(7.0)));
            require(tilesShown.last - tilesShown.first + 1 > DjDeckDisplay::picturesPerFrame,
                    "and shows more tiles than a frame paints with something standing in");
            standing.positionSeconds = 7.0;
            ++standing.jumps;
            screen.tick(standing);
            const auto afterJump = bareColumns();
            require(afterJump == 0, ("A jump far along shows no bare ground on the frame it lands: " + juce::String(afterJump)
                                     + " columns do").toRawUTF8());

            // Hidden, the view's displays stand still; shown again, the deck is
            // somewhere they never painted. A zoom painted nowhere yet.
            screen.secondsAcross = 3.6;
            booth.djSeek(0, 0.5);
            booth.djProcessOffline(512);
            for (int frame = 0; frame < 10; ++frame)
                djView->frame();
            require(screen.tiles.complete && bareColumns() == 0, "The strip of the shown view is painted where the deck stands");
            djView->setVisible(false);
            booth.djSeek(0, 7.0);
            booth.djProcessOffline(512);
            djView->frame();
            djView->setVisible(true);
            djView->frame();
            const auto afterShowing = bareColumns();
            require(afterShowing == 0, ("The view shown again after its deck moved on shows no bare ground on its first frame: "
                                        + juce::String(afterShowing) + " columns do").toRawUTF8());

            screen.secondsAcross = zoomBefore;
            booth.djSeek(0, 0.0);
            booth.djProcessOffline(512);
            djView->timerCallback();
            juce::Image frame(juce::Image::RGB, screen.getWidth(), screen.getHeight(), true, juce::SoftwareImageType());
            renderRegion(screen, frame, juce::RectangleList<int>(screen.getLocalBounds()), 1.0f);
            for (int settle = 0; settle < 10; ++settle)
                screen.tick(booth.djDeckState(0));
        }

        // Another file loaded on a deck showing one: while it is read the deck
        // goes on playing the first, and the screen goes on showing it,
        // pictures and all; once it lands, the first file's pictures stand in
        // for nothing. A load starting dropped the pictures of the material
        // still playing, and the landing took the ones painted again since as
        // the new file's stand-ins.
        {
            auto& screen = djView->decks[0]->display;
            juce::TemporaryFile otherFile(".wav");
            {
                juce::WavAudioFormat format;
                std::unique_ptr<juce::OutputStream> stream = otherFile.getFile().createOutputStream();
                auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
                    .withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));
                require(writer != nullptr, "Another file for the deck");
                juce::AudioBuffer<float> audio(1, 48000 * 6);
                for (int i = 0; i < audio.getNumSamples(); ++i)
                {
                    const auto inBeat = std::fmod(i / 48000.0, 0.4);
                    audio.setSample(0, i, static_cast<float>(0.6 * std::sin(juce::MathConstants<double>::twoPi * 220.0 * inBeat)
                                                             * std::exp(-inBeat * 6.0)));
                }
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "written");
            }
            const auto waitForTheFile = [&booth]
            {
                for (int waited = 0; waited < 400 && booth.djDeckInfo(0).loading; ++waited)
                {
                    juce::Thread::sleep(25);
                    booth.djPoll();
                }
                return booth.djDeckInfo(0).loaded;
            };
            for (int frame = 0; frame < 10; ++frame)
                screen.tick(booth.djDeckState(0));
            require(!screen.tiles.pictures.empty() && !screen.overview.pictures.empty(), "The file deck's screen has its pictures");
            const auto* playing = screen.track;
            const auto firstIndex = screen.tiles.pictures.front().index;
            const auto firstTile = screen.tiles.pictures.front().image;
            const auto firstOverview = screen.overview.pictures.front().image;
            require(booth.loadDjDeckFile(0, otherFile.getFile()).wasOk() && booth.djDeckInfo(0).loading,
                    "Another file is being read onto the deck");
            for (int frame = 0; frame < 4; ++frame)
                screen.tick(booth.djDeckState(0));
            const auto* kept = screen.tiles.find(firstIndex);
            require(screen.track == playing && kept != nullptr && kept->image == firstTile && !screen.overview.pictures.empty()
                        && screen.overview.pictures.front().image == firstOverview,
                    "and the screen goes on showing the file still playing, with the pictures it had");
            require(waitForTheFile() && booth.djDeckTrack(0) != playing, "The new file lands");
            require(screen.track == booth.djDeckTrack(0) && screen.tiles.pictures.empty() && screen.standIns.pictures.empty()
                        && screen.overview.pictures.empty() && screen.overviewStandIn.pictures.empty(),
                    "and the first file's pictures go with it");
            screen.tick(booth.djDeckState(0));
            require(!screen.tiles.pictures.empty() && screen.standIns.pictures.empty() && screen.overviewStandIn.pictures.empty(),
                    "The new file's own pictures come in, with nothing standing in for them");
            require(booth.loadDjDeckFile(0, beatFile.getFile()).wasOk() && waitForTheFile(), "The beat goes back on the deck");
            for (int frame = 0; frame < 10; ++frame)
                screen.tick(booth.djDeckState(0));
        }

        // The arithmetic under the screens.
        {
            require(djwave::scrollFor(1.0, 33.75, 1.25) == 42 && djwave::scrollFor(-0.5, 40.0, 1.0) == -20,
                    "The strip scrolls by whole device pixels");
            const auto range = djwave::tilesTouching(-100.0, 300.0, 256);
            require(range.first == -1 && range.last == 1 && djwave::tilesTouching(0.0, 256.0, 256).last == 0,
                    "and shows the tiles its edges touch");
            require(djwave::tilesCover({0, 1, 2}, 100.0, 50.0, 250.0) && djwave::tilesCover({3, 2}, 50.0, 110.0, 190.0)
                        && !djwave::tilesCover({0, 2}, 100.0, 50.0, 250.0) && !djwave::tilesCover({}, 100.0, 0.0, 10.0)
                        && !djwave::tilesCover({1}, 100.0, 50.0, 150.0),
                    "Tiles stand in for a stretch of the strip only when they cover it without a gap");
            struct Event
            {
                double beat;
                int note, velocity;
                bool on;
            };
            const std::vector<Event> events {{0.0, 60, 100, true}, {0.5, 60, 90, true}, {1.0, 60, 0, false},
                                             {1.5, 64, 80, true}, {2.0, 62, 70, true}, {2.5, 62, 0, false}};
            const auto spans = djwave::noteSpans(events);
            require(spans.size() == 4, "Every note-on is a note");
            require(spans[0].startBeat == 0.0 && spans[0].endBeat == 1.0 && spans[1].startBeat == 0.5 && spans[1].endBeat == 1.0,
                    "Two notes of one pitch both end at the next note-off of it");
            require(spans[2].note == 64 && spans[2].endBeat == 1.75, "A note with no note-off runs a quarter beat");
            require(spans[3].note == 62 && spans[3].endBeat == 2.5 && spans[3].velocity == 70, "and a note keeps its velocity");
        }
