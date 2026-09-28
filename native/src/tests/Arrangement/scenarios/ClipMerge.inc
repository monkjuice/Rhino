        // Merging renders, so what it makes is measured rather than read back
        // off the clips. Three things are worth holding: the clips' own
        // material lands in the file with the gap between them intact, a
        // clip's own mix is baked in because it belongs to the clip, and the
        // track's fader is not, because the merged clip still plays through
        // it. The last of those is the one that would silently go wrong: a
        // merge that baked the fader in would sound right once and wrong
        // again the moment the lane was re-levelled.
        //
        // Each session below is given a project file in a scratch folder, so
        // the merged audio lands beside a project - the branch that matters -
        // and the folder takes it away again. Without that, every run of the
        // suite would leave three files in the application's own folder.
        const auto mergeScratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("Rhino merge test " + juce::String(juce::Time::getHighResolutionTicks()));
        require(mergeScratch.createDirectory().wasOk(), "A scratch folder for the merged audio");
        const auto scratchProject = [&mergeScratch](Session& session, const char* name)
        {
            session.projectFile = mergeScratch.getChildFile(juce::String(name) + ".rhinoedit");
        };
        {
            Session merge;
            scratchProject(merge, "merge");
            require(merge.importAudio(source.getFile()).wasOk(), "A file imports for the merge fixture");
            const auto mergeTrack = merge.trackCount() - 1;
            auto* lane = te::getAudioTracks(*merge.edit)[mergeTrack];
            require(lane != nullptr && lane->getClips().size() == 1, "The import lands as one clip");
            const auto firstID = lane->getClips().getFirst()->itemID;
            require(merge.importAudioAt(source.getFile(), mergeTrack, 2.0).wasOk(),
                    "A second clip lands a second after the first one ends");
            const auto secondID = lane->getClips().getLast()->itemID;
            require(firstID != secondID && lane->getClips().size() == 2, "Two clips with a gap between them");

            require(merge.mergeClips({firstID}).failed(), "One clip is nothing to merge");
            require(merge.mergeClips({}).failed(), "Neither is an empty selection");

            // Pulled right down: if this reached the file, everything measured
            // below would be silence.
            require(merge.setTrackVolumeDb(mergeTrack, -60.0f).wasOk(), "The lane can be pulled down before merging");
            // A clip property, so this one has to reach the file.
            require(merge.setAudioClipGainDb(secondID, -20.0f).wasOk(), "The second clip can be turned down");

            Session::MergeResult merged;
            require(merge.mergeClips({firstID, secondID}, &merged).wasOk(), "Two clips on one track merge");
            require(merged.sourceCount == 2 && merged.clips.size() == 1, "Two clips in, one clip out");
            require(lane->getClips().size() == 1, "and the lane holds only what the merge made");
            auto* mergedClip = merge.findAudioClip(merged.clips.front());
            require(mergedClip != nullptr, "The merged clip is an audio clip");
            const auto mergedMix = merge.audioClipMix(merged.clips.front());
            require(close(mergedMix.startSeconds, 0.0) && close(mergedMix.endSeconds, 3.0),
                    "It spans from the first clip's start to the last one's end");
            require(mergedMix.sourceFile.existsAsFile(), "and reads a file of its own");
            require(mergedMix.sourceFile != source.getFile(), "rather than the file the clips were imported from");
            require(mergedMix.sourceFile.isAChildOf(mergeScratch),
                    "written beside the project rather than in the application's folder");
            require(close(mergedMix.gainDb, 0.0),
                    "The merged clip starts at unity: what the sources were doing is in the file now");

            juce::AudioFormatManager mergeFormats;
            mergeFormats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> mergeReader(mergeFormats.createReaderFor(mergedMix.sourceFile));
            require(mergeReader != nullptr, "The merged file can be read back");
            require(mergeReader->sampleRate > 7000.0, "at a sensible rate");
            juce::AudioBuffer<float> mergedAudio(static_cast<int>(mergeReader->numChannels),
                                                 static_cast<int>(mergeReader->lengthInSamples));
            require(mergeReader->read(&mergedAudio, 0, mergedAudio.getNumSamples(), 0, true, true),
                    "Read the merged samples");
            const auto rateOfMerge = mergeReader->sampleRate;
            const auto windowRms = [&mergedAudio, rateOfMerge](double fromSeconds, double toSeconds)
            {
                const auto from = juce::jlimit(0, mergedAudio.getNumSamples() - 1,
                                               static_cast<int>(fromSeconds * rateOfMerge));
                const auto to = juce::jlimit(from + 1, mergedAudio.getNumSamples(),
                                             static_cast<int>(toSeconds * rateOfMerge));
                return mergedAudio.getRMSLevel(0, from, to - from);
            };
            // The fixture is quiet for its first half and loud for its second,
            // so these read the loud halves and stay clear of the boundary.
            const auto firstClipLevel = windowRms(0.6, 0.95);
            const auto gapLevel = windowRms(1.1, 1.9);
            const auto secondClipLevel = windowRms(2.6, 2.95);
            require(firstClipLevel > 0.2f, "The first clip's audio is in the merged file at full level");
            require(gapLevel < 0.001f, "The gap between the clips is silent rather than closed up");
            require(secondClipLevel > 0.001f, "The second clip's audio is there too");
            // -20 dB is a tenth of the level, which no rounding turns into the
            // half or the tenfold that a missed or doubled gain would show.
            require(secondClipLevel < firstClipLevel * 0.2f && secondClipLevel > firstClipLevel * 0.05f,
                    "The second clip's own gain is baked into the merge, and only once");

            require(close(merge.trackMixer(mergeTrack).volumeDb, -60.0),
                    "The lane is still where it was left, which is what the merge had to ignore");

            merge.undo();
            require(te::getAudioTracks(*merge.edit)[mergeTrack]->getClips().size() == 2,
                    "Undoing a merge brings both clips back");
            require(merge.findAudioClip(firstID) != nullptr && merge.findAudioClip(secondID) != nullptr,
                    "and they are the clips that went in");
        }

        // A clip the merged span reaches is merged whether or not it was
        // picked. Before this rule, the clip that owns the ground it lands on
        // would have deleted the unselected one in the middle.
        {
            Session between;
            scratchProject(between, "between");
            require(between.importAudio(source.getFile()).wasOk(), "A file imports for the middle-clip fixture");
            const auto betweenTrack = between.trackCount() - 1;
            auto* lane = te::getAudioTracks(*between.edit)[betweenTrack];
            const auto leftID = lane->getClips().getFirst()->itemID;
            require(between.importAudioAt(source.getFile(), betweenTrack, 1.5).wasOk(), "A clip in the middle");
            const auto middleID = lane->getClips().getLast()->itemID;
            require(between.importAudioAt(source.getFile(), betweenTrack, 3.0).wasOk(), "A clip beyond it");
            const auto rightID = lane->getClips().getLast()->itemID;
            require(lane->getClips().size() == 3, "Three clips on the lane");

            Session::MergeResult merged;
            require(between.mergeClips({leftID, rightID}, &merged).wasOk(), "The outer two merge");
            require(merged.sourceCount == 3,
                    "and the clip between them goes in rather than being run over");
            require(merged.clips.size() == 1 && lane->getClips().size() == 1, "The lane is left with one clip");
            require(between.findAudioClip(middleID) == nullptr, "The middle clip is part of it now");
            const auto mix = between.audioClipMix(merged.clips.front());
            require(close(mix.startSeconds, 0.0) && close(mix.endSeconds, 4.0),
                    "The merged clip covers all three");
        }

        // The keyboard and the selection, through the arrangement rather than
        // the model: Ctrl+J has to reach the command, and the selection has to
        // land on what the merge made, since what it was pointing at is gone.
        {
            Session keyed;
            scratchProject(keyed, "keyed");
            require(keyed.importAudio(source.getFile()).wasOk(), "A file imports for the shortcut fixture");
            const auto keyedTrack = keyed.trackCount() - 1;
            auto* lane = te::getAudioTracks(*keyed.edit)[keyedTrack];
            const auto leftID = lane->getClips().getFirst()->itemID;
            require(keyed.importAudioAt(source.getFile(), keyedTrack, 1.0).wasOk(), "A clip flush against it");
            const auto rightID = lane->getClips().getLast()->itemID;
            Arrangement keyedView(keyed);
            keyedView.setSize(1200, 400);
            keyedView.sync();
            keyedView.fit();
            keyedView.setSelection({leftID, rightID}, leftID);
            keyedView.keyPressed(juce::KeyPress('J', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0));
            require(lane->getClips().size() == 1, "Ctrl+J merges the selected clips");
            const auto mergedID = lane->getClips().getFirst()->itemID;
            require(mergedID != leftID && mergedID != rightID, "into a clip neither of them was");
            require(keyedView.selectedClipID() == mergedID,
                    "and the selection follows onto it rather than naming a clip that is gone");
        }
        // The clips reading those files are gone with their sessions by now,
        // so nothing is holding them open.
        mergeScratch.deleteRecursively();
