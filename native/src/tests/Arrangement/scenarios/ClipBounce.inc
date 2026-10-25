        // A bounce renders, so what it makes is measured rather than read back
        // off the clips. What is worth holding: a track is bounced after its
        // devices and before its fader, which the new track takes over; a
        // group is bounced after its bus, fader included; a track in a group
        // can be bounced on its own at all, which the engine does not do by
        // itself; and the source is muted, so the song sounds as it did.
        //
        // Each session is given a project file in a scratch folder, so the
        // bounced audio lands beside a project and the folder takes it away.
        const auto bounceScratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("Rhino bounce test " + juce::String(juce::Time::getHighResolutionTicks()));
        require(bounceScratch.createDirectory().wasOk(), "A scratch folder for the bounced audio");
        // The RMS of the first channel of a file between two times.
        const auto bounceRms = [&require](const juce::File& file, double fromSeconds, double toSeconds)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            require(reader != nullptr, "The bounced file can be read back");
            juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, true), "Read the bounced samples");
            const auto from = juce::jlimit(0, audio.getNumSamples() - 1, static_cast<int>(fromSeconds * reader->sampleRate));
            const auto to = juce::jlimit(from + 1, audio.getNumSamples(), static_cast<int>(toSeconds * reader->sampleRate));
            return audio.getRMSLevel(0, from, to - from);
        };
        // The fixture is a sine at 0.1 for its first half and 0.7 for its
        // second, so a loud half read straight off the file is about 0.49.
        {
            Session bounce;
            bounce.projectFile = bounceScratch.getChildFile("clips.rhinoedit");
            require(bounce.importAudio(source.getFile()).wasOk(), "A file imports for the bounce fixture");
            const auto lane = 1;
            auto* track = te::getAudioTracks(*bounce.edit)[lane];
            const auto firstID = track->getClips().getFirst()->itemID;
            require(bounce.importAudioAt(source.getFile(), lane, 2.0).wasOk(), "A second clip a second after the first");
            const auto secondID = track->getClips().getLast()->itemID;
            // If the fader reached the file, the loud halves would read a
            // quarter of what they do.
            require(bounce.setTrackVolumeDb(lane, -12.0f).wasOk() && bounce.setTrackPan(lane, 0.5f).wasOk(),
                    "The lane's fader and pan are moved before bouncing");

            Session::BounceResult made;
            require(bounce.bounceClipsToNewTracks({firstID, secondID}, &made).wasOk(), "Two clips bounce");
            require(bounce.firstReturnTrack() == 5 && made.tracks.size() == 1 && made.tracks.front() == lane + 1,
                    "to one new track, directly under the lane they came from");
            const auto bounced = made.tracks.front();
            require(bounce.trackName(bounced) == bounce.trackName(lane) + " (Bounce)",
                    "named for its source, as Live names a bounce");
            require(bounce.trackType(bounced) == Session::TrackType::audio, "and it is an audio track");
            require(bounce.trackColour(bounced) == bounce.trackColour(lane), "wearing its source's colour");
            require(made.sourceCount == 2 && made.clips.size() == 2, "One clip for each clip that went in");
            const auto first = bounce.audioClipMix(made.clips[0]);
            const auto second = bounce.audioClipMix(made.clips[1]);
            require(close(first.startSeconds, 0.0) && close(first.endSeconds, 1.0)
                        && close(second.startSeconds, 2.0) && close(second.endSeconds, 3.0),
                    "each over the span its source covered");
            require(first.sourceFile == second.sourceFile && close(second.offsetSeconds, 2.0),
                    "as two windows onto one render of the lane");
            require(first.sourceFile.isAChildOf(bounceScratch), "written beside the project");
            require(bounce.findClip(firstID)->isMuted() && bounce.findClip(secondID)->isMuted(),
                    "The sources are muted, so the song does not play them twice");
            require(close(bounce.trackMixer(bounced).volumeDb, -12.0) && close(bounce.trackMixer(bounced).pan, 0.5),
                    "The new track takes over the source's fader and pan");
            const auto loud = bounceRms(first.sourceFile, 0.6, 0.95);
            require(loud > 0.3f, "and the file is from before them: the fader is not baked in");
            require(bounceRms(first.sourceFile, 1.1, 1.9) < 0.001f, "The gap between the clips is silent");
            require(bounceRms(first.sourceFile, 2.6, 2.95) > 0.3f, "and the second clip is there too");
            require(te::getAudioTracks(*bounce.edit)[lane]->getVolumePlugin()->isEnabled(),
                    "The source's fader is back on after the render");

            require(bounce.bounceClipsToNewTracks({firstID}).failed(),
                    "A muted clip is refused, since it would bounce to silence");
            bounce.undo();
            require(bounce.firstReturnTrack() == 4, "Undo takes the new track away");
            require(!bounce.findClip(firstID)->isMuted() && !bounce.findClip(secondID)->isMuted(),
                    "and plays the sources again");

            require(bounce.setClipsMuted({firstID}, true).wasOk() && bounce.findClip(firstID)->isMuted(),
                    "A clip can be muted by hand");
            require(bounce.setClipsMuted({firstID}, false).wasOk() && !bounce.findClip(firstID)->isMuted(),
                    "and unmuted the same way");

            // The starter's first track is MIDI and runs nothing.
            te::EditItemID empty;
            require(bounce.createClip(0, 4.0, &empty).wasOk(), "A MIDI clip on a track with no instrument");
            const auto refused = bounce.bounceClipsToNewTracks({empty});
            require(refused.failed() && refused.getErrorMessage().contains("instrument"),
                    "is refused: nothing would turn its notes into sound");
            require(bounce.firstReturnTrack() == 4, "and nothing is made");
            bounce.deleteClip(empty);

            // A preset brings the instrument and the notes it plays.
            require(bounce.insertPatternPreset(Session::PatternPreset::SubBass, 0, 0.0).wasOk(),
                    "A pattern preset lands on the MIDI track");
            te::Clip* pattern = nullptr;
            for (auto* each : te::getAudioTracks(*bounce.edit)[0]->getClips())
                if (bounce.shouldShowClipInArrangement(*each))
                    pattern = each;
            require(pattern != nullptr, "and shows as a clip");
            const auto presetEnd = pattern->getPosition().time.getEnd().inSeconds();
            require(bounce.bounceClipsToNewTracks({pattern->itemID}, &made).wasOk(), "A MIDI clip bounces");
            const auto midiMix = bounce.audioClipMix(made.clips.front());
            require(midiMix.valid && made.tracks.front() == 1, "to an audio clip on a new track under its own");
            require(bounceRms(midiMix.sourceFile, 0.0, presetEnd) > 0.001f,
                    "and the file holds what the instrument played");
        }

        // A track in a group feeds its bus, and the engine renders such a track
        // only as part of the bus. Bounced on its own it has to be heard on
        // its own, before its fader; the group is heard after its bus.
        {
            Session grouped;
            grouped.projectFile = bounceScratch.getChildFile("groups.rhinoedit");
            require(grouped.importAudio(source.getFile()).wasOk(), "A file imports for the group fixture");
            require(grouped.groupTracks({1}, "Bus").wasOk(), "The track is given a bus");
            const auto group = grouped.trackGroups().front();
            const auto member = group.firstTrack;
            auto* bus = te::getAudioTracks(*grouped.edit)[group.busTrack];
            require(grouped.setTrackVolumeDb(group.busTrack, -20.0f).wasOk()
                        && grouped.setTrackVolumeDb(member, -6.0f).wasOk(),
                    "The bus and the member are both turned down");

            Session::BounceResult made;
            require(grouped.bounceTracksToNewTracks({member}, &made).wasOk(), "The member bounces on its own");
            const auto alone = made.tracks.front();
            require(alone == member + 1 && grouped.trackGroupId(alone) == group.id,
                    "to a track that joins the group, so it plays into the bus as its source did");
            require(grouped.trackMixer(member).muted && !made.group, "The member is muted");
            require(close(grouped.trackMixer(alone).volumeDb, -6.0), "and its fader comes across");
            require(te::getAudioTracks(*grouped.edit)[member]->getOutput().getDestinationTrack() == bus,
                    "The member feeds its bus again after the render");
            const auto aloneMix = grouped.audioClipMix(made.clips.front());
            require(close(aloneMix.startSeconds, 0.0) && close(aloneMix.endSeconds, 1.0),
                    "A whole track becomes one clip over its clips");
            const auto memberLevel = bounceRms(aloneMix.sourceFile, 0.6, 0.95);
            require(memberLevel > 0.3f, "and is heard before the member's fader and the bus");
            grouped.undo();
            require(grouped.firstReturnTrack() == 5 && !grouped.trackMixer(member).muted, "Undo puts the member back");

            require(grouped.bounceTracksToNewTracks({group.busTrack, member}, &made).wasOk(),
                    "The bus bounces the group, the member chosen with it going in once");
            require(made.tracks.size() == 1 && made.group, "to one new track");
            const auto below = made.tracks.front();
            require(below == group.lastTrack() + 1 && grouped.trackGroupId(below) == 0,
                    "under the whole group and outside it, since the bus is already in the file");
            require(grouped.trackMixer(group.busTrack).muted && !grouped.trackMixer(member).muted,
                    "The bus is what is muted");
            require(close(grouped.trackMixer(below).volumeDb, 0.0), "The new track plays at unity");
            const auto groupLevel = bounceRms(grouped.audioClipMix(made.clips.front()).sourceFile, 0.6, 0.95);
            require(groupLevel < memberLevel * 0.2f && groupLevel > memberLevel * 0.005f,
                    "and the bus's fader and the member's are both in the file");
            require(grouped.bounceTracksToNewTracks({group.busTrack}).failed(),
                    "A muted bus is refused rather than bounced to silence");
        }

        // The shortcut, through the arrangement: the selection says what is
        // bounced, and moves onto what was made.
        {
            Session keyed;
            keyed.projectFile = bounceScratch.getChildFile("keyed.rhinoedit");
            require(keyed.importAudio(source.getFile()).wasOk(), "A file imports for the shortcut fixture");
            const auto clipID = te::getAudioTracks(*keyed.edit)[1]->getClips().getFirst()->itemID;
            Arrangement keyedView(keyed);
            keyedView.setSize(1200, 508);
            keyedView.sync();
            keyedView.fit();
            keyedView.setSelection({clipID}, clipID);
            const auto ctrlB = juce::KeyPress('B', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
            keyedView.keyPressed(ctrlB);
            require(keyed.firstReturnTrack() == 5 && keyed.findClip(clipID)->isMuted(), "Ctrl+B bounces the selected clip");
            const auto bouncedID = keyedView.selectedClipID();
            require(bouncedID != clipID && keyed.audioClipMix(bouncedID).valid,
                    "and the selection follows onto the bounced clip");
            // The strip along a clip's top is drawn opaque in its colour, so
            // it says plainly whether the clip was drawn grey. Nothing is
            // selected, so no selection outline is drawn over it.
            keyedView.setSelection({});
            const auto picture = keyedView.createComponentSnapshot(keyedView.getLocalBounds());
            const auto stripColour = [&keyedView, &picture](te::EditItemID id)
            {
                for (const auto& clip : keyedView.clips)
                    if (clip.id == id)
                    {
                        const auto box = keyedView.bounds(clip);
                        return picture.getPixelAt(juce::roundToInt(box.getCentreX()), juce::roundToInt(box.getY() + 3.0f));
                    }
                return juce::Colour();
            };
            require(stripColour(clipID).getSaturation() < 0.05f,
                    "A muted clip is drawn grey, so what a bounce muted can be seen");
            require(stripColour(bouncedID).getSaturation() > 0.3f, "while the clip that plays keeps its colour");
            // The bounced track is the one holding a clip that plays.
            keyedView.selectTrack(2);
            keyedView.focus = Arrangement::Focus::track;
            keyedView.keyPressed(ctrlB);
            require(keyed.firstReturnTrack() == 6 && keyed.trackMixer(2).muted && keyedView.selectedTrack == 3,
                    "With a card selected it bounces the track, and the new card is selected");
        }

        // A time selection is bounced as a span, as Live bounces one: the new
        // clip covers exactly the selection, and the source is cut at its edges
        // and muted only between them, so the song still plays the rest.
        {
            Session spanned;
            spanned.projectFile = bounceScratch.getChildFile("span.rhinoedit");
            require(spanned.importAudio(source.getFile()).wasOk(), "A file imports for the span fixture");
            const auto lane = 1;
            const auto sourceID = te::getAudioTracks(*spanned.edit)[lane]->getClips().getFirst()->itemID;
            // Every clip on the lane, in time order.
            struct LanePiece
            {
                double start = 0.0, end = 0.0;
                bool muted = false;
            };
            const auto laneClips = [&spanned, lane]
            {
                std::vector<LanePiece> found;
                for (auto* clip : te::getAudioTracks(*spanned.edit)[lane]->getClips())
                    found.push_back({clip->getPosition().time.getStart().inSeconds(),
                                     clip->getPosition().time.getEnd().inSeconds(), clip->isMuted()});
                std::sort(found.begin(), found.end(), [](const LanePiece& a, const LanePiece& b) { return a.start < b.start; });
                return found;
            };
            const auto refusedEmpty = spanned.bounceRegionToNewTracks(0.25, 0.75, 0, 0);
            require(refusedEmpty.failed() && spanned.firstReturnTrack() == 4,
                    "A selection over a lane with nothing in it is refused, and nothing is made");

            Session::BounceResult made;
            require(spanned.bounceRegionToNewTracks(0.25, 0.75, 0, lane, &made).wasOk(),
                    "A selection across part of a clip bounces");
            require(spanned.firstReturnTrack() == 5 && made.tracks == std::vector<int>({lane + 1}),
                    "to one new track under the lane that had a clip in it, the empty lane passed over");
            require(made.clips.size() == 1, "as one clip");
            const auto span = spanned.audioClipMix(made.clips.front());
            require(close(span.startSeconds, 0.25) && close(span.endSeconds, 0.75) && close(span.offsetSeconds, 0.0),
                    "over exactly the selection");
            // The fixture turns loud half way through, at 0.5 s in the song,
            // which is 0.25 s into a file that starts at the selection.
            const auto quiet = bounceRms(span.sourceFile, 0.05, 0.2);
            require(quiet > 0.03f && quiet < 0.15f, "The file starts where the selection does, on the quiet half");
            require(bounceRms(span.sourceFile, 0.3, 0.45) > 0.3f, "and turns loud where the song does");
            const auto cut = laneClips();
            require(cut.size() == 3, "The source is cut at both edges of the selection");
            require(close(cut[0].start, 0.0) && close(cut[0].end, 0.25) && !cut[0].muted
                        && close(cut[1].start, 0.25) && close(cut[1].end, 0.75) && cut[1].muted
                        && close(cut[2].start, 0.75) && close(cut[2].end, 1.0) && !cut[2].muted,
                    "and only the part inside it is muted");
            spanned.undo();
            const auto whole = laneClips();
            require(spanned.firstReturnTrack() == 4 && whole.size() == 1 && close(whole[0].end, 1.0)
                        && !whole[0].muted && spanned.findClip(sourceID) != nullptr,
                    "One undo takes the bounce away and puts the clip back whole and playing");

            // Through the arrangement: a span dragged out over the lanes, with
            // no clip picked by its header, is what Ctrl+B bounces.
            Arrangement spanView(spanned);
            spanView.setSize(1200, 508);
            spanView.sync();
            spanView.fit();
            spanView.setTimeSelection(0.25, 0.75, lane, lane);
            spanView.focus = Arrangement::Focus::region;
            const auto ctrlB = juce::KeyPress('B', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
            spanView.keyPressed(ctrlB);
            require(spanned.firstReturnTrack() == 5 && laneClips().size() == 3,
                    "Ctrl+B on a dragged span bounces the span, not the whole clip");
            const auto bouncedSpan = spanned.audioClipMix(spanView.selectedClipID());
            require(bouncedSpan.valid && close(bouncedSpan.startSeconds, 0.25) && close(bouncedSpan.endSeconds, 0.75),
                    "and the selection moves onto what it made");
            spanView.setSelection({});
            spanView.setTimeSelection(0.25, 0.75, lane, lane);
            spanView.keyPressed(ctrlB);
            require(spanned.firstReturnTrack() == 5,
                    "A span whose clips are already muted has nothing playing in it to bounce");
        }
        // In steps, as the shell's loading screen runs it: a slice renders and
        // hands the thread back, nothing undoes under it, and cancelling
        // leaves the document and the disk as they were.
        {
            Session stepped;
            stepped.projectFile = bounceScratch.getChildFile("stepped.rhinoedit");
            require(stepped.importAudio(source.getFile()).wasOk(), "A file imports for the stepped fixture");
            const auto lane = 1;
            const auto bounces = [&stepped]
            {
                return stepped.mergedAudioDirectory().findChildFiles(juce::File::findFiles, false, "*Bounce*.wav").size();
            };
            const auto before = bounces();
            require(stepped.startBounceTracks({lane}).wasOk() && stepped.isBouncing(),
                    "A bounce can be started and left running");
            require(stepped.startBounceTracks({lane}).failed(), "and a second waits for it");
            require(stepped.continueBounce(0.0), "A slice renders and hands the thread back with work left");
            const auto midway = stepped.bounceProgress();
            require(midway.steps == 1 && midway.source == stepped.trackName(lane)
                        && midway.fraction >= 0.0f && midway.fraction <= 1.0f,
                    "and says what it is on and how far it has got");
            stepped.undo();
            require(stepped.isBouncing() && stepped.firstReturnTrack() == 4, "Undo waits rather than pulling the track away");
            stepped.cancelBounce();
            require(!stepped.isBouncing() && stepped.bounceOutcome().getErrorMessage().contains("cancelled"),
                    "Cancelling stops it");
            require(stepped.firstReturnTrack() == 4 && !stepped.trackMixer(lane).muted && bounces() == before,
                    "with nothing changed and nothing left on disk");
            require(te::getAudioTracks(*stepped.edit)[lane]->getVolumePlugin()->isEnabled(),
                    "and the fader it switched off for the render back on");

            // The screen the shell shows, driving the real thing.
            LoadingScreen screen;
            screen.setSize(900, 600);
            auto finished = 0;
            const auto task = [&stepped, &finished]
            {
                return LoadingScreen::Task {"Bouncing to new track",
                    [&stepped](double milliseconds) { return stepped.continueBounce(milliseconds); },
                    [&stepped]
                    {
                        const auto progress = stepped.bounceProgress();
                        return std::make_pair(progress.fraction, progress.source);
                    },
                    [&stepped] { stepped.cancelBounce(); },
                    [&finished] { ++finished; }};
            };
            require(stepped.startBounceTracks({lane}).wasOk(), "It starts again");
            screen.run(task());
            require(screen.isRunning() && screen.isVisible(), "The loading screen shows while it runs");
            const auto drawn = screen.createComponentSnapshot(screen.getLocalBounds());
            require(drawn.isValid(), "and draws");
            for (auto frames = 0; screen.isRunning(); ++frames)
            {
                require(frames < 10000, "The loading screen finishes the bounce");
                screen.timerCallback();
            }
            require(finished == 1 && !screen.isVisible(), "and goes once it is done, handing it back once");
            require(stepped.bounceOutcome().wasOk() && stepped.firstReturnTrack() == 5 && stepped.trackMixer(lane).muted,
                    "What the screen drove lands like any bounce");

            require(stepped.startBounceTracks({lane + 1}).wasOk(), "The bounce can itself be bounced");
            screen.run(task());
            screen.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            require(!screen.isRunning() && finished == 2 && !stepped.isBouncing() && stepped.firstReturnTrack() == 5,
                    "Esc on the screen cancels the bounce, and the screen still hands it back");
        }
        bounceScratch.deleteRecursively();
