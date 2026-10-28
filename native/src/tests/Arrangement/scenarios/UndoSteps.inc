        // One Ctrl+Z takes back one thing the person did, all of it, and leaves
        // it to redo (wiki: undo-history). Each block is a way that failed:
        // the engine re-sorting a lane or the stack a message after a command
        // had closed its step, so the sort was a step of its own; a clip the
        // note editor needed made again after every undo, so Ctrl+Z went no
        // further; parameters the engine left where an undo found them; a
        // gesture split by the engine's undo timer. A document of its own.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            auto& history = stack.edit->getUndoManager();
            // The runners dispatch no message loop, and every one of these
            // failures arrived by it: what JUCE and the engine post for later
            // is handed over here, as the shell's loop hands it over.
            const auto settle = [](int milliseconds)
            {
                const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(milliseconds);
                do
                {
                   #if JUCE_WINDOWS
                    MSG message;
                    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                    {
                        TranslateMessage(&message);
                        DispatchMessage(&message);
                    }
                   #endif
                    juce::Thread::sleep(1);
                }
                while (juce::Time::getMillisecondCounter() < until);
            };
            const auto steps = [&history] { return history.getUndoDescriptions().size(); };
            const auto clipsOn = [&stack](int track)
            {
                std::vector<te::EditItemID> ids;
                for (auto* clip : stack.audioTrack(track)->getClips())
                    ids.push_back(clip->itemID);
                return ids;
            };
            const auto slotOf = [&stack](int track, const juce::String& deviceId)
            {
                for (const auto& slot : stack.deviceSlots(track))
                    if (slot.deviceId == deviceId)
                        return slot.pluginIndex;
                return -1;
            };
            const auto valueOf = [&stack](int track, int slot, int parameter)
            {
                const auto found = stack.deviceParameter(track, slot, parameter);
                return found.has_value() ? found->value : -1.0e9f;
            };

            settle(50);
            require(steps() == 0, "A new document has nothing to undo once the engine has sorted it");

            // [MIDI, Audio, MIDI, Audio]. Clips and tracks: the engine's sort
            // of the lane or the stack lands inside the command's own step.
            te::EditItemID made;
            require(stack.createClip(0, 4.0, &made).wasOk(), "A clip on the first lane");
            settle(30);
            require(steps() == 1 && history.getUndoDescription() == "Add clip",
                    "Adding a clip is one step, the engine's sort of its lane inside it");
            require(stack.importAudioAt(source.getFile(), 1, 0.0).wasOk(), "Audio on the second lane");
            settle(30);
            require(steps() == 2, "Importing audio is one step");
            require(stack.splitClip(clipsOn(1).front(), 0.5).wasOk() && clipsOn(1).size() == 2, "The audio split");
            settle(30);
            require(steps() == 3, "Splitting is one step");
            stack.undo();
            require(clipsOn(1).size() == 1 && steps() == 2 && stack.canRedo(),
                    "One Ctrl+Z takes the split back whole and leaves it to redo");
            stack.redo();
            require(clipsOn(1).size() == 2 && steps() == 3, "and redo cuts it again");
            require(stack.duplicateTracks({1}).wasOk(), "The audio lane duplicated");
            settle(30);
            require(steps() == 4 && history.getUndoDescription() == "Duplicate track",
                    "Duplicating a track is one step, the engine's sort of the stack inside it");
            require(stack.createSlotClip(0, 0).wasOk(), "A clip in a cell");
            settle(60);
            require(steps() == 5 && history.getNumActionsInCurrentTransaction() == 0,
                    "A clip in a cell is one step, its follow actions made inside it");
            require(stack.addClipDevice("Saturator", clipsOn(1).front()).wasOk(), "A device on an audio clip");
            settle(30);
            require(steps() == 6 && history.getUndoDescription() == "Add Saturator to clip",
                    "A clip's device is one step, what making it wrote inside it");
            stack.setEditorStepCount(32);
            require(steps() == 6 && history.getNumActionsInCurrentTransaction() == 0,
                    "How finely the note editor divides a clip is no step");

            // Parameters: one Ctrl+Z puts the engine's parameter back, not only
            // its stored value, and leaves the drag to redo - from a value the
            // knob had never left too, which the engine wrote back as a step.
            require(stack.addDevice("Compressor", 1).wasOk(), "The engine's Compressor on the audio lane");
            const auto compressor = slotOf(1, "Compressor");
            const auto threshold = valueOf(1, compressor, 0);
            const auto range = stack.deviceParameter(1, compressor, 0);
            require(range.has_value(), "The Compressor has a first control");
            const auto dragged = range->minimum + (range->maximum - range->minimum) * 0.8f;
            require(stack.beginDeviceParameterGesture(1, compressor, 0).wasOk(), "Taking hold of the Compressor's knob");
            stack.setDeviceParameter(1, compressor, 0, range->minimum + (range->maximum - range->minimum) * 0.3f);
            stack.setDeviceParameter(1, compressor, 0, dragged);
            stack.endDeviceParameterGesture(1, compressor, 0);
            require(history.getUndoDescription().startsWith("Change "), "A knob drag is a named step");
            stack.undo();
            require(close(valueOf(1, compressor, 0), threshold) && stack.canRedo()
                        && history.getRedoDescription().startsWith("Change "),
                    "Ctrl+Z moves the engine's own Compressor back, and leaves the drag to redo");
            stack.redo();
            require(close(valueOf(1, compressor, 0), dragged), "and redo moves it again");

            const auto volumeBefore = stack.trackMixer(1).volumeDb;
            stack.beginTrackVolumeGesture(1);
            stack.setTrackVolumeDb(1, -12.0f);
            stack.endTrackVolumeGesture(1);
            stack.undo();
            require(std::abs(stack.trackMixer(1).volumeDb - volumeBefore) < 0.01f && stack.canRedo(),
                    "Ctrl+Z moves a fader back");
            stack.redo();
            require(std::abs(stack.trackMixer(1).volumeDb + 12.0f) < 0.01f, "and redo moves it again");

            // A knob set with no drag around it - a double-click, a wheel - is a
            // step of its own: the drag after it does not fold it in.
            const auto attack = stack.deviceParameter(1, compressor, 1);
            const auto ratio = stack.deviceParameter(1, compressor, 2);
            require(attack.has_value() && ratio.has_value(), "The Compressor has more controls");
            const auto loose = attack->minimum + (attack->maximum - attack->minimum) * 0.7f;
            stack.setDeviceParameter(1, compressor, 1, loose);
            require(stack.beginDeviceParameterGesture(1, compressor, 2).wasOk(), "Taking hold of another knob");
            stack.setDeviceParameter(1, compressor, 2, ratio->minimum + (ratio->maximum - ratio->minimum) * 0.6f);
            stack.endDeviceParameterGesture(1, compressor, 2);
            stack.undo();
            require(close(valueOf(1, compressor, 1), loose) && close(valueOf(1, compressor, 2), ratio->value),
                    "Ctrl+Z after a knob set and a drag on another takes back the drag alone");
            stack.redo();

           #if JUCE_WINDOWS
            // A gesture held with no mouse button down - a finger on an
            // encoder, V held over the velocity lane - paused for longer than
            // the engine's undo timer runs is still one step.
            const auto stepsBeforePause = steps();
            stack.beginTrackVolumeGesture(3);
            stack.setTrackVolumeDb(3, -4.0f);
            settle(600);
            stack.setTrackVolumeDb(3, -9.0f);
            stack.endTrackVolumeGesture(3);
            require(steps() == stepsBeforePause + 1, "A fader held through a pause is one step");
           #endif

            // A lane that stops driving its knob hands the knob back outside
            // the history: after an undo, a step of its own threw away what
            // could be redone, and Ctrl+Z took back only that.
            require(stack.addDevice("RhinoFM", 0).wasOk(), "Rhino FM on the first lane");
            const auto fm = slotOf(0, "RhinoFM");
            const auto fmRange = stack.deviceParameter(0, fm, 0);
            require(fmRange.has_value(), "Rhino FM has a first control");
            require(stack.setTrackAutomationPoints({0, fm, 0}, {{0.0, fmRange->minimum},
                                                                {2.0, fmRange->maximum}}).wasOk(),
                    "A lane drawn on it");
            // As though the song had played half way along the lane.
            for (auto* parameter : stack.devicePlugin(0, fm)->getAutomatableParameters())
                if (parameter->hasAutomationPoints())
                    parameter->updateToFollowCurve(tracktion::core::TimePosition::fromSeconds(1.0));
            settle(30);
            stack.undo();
            require(stack.canRedo() && history.getRedoDescription() == "Edit automation",
                    "Undoing a lane that was moving its knob leaves the lane to redo");
            stack.redo();
            require(stack.trackAutomationState({0, fm, 0}).active, "and redo draws it again");

            // The history holds far more than the engine's 30 steps.
            const auto nameBefore = stack.trackName(2);
            for (int i = 0; i < 40; ++i)
                stack.setTrackName(2, "Name " + juce::String(i));
            for (int i = 0; i < 40; ++i)
                stack.undo();
            require(stack.trackName(2) == nameBefore, "Forty renames are forty steps, all of them undoable");
            stack.releaseAudioDevice();
        }

        // The note editor's clip taken away by an undo, on a first MIDI track
        // with no other MIDI clip: the editor used to make itself a clip there,
        // as a step of its own after the undo, and undoing that made it again,
        // so Ctrl+Z could go no further back.
        {
            auto document = std::make_unique<Session>();
            auto& stack = *document;
            // [MIDI, Audio, MIDI, Audio]: the second MIDI track is empty.
            require(stack.moveTrack(2, 0).wasOk(), "The empty MIDI track carried to the top");
            te::EditItemID made;
            require(stack.createClip(0, 0.0, &made).wasOk() && stack.selectPatternClip(made).wasOk(),
                    "A clip on it, open in the note editor");
            stack.undo();
            require(stack.canRedo() && stack.edit->getUndoManager().getUndoDescription() == "Move track",
                    "Undoing the editor's clip leaves it to redo and nothing new to undo");
            stack.undo();
            require(stack.trackType(0) == Session::TrackType::midi && stack.audioTrack(0)->getClips().size() == 1,
                    "and the next Ctrl+Z takes the move back");
            stack.releaseAudioDevice();
        }
