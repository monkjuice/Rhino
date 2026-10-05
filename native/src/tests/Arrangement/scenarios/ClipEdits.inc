        // Clip edits are whole: one gesture is one undo step, a refused gesture
        // changes nothing, and a copy is the clip rather than a sketch of it.
        // A document of its own, so nothing here disturbs the shared session.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            const auto barSeconds = stack.beatsPerBar() * 60.0 / stack.tempo();
            const auto startOf = [&stack](te::EditItemID id)
            {
                auto* clip = stack.findClip(id);
                return clip != nullptr ? clip->getPosition().time.getStart().inSeconds() : -1.0;
            };
            const auto overlapping = [&stack](int track)
            {
                std::vector<std::pair<double, double>> spans;
                for (auto* clip : te::getAudioTracks(*stack.edit)[track]->getClips())
                    if (stack.shouldShowClipInArrangement(*clip))
                        spans.emplace_back(clip->getPosition().time.getStart().inSeconds(),
                                           clip->getPosition().time.getEnd().inSeconds());
                std::sort(spans.begin(), spans.end());
                for (size_t i = 1; i < spans.size(); ++i)
                    if (spans[i].first < spans[i - 1].second - 1.0e-6)
                        return true;
                return false;
            };

            // Three clips carried by one drag move in one undo step.
            te::EditItemID a, b, c;
            require(stack.createClip(0, 0.0, &a).wasOk() && stack.createClip(0, 2.0 * barSeconds, &b).wasOk()
                        && stack.createClip(0, 4.0 * barSeconds, &c).wasOk(),
                    "Three clips on one lane");
            std::vector<Session::ClipMove> carried;
            for (const auto id : {a, b, c})
                carried.push_back({id, {startOf(id) + barSeconds, startOf(id) + 2.0 * barSeconds, 0.0}, 0});
            require(stack.moveClips(carried).wasOk(), "Several clips move together");
            require(std::abs(startOf(a) - barSeconds) < 1.0e-6 && std::abs(startOf(c) - 5.0 * barSeconds) < 1.0e-6,
                    "and all of them land a bar later");
            stack.undo();
            require(std::abs(startOf(a)) < 1.0e-6 && std::abs(startOf(b) - 2.0 * barSeconds) < 1.0e-6
                        && std::abs(startOf(c) - 4.0 * barSeconds) < 1.0e-6,
                    "One undo puts every clip of the drag back");

            // A move one clip of which may not make is refused whole.
            const std::vector<Session::ClipMove> mixed {
                {a, {0.0, barSeconds, 0.0}, 2},   // a MIDI lane: fine on its own
                {b, {0.0, barSeconds, 0.0}, 1}};  // an audio lane: refused
            require(stack.moveClips(mixed).failed(), "A drag that would put a MIDI clip on an audio lane is refused");
            require(stack.findClip(a)->getClipTrack() == te::getAudioTracks(*stack.edit)[0],
                    "and the clip that could have moved stays where it was");

            // Deleting a selection is one undo step.
            stack.deleteClips({a, b, c});
            require(stack.findClip(a) == nullptr && stack.findClip(c) == nullptr, "A selection deletes together");
            stack.undo();
            require(stack.findClip(a) != nullptr && stack.findClip(b) != nullptr && stack.findClip(c) != nullptr,
                    "and one undo brings every clip of it back");

            // A copy of an audio clip keeps the clip's own mix and warp.
            require(stack.importAudioAt(source.getFile(), 1, 0.0).wasOk(), "An audio clip to copy");
            const auto original = te::getAudioTracks(*stack.edit)[1]->getClips().getFirst()->itemID;
            require(stack.setAudioClipGainDb(original, -6.0f).wasOk() && stack.setAudioClipPan(original, 0.4f).wasOk()
                        && stack.setAudioClipPitch(original, 3.0f).wasOk()
                        && stack.setAudioClipFadeIn(original, 0.1).wasOk()
                        && stack.setAudioClipFadeOut(original, 0.15).wasOk()
                        && stack.setAudioClipMuted(original, true).wasOk(),
                    "The clip is given a mix of its own");
            require(stack.setClipFollowsTempo(original, true).wasOk()
                        && stack.setClipWarpMode(original, Session::WarpMode::complex).wasOk()
                        && stack.setClipBpm(original, 100.0).wasOk(),
                    "and is warped");
            const auto before = stack.audioClipMix(original);
            const auto warpBefore = stack.clipWarp(original);
            require(stack.duplicateClip(original).wasOk(), "The clip duplicates");
            te::EditItemID copy;
            for (auto* clip : te::getAudioTracks(*stack.edit)[1]->getClips())
                if (clip->itemID != original) copy = clip->itemID;
            const auto after = stack.audioClipMix(copy);
            const auto warpAfter = stack.clipWarp(copy);
            require(after.valid && std::abs(after.gainDb - before.gainDb) < 0.01f && std::abs(after.pan - before.pan) < 0.01f
                        && std::abs(after.pitchSemitones - before.pitchSemitones) < 0.01f
                        && std::abs(after.fadeInSeconds - before.fadeInSeconds) < 0.001
                        && std::abs(after.fadeOutSeconds - before.fadeOutSeconds) < 0.001
                        && after.muted == before.muted,
                    "The copy keeps the clip's gain, pan, pitch, fades and mute");
            require(warpAfter.followsTempo && warpAfter.mode == warpBefore.mode
                        && std::abs(warpAfter.clipBpm - warpBefore.clipBpm) < 0.01
                        && std::abs(after.lengthSeconds() - before.lengthSeconds()) < 0.001,
                    "and its warp: the same mode, the same clip tempo and the same length");

            // Duplicating a clip on a track whose instrument is bypassed keeps
            // that instrument rather than swapping in a 4OSC.
            require(stack.addDrumKit(DrumKit::Rhino808, 0).wasOk(), "Drums on the first lane");
            int drumSlot = -1;
            for (const auto& slot : stack.deviceSlots(0))
                if (slot.kind == Session::DeviceKind::Instrument) drumSlot = slot.pluginIndex;
            require(drumSlot >= 0 && stack.toggleDeviceEnabled(0, drumSlot).wasOk(), "and the drums bypassed");
            require(stack.duplicateClip(a).wasOk(), "A clip on the bypassed lane duplicates");
            require(stack.patternInstrumentForTrack(0) != nullptr
                        && stack.patternInstrumentForTrack(0)->getPluginType() == DrumDevice::xmlTypeName,
                    "and the lane still plays its drums");

            // A clip from a lane that plays nothing leaves the destination's
            // instrument alone.
            te::EditItemID bare;
            require(stack.createClip(2, 8.0 * barSeconds, &bare).wasOk() && !stack.trackHasInstrument(2),
                    "A clip on a lane with no instrument");
            std::vector<te::EditItemID> pasted;
            require(stack.pasteClipRegion(stack.copyClipRegion(8.0 * barSeconds, 9.0 * barSeconds, 2, 2),
                                          12.0 * barSeconds, 0, pasted).wasOk(),
                    "pastes onto the drum lane");
            require(stack.patternInstrumentForTrack(0) != nullptr
                        && stack.patternInstrumentForTrack(0)->getPluginType() == DrumDevice::xmlTypeName,
                    "and the drum lane keeps its drums");

            // An import with no free lane brings one, in the same undo step.
            require(stack.importAudioAt(source.getFile(), 3, 0.0).wasOk(), "The last free audio lane is filled");
            const auto lanesBefore = stack.trackCount();
            require(stack.importAudio(source.getFile()).wasOk() && stack.trackCount() == lanesBefore + 1,
                    "An import with no free audio lane adds one");
            stack.undo();
            require(stack.trackCount() == lanesBefore, "and one undo takes the lane and the clip together");

            // A group bus takes no clips, pasted or not.
            require(stack.groupTracks({3}, "Bus").wasOk() && stack.isGroupBusTrack(3), "A bus to paste onto");
            require(stack.pasteClipRegion(stack.copyClipRegion(0.0, barSeconds, 1, 1), 20.0, 3, pasted).failed(),
                    "A paste onto a bus is refused");

            // Pasting notes past the end of the open clip grows it, and the
            // clip that grew wins the ground it grew over.
            require(stack.selectPatternClip(a).wasOk(), "The note editor opens a clip");
            require(stack.ensurePatternLengthSteps(Session::defaultSteps * 3), "The clip grows to take more steps");
            require(!overlapping(0), "and no clip on its lane overlaps another");

            // A session slot is no way round a track's kind.
            require(stack.insertDeviceClipInSlot("FourOsc", 1, 0).failed(),
                    "A slot on an audio lane refuses an instrument clip");
            require(stack.insertAudioFileInSlot(source.getFile(), 0, 0).failed(),
                    "A slot on a MIDI lane refuses audio");

            // A built-in sample whose file was cut short is written again
            // whole, not appended after the stub that was left behind.
            {
                const auto whistle = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                         .getChildFile("Rhino").getChildFile("Built-in audio").getChildFile("Rhino Whistle.wav");
                whistle.getParentDirectory().createDirectory();
                require(whistle.replaceWithText("RIFF stub"), "A stub where the whistle should be");
                require(stack.importBuiltInSample(Session::BuiltInSample::Whistle, 1, 30.0).wasOk(),
                        "The whistle imports over a stub left by an interrupted write");
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(whistle));
                require(reader != nullptr && std::abs(reader->lengthInSamples / reader->sampleRate - 1.5) < 0.01,
                        "and the file is the whole whistle again");
            }
            stack.releaseAudioDevice();
        }
