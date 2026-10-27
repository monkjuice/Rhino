        // The Loop switch in the note editor's tools panel, Live's clip Loop: a looped clip
        // repeats its pattern for as long as it is dragged out, and the editor
        // shows the pattern that repeats. The engine does the repeating, so
        // what is checked is what the engine plays, what the arrangement draws
        // and, once, what a render sounds like. Renders, so before the audio
        // device closes. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& song = *document;
            constexpr int lane = 0;
            require(song.trackType(lane) == Session::TrackType::midi && song.addDevice("FourOsc", lane).wasOk(),
                    "The starter's first track is MIDI, given an instrument to hear the loop through");
            te::EditItemID looped;
            require(song.createClip(lane, 0.0, &looped).wasOk() && song.selectPatternClip(looped).wasOk(),
                    "A one-bar clip, open in the editor");
            song.setEditorStepCount(16);
            require(song.editorStepCount() == 16, "One bar of sixteenths");
            const auto bar = song.beatsToSeconds(4.0);
            song.beginNoteGesture("Loop fixture");
            require(song.addNote(0.0, 60, 4.0).wasOk(), "A note on the bar's first beat");
            song.endNoteGesture();
            auto* loopClip = dynamic_cast<te::MidiClip*>(song.findClip(looped));
            require(loopClip != nullptr, "The clip is a MIDI clip");
            // Where the engine plays a pitch, in beats from the clip's content
            // start: the loop laid end to end when it loops.
            const auto played = [&loopClip](int pitch)
            {
                std::vector<double> starts;
                for (auto* note : loopClip->getSequenceLooped().getNotes())
                    if (note->getNoteNumber() == pitch)
                        starts.push_back(note->getStartBeat().inBeats());
                std::sort(starts.begin(), starts.end());
                return starts;
            };
            // The clip's right edge dragged, as the arrangement drags it.
            const auto stretchTo = [&song, &loopClip, looped](double end)
            {
                const auto position = loopClip->getPosition();
                return song.editClip(looped, {position.time.getStart().inSeconds(), end, position.offset.inSeconds()},
                                     ClipGesture::trimRight);
            };

            scenario("clip loop: off, a clip dragged longer adds silence");
            require(!song.isPatternLooping(), "A new clip plays once");
            require(stretchTo(4.0 * bar).wasOk() && song.editorStepCount() == 64,
                    "Dragged out to four bars, the editor shows all four");
            require(played(60).size() == 1, "and the clip plays its note once");
            require(stretchTo(bar).wasOk() && song.editorStepCount() == 16, "Back to one bar");

            scenario("clip loop: the switch repeats the bar for as long as the clip runs");
            auto editor = std::make_unique<StepGrid>(song);
            editor->setSize(1000, 300);
            editor->changeListenerCallback(nullptr);
            juce::String said;
            editor->status = [&said](juce::String message) { said = std::move(message); };
            // The switch is the tools panel's, beside the editor as the shell
            // lays it out, and larger than the panel's other controls.
            auto tools = std::make_unique<NoteToolsPanel>(song, *editor);
            tools->setSize(NoteToolsPanel::width, StepGrid::openingHeight);
            auto& loopSwitch = tools->loopButton;
            require(loopSwitch.getHeight() > tools->highlightButton.getHeight()
                        && loopSwitch.getWidth() >= tools->scaleBox.getRight() - tools->rootBox.getX(),
                    "The tools panel's Loop switch is taller than its other controls and as wide as its rows");
            require(loopSwitch.isEnabled() && !loopSwitch.getToggleState(),
                    "and it is off for a clip that plays once");
            loopSwitch.onClick();
            require(song.isPatternLooping() && loopSwitch.getToggleState() && said.startsWith("Clip loops"),
                    "Pressed, it loops the clip, shows it does and says so");
            require(editor->loopButton.isVisible(), "The button over the keys, the song's loop over the clip, stays");
            require(close(loopClip->getLoopStartBeats().inBeats(), 0.0) && close(loopClip->getLoopLengthBeats().inBeats(), 4.0)
                        && close(loopClip->getPosition().time.getLength().inSeconds(), bar)
                        && close(loopClip->getPosition().offset.inSeconds(), 0.0),
                    "The loop is the bar the clip played, and the clip stays where and as long as it was");
            require(played(60).size() == 1, "so until it is dragged out it plays what it played before");
            require(stretchTo(2.0 * bar).wasOk(), "Dragged out to two bars");
            require(song.editorStepCount() == 16, "the editor still shows the one bar that repeats");
            {
                const auto starts = played(60);
                require(starts.size() == 2 && close(starts[0], 0.0) && close(starts[1], 4.0),
                        "and the engine plays the bar twice");
            }
            {
                auto arrangement = std::make_unique<Arrangement>(song);
                arrangement->setSize(1200, 600);
                arrangement->sync();
                const Arrangement::ClipView* drawn = nullptr;
                for (const auto& onScreen : arrangement->clips)
                    if (onScreen.id == looped) drawn = &onScreen;
                require(drawn != nullptr && drawn->midiNotes != nullptr, "The looped clip is on screen with its notes");
                std::vector<double> drawnStarts;
                const auto origin = drawn->position.start - drawn->position.offset;
                for (const auto& note : *drawn->midiNotes)
                    if (note.pitch == 60) drawnStarts.push_back(note.start + origin);
                std::sort(drawnStarts.begin(), drawnStarts.end());
                require(drawnStarts.size() == 2 && close(drawnStarts[1], bar),
                        "The arrangement draws the note again where the second pass plays it");

                // A clip fills its lane, and a looped one is notched at its top
                // where each pass begins; one that plays once is not.
                const auto box = arrangement->bounds(*drawn);
                const auto row = arrangement->lane(lane);
                require(close(box.getY(), row.getY())
                            && close(box.getBottom(), row.getBottom() - Arrangement::trackDividerThickness),
                        "A clip fills its lane from the lane's top to the rule under it");
                require(close(drawn->loopSeconds, bar), "The clip's view knows how long one pass is");
                const auto notchX = juce::roundToInt(arrangement->xFor(bar)) - 1;
                const auto notchY = static_cast<int>(box.getY()) + 3;
                require(notchX + 12 < static_cast<int>(box.getRight()) - 2, "The second pass begins on screen");
                const auto notched = [&arrangement, notchX, notchY]
                {
                    juce::Image image(juce::Image::RGB, arrangement->getWidth(), arrangement->getHeight(), true,
                                      juce::SoftwareImageType());
                    juce::Graphics g(image);
                    arrangement->paintEntireComponent(g, false);
                    return image.getPixelAt(notchX, notchY) != image.getPixelAt(notchX + 12, notchY);
                };
                require(notched(), "The looped clip is notched at its top where the second pass begins");
                // To look at: the looped clip on its lane, written only when asked for.
                if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_CLIP_LOOP_SNAPSHOT", {});
                    shot.isNotEmpty())
                {
                    // Zoomed to the clip, the view put back after.
                    const auto span = arrangement->viewSpan;
                    arrangement->viewSpan = 3.0 * bar;
                    juce::File(shot).deleteFile();
                    if (auto stream = juce::File(shot).createOutputStream())
                        juce::PNGImageFormat().writeImageToStream(
                            arrangement->createComponentSnapshot(row.expanded(0.0f, 30.0f).getSmallestIntegerContainer()
                                                                    .getIntersection(arrangement->getLocalBounds()),
                                                                 true, 2.0f),
                            *stream);
                    arrangement->viewSpan = span;
                }
                require(song.setPatternLooping(false).wasOk(), "Looping off for a moment");
                arrangement->sync();
                require(!notched(), "and a clip that plays once has no notch there");
                song.undo();
                require(song.isPatternLooping(), "Undo loops the clip again");
            }

            scenario("clip loop: a render hears the second pass");
            {
                // The note sounds for half a bar and then dies away. Without a
                // second pass the start of the second bar is quieter still
                // than the end of the first; with one, a new note starts there.
                juce::TemporaryFile rendered(".wav");
                const auto result = Session::renderTimelineWav(*song.edit, rendered.getFile(), 2.0 * bar, 48000.0, 512);
                require(result.wasOk(), ("The looped clip renders: " + result.getErrorMessage()).toRawUTF8());
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
                require(reader != nullptr && reader->lengthInSamples >= static_cast<juce::int64>(1.9 * bar * reader->sampleRate),
                        "and what it wrote reads back");
                juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "every sample of it");
                const auto level = [&audio, &reader](double from, double until)
                {
                    const auto first = static_cast<int>(from * reader->sampleRate);
                    const auto last = std::min(audio.getNumSamples(), static_cast<int>(until * reader->sampleRate));
                    return audio.getRMSLevel(0, first, last - first);
                };
                const auto firstPass = level(0.02, 0.2 * bar);
                const auto beforeSecond = level(0.75 * bar, 0.97 * bar);
                const auto secondPass = level(bar + 0.02, 1.2 * bar);
                require(firstPass > 0.001f, ("The first pass sounds: " + juce::String(firstPass)).toRawUTF8());
                require(secondPass > 4.0f * beforeSecond && secondPass > 0.5f * firstPass,
                        ("and the note starts again at the second bar, as loud: " + juce::String(secondPass) + " after "
                         + juce::String(beforeSecond) + ", first pass " + juce::String(firstPass)).toRawUTF8());
            }

            scenario("clip loop: the editor's playhead and its edits are the loop's");
            require(close(song.patternStepAt(bar + song.beatsToSeconds(1.0)), 4.0),
                    "A beat into the second pass is the editor's fourth step");
            require(song.patternStepAt(2.0 * bar) < 0.0, "and past the clip's end there is no step");
            require(std::abs(editor->playheadXForTime(bar + song.beatsToSeconds(1.0)) - editor->xForStep(4.0)) < 0.01f,
                    "The editor draws its playhead on that step in the second pass");
            song.beginNoteGesture("Loop edit");
            require(song.addNote(8.0, 67, 2.0).wasOk(), "A note drawn in the loop");
            song.endNoteGesture();
            {
                const auto starts = played(67);
                require(starts.size() == 2 && close(starts[0], 2.0) && close(starts[1], 6.0), "plays on both passes");
            }
            song.beginNoteGesture("Room for a paste");
            require(song.ensurePatternLengthSteps(32) && close(loopClip->getLoopLengthBeats().inBeats(), 8.0)
                        && song.editorStepCount() == 32 && close(loopClip->getPosition().time.getLength().inSeconds(), 2.0 * bar),
                    "Room for a paste past the loop's end makes the loop longer, and a clip long enough already keeps its length");
            song.endNoteGesture();
            song.undo();
            require(close(loopClip->getLoopLengthBeats().inBeats(), 4.0) && song.editorStepCount() == 16,
                    "Undo puts the one-bar loop back");

            scenario("clip loop: copies loop as the clip does");
            {
                const auto region = song.copyClipRegion(0.0, 2.0 * bar, lane, lane);
                std::vector<te::EditItemID> pasted;
                require(song.pasteClipRegion(region, 0.0, 2, pasted).wasOk() && pasted.size() == 1,
                        "The looped clip pastes onto the other MIDI track");
                auto* copy = dynamic_cast<te::MidiClip*>(song.findClip(pasted.front()));
                require(copy != nullptr && copy->isLooping() && close(copy->getLoopLengthBeats().inBeats(), 4.0),
                        "and the copy loops the same bar");
                int copied = 0;
                for (auto* note : copy->getSequenceLooped().getNotes())
                    if (note->getNoteNumber() == 60) ++copied;
                require(copied == 2, "twice over the two bars it covers");
                require(song.copyClipToTrackSlot(looped, lane, 0).wasOk(), "The looped clip copies into a DJ console's cell");
                auto* cell = song.clipSlotAt(lane, 0) != nullptr ? song.clipSlotAt(lane, 0)->getClip() : nullptr;
                require(cell != nullptr && cell->isLooping() && close(cell->getLoopLengthBeats().inBeats(), 4.0)
                            && close(cell->getPosition().time.getLength().inSeconds(), bar),
                        "where it is one pass of the bar it loops, not the two bars it was dragged to");
            }

            scenario("clip loop: off, the clip keeps its length and plays once");
            require(song.editClip(looped, {song.beatsToSeconds(1.0), 2.0 * bar, song.beatsToSeconds(1.0)},
                                  ClipGesture::trimLeft).wasOk(),
                    "A beat trimmed from the looped clip's start");
            require(close(song.patternStepAt(song.beatsToSeconds(1.0)), 4.0), "It opens a beat into its loop");
            require(song.editorStepCount() == 16, "and the editor still shows the whole loop");
            loopSwitch.onClick();
            require(!song.isPatternLooping() && !loopSwitch.getToggleState() && said == "Clip plays once",
                    "Pressed again, the switch is off");
            require(close(loopClip->getPosition().time.getStart().inSeconds(), song.beatsToSeconds(1.0))
                        && close(loopClip->getPosition().time.getEnd().inSeconds(), 2.0 * bar),
                    "The clip keeps its place and its length");
            require(close(loopClip->getPosition().offset.inSeconds(), song.beatsToSeconds(1.0)),
                    "and opens where the loop had reached");
            require(played(67).size() == 1 && song.editorStepCount() == 28,
                    "It plays its notes once, and the editor shows the whole clip");
            song.undo();
            require(song.isPatternLooping() && loopSwitch.getToggleState(), "Undo loops it again");

            tools.reset();
            editor.reset();
            song.releaseAudioDevice();
        }
