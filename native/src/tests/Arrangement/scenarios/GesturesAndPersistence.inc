        // Fit is four seconds wide here. Exercise actual component hit testing,
        // gesture preview, and commit rather than calling only the model facade.
        scenario("gestures: clip drag and trim");
        // Undo rebuilds clips from the edit state, so the pointer the earlier
        // scenarios held no longer addresses this clip. Look it up by id again.
        clip = session.findAudioClip(id);
        require(clip != nullptr, "The imported audio clip survives the track-management scenarios");
        view.mouseDown(event({240, audioLaneY}, {240, audioLaneY}, false));
        view.mouseDrag(event({240, audioLaneY}, {346.5f, audioLaneY}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "Drag must not rebuild live clip before release");
        view.mouseUp(event({240, audioLaneY}, {346.5f, audioLaneY}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.5), "Pointer drag moves audio");
        require(close(clip->getPosition().offset.inSeconds(), 0.0), "Move preserves source offset");
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.0), "One undo restores move");
        session.redo();

        drag({view.xFor(0.5) + 2.0f, audioLaneY}, {view.xFor(0.75), audioLaneY});
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.75), "Left handle trims start");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Left trim advances source offset");
        drag({view.xFor(1.5) - 2.0f, audioLaneY}, {view.xFor(1.25), audioLaneY});
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.25), "Right handle trims end");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Right trim preserves source offset");
        drag({view.xFor(1.25) - 2.0f, audioLaneY}, {view.xFor(1.5), audioLaneY});
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.5), "Right handle expands audio to available source end");
        drag({view.xFor(0.75) + 2.0f, audioLaneY}, {view.xFor(0.5), audioLaneY});
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.5), "Left handle expands audio to available source start");
        require(close(clip->getPosition().offset.inSeconds(), 0.0), "Left expansion rewinds source offset");
        session.undo();
        session.undo();
        require(close(clip->getPosition().time.getStart().inSeconds(), 0.75), "Undo restores left expansion");
        require(close(clip->getPosition().time.getEnd().inSeconds(), 1.25), "Undo restores right expansion");
        const auto originalPosition = clip->getPosition();
        view.mouseDown(event({360, audioLaneY}, {360, audioLaneY}, false));
        view.mouseDrag(event({360, audioLaneY}, {500, audioLaneY}, true));
        view.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        view.mouseUp(event({360, audioLaneY}, {500, audioLaneY}, true));
        require(close(clip->getPosition().time.getStart().inSeconds(), originalPosition.time.getStart().inSeconds()), "Escape cancels preview");

        // AudioThumbnail scans asynchronously. Its cache is intentionally
        // driven by the UI message loop, which a command-line test does not
        // run. Verify the source contract here; visible thumbnail painting is
        // exercised by the desktop UI rather than repeatedly snapshotting an
        // active scanner from a headless process.
        scenario("gestures: waveform source");
        juce::AudioFormatManager waveformFormats;
        waveformFormats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> waveformReader(
            waveformFormats.createReaderFor(clip->getSourceFileReference().getFile()));
        require(waveformReader != nullptr && waveformReader->lengthInSamples > 0,
                "Imported audio provides a readable waveform source");

        scenario("gestures: clip clipboard");
        view.setSelection({id});
        view.selectedTrack = 1;
        // Copy takes the selected clip; the insert point says where the paste
        // lands, exactly as clicking an empty part of the lane would.
        view.setInsertPoint(2.0, 1);
        view.keyPressed(juce::KeyPress('C', juce::ModifierKeys::commandModifier, 0));
        view.keyPressed(juce::KeyPress('V', juce::ModifierKeys::commandModifier, 0));
        require(view.selectedClips.size() == 1, "Paste selects the new clip");
        const auto pastedID = view.selectedClips.front();
        const auto* pasted = session.findAudioClip(pastedID);
        require(pasted != nullptr, "Ctrl-V creates an audio clip");
        require(close(pasted->getPosition().time.getStart().inSeconds(), 2.0), "Paste anchors the copied clip at the insert point");
        require(close(view.timeSelection.start, 2.0) && close(view.timeSelection.end, 2.0 + view.clipboard.front().end),
                "Paste moves the time selection onto what it just pasted");
        session.deleteClip(pastedID);
        view.setSelection({id});
        view.clearTimeSelection();

        scenario("persistence: track state");
        session.toggleTrackMute(1);
        require(te::getAudioTracks(*session.edit)[1]->isMuted(false), "Audio mute");
        session.undo();
        require(!te::getAudioTracks(*session.edit)[1]->isMuted(false), "Undo mute");
        session.toggleTrackSolo(1);
        require(te::getAudioTracks(*session.edit)[1]->isSolo(false), "Audio solo");
        session.undo();
        view.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        require(session.findAudioClip(id) == nullptr, "Delete selected clip");
        session.undo();
        require(session.findAudioClip(id) != nullptr, "Undo restores deleted clip");
        scenario("persistence: project restore");
        const auto saved = session.projectSnapshot();
        juce::TemporaryFile project(".rhinoedit");
        require(session.restoreProject(saved, project.getFile()).wasOk(), "Reopen arranged project");
        clip = session.findAudioClip(id);
        require(clip != nullptr, "Clip identity survives reopen");
        require(close(clip->getPosition().offset.inSeconds(), 0.25), "Source offset survives reopen");

        scenario("persistence: trimmed render");
        juce::TemporaryFile rendered(".wav");
        te::Renderer::Parameters parameters(*session.edit);
        parameters.destFile = rendered.getFile();
        parameters.audioFormat = &wav;
        parameters.sampleRateForAudio = 48000;
        parameters.bitDepth = 24;
        parameters.time = clip->getPosition().time;
        {
            te::Renderer::RenderTask task("Trimmed audio test", parameters, nullptr, nullptr);
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
            while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                require(juce::Time::getMillisecondCounterHiRes() < deadline, "Render timeout");
            require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
        }
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(rendered.getFile()));
        require(reader != nullptr, "Read trimmed render");
        require(std::abs(static_cast<double>(reader->lengthInSamples) - 24000.0) <= 512.0, "Render matches trimmed duration");
        juce::AudioBuffer<float> output(2, static_cast<int>(reader->lengthInSamples));
        require(reader->read(&output, 0, output.getNumSamples(), 0, true, true), "Read trimmed samples");
        const auto early = output.getRMSLevel(0, 2400, 4800);
        const auto late = output.getRMSLevel(0, 14400, 4800);
        require(early > 0.01f && late > early * 4.0f, "Rendered source window must match the visible trim");
        const auto audioClipsBeforeSample = te::getAudioTracks(*session.edit)[1]->getClips().size();
        require(view.applyBrowserDrop("rhino-browser:sample:Whistle", 1, 0.5).wasOk(),
                "Browser audio sample drop imports a whistle WAV");
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == audioClipsBeforeSample + 1,
                "Dropped browser sample creates an audio clip rather than MIDI notes");
        view.setLookAndFeel(nullptr);
        session.releaseAudioDevice();
