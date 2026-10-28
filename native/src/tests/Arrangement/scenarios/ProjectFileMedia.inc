        // Where a saved document finds the audio its clips play, and what the
        // WAV export does with a clip whose file it cannot find or read. A file
        // beside a document is stored relative to it, so a project folder can
        // move whole; a document saved as a file in another folder has to name
        // each file again for that file, or every name points into a folder
        // beside it that was never there. And a render waits for every clip's
        // file to open, so the export refuses a file that is gone and gives up
        // on one it cannot read, where it used to hold every file command and
        // the window's close button for good. Documents of their own, written
        // to disk and opened from it as the app opens one; they render, so
        // they come before the audio device closes.
        {
            const auto media = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("Rhino project media " + juce::String(juce::Time::getHighResolutionTicks()));
            const auto songs = media.getChildFile("Songs"), samples = media.getChildFile("Samples");
            require(songs.createDirectory().wasOk() && samples.createDirectory().wasOk(), "A folder for songs and one for samples");
            const auto loop = samples.getChildFile("Loop.wav"), laterLoop = samples.getChildFile("Later loop.wav");
            require(source.getFile().copyFileTo(loop) && source.getFile().copyFileTo(laterLoop), "with two samples in it");
            const auto writeDocument = [](const juce::ValueTree& state, const juce::File& file)
            {
                const auto xml = state.createXml();
                return xml != nullptr && xml->writeTo(file);
            };
            const auto openDocument = [](Session& document, const juce::File& file)
            {
                const auto xml = juce::parseXML(file);
                return xml != nullptr && document.restoreProject(juce::ValueTree::fromXml(*xml), file).wasOk();
            };
            const auto nameOf = [](Session& document, te::EditItemID clip)
            {
                auto* audio = document.findAudioClip(clip);
                return audio != nullptr ? audio->state[te::IDs::source].toString() : juce::String();
            };
            const auto fileOf = [](Session& document, te::EditItemID clip)
            {
                auto* audio = document.findAudioClip(clip);
                return audio != nullptr ? audio->getSourceFileReference().getFile() : juce::File();
            };
            // The level of the loud half of the fixture's first second in a
            // WAV the export wrote.
            const auto writtenLevel = [&require](const juce::File& file)
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                require(reader != nullptr && reader->lengthInSamples > static_cast<juce::int64>(reader->sampleRate),
                        "and what it wrote reads back");
                juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
                require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "every sample of it");
                const auto from = static_cast<int>(0.6 * reader->sampleRate), until = static_cast<int>(0.95 * reader->sampleRate);
                return audio.getRMSLevel(0, from, until - from);
            };
            // The song rendered as Export WAV renders it, and that level.
            const auto exportedLevel = [&require, &writtenLevel](Session& document, const juce::File& file)
            {
                const auto rendered = Session::renderTimelineWav(*document.edit, file, document.edit->getLength().inSeconds(),
                                                                 48000.0, 512);
                require(rendered.wasOk(), ("The song renders as Export WAV renders it: " + rendered.getErrorMessage()).toRawUTF8());
                return writtenLevel(file);
            };
            // The first audio lane of a new document.
            const auto lane = 2;
            const auto original = songs.getChildFile("Set.rhinoedit");
            const auto copy = media.getChildFile("Elsewhere").getChildFile("Deeper").getChildFile("Set copy.rhinoedit");
            require(copy.getParentDirectory().createDirectory().wasOk(), "and a folder for a copy, at another depth");
            {
                auto song = std::make_unique<Session>();
                require(writeDocument(song->projectSnapshot(), original) && openDocument(*song, original),
                        "A document opened from its file");
                require(song->importAudioAt(loop, lane, 0.0).wasOk(), "with a sample imported onto its audio lane");
                const auto first = te::getAudioTracks(*song->edit)[lane]->getClips().getFirst()->itemID;
                const auto storedName = nameOf(*song, first);
                require(!juce::File::isAbsolutePath(storedName), ("stored relative to the document: " + storedName).toRawUTF8());
                require(writeDocument(song->projectSnapshot(original), original), "and saved where it was opened");

                // Moved to a folder at another depth, as a file is moved, its
                // samples left behind: the name it stores points at nothing.
                scenario("project files: the export refuses a sample that is not where the document says");
                {
                    const auto moved = media.getChildFile("Moved").getChildFile("Away").getChildFile("Set.rhinoedit");
                    require(moved.getParentDirectory().createDirectory().wasOk() && original.copyFileTo(moved), "The document moved");
                    auto strayed = std::make_unique<Session>();
                    require(openDocument(*strayed, moved) && !fileOf(*strayed, first).existsAsFile(),
                            "opens with its sample nowhere to be found");
                    const auto asked = juce::Time::getMillisecondCounterHiRes();
                    const auto refusal = strayed->checkTimelineFiles();
                    require(refusal.failed() && refusal.getErrorMessage().contains(loop.getFileName())
                                && juce::Time::getMillisecondCounterHiRes() - asked < 2000.0,
                            ("and the export refuses it at once, naming the file: " + refusal.getErrorMessage()).toRawUTF8());
                    // The refusal changes nothing, so the document still saves
                    // and opens: the export used to hold every save until the
                    // app was killed.
                    const auto rescued = media.getChildFile("Moved").getChildFile("Rescued.rhinoedit");
                    require(writeDocument(strayed->projectSnapshot(rescued), rescued), "The document saves after the refusal");
                    require(openDocument(*strayed, rescued) && strayed->findAudioClip(first) != nullptr,
                            "and opens again with its clip");

                    // A file that is there but cannot be read passes that check,
                    // and a render waits on it as it waits on a missing one: the
                    // export gives up once the render stops moving, and the file
                    // it was writing goes.
                    scenario("project files: the export gives up on a sample it cannot read");
                    const auto unreadable = samples.getChildFile("Unreadable.wav");
                    require(unreadable.replaceWithText("These bytes are not audio, whatever the name says."), "A file that is not audio");
                    strayed->findAudioClip(first)->state.setProperty(te::IDs::source, unreadable.getFullPathName(), nullptr);
                    require(strayed->checkTimelineFiles().wasOk(), "named by the clip, is there, so the export starts");
                    const auto written = media.getChildFile("Unreadable export.wav");
                    const auto started = juce::Time::getMillisecondCounterHiRes();
                    const auto rendered = Session::renderTimelineWav(*strayed->edit, written, strayed->edit->getLength().inSeconds(),
                                                                     48000.0, 512, 1.5);
                    const auto took = juce::Time::getMillisecondCounterHiRes() - started;
                    require(rendered.failed() && rendered.getErrorMessage().contains("no progress") && took < 10000.0,
                            ("and gives up when the render stops moving, after " + juce::String(took, 0) + " ms: "
                             + rendered.getErrorMessage()).toRawUTF8());
                    require(!written.exists(), "deleting what it was writing");
                }

                scenario("project files: a Save As to another folder names the samples for the copy");
                const auto saved = song->projectSnapshot(copy);
                require(writeDocument(saved, copy), "The document saved as a copy two folders deeper");
                song->projectSaved(saved, copy);
                require(nameOf(*song, first) == storedName,
                        "while the open document's clip keeps its name: a new one would restart it under playback");
                // The copy is opened twice, as the app opens one: into the
                // same session, which replaces what it had.
                auto reopened = std::make_unique<Session>();
                {
                    require(openDocument(*reopened, copy), "The copy opens");
                    require(fileOf(*reopened, first) == loop,
                            ("and finds the sample where it is: " + fileOf(*reopened, first).getFullPathName()).toRawUTF8());
                    require(!juce::File::isAbsolutePath(nameOf(*reopened, first)),
                            "named relative to the copy, so the folder still moves whole");
                    const auto level = exportedLevel(*reopened, media.getChildFile("Copy export.wav"));
                    require(level > 0.1f, ("and the song plays it: " + juce::String(level, 3)).toRawUTF8());

                    // Ctrl+J reaches the timeline from the DJ view too, and the
                    // selection it leaves is read off the timeline's list of
                    // the clips: hidden, the list was the one from before the
                    // merge, the merged clip was not on it, and the line went.
                    scenario("hidden timeline: a merge leaves the line on what it made");
                    require(reopened->importAudioAt(loop, lane, 1.0).wasOk(), "A second clip flush against the first");
                    const auto second = te::getAudioTracks(*reopened->edit)[lane]->getClips().getLast()->itemID;
                    juce::Component shell;
                    auto timeline = std::make_unique<Arrangement>(*reopened);
                    shell.addChildComponent(*timeline);
                    timeline->setBounds(0, 0, 1240, 720);
                    timeline->setSelection({first, second}, first);
                    timeline->mergeSelected();
                    const auto lanes = te::getAudioTracks(*reopened->edit)[lane]->getClips();
                    require(lanes.size() == 1 && lanes.getFirst()->itemID != first, "The hidden timeline merges the two");
                    require(timeline->timeSelection.active && close(timeline->timeSelection.start, 0.0)
                                && close(timeline->timeSelection.end, 2.0),
                            "and its line spans what the merge made");
                    timeline.reset();
                }

                // Imported, recorded or merged after the Save As: the open
                // edit names it as it names everything else, against the file
                // it was opened from, and saving names it for the copy.
                scenario("project files: what arrives after a Save As is named for the copy");
                require(song->importAudioAt(laterLoop, lane, 2.0).wasOk(), "A sample imported after the Save As");
                const auto later = te::getAudioTracks(*song->edit)[lane]->getClips().getLast()->itemID;
                const auto takes = song->recordingDirectory();
                require(takes == copy.getSiblingFile("Set copy Recordings"),
                        ("A take recorded now goes beside the copy: " + takes.getFullPathName()).toRawUTF8());
                const auto take = takes.getChildFile("Audio Take 1.wav");
                require(takes.createDirectory().wasOk() && source.getFile().copyFileTo(take)
                            && song->importAudioAt(take, lane, 4.0).wasOk(),
                        "and one laid on the lane as a take is");
                const auto taken = te::getAudioTracks(*song->edit)[lane]->getClips().getLast()->itemID;
                require(writeDocument(song->projectSnapshot(copy), copy), "The copy saved again");
                {
                    require(openDocument(*reopened, copy), "and opened again");
                    require(fileOf(*reopened, later) == laterLoop && !juce::File::isAbsolutePath(nameOf(*reopened, later)),
                            ("The sample imported after the Save As is named relative to the copy: " + nameOf(*reopened, later)).toRawUTF8());
                    require(fileOf(*reopened, taken) == take && nameOf(*reopened, taken).startsWith("Set copy Recordings"),
                            ("and so is the take beside it: " + nameOf(*reopened, taken)).toRawUTF8());
                    require(fileOf(*reopened, first) == loop, "with the first sample still found");
                }
            }

            // Export WAV itself renders in steps, as the shell's loading screen
            // runs it: a slice hands the thread back, nothing undoes under it,
            // a cancel leaves the file that was there, and what the screen
            // drives to its end is the song.
            scenario("project files: the export renders in steps behind the loading screen");
            {
                auto song = std::make_unique<Session>();
                require(song->importAudioAt(loop, lane, 0.0).wasOk(), "A song with a sample on it");
                const auto target = media.getChildFile("Stepped export.wav");
                require(target.replaceWithText("An earlier export"), "and a file where its export will go");
                const auto beside = [&media] { return media.findChildFiles(juce::File::findFiles, false, "Stepped export*").size(); };
                require(song->startExport(target).wasOk() && song->isExporting(), "An export can be started and left running");
                require(song->startExport(target).failed() && song->startBounceTracks({lane}).failed(),
                        "and nothing else renders until it is over");
                require(song->continueExport(0.0), "A slice renders and hands the thread back with work left");
                require(song->exportProgress() >= 0.0f && song->exportProgress() < 1.0f, "and says how far it has got");
                song->undo();
                require(song->isExporting() && te::getAudioTracks(*song->edit)[lane]->getClips().size() == 1,
                        "Undo waits rather than pulling the sample away");
                song->cancelExport();
                require(!song->isExporting() && song->exportOutcome().getErrorMessage().contains("cancelled"),
                        "Cancelling stops it");
                require(target.loadFileAsString() == "An earlier export" && beside() == 1,
                        "leaving the file that was there and nothing beside it");

                // The screen the shell shows, driving the real thing.
                LoadingScreen screen;
                screen.setSize(900, 600);
                auto finished = 0;
                require(song->startExport(target).wasOk(), "It starts again");
                screen.run({"Exporting WAV",
                            [&song](double milliseconds) { return song->continueExport(milliseconds); },
                            [&song] { return std::make_pair(song->exportProgress(), juce::String("Rendering")); },
                            [&song] { song->cancelExport(); },
                            [&finished] { ++finished; }});
                require(screen.isRunning() && screen.isVisible(), "The loading screen shows while it runs");
                for (auto frames = 0; screen.isRunning(); ++frames)
                {
                    require(frames < 10000, "The loading screen finishes the export");
                    screen.timerCallback();
                }
                require(finished == 1 && !screen.isVisible() && song->exportOutcome().wasOk(),
                        ("and goes once it is done: " + song->exportOutcome().getErrorMessage()).toRawUTF8());
                const auto level = writtenLevel(target);
                require(level > 0.1f && beside() == 1,
                        ("The export replaced the file with the song: " + juce::String(level, 3)).toRawUTF8());
            }

            // Every session reading these files is gone by now.
            media.deleteRecursively();
        }
