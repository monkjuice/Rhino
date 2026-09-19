        // Auditioning a library sound is deliberately outside the document: it
        // picks no track, writes no clip and opens no undo transaction, which
        // is what makes it safe to click through a folder of samples while a
        // project is open.
        {
            const auto tracksBefore = session.trackCount();
            const auto clipsBefore = te::getAudioTracks(*session.edit)[0]->getClips().size();
            const auto couldUndo = session.edit->getUndoManager().canUndo();
            const auto wasModified = session.hasUnsavedChanges();

            require(session.previewEnabled(), "A test run auditions whatever the developer's own setting says");
            require(!session.isPreviewing(), "and is playing nothing to begin with");
            require(session.previewSample(source.getFile()).wasOk(), "A library sample can be auditioned");
            require(session.isPreviewing(), "and starts playing straight away");
            // The fixture is one second long. Reading that back off the
            // transport is what says the file was opened and its reader
            // installed, which isPreviewing alone would not.
            require(std::abs(session.previewTransport.getLengthInSeconds() - 1.0) < 0.05,
                    "with the sample's own length behind it");
            require(session.trackCount() == tracksBefore, "Auditioning creates no track");
            require(te::getAudioTracks(*session.edit)[0]->getClips().size() == clipsBefore,
                    "and no clip");
            require(session.edit->getUndoManager().canUndo() == couldUndo,
                    "and nothing that could be undone");
            require(session.hasUnsavedChanges() == wasModified, "so there is nothing new to save");

            // A second click replaces the first rather than stacking on it.
            require(session.previewSample(source.getFile()).wasOk(), "Clicking another sound is allowed while one plays");
            require(session.isPreviewing(), "and the new one is what is playing");
            session.stopPreview();
            require(!session.isPreviewing(), "Auditioning stops when it is asked to");

            require(session.previewSample(juce::File()).failed(), "A sample that is not there cannot be auditioned");
            require(!session.isPreviewing(), "and leaves nothing playing");

            session.setPreviewEnabled(false);
            require(!session.previewEnabled(), "Auditioning can be switched off from the Edit menu");
            require(session.previewSample(source.getFile()).wasOk(),
                    "and then clicking a sound is not an error, it simply does nothing");
            require(!session.isPreviewing(), "Nothing plays while it is off");
            session.setPreviewEnabled(true);
            require(session.previewSample(source.getFile()).wasOk(), "Switching it back on auditions again");
            require(session.isPreviewing(), "and the sound plays");
            session.stopPreview();
        }
