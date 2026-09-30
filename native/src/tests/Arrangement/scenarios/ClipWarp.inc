// Time warp: an audio clip that follows the song's tempo instead of playing at
// the speed it was recorded at.
//
// The claim this makes - "the clip stretches and the pitch holds" - cannot be
// settled by reading the model, because none of the stretching happens there.
// So it renders, and measures the rendered audio: how long the clip actually
// is, and what note is actually in it. A 440 Hz tone stretched to twice its
// length is still 440 Hz in every mode but Repitch, where it is 220 - and
// that difference between two modes over one file is the whole of what the
// mode chooser means.
{
    // Two seconds of a steady tone, which is four beats at 120 and so reads
    // back as a clip tempo of 120 with no metadata in the file at all.
    juce::TemporaryFile warpSource(".wav");
    {
        std::unique_ptr<juce::OutputStream> stream = warpSource.getFile().createOutputStream();
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
            .withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        require(writer != nullptr, "A warp fixture can be written");
        juce::AudioBuffer<float> audio(1, 96000);
        for (int i = 0; i < 96000; ++i)
            audio.setSample(0, i, 0.5f * static_cast<float>(
                std::sin(i * 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0)));
        require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write the warp fixture");
    }

    Session warp;
    require(warp.importAudio(warpSource.getFile()).wasOk(), "A two second tone imports");
    auto* warpTrack = te::getAudioTracks(*warp.edit)[1];
    require(warpTrack != nullptr && !warpTrack->getClips().isEmpty(), "and lands on the first free audio lane");
    const auto warpClip = warpTrack->getClips().getLast()->itemID;
    const auto clipLength = [&warp, warpClip]
    {
        auto* clip = warp.findAudioClip(warpClip);
        return clip != nullptr ? clip->getPosition().time.getLength().inSeconds() : 0.0;
    };

    // How much of a rendered file is at a given frequency. One DFT bin, worked
    // out directly rather than through an FFT, because two frequencies are all
    // this has to tell apart and the window is not a power of two.
    const auto toneAt = [&require](const juce::File& file, double fromSeconds, double toSeconds, double hertz)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        require(reader != nullptr, "A render can be read back");
        const auto rate = reader->sampleRate;
        const auto first = static_cast<juce::int64>(fromSeconds * rate);
        const auto count = static_cast<int>((toSeconds - fromSeconds) * rate);
        require(count > 0 && first + count <= reader->lengthInSamples, "The render is long enough to measure");
        juce::AudioBuffer<float> audio(static_cast<int>(reader->numChannels), count);
        reader->read(&audio, 0, count, first, true, true);
        double real = 0.0, imaginary = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const auto angle = 2.0 * juce::MathConstants<double>::pi * hertz * i / rate;
            const auto sample = static_cast<double>(audio.getSample(0, i));
            real += sample * std::cos(angle);
            imaginary += sample * std::sin(angle);
        }
        return std::sqrt(real * real + imaginary * imaginary) / count;
    };
    const auto renderWarpClip = [&warp, &wav, &require, warpClip](const juce::File& destination)
    {
        auto* clip = warp.findAudioClip(warpClip);
        require(clip != nullptr, "The warped clip is still there to render");
        te::Renderer::Parameters parameters(*warp.edit);
        parameters.destFile = destination;
        parameters.audioFormat = &wav;
        parameters.sampleRateForAudio = 48000;
        parameters.bitDepth = 24;
        parameters.time = clip->getPosition().time;
        te::Renderer::RenderTask task("Warp test", parameters, nullptr, nullptr);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
        while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
            require(juce::Time::getMillisecondCounterHiRes() < deadline, "Warp render timeout");
        require(task.errorMessage.isEmpty(), task.errorMessage.toRawUTF8());
    };

    {
        const auto state = warp.clipWarp(warpClip);
        require(state.valid, "An audio clip has a warp state");
        require(!state.followsTempo, "and does not follow the tempo until it is asked to");
        require(!state.markersEnabled, "and carries no warp markers");
        require(std::abs(state.clipBpm - 120.0) < 0.01,
                "A two second file reads as 120 BPM in a 120 BPM document");
        require(std::abs(state.beats - 4.0) < 0.01, "which is four beats");
        require(std::abs(state.sourceLengthSeconds - 2.0) < 0.01, "and two seconds of audio");
    }
    require(warp.clipWarp({}).valid == false, "Asking about a clip that is not there answers rather than crashing");

    // Warping on, at the same tempo, changes nothing: the clip is already at
    // the tempo it is being told to follow.
    require(warp.setClipFollowsTempo(warpClip, true).wasOk(), "A clip can be warped");
    require(warp.clipWarp(warpClip).followsTempo, "and says so");
    require(std::abs(clipLength() - 2.0) < 0.01, "Warping at the same tempo leaves the clip where it was");
    require(warp.clipWarp(warpClip).mode != Session::WarpMode::repitch,
            "A newly warped clip holds its pitch rather than being resampled");

    // Half the tempo, and the clip covers twice the bars it did - which is the
    // same four beats.
    warp.setTempo(60.0);
    require(std::abs(clipLength() - 4.0) < 0.05, "Halving the tempo stretches a warped clip to twice its length");

    {
        // The arrangement has to read the clip at the speed it actually plays
        // at rather than at the speed ratio, which the engine leaves at one for
        // a warped clip. Reading the ratio draws the first half of the file
        // across the whole of a clip stretched to twice its length, and silence
        // after it - so this is checked where the timeline gets its answer.
        Arrangement warpView(warp);
        warpView.setSize(1000, 246);
        warpView.sync();
        warpView.fit();
        require(warpView.clips.size() == 1, "The stretched clip is in the arrangement");
        const auto& drawn = warpView.clips.front();
        require(std::abs(drawn.speed - 0.5) < 0.001,
                "and is read at half speed, which is the song's tempo over the clip's");
        require(std::abs(drawn.sourceDuration - 4.0) < 0.05,
                "so the whole file covers the four seconds the clip now takes");
    }

    {
        scenario("clip warp: a stretched clip keeps its pitch");
        juce::TemporaryFile stretched(".wav");
        renderWarpClip(stretched.getFile());
        // Measured away from both ends, where the stretcher is still filling.
        const auto atPitch = toneAt(stretched.getFile(), 1.0, 3.0, 440.0);
        const auto anOctaveDown = toneAt(stretched.getFile(), 1.0, 3.0, 220.0);
        require(atPitch > 0.05, "The stretched clip still sounds its own note");
        require(atPitch > anOctaveDown * 4.0, "and not the octave below it");
    }
    {
        scenario("clip warp: Repitch resamples instead");
        require(warp.setClipWarpMode(warpClip, Session::WarpMode::repitch).wasOk(), "Repitch can be chosen");
        require(warp.clipWarp(warpClip).mode == Session::WarpMode::repitch, "and is what the clip reports");
        juce::TemporaryFile repitched(".wav");
        renderWarpClip(repitched.getFile());
        const auto atPitch = toneAt(repitched.getFile(), 1.0, 3.0, 440.0);
        const auto anOctaveDown = toneAt(repitched.getFile(), 1.0, 3.0, 220.0);
        require(anOctaveDown > 0.05, "Played at half speed, the tone is an octave down");
        require(anOctaveDown > atPitch * 4.0, "which is the whole difference between Repitch and the rest");
        require(warp.setClipWarpMode(warpClip, Session::WarpMode::tones).wasOk(), "and the mode can be put back");
    }

    // Every mode survives a save and a load, because the mode is a musical
    // decision about that clip and not a preference of the session.
    for (int i = 0; i < Session::warpModeCount; ++i)
    {
        const auto mode = static_cast<Session::WarpMode>(i);
        require(Session::warpModeName(mode).isNotEmpty(), "Every warp mode has a name");
        require(Session::warpModeBlurb(mode).isNotEmpty(), "and says what it is for");
        require(warp.setClipWarpMode(warpClip, mode).wasOk(), "Every warp mode can be chosen");
        require(warp.clipWarp(warpClip).mode == mode, "and reads back as itself");
    }
    require(warp.setClipWarpMode(warpClip, Session::WarpMode::tones).wasOk(), "Back to Tones");

    warp.setTempo(120.0);
    require(std::abs(clipLength() - 2.0) < 0.05, "and the clip comes back with the tempo");

    // The clip's own tempo, which is Live's :2 and *2. Saying the loop is
    // twice as fast as Rhino thought means it holds twice as many beats, and
    // twice as many beats need twice the room at the song's tempo - so the
    // clip gets longer and plays slower. That is the direction that fixes the
    // usual mistake, which is a loop detected an octave out and playing at
    // double speed.
    require(warp.scaleClipBpm(warpClip, 2.0).wasOk(), "A clip tempo can be doubled");
    require(std::abs(warp.clipWarp(warpClip).clipBpm - 240.0) < 0.01, "to 240");
    require(std::abs(warp.clipWarp(warpClip).beats - 8.0) < 0.01, "which is eight beats in the same file");
    require(std::abs(clipLength() - 4.0) < 0.05, "and eight beats need twice the room on the timeline");
    require(warp.scaleClipBpm(warpClip, 0.5).wasOk(), "and halved again");
    require(std::abs(warp.clipWarp(warpClip).clipBpm - 120.0) < 0.01, "back to 120");
    require(std::abs(clipLength() - 2.0) < 0.05, "with the clip back at its own length");
    require(warp.setClipBpm(warpClip, 1.0e9).wasOk(), "An absurd tempo is clamped rather than refused");
    require(std::abs(warp.clipWarp(warpClip).clipBpm - Session::maximumClipBpm) < 0.01,
            "and lands on the fastest clip tempo there is");
    require(warp.setClipBpm(warpClip, 120.0).wasOk(), "Back to 120");
    require(std::abs(clipLength() - 2.0) < 0.05, "and so is the clip");
    require(warp.scaleClipBpm(warpClip, 16.0).failed(),
            "A doubling that would leave the range says so rather than clamping silently");

    // Warp markers.
    require(warp.addClipWarpMarker(warpClip, 1.0).failed(), "A marker needs the markers switched on first");
    require(warp.setClipWarpMarkersEnabled(warpClip, true).wasOk(), "Warp markers can be switched on");
    {
        const auto state = warp.clipWarp(warpClip);
        require(state.markersEnabled, "and the clip says so");
        require(state.markers.size() == 2, "A new warp starts with a marker at each end of the file");
        require(std::abs(state.markers.front().sourceSeconds) < 0.001, "one at the start");
        require(std::abs(state.markers.back().sourceSeconds - 2.0) < 0.01, "and one at the end");
    }
    require(warp.addClipWarpMarker(warpClip, 1.0).wasOk(), "A marker can be dropped inside the clip");
    require(warp.clipWarp(warpClip).markers.size() == 3, "and there are three of them");
    require(warp.addClipWarpMarker(warpClip, 1.0).failed(), "Two markers cannot share one moment in the file");
    {
        const auto state = warp.clipWarp(warpClip);
        require(std::abs(state.markers[1].sourceSeconds - 1.0) < 0.001, "The new marker is where it was put");
        require(std::abs(state.markers[1].warpSeconds - state.markers[1].sourceSeconds) < 0.001,
                "and adding one changes nothing until it is dragged");
    }
    require(warp.moveClipWarpMarker(warpClip, 1, 1.5).wasOk(), "A marker can be dragged");
    require(warp.clipWarp(warpClip).markers[1].warpSeconds > 1.4,
            "and the first half of the clip is stretched to reach it");
    require(std::abs(warp.clipWarp(warpClip).markers[1].sourceSeconds - 1.0) < 0.001,
            "while the moment in the file it pins has not moved");
    require(warp.moveClipWarpMarker(warpClip, 7, 1.0).failed(), "Moving a marker that is not there says so");
    require(warp.removeClipWarpMarker(warpClip, 1).wasOk(), "A marker can be taken away");
    require(warp.clipWarp(warpClip).markers.size() == 2, "leaving the two ends");
    require(warp.removeClipWarpMarker(warpClip, 0).wasOk(), "Removing an end marker is allowed");
    require(warp.clipWarp(warpClip).markers.size() == 2, "and straightens it rather than taking it away");
    require(warp.addClipWarpMarker(warpClip, 0.5).wasOk(), "Another marker");
    require(warp.moveClipWarpMarker(warpClip, 1, 0.9).wasOk(), "dragged out of line");
    require(warp.resetClipWarpMarkers(warpClip).wasOk(), "and everything can be straightened at once");
    {
        const auto state = warp.clipWarp(warpClip);
        require(state.markers.size() == 2, "which leaves the two ends");
        for (const auto& marker : state.markers)
            require(std::abs(marker.warpSeconds - marker.sourceSeconds) < 0.001,
                    "each one back where the file puts it");
    }

    // The whole of it survives a save and a load: warping is a property of the
    // clip, so it belongs in the document rather than in this session.
    {
        require(warp.addClipWarpMarker(warpClip, 1.25).wasOk(), "One marker to carry across");
        require(warp.moveClipWarpMarker(warpClip, 1, 1.4).wasOk(), "moved somewhere it would not be by accident");
        require(warp.setClipWarpMode(warpClip, Session::WarpMode::texture).wasOk(), "and a mode to match");
        const auto before = warp.clipWarp(warpClip);
        juce::TemporaryFile saved(".rhinoedit");
        const auto snapshot = warp.projectSnapshot();
        require(warp.restoreProject(snapshot, saved.getFile()).wasOk(), "The project reloads");
        auto* reloadedTrack = te::getAudioTracks(*warp.edit)[1];
        require(reloadedTrack != nullptr && !reloadedTrack->getClips().isEmpty(), "with its audio clip");
        const auto after = warp.clipWarp(reloadedTrack->getClips().getLast()->itemID);
        require(after.followsTempo == before.followsTempo, "still warped");
        require(after.markersEnabled == before.markersEnabled, "still carrying markers");
        require(after.mode == before.mode, "in the mode it was left in");
        require(std::abs(after.clipBpm - before.clipBpm) < 0.01, "at the tempo it was told");
        require(after.markers.size() == before.markers.size(), "with the same markers");
        for (size_t i = 0; i < after.markers.size(); ++i)
        {
            require(std::abs(after.markers[i].sourceSeconds - before.markers[i].sourceSeconds) < 0.001,
                    "each pinning the same moment in the file");
            require(std::abs(after.markers[i].warpSeconds - before.markers[i].warpSeconds) < 0.001,
                    "at the same moment in the song");
        }
    }

    // The panel. A drawn face has no child controls to assert against over the
    // waveform, so this renders it: the markers have to land where the warp
    // says they are, and the strip's controls have to fit the panel at the
    // narrowest it is ever given.
    {
        scenario("clip warp: the panel");
        // Reloading the project above replaced the document, so the clip is
        // fetched again rather than assumed to be the one from before it.
        auto* panelTrack = te::getAudioTracks(*warp.edit)[1];
        require(panelTrack != nullptr && !panelTrack->getClips().isEmpty(), "The reloaded clip is there");
        const auto panelClip = panelTrack->getClips().getLast()->itemID;
        require(warp.setClipWarpMode(panelClip, Session::WarpMode::tones).wasOk(), "A stretching mode");
        require(warp.setClipWarpMarkersEnabled(panelClip, true).wasOk(), "with markers on");
        require(warp.resetClipWarpMarkers(panelClip).wasOk(), "straightened first");
        require(warp.addClipWarpMarker(panelClip, 1.0).wasOk(), "and a marker in the middle");
        require(warp.moveClipWarpMarker(panelClip, 1, 1.4).wasOk(), "dragged late");

        AudioClipPanel panel(warp);
        panel.setClip(panelClip);
        panel.setSize(880, 150);
        panel.resized();
        const auto bounds = panel.getLocalBounds();
        require(panel.warp.markersEnabled && panel.warp.markers.size() == 3,
                "The panel reads the clip's markers back from the model");
        for (const auto* control : {&panel.warpToggle, &panel.warpModeButton, &panel.halveBpm,
                                    &panel.doubleBpm, &panel.markerToggle})
            require(bounds.contains(control->getBounds()), "Every warp control lands inside the panel");
        require(bounds.contains(panel.clipBpm.getBounds()), "and so does the clip tempo field");
        require(!panel.warpToggle.getBounds().intersects(panel.warpModeButton.getBounds()),
                "and none of them is drawn over the next");
        require(!panel.warpModeButton.getBounds().intersects(panel.clipBpm.getBounds()),
                "including the tempo field");
        require(panel.warpToggle.getToggleState(), "The Warp switch reads as on");
        require(panel.warpModeButton.getButtonText() == Session::warpModeName(Session::WarpMode::tones),
                "and the mode button names the mode");
        require(!panel.markerBar.isEmpty(), "The marker bar is reserved over the waveform");

        // The handles, where the warp puts them. The middle one was dragged
        // late, so it sits past the middle of the box rather than at it.
        const auto middle = panel.xForWarpTime(panel.warp.markers[1].warpSeconds);
        const auto first = panel.xForWarpTime(panel.warp.markers[0].warpSeconds);
        const auto last = panel.xForWarpTime(panel.warp.markers[2].warpSeconds);
        require(first < middle && middle < last, "The three handles are in order across the box");
        require(middle > (first + last) * 0.5f,
                "and the dragged one is past halfway, because the audio before it was stretched");
        require(panel.warpMarkerAt({middle, static_cast<float>(panel.markerBar.getCentreY())}) == 1,
                "A press on a handle takes that marker");
        require(panel.warpMarkerAt({middle + 40.0f, static_cast<float>(panel.markerBar.getCentreY())}) == -1,
                "and a press away from one takes nothing");
        require(std::abs(panel.sourceTimeForWarpTime(panel.warp.markers[1].warpSeconds) - 1.0) < 0.001,
                "The map runs both ways: the handle's own moment in the file comes back");

        const auto picture = panel.createComponentSnapshot(panel.getLocalBounds());
        require(picture.getWidth() == 880, "The panel renders at the size it is given");
        // The handles are the one thing on this panel that is drawn rather
        // than composed, so they are the one thing worth counting pixels for.
        const auto litColumn = [&picture](int x, int top, int bottom)
        {
            for (int y = top; y < bottom; ++y)
                if (const auto pixel = picture.getPixelAt(x, y);
                    pixel.getRed() > 150 && pixel.getGreen() > 120 && pixel.getBlue() < 140)
                    return true;
            return false;
        };
        require(litColumn(juce::roundToInt(middle), panel.markerBar.getY(), panel.markerBar.getBottom()),
                "The dragged handle is drawn where the warp says it is");
        require(!litColumn(juce::roundToInt(middle) + 30, panel.markerBar.getY(), panel.markerBar.getBottom()),
                "and nowhere else");
        if (const auto shot = juce::SystemStats::getEnvironmentVariable("RHINO_CLIP_PANEL_SNAPSHOT", {});
            shot.isNotEmpty())
        {
            juce::File(shot).deleteFile();
            if (auto stream = juce::File(shot).createOutputStream())
                juce::PNGImageFormat().writeImageToStream(picture, *stream);
        }

        // The narrowest the shell ever hands this panel. The strip drops its
        // conveniences rather than squeezing them into slivers.
        panel.setSize(360, 150);
        panel.resized();
        for (auto* control : {&panel.warpToggle, &panel.warpModeButton, &panel.halveBpm, &panel.doubleBpm})
            require(panel.getLocalBounds().contains(control->getBounds()) && control->getWidth() > 10,
                    "A narrow panel keeps the controls the strip is for");
        require(!panel.straighten.isVisible() || panel.getLocalBounds().contains(panel.straighten.getBounds()),
                "and drops the rest rather than running them off the edge");
    }
}
