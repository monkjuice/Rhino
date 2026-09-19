        // A group is a bus: its own track carries the name, fader and devices,
        // and every member's output is routed into it. That track is an
        // ordinary audio track, which is what lets the mixer, the devices and
        // the automation reach it with no code of their own - and it is what
        // these first assertions are really about.
        const auto tracksBeforeGroups = session.trackCount();
        while (session.trackCount() < 4)
            require(session.addAudioTrack().wasOk(), "Groups need four tracks to work with");
        view.setSize(1000, 620);
        view.sync();
        view.resized();
        const auto clickAt = [&view](juce::Point<float> point)
        {
            const juce::ModifierKeys leftButton(juce::ModifierKeys::leftButtonModifier);
            const juce::MouseEvent press(juce::Desktop::getInstance().getMainMouseSource(), point, leftButton,
                1.0f, 0, 0, 0, 0, &view, &view, juce::Time::getCurrentTime(), point,
                juce::Time::getCurrentTime(), 1, false);
            view.mouseDown(press);
            view.mouseUp(press);
        };
        const auto destinationOf = [&session](int track)
        {
            return te::getAudioTracks(*session.edit)[track]->getOutput().getDestinationTrack();
        };

        scenario("track groups: grouping builds a bus above the tracks");
        require(session.trackGroups().empty(), "A document starts with no groups");
        const auto looseCount = session.trackCount();
        require(session.groupTracks({1, 2}, "Drums").wasOk(), "Two cards can be grouped");
        require(session.trackCount() == looseCount + 1, "Grouping adds the group's own track");
        auto bands = session.trackGroups();
        require(bands.size() == 1, "and there is one group");
        const auto bandId = bands[0].id;
        const auto bus = bands[0].busTrack;
        require(bus == 1, "The bus takes the place the topmost grouped card had");
        require(bands[0].firstTrack == 2 && bands[0].trackCount == 2,
                "and its members are the run directly under it");
        require(session.trackName(bus) == "Drums", "The bus carries the group's name");
        require(session.isGroupBusTrack(bus) && session.trackGroupId(bus) == 0,
                "A bus is the group rather than a member of it");
        require(session.trackGroupId(2) == bandId && session.trackGroupId(3) == bandId,
                "Both cards under it belong to the group");

        scenario("track groups: the members feed the bus");
        require(destinationOf(2) == te::getAudioTracks(*session.edit)[bus]
                    && destinationOf(3) == te::getAudioTracks(*session.edit)[bus],
                "Every member sends its audio to the bus rather than to the main output");
        require(destinationOf(0) == nullptr && destinationOf(4) == nullptr,
                "and the tracks outside it still go straight to the main output");

        scenario("track groups: a bus takes audio effects and nothing else");
        {
            const auto slots = session.deviceSlots(bus).size();
            require(session.addAudioEffect(Session::AudioEffect::Reverb, bus).wasOk(),
                    "An audio effect can go on a group");
            require(session.deviceSlots(bus).size() == slots + 1, "and joins its chain");
            require(session.addInstrument(Session::Instrument::FourOsc, bus).failed(),
                    "An instrument cannot: a group carries audio that has already been played");
            require(session.addMidiEffect(Session::MidiEffect::RhinoArp, bus).failed(),
                    "and a MIDI effect cannot, because there is no sequence left to act on");
            require(!session.trackHasInstrument(bus), "so a bus never runs an instrument");
        }

        scenario("track groups: a bus takes no clips");
        require(session.createClip(bus, 0.0).failed(), "A group refuses a MIDI clip");
        require(session.importAudioAt(source.getFile(), bus, 0.0).failed(), "and refuses audio");
        require(te::getAudioTracks(*session.edit)[bus]->getClips().isEmpty(), "so it stays empty");
        require(session.insertAudioFileInSlot(source.getFile(), bus, 0).failed(),
                "and has no session slots to fill either");

        scenario("track groups: one level only");
        require(session.groupTracks({bus, 4}).failed(), "A group cannot go inside another group");
        require(session.addTracksToGroup({bus}, bandId).failed(), "nor be added to one");

        scenario("track groups: the cards read as one block");
        view.sync();
        view.resized();
        require(view.groups.size() == 1, "The arrangement reads the group from the session");
        require(view.trackIndent(bus) == 0.0f, "A bus card starts at the panel edge");
        require(view.trackIndent(2) == view.groupIndent && view.trackIndent(3) == view.groupIndent,
                "and the cards inside it start at the indent");
        require(view.mute[2]->getX() - view.mute[bus]->getX() == static_cast<int>(view.groupIndent),
                "so their controls move right by exactly that much");
        require(view.cardAt({90.0f, view.lane(bus).getCentreY()}) == bus,
                "A bus is a card like any other: it can be selected, carried and resized");

        scenario("track groups: collapsing folds the members away");
        const auto openStackHeight = view.rowsHeight;
        require(session.setTrackGroupCollapsed(bandId, true).wasOk(), "A group can be collapsed");
        view.sync();
        view.resized();
        require(view.lane(2).getHeight() == 0.0f && view.lane(3).getHeight() == 0.0f,
                "A collapsed group leaves its members no height");
        require(view.lane(bus).getHeight() > 0.0f, "while the bus keeps its own row");
        require(view.rowsHeight < openStackHeight, "The stack is shorter by what the folded rows took");
        require(!view.mute[2]->isVisible() && !view.volume[2]->isVisible(),
                "A folded member's controls go with it");
        require(destinationOf(2) == te::getAudioTracks(*session.edit)[bus],
                "Folding is a view setting: the audio still goes where it did");

        scenario("track groups: the disclosure folds and unfolds");
        clickAt(view.busDisclosureBounds(view.lane(bus)).getCentre());
        require(!session.trackGroups().front().collapsed, "Clicking the arrow on the bus expands a folded group");
        view.sync();
        view.resized();
        require(view.lane(2).getHeight() > 0.0f && view.mute[2]->isVisible(),
                "Expanding brings the members back with their controls");
        clickAt(view.busDisclosureBounds(view.lane(bus)).getCentre());
        require(session.trackGroups().front().collapsed, "and clicking it again folds the group back up");
        require(session.setTrackGroupCollapsed(bandId, false).wasOk(), "A group can be expanded again");
        view.sync();
        view.resized();

        scenario("track groups: the bus is part of the document");
        {
            const std::function<juce::ValueTree(const juce::ValueTree&)> findBus =
                [&findBus](const juce::ValueTree& tree) -> juce::ValueTree
            {
                if (tree.hasProperty("rhinoGroupBus")) return tree;
                for (int i = 0; i < tree.getNumChildren(); ++i)
                    if (const auto found = findBus(tree.getChild(i)); found.isValid()) return found;
                return {};
            };
            const auto snapshot = session.projectSnapshot();
            const auto savedBus = findBus(snapshot);
            require(savedBus.isValid(), "The bus is written into the saved project");
            require(static_cast<int>(savedBus.getProperty("rhinoGroupBus")) == bandId,
                    "carrying the id its members name");
        }

        scenario("track groups: a command reaches every selected card");
        require(session.addTracksToGroup({0, 4}, bandId).wasOk(), "Several loose cards can join at once");
        require(session.trackGroups().front().trackCount == 4, "and the group covers all of them");
        require(destinationOf(session.trackGroups().front().firstTrack)
                    == te::getAudioTracks(*session.edit)[session.trackGroups().front().busTrack],
                "with their audio following them in");
        require(session.removeTracksFromGroup({session.trackGroups().front().firstTrack,
                                               session.trackGroups().front().lastTrack()}).wasOk(),
                "Several members can leave at once");
        bands = session.trackGroups();
        require(bands.size() == 1 && bands[0].trackCount == 2, "leaving the group with the two that stayed");

        scenario("track groups: where a track sits decides what it feeds");
        {
            const auto below = bands[0].lastTrack() + 1;
            require(session.trackGroupId(below) == 0 && destinationOf(below) == nullptr,
                    "A card below the group is outside it");
            require(session.moveTrack(below, bands[0].lastTrack()).wasOk(),
                    "and can be carried into the middle of it");
            bands = session.trackGroups();
            require(bands[0].trackCount == 3, "Landing between two members joins the group");
            require(destinationOf(bands[0].lastTrack()) == te::getAudioTracks(*session.edit)[bands[0].busTrack],
                    "and the audio follows it in without anything else being asked");
            require(session.removeTracksFromGroup({bands[0].lastTrack()}).wasOk(), "It can be taken back out");
            bands = session.trackGroups();
            require(bands[0].trackCount == 2 && destinationOf(bands[0].lastTrack() + 1) == nullptr,
                    "and its audio goes back to the main output");
        }

        scenario("renaming: the name is edited on the card, not in a dialog");
        {
            require(!view.isRenaming(), "Nothing is being renamed to start with");
            // Adding a card above the group carried the bus up with it, so the
            // index it started at is not the one it is at now.
            const auto renamed = session.trackGroups().front().busTrack;
            const auto original = session.trackName(renamed);
            view.sync();
            view.selectTrack(renamed);
            require(view.keyPressed(juce::KeyPress(juce::KeyPress::F2Key)), "F2 is answered by the arrangement");
            require(view.isRenaming() && view.nameEditor.isVisible(),
                    "and puts an editor on the card rather than opening a window");
            require(view.nameEditor.getText() == original, "primed with the name it is replacing");
            view.nameEditor.setText("Kit");
            view.endRename(true);
            require(session.trackName(renamed) == "Kit", "and a group is renamed by renaming its card");
            require(session.trackGroups().front().name == "Kit", "which is the group's name");
            view.keyPressed(juce::KeyPress(juce::KeyPress::F2Key));
            view.nameEditor.setText("Discarded");
            view.endRename(false);
            require(session.trackName(renamed) == "Kit", "Escape leaves the name as it was");
            require(session.setTrackName(renamed, original).wasOk(), "The card can be named back");
            view.selectTrack(session.masterTrackIndex());
            view.keyPressed(juce::KeyPress(juce::KeyPress::F2Key));
            require(!view.isRenaming(), "The main row has no name to edit");
        }

        scenario("track groups: Ctrl+G groups the selected cards");
        require(session.ungroupTracks(bandId).wasOk(), "A group can be taken away");
        require(session.trackGroups().empty(), "Ungrouping leaves no group behind");
        require(session.trackCount() == looseCount, "and takes the bus track with it");
        require(destinationOf(1) == nullptr && destinationOf(2) == nullptr,
                "handing every member back to the main output");
        view.sync();
        view.selectTrack(1);
        view.selectTrackRange(1, 2);
        const auto commandG = juce::KeyPress('G', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
        require(view.keyPressed(commandG), "Ctrl+G is answered by the arrangement");
        require(session.trackGroups().size() == 1, "and groups the cards the selection had gathered");
        require(session.trackGroups().front().trackCount == 2, "both of them");
        const auto commandShiftG = juce::KeyPress('G',
            juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);
        require(view.keyPressed(commandShiftG), "Ctrl+Shift+G is answered too");
        require(session.trackGroups().empty(), "and takes the group off them again");

        scenario("track groups: deleting the bus dissolves the group");
        require(session.groupTracks({1, 2}, "Temp").wasOk(), "A group can be made again");
        {
            const auto temp = session.trackGroups().front();
            require(session.removeAudioTrack(temp.busTrack).wasOk(), "Its bus can be deleted like any track");
            require(session.trackGroups().empty(), "which dissolves the group");
            require(session.trackCount() == looseCount, "leaving every track it held");
            require(destinationOf(1) == nullptr, "and putting their audio back on the main output");
        }

        while (session.trackCount() > tracksBeforeGroups)
            require(session.removeAudioTrack(session.trackCount() - 1).wasOk(),
                    "The tracks this scenario added can be removed again");
        view.sync();
        view.resized();
