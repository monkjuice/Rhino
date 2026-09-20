        // The audio editor's whole claim is that its controls belong to one
        // clip: the track fader beside it in the arrangement moves the lane,
        // and these have to move only what was double-clicked. Every assertion
        // here therefore checks the clip next to it as well.
        {
            view.sync();
            view.fit();
            require(session.findAudioClip(id) != nullptr, "The imported audio clip survives the earlier scenario");
            require(session.audioClipMix(id).valid, "An audio clip reports a clip mix");
            require(!session.audioClipMix(session.pattern().itemID).valid, "A MIDI clip has no audio clip mix");
            require(!session.audioClipMix({}).valid, "An id that names nothing has no audio clip mix");

            require(session.duplicateClip(id).wasOk(), "Can put a second clip on the lane to compare against");
            auto* neighbourClip = te::getAudioTracks(*session.edit)[1]->getClips().getLast();
            const auto neighbourID = neighbourClip->itemID;
            require(neighbourID != id, "The duplicate is a clip of its own");
            const auto trackVolumeBefore = session.trackMixer(1).volumeDb;

            require(session.setAudioClipGainDb(id, -8.5f).wasOk(), "Can set one clip's gain");
            require(close(session.audioClipMix(id).gainDb, -8.5), "Clip gain is stored on the clip");
            require(close(session.audioClipMix(neighbourID).gainDb, 0.0), "The clip beside it keeps its own gain");
            require(close(session.trackMixer(1).volumeDb, trackVolumeBefore), "Clip gain leaves the track fader alone");
            session.undo();
            require(close(session.audioClipMix(id).gainDb, 0.0), "Undo restores clip gain");

            require(session.setAudioClipPan(id, -0.75f).wasOk(), "Can pan one clip");
            require(close(session.audioClipMix(id).pan, -0.75), "Clip pan is stored on the clip");
            require(close(session.audioClipMix(neighbourID).pan, 0.0), "The clip beside it stays centred");

            require(session.setAudioClipPitch(id, 900.0f).wasOk(), "An out-of-range pitch is clamped rather than refused");
            require(close(session.audioClipMix(id).pitchSemitones, Session::maximumClipPitchSemitones),
                    "Clip pitch stops at the range the panel offers");
            require(session.setAudioClipPitch(id, 5.0f).wasOk(), "Can transpose one clip");
            require(close(session.audioClipMix(id).pitchSemitones, 5.0), "Clip pitch is stored on the clip");
            require(close(session.audioClipMix(neighbourID).pitchSemitones, 0.0), "The clip beside it keeps its pitch");

            const auto clipLength = session.audioClipMix(id).lengthSeconds();
            require(clipLength > 0.1, "The clip is long enough to fade over");
            require(session.setAudioClipFadeIn(id, clipLength * 0.25).wasOk(), "Can fade one clip in");
            require(close(session.audioClipMix(id).fadeInSeconds, clipLength * 0.25), "The fade in is stored on the clip");
            require(close(session.audioClipMix(neighbourID).fadeInSeconds, 0.0), "The clip beside it keeps its own fades");
            // Asking for more fade than there is room for shortens the other
            // one rather than failing, which is why the panel reads both back.
            require(session.setAudioClipFadeOut(id, clipLength).wasOk(), "Can ask for a fade out with no room left for it");
            const auto faded = session.audioClipMix(id);
            require(faded.fadeInSeconds + faded.fadeOutSeconds <= clipLength + 0.0001,
                    "The two fades are kept from overlapping");
            require(faded.fadeOutSeconds > 0.0, "The fade out that was asked for is still there");

            require(session.setAudioClipMuted(id, true).wasOk(), "Can mute one clip");
            require(session.audioClipMix(id).muted, "The clip is muted");
            require(!session.audioClipMix(neighbourID).muted, "The clip beside it goes on playing");
            require(!session.trackMixer(1).muted, "Muting a clip does not mute its track");
            require(session.setAudioClipReversed(id, true).wasOk(), "Can reverse one clip");
            require(session.audioClipMix(id).reversed, "The clip plays backwards");
            require(!session.audioClipMix(neighbourID).reversed, "The clip beside it plays forwards");

            // A fader drag is hundreds of values and one undo step. Without the
            // bracket every pixel of it would be an entry of its own.
            const auto gainBeforeGesture = session.audioClipMix(id).gainDb;
            session.beginAudioClipGesture("Clip gain");
            for (int gainStep = 0; gainStep < 8; ++gainStep)
                require(session.setAudioClipGainDb(id, static_cast<float>(-gainStep)).wasOk(), "A gesture takes every value");
            session.endAudioClipGesture();
            require(close(session.audioClipMix(id).gainDb, -7.0), "A gesture leaves the last value it was handed");
            session.undo();
            require(close(session.audioClipMix(id).gainDb, gainBeforeGesture), "A whole gesture undoes in one step");

            // Double-clicking a clip is what opens it; a single click only
            // selects it, and a double-click on empty lane space still makes
            // a clip rather than opening one.
            te::EditItemID openedClip;
            int openedCount = 0;
            view.clipOpened = [&openedClip, &openedCount](te::EditItemID clip)
            {
                openedClip = clip;
                ++openedCount;
            };
            const auto doubleClick = [&view](juce::Point<float> point)
            {
                const juce::MouseEvent clicked(juce::Desktop::getInstance().getMainMouseSource(), point,
                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                    &view, &view, juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 2, false);
                view.mouseDown(clicked);
                view.mouseUp(clicked);
            };
            view.sync();
            view.fit();
            const auto opened = session.audioClipMix(id);
            const auto clipCentre = juce::Point<float>(view.xFor((opened.startSeconds + opened.endSeconds) * 0.5),
                                                       view.lane(1).getCentreY());
            doubleClick(clipCentre);
            require(openedCount == 1 && openedClip == id, "Double-clicking an audio clip reports it as opened");
            const auto singleClick = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), clipCentre,
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                &view, &view, juce::Time::getCurrentTime(), clipCentre, juce::Time::getCurrentTime(), 1, false);
            view.mouseDown(singleClick);
            view.mouseUp(singleClick);
            require(openedCount == 1, "A single click selects a clip without opening it");
            view.clipOpened = nullptr;

            te::EditItemID reportedClip;
            int reportedCount = 0;
            view.clipSelected = [&reportedClip, &reportedCount](te::EditItemID clip)
            {
                reportedClip = clip;
                ++reportedCount;
            };
            view.setSelection({neighbourID}, neighbourID);
            require(reportedCount == 1 && reportedClip == neighbourID, "Selecting a different clip reports it");
            view.setSelection({neighbourID}, neighbourID);
            require(reportedCount == 1, "Selecting the same clip again reports nothing");
            view.clipSelected = nullptr;

            // The panel itself: it shows whatever audio clip it is handed, and
            // empties rather than refusing when handed anything else, because
            // the shell passes it whatever the arrangement has selected.
            {
                auto panel = std::make_unique<AudioClipPanel>(session);
                panel->setBounds(0, 0, 900, 200);
                panel->setClip(id);
                require(panel->hasClip(), "The audio editor opens on an audio clip");
                require(panel->clipID() == id, "The audio editor shows the clip it was handed");
                require(panel->clipName().isNotEmpty(), "The audio editor names the clip it is showing");
                // Painting is the one thing the API above cannot check, and an
                // editor that throws or draws nothing but its own background is
                // better caught here than on screen.
                const auto drawn = panel->createComponentSnapshot(panel->getLocalBounds());
                require(drawn.getWidth() == 900 && drawn.getHeight() == 200, "The audio editor paints at the size it was given");
                bool paintedSomething = false;
                for (int x = 4; x < drawn.getWidth() && !paintedSomething; x += 7)
                    for (int y = 4; y < drawn.getHeight(); y += 7)
                        if (drawn.getPixelAt(x, y) != juce::Colour(0xff1a1f24))
                        {
                            paintedSomething = true;
                            break;
                        }
                require(paintedSomething, "The audio editor draws its controls and waveform, not just a background");
                panel->setClip(session.pattern().itemID);
                require(!panel->hasClip(), "A MIDI clip empties the audio editor instead of being drawn in it");
            }

            // Put the lane back the way the next scenario expects to find it.
            require(session.setAudioClipGainDb(id, 0.0f).wasOk(), "Can restore clip gain");
            require(session.setAudioClipPan(id, 0.0f).wasOk(), "Can restore clip pan");
            require(session.setAudioClipPitch(id, 0.0f).wasOk(), "Can restore clip pitch");
            require(session.setAudioClipFadeIn(id, 0.0).wasOk(), "Can clear the fade in");
            require(session.setAudioClipFadeOut(id, 0.0).wasOk(), "Can clear the fade out");
            require(session.setAudioClipMuted(id, false).wasOk(), "Can unmute the clip");
            require(session.setAudioClipReversed(id, false).wasOk(), "Can play the clip forwards again");
            session.deleteClip(neighbourID);
            view.sync();
            view.fit();
            view.setSelection({id}, id);
            require(te::getAudioTracks(*session.edit)[1]->getClips().size() == 1, "The lane is back to one audio clip");
            require(close(session.audioClipMix(id).gainDb, 0.0) && !session.audioClipMix(id).reversed,
                    "The clip is back to its default mix");
        }
