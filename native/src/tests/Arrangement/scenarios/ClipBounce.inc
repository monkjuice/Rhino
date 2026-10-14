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
            require(bounce.trackCount() == 5 && made.tracks.size() == 1 && made.tracks.front() == lane + 1,
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
            require(bounce.trackCount() == 4, "Undo takes the new track away");
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
            require(bounce.trackCount() == 4, "and nothing is made");
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
            require(grouped.trackCount() == 5 && !grouped.trackMixer(member).muted, "Undo puts the member back");

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
            keyedView.setSize(1200, 400);
            keyedView.sync();
            keyedView.fit();
            keyedView.setSelection({clipID}, clipID);
            const auto ctrlB = juce::KeyPress('B', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
            keyedView.keyPressed(ctrlB);
            require(keyed.trackCount() == 5 && keyed.findClip(clipID)->isMuted(), "Ctrl+B bounces the selected clip");
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
            require(keyed.trackCount() == 6 && keyed.trackMixer(2).muted && keyedView.selectedTrack == 3,
                    "With a card selected it bounces the track, and the new card is selected");
        }
        bounceScratch.deleteRecursively();
