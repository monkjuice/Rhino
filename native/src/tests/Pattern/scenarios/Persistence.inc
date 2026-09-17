        require(session.importAudio(output.getFile()).wasOk(), "Import rendered audio");
        auto* audioTrack = te::getAudioTracks(*session.edit)[1];
        require(audioTrack->getClips().size() == 1, "Import creates an audio clip");
        require(audioTrack->getClips()[0]->getPosition().time.getStart().inSeconds() == 0.0, "First audio import starts at zero");

        // Automation lanes live on the track's own state, so they ride the
        // project snapshot rather than needing a save path of their own.
        const auto persistedParameters = session.deviceParameters(0, 0);
        require(!persistedParameters.empty(), "Pattern device exposes a parameter before saving");
        require(session.showTrackAutomation({0, 0, 0}, true).wasOk(), "Show an automation lane before saving");
        require(session.setTrackAutomationPoints({0, 0, 0}, {{0.0, persistedParameters.front().minimum},
                                                             {2.0, persistedParameters.front().maximum}}).wasOk(),
                "Draw an automation curve before saving");

        juce::TemporaryFile project(".rhinoedit");
        const auto snapshot = session.projectSnapshot();
        auto projectXml = snapshot.createXml();
        require(projectXml->writeTo(project.getFile()), "Write project snapshot");
        session.projectSaved(snapshot, project.getFile());
        require(!session.hasUnsavedChanges(), "Saved snapshot is clean");
        session.clearPattern();
        require(session.hasUnsavedChanges(), "Edits become dirty immediately");
        session.projectSaved(snapshot, project.getFile());
        require(session.hasUnsavedChanges(), "Saving an older snapshot must preserve dirty state");
        require(session.restoreProject(juce::ValueTree("invalid"), project.getFile()).failed(), "Reject invalid project");
        require(session.pattern().getSequence().getNumNotes() == 0, "Failed open preserves current edit");
        auto loadedXml = juce::parseXML(project.getFile());
        require(loadedXml != nullptr, "Read saved project");
        require(session.restoreProject(juce::ValueTree::fromXml(*loadedXml), project.getFile()).wasOk(), "Restore project");
        require(session.utility != nullptr && session.audioUtility != nullptr,
                "Project restore keeps the Utility devices");
        require(session.patternInstrument() != nullptr, "Project restore keeps the pattern track instrument");
        require(session.isPatternDrums(), "Project restore keeps drum instrument selection");
        require(session.pattern().getSequence().getNumNotes() == 10, "Notes survive project reopen");
        require(std::abs(session.tempo() - 90.0) < 0.001, "Tempo survives project reopen");
        {
            const auto restoredLane = session.trackAutomationState({0, 0, 0});
            require(restoredLane.visible && restoredLane.ownLane && restoredLane.active,
                    "Automation lanes and their own-lane placement survive project reopen");
            const auto restored = session.trackAutomations(0);
            require(restored.size() == 1 && restored.front().points.size() == 2,
                    "A restored automation curve keeps its points");
        }
        require(!session.hasUnsavedChanges(), "Opened project is clean");
        require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "Audio clip survives project reopen");
        require(te::getAudioTracks(*session.edit)[1]->getClips()[0]->getSourceFileReference().getFile() == output.getFile(),
                "Imported audio path survives project reopen");
        const auto patternTrack = te::getAudioTracks(*session.edit)[0];
        session.deleteClip(session.pattern().itemID);
        require(patternTrack->getClips().size() >= 1, "Deleting the last edited pattern keeps an editable clip alive");
        require(session.pattern().getSequence().getNumNotes() == 0, "Replacement pattern clip is empty and readable");
        session.releaseAudioDevice();
