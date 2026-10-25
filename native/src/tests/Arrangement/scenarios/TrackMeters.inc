        // The meters on the track cards: every track carries one of the
        // engine's level meters behind its fader, the session reads what each
        // measured since the last read, and each card shows it. The chain is
        // played by hand, every device in order over one block, as the song's
        // graph would; the graph puts the same meter in the same place and
        // feeds the same measurer (LevelMeasurerProcessingNode at 4536d8a).
        {
            constexpr double meterRate = 48000.0;
            constexpr int meterBlock = 512;
            auto document = std::make_unique<Session>();
            auto& song = *document;
            const auto trackAt = [&song](int track) -> te::AudioTrack& { return *te::getAudioTracks(*song.edit)[track]; };
            const auto metersOn = [](te::AudioTrack& track)
            {
                int count = 0;
                for (auto* plugin : track.pluginList)
                    if (dynamic_cast<te::LevelMeterPlugin*>(plugin) != nullptr)
                        ++count;
                return count;
            };
            const auto meterBehindFader = [](te::AudioTrack& track)
            {
                for (int i = 0; i + 1 < track.pluginList.size(); ++i)
                    if (track.pluginList[i] == track.getVolumePlugin())
                        return track.pluginList[i + 1] == track.getLevelMeterPlugin();
                return false;
            };
            // A 1 kHz sine at half scale on both sides, -6.02 dBFS at its
            // peaks, through every device of the track.
            juce::int64 sample = 0;
            const auto play = [&sample](te::AudioTrack& track, int blocks)
            {
                juce::AudioBuffer<float> buffer(2, meterBlock);
                te::MidiMessageArray midi;
                for (int block = 0; block < blocks; ++block)
                {
                    for (int i = 0; i < meterBlock; ++i, ++sample)
                    {
                        const auto value = 0.5f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0
                                                                              * static_cast<double>(sample) / meterRate));
                        buffer.setSample(0, i, value);
                        buffer.setSample(1, i, value);
                    }
                    midi.clear();
                    for (auto* plugin : track.pluginList)
                        if (plugin != nullptr && plugin->isEnabled())
                            plugin->applyToBuffer(te::PluginRenderContext(&buffer, 0, meterBlock, &midi, 0.0, {},
                                                                          false, false, false, false));
                }
            };
            // What a track reads after a few blocks at its current settings,
            // the fader's ramp to a new level played out first.
            const auto settledReading = [&song, &play, &trackAt](int track)
            {
                play(trackAt(track), 2);
                song.takeOutputLevels();
                play(trackAt(track), 2);
                return song.takeOutputLevels()[static_cast<size_t>(track)];
            };

            scenario("track meters: every track carries a meter straight behind its fader");
            {
                int added = -1;
                require(song.addTrack(Session::TrackType::audio, -1, &added).wasOk() && added >= 0,
                        "A track added after the starter stack");
                require(song.groupTracks({0, 1}).wasOk(), "Two tracks grouped onto a bus");
                for (int track = 0; track < song.trackCount(); ++track)
                    require(metersOn(trackAt(track)) == 1 && meterBehindFader(trackAt(track)),
                            ("Track " + juce::String(track + 1) + " - starter, added or a bus - carries one meter, "
                             "straight behind its fader").toRawUTF8());
                // Hidden, as the fader is: the rack shows what the person put there.
                for (int track = 0; track < song.trackCount(); ++track)
                    for (const auto& slot : song.deviceSlots(track))
                        require(slot.pluginIndex < 0 || trackAt(track).pluginList[slot.pluginIndex] != trackAt(track).getLevelMeterPlugin(),
                                "No card's rack offers the meter as a device");
                const auto levels = song.takeOutputLevels();
                require(static_cast<int>(levels.size()) == song.trackCount() + 1,
                        "One reading per track and one for the main output");
                for (int track = 0; track < song.trackCount(); ++track)
                    require(levels[static_cast<size_t>(track)].leftDb <= -100.0f && levels[static_cast<size_t>(track)].rightDb <= -100.0f,
                            "A track nothing has played through reads as silence");
            }

            scenario("track meters: a meter reads after the fader, and what the song cannot hear reads as silence");
            {
                // The last track: an audio lane outside the group, playing
                // what it is given straight through.
                const auto lane = song.firstReturnTrack() - 1;
                require(!song.isGroupBusTrack(lane) && song.trackType(lane) == Session::TrackType::audio,
                        "An audio lane to play a tone through");
                for (auto* plugin : trackAt(lane).pluginList)
                    plugin->baseClassInitialise({{}, meterRate, meterBlock});
                require(song.setTrackVolumeDb(lane, 0.0f).wasOk(), "The fader at 0 dB");
                const auto unity = settledReading(lane);
                require(unity.leftDb > -10.5f && unity.leftDb < -5.5f && std::abs(unity.rightDb - unity.leftDb) < 0.01f,
                        ("A tone peaking at -6 dB reads near it on both sides at 0 dB: "
                         + juce::String(unity.leftDb, 2) + " / " + juce::String(unity.rightDb, 2)).toRawUTF8());
                require(song.setTrackVolumeDb(lane, -6.0f).wasOk(), "The fader down 6 dB");
                const auto down = settledReading(lane);
                require(std::abs(down.leftDb - unity.leftDb + 6.0f) < 0.1f,
                        ("The meter is after the fader: 6 dB down on it reads 6 dB down, "
                         + juce::String(down.leftDb - unity.leftDb, 2)).toRawUTF8());
                play(trackAt(lane), 2);
                song.takeOutputLevels();
                require(song.takeOutputLevels()[static_cast<size_t>(lane)].leftDb <= -100.0f,
                        "A read takes the peaks with it, so nothing played since reads as silence");

                // The engine mutes after the chain, so the meter still measures
                // a muted track; the reading says what the song hears.
                song.setTrackMuted(lane, true);
                require(settledReading(lane).leftDb <= -100.0f, "A muted track reads as silence");
                song.setTrackMuted(lane, false);
                require(std::abs(settledReading(lane).leftDb - down.leftDb) < 0.1f, "and unmuted reads again");
                song.setTrackSoloed(0, true);
                require(settledReading(lane).leftDb <= -100.0f, "A track a solo elsewhere silences reads as silence");
                song.setTrackSoloed(0, false);
                require(std::abs(settledReading(lane).leftDb - down.leftDb) < 0.1f, "and reads again once the solo is off");
                for (auto* plugin : trackAt(lane).pluginList)
                    plugin->baseClassDeinitialise();
            }

            scenario("track meters: every card and the main row open with a meter, ahead of their controls");
            {
                // Tall enough for every card, the two returns at the foot
                // included.
                auto panel = std::make_unique<Arrangement>(song);
                panel->setSize(1200, 1000);
                require(static_cast<int>(panel->meters.size()) == song.trackCount(), "One meter per card");
                for (int track = 0; track < song.trackCount(); ++track)
                {
                    const auto index = static_cast<size_t>(track);
                    const auto& meter = *panel->meters[index];
                    const auto row = panel->lane(track);
                    const auto indent = static_cast<int>(panel->trackIndent(track));
                    require(meter.isVisible() && meter.isOpaque() && meter.getHeight() > 0,
                            "Each card's meter shows, and paints all of itself so a frame repaints nothing behind it");
                    require(meter.getX() == Arrangement::cardMeterInset + indent && meter.getWidth() == LevelMeter::width,
                            "at the start of the card, pushed in with the rest of it by a group");
                    require(meter.getRight() < panel->mute[index]->getX() && meter.getRight() < panel->volume[index]->getX(),
                            "and ahead of the buttons and the faders");
                    // In whichever container the card's controls are, the
                    // lanes' or the returns' pinned over the main row.
                    const auto top = static_cast<float>(meter.getY() + meter.getParentComponent()->getY());
                    require(top >= row.getY() && top + static_cast<float>(meter.getHeight()) <= row.getBottom() - Arrangement::trackDividerThickness,
                            "and inside its row, above the rule under it");
                }
                const auto& main = panel->masterMeter;
                const auto master = panel->masterLane();
                require(main.isVisible() && main.getX() == Arrangement::cardMeterInset
                            && main.getRight() < panel->masterVolume.getX()
                            && static_cast<float>(main.getY()) >= master.getY()
                            && static_cast<float>(main.getBottom()) <= master.getBottom() && main.getHeight() > 0,
                        "The main row opens with its meter too, ahead of its faders and inside the row");

                // What a fed meter draws: the left side lit green from the
                // bottom, the right side dark, and red at the top past 0 dB.
                auto& meter = *panel->meters[0];
                const auto pixel = [&meter](int x, int y)
                {
                    return meter.createComponentSnapshot(meter.getLocalBounds(), true, 1.0f).getPixelAt(x, y);
                };
                const auto lit = [](juce::Colour colour) { return colour.getBrightness() > 0.35f; };
                meter.feed(-6.0f, -100.0f, 0.02);
                const auto bottom = meter.getHeight() - 2;
                const auto low = pixel(1, bottom);
                require(lit(low) && low.getGreen() > low.getRed() && low.getGreen() > low.getBlue(),
                        "A level lights its side green from the bottom");
                require(!lit(pixel(LevelMeter::barWidth + LevelMeter::barGap + 1, bottom)), "and leaves the silent side dark");
                require(!lit(pixel(1, 1)), "and nothing above where -6 dB reaches");
                meter.feed(3.0f, 3.0f, 0.02);
                const auto high = pixel(1, juce::roundToInt(meter.getHeight() * 0.08f));
                require(lit(high) && high.getRed() > high.getGreen() && high.getRed() > high.getBlue(),
                        "Past full scale the top of the bar is red");

                // To look at: the cards with every meter at a different level,
                // a held peak above a falling bar on each, written only when
                // asked for.
                if (const auto path = juce::SystemStats::getEnvironmentVariable("RHINO_TRACK_METERS_SNAPSHOT", {});
                    path.isNotEmpty())
                {
                    // One row folded, so both ways the arrow points are in it.
                    require(song.setTrackFolded(song.firstReturnTrack() - 2, true).wasOk(), "A row folds for the picture");
                    const float levels[] {-30.0f, -9.0f, -2.0f, 2.0f, -48.0f, -14.0f};
                    for (size_t track = 0; track < panel->meters.size(); ++track)
                    {
                        const auto db = levels[track % std::size(levels)];
                        panel->meters[track]->feed(db + 6.0f, db + 4.0f, 0.02);
                        panel->meters[track]->feed(db, db - 3.0f, 0.3);
                    }
                    panel->masterMeter.feed(-4.0f, -6.0f, 0.02);
                    panel->masterMeter.feed(-10.0f, -12.0f, 0.3);
                    const juce::File file(path);
                    file.deleteFile();
                    if (auto stream = file.createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(
                            panel->createComponentSnapshot({0, 0, 420, panel->getHeight()}, true, 2.0f), *stream);
                }
                panel.reset();
            }
        }
