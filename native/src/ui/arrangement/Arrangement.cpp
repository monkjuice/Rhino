#include "ArrangementInternal.h"
#include "ui/shared/Playhead.h"
#include "ui/shared/UiVisibility.h"
#include <optional>
#include <set>
#include <utility>

namespace rhino
{

// One frame clock for all three: a drag that has reached the edge pulls the
// view after it on the same tick the meters and the playhead move on. The
// meters go first, so the damage the playhead hands over at once
// (movePlayhead) carries theirs with it.
Arrangement::Arrangement(Session& s)
    : session(s), vblank(this, [this] { autoScrollDrag(); updateMeters(); updatePlayhead(); })
{
    configureMasterControls();
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setTitle("Arrangement");
    formats.registerBasicFormats();
    session.addChangeListener(this);
    session.listeners.add(this);
    scroll.addListener(this);
    trackScrollBar.addListener(this);
    duplicateButton.setButtonText(L"\u29c9");
    addTrack.setButtonText(L"+");
    snap.setButtonText(L"\u2317");
    duplicateButton.setTooltip("Duplicate the selection - puts a copy flush against the end of what is "
                               "selected, ready to be dragged. Ctrl+D.");
    addTrack.setTooltip("Add a track: pick audio or MIDI (Ctrl+T repeats the last kind)");
    snap.setTooltip("Snap - clips, edges and the line land on the grid. Turn it off to place them freely, "
                    "or hold Alt for one gesture.");
    gridControl.setTooltip("Grid spacing: how far apart the lines are, and what a clip snaps to");
    toolbarGrid.setTooltip("Grid spacing: how far apart the lines are, and what a clip snaps to");
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    gridControl.onClick = [this] { showGridMenu(&gridControl); };
    toolbarGrid.onClick = [this] { showGridMenu(&toolbarGrid); };
    snap.onClick = [this] { setGridEnabled(snap.getToggleState()); };
    duplicateButton.onClick = [this] { duplicateSelected(); };
    addTrack.onClick = [this] { showAddTrackMenu(); };
    for (auto* control : std::initializer_list<juce::Component*>{&duplicateButton, &addTrack, &snap, &gridControl, &toolbarGrid, &scroll, &trackScrollBar})
        addAndMakeVisible(control);
    laneHeaders.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(laneHeaders);
    returnHeaders.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(returnHeaders);
    configureNameEditor();
    sync();
}

Arrangement::~Arrangement()
{
    session.removeChangeListener(this);
    session.listeners.remove(this);
    scroll.removeListener(this);
    trackScrollBar.removeListener(this);
}

void Arrangement::configureMasterControls()
{
    masterVolume.setSliderStyle(juce::Slider::LinearBar);
    masterVolume.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    masterVolume.setRange(Session::minimumVolumeDb, Session::maximumVolumeDb, 0.1);
    masterVolume.setTextValueSuffix({});
    masterVolume.setDoubleClickReturnValue(true, 0.0);
    masterVolume.setTooltip("Main volume - the level of everything the arrangement plays. Drag to set it, "
                            "double-click for 0.0 dB.");
    masterVolume.onDragStart = [this] { session.beginMasterVolumeGesture(); };
    masterVolume.onDragEnd = [this] { session.endMasterVolumeGesture(); };
    masterVolume.onValueChange = [this] { session.setMasterVolumeDb(static_cast<float>(masterVolume.getValue())); };
    masterPan.setSliderStyle(juce::Slider::LinearBar);
    masterPan.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    masterPan.setRange(-1.0, 1.0, 0.01);
    masterPan.setDoubleClickReturnValue(true, 0.0);
    masterPan.setTooltip("Main pan - where the whole mix sits between the speakers. Drag to move it, "
                         "double-click to centre it.");
    masterPan.onDragStart = [this] { session.beginMasterPanGesture(); };
    masterPan.onDragEnd = [this] { session.endMasterPanGesture(); };
    masterPan.onValueChange = [this] { session.setMasterPan(static_cast<float>(masterPan.getValue())); };
    masterVolume.setColour(juce::Slider::trackColourId, palette::volume);
    masterVolume.setColour(juce::Slider::backgroundColourId, palette::displayInset);
    masterVolume.setColour(juce::Slider::textBoxTextColourId, palette::appBackground);
    masterVolume.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
    masterPan.setColour(juce::Slider::trackColourId, palette::pan);
    masterPan.setColour(juce::Slider::backgroundColourId, palette::displayInset);
    masterPan.setColour(juce::Slider::textBoxTextColourId, palette::appBackground);
    masterPan.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
    addAndMakeVisible(masterVolume);
    addAndMakeVisible(masterPan);
    addAndMakeVisible(masterMeter);
}

void Arrangement::resized()
{
    addTrack.setBounds(10, 5, 34, 26);
    duplicateButton.setBounds(48, 5, 34, 26);
    snap.setBounds(86, 5, 34, 26);
    toolbarGrid.setBounds(124, 5, 92, 26);
    syncTrackControls();
    // Lane height follows the component height, so the row stack has to reflow
    // before anything is positioned against it. A track added since the last
    // sync needs the full rebuild; a plain resize only needs the geometry.
    if (static_cast<int>(trackRowIndex.size()) != session.trackCount())
        buildRows();
    else
        layoutRows();
    // Above the lanes' foot rather than over the returns or the main row.
    gridControl.setBounds(getWidth() - 104, static_cast<int>(returnsTop()) - 26, 76, 20);
    updateGridControl();
    // Two containers, one for the cards that scroll and one for the returns'
    // pinned over the main row, each cropping what leaves it.
    const auto pinnedTop = static_cast<int>(returnsTop());
    laneHeaders.setBounds(0, static_cast<int>(lanesTop), static_cast<int>(headerWidth),
                          std::max(1, pinnedTop - static_cast<int>(lanesTop)));
    returnHeaders.setBounds(0, pinnedTop, static_cast<int>(headerWidth),
                            std::max(1, static_cast<int>(masterLane().getY()) - pinnedTop));
    for (int i = 0; i < session.trackCount() && i < static_cast<int>(mute.size()); ++i)
    {
        const auto row = lane(i);
        const auto index = static_cast<size_t>(i);
        // A track's controls go in the container its row is drawn in, moved
        // there when a track becomes a return or stops being one.
        auto& container = headerFor(i);
        for (auto* control : std::initializer_list<juce::Component*> {mute[index].get(), solo[index].get(),
                                                                         arm[index].get(), trackInput[index].get(),
                                                                         automationChooser[index].get(),
                                                                         monitor[index].get(), volume[index].get(),
                                                                         pan[index].get(), meters[index].get()})
            if (control->getParentComponent() != &container)
                container.addChildComponent(control);
        if (index < sends.size())
            for (auto& knob : sends[index])
                if (knob->getParentComponent() != &container)
                    container.addChildComponent(knob.get());
        const auto areaTop = static_cast<float>(container.getY());
        const auto areaBottom = static_cast<float>(container.getBottom());
        // Mute and solo sit on the name's line; the two faders share the line
        // under them at matching widths. A row dragged to its shortest gives up
        // the faders and keeps the buttons. Positions are inside the header
        // container, which crops whatever leaves the lanes.
        const auto top = static_cast<int>(row.getY() - areaTop) + cardControlsTop;
        // A card inside a group is pushed right, controls and all, so the step
        // in the left edge is unbroken down the whole group.
        const auto left = cardControlsLeft() + static_cast<int>(trackIndent(i));
        const auto right = left + cardControlWidth + cardControlGap;
        const auto mixerVisible = row.getHeight() >= mixerLaneHeight;
        // A track that takes an input gets both of the lines that describe it:
        // which input, and whether you hear it. A group bus and the main row
        // take none, so they get neither. Each line only appears once the row
        // is tall enough to hold it -- the same rule the faders already follow
        // one line up, and the track menu carries both for the rows that are
        // not.
        const auto takesInput = session.trackRecordInput(i) != Session::RecordInput::none;
        const auto chooserLine = row.getHeight() >= inputLaneHeight;
        const auto inputVisible = chooserLine && takesInput;
        const auto monitorVisible = row.getHeight() >= monitorLaneHeight && takesInput;
        // A row folded into a collapsed group is laid out at no height, so its
        // controls go with it rather than piling up under the band above.
        const auto visible = row.getHeight() > 1.0f && row.getBottom() > areaTop && row.getY() < areaBottom;
        mute[index]->setVisible(visible);
        solo[index]->setVisible(visible);
        // A group bus records nothing of its own, so it is given no dot to
        // press rather than one that refuses.
        arm[index]->setVisible(visible && takesInput);
        volume[index]->setVisible(visible && mixerVisible);
        pan[index]->setVisible(visible && mixerVisible);
        trackInput[index]->setVisible(visible && inputVisible);
        // Every track can carry lanes, a bus included, so the automation
        // chooser keeps its column where the input chooser is not given one.
        automationChooser[index]->setVisible(visible && chooserLine);
        monitor[index]->setVisible(visible && monitorVisible);
        const auto buttonStep = cardButtonWidth + cardControlGap;
        mute[index]->setBounds(left, top, cardButtonWidth, 18);
        solo[index]->setBounds(left + buttonStep, top, cardButtonWidth, 18);
        arm[index]->setBounds(left + 2 * buttonStep, top, cardButtonWidth, 18);
        volume[index]->setBounds(left, top + 23, cardControlWidth, 16);
        pan[index]->setBounds(right, top + 23, cardControlWidth, 16);
        // The input and automation choosers share the line under the faders,
        // one column each; the monitor control spans both columns under them,
        // because three segments divide that width evenly.
        trackInput[index]->setBounds(left, top + 42, cardControlWidth, 16);
        automationChooser[index]->setBounds(right, top + 42, cardControlWidth, 16);
        monitor[index]->setBounds(left, top + 61, cardControlWidth * 2 + cardControlGap, 15);
        // The meter opens the card, pushed in with the controls by a group.
        meters[index]->setVisible(visible);
        meters[index]->setBounds(cardMeterInset + static_cast<int>(trackIndent(i)), top, LevelMeter::width,
                                 std::max(0, static_cast<int>(row.getHeight() - trackDividerThickness)
                                                 - 2 * cardControlsTop));
        // The sends stand in their column between the meter and the controls,
        // one return under another from the button line down. A knob the row
        // has no room for is left off rather than the card grown for it: a
        // row dragged taller brings it back.
        if (index < sends.size())
        {
            const auto& knobs = sends[index];
            const auto column = cardControlLeft + static_cast<int>(trackIndent(i));
            const auto pitch = SendKnob::size + cardControlGap;
            const auto room = row.getHeight() - trackDividerThickness;
            for (size_t knob = 0; knob < knobs.size(); ++knob)
            {
                const auto offset = static_cast<int>(knob) * pitch;
                knobs[knob]->setBounds(column, top + offset, SendKnob::size, SendKnob::size);
                knobs[knob]->setVisible(visible && static_cast<float>(cardControlsTop + offset + SendKnob::size) <= room);
            }
        }
    }
    {
        // The main row is a card too, on its single line: the meter where
        // every card's starts and its two faders over a track's two, with its
        // name on its colour in the card's other column.
        const auto master = masterLane();
        const auto controlsY = static_cast<int>(master.getY()) + 5;
        masterVolume.setBounds(cardControlsLeft(), controlsY, cardControlWidth, 16);
        masterPan.setBounds(cardControlsLeft() + cardControlWidth + cardControlGap, controlsY, cardControlWidth, 16);
        masterMeter.setBounds(cardMeterInset, controlsY, LevelMeter::width,
                              std::max(0, static_cast<int>(master.getBottom()) - 5 - controlsY));
    }
    layoutNameEditor();
    // Both scrollbars stop at the top of whatever covers the foot of the panel,
    // so the pane floating over the arrangement never buries them.
    const auto foot = getHeight() - static_cast<int>(bottomInset);
    // Two pixels clear of the foot, because the rule that closes the main row
    // off is drawn there and a scrollbar sitting on it would hide it.
    scroll.setBounds(static_cast<int>(headerWidth),
                     foot - static_cast<int>(scrollBarHeight + trackDividerThickness),
                     getWidth() - static_cast<int>(headerWidth) - 14, static_cast<int>(scrollBarHeight));
    // The vertical one scrolls the lanes, so it stops where the returns start.
    trackScrollBar.setBounds(getWidth() - 12, static_cast<int>(lanesTop), 12,
                             std::max(1, pinnedTop - static_cast<int>(lanesTop)));
    updateScroll();
    updatePlayhead();
}

int Arrangement::sendColumnWidth() const
{
    return returns.empty() ? 0 : SendKnob::size + cardControlGap;
}

juce::Component& Arrangement::headerFor(int track)
{
    return isPinnedTrack(track) ? returnHeaders : laneHeaders;
}

void Arrangement::fit()
{
    cancelDrag();
    viewStart = 0.0;
    viewSpan = std::max(4.0, songEnd * 1.1);
    updateGridControl();
    updateScroll();
    updatePlayhead();
    repaint();
}

void Arrangement::zoom(double factor, double anchor)
{
    if (dragging) return;
    const auto fraction = (anchor - viewStart) / viewSpan;
    const auto deepest = lane(0).getWidth() / deepestZoomPixelsPerBeat * (60.0 / session.tempo());
    viewSpan = std::clamp(viewSpan * factor, deepest, std::max(defaultViewSeconds * 2.0, songEnd * 2.0));
    viewStart = std::max(0.0, anchor - viewSpan * fraction);
    updateGridControl();
    updateScroll();
    updatePlayhead();
    repaint();
}

void Arrangement::scrollBarMoved(juce::ScrollBar* bar, double start)
{
    cancelDrag();
    if (bar == &trackScrollBar)
        trackScroll = start;
    else
        viewStart = start;
    updatePlayhead();
    resized();
    repaint();
}

bool Arrangement::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() >= '1' && key.getKeyCode() <= '5')
    {
        switch (key.getKeyCode())
        {
            case '1': gridSettings.fixedDivision = narrowerGridDivision(resolvedGridDivision()); gridSettings.mode = GridMode::fixed; break;
            case '2': gridSettings.fixedDivision = widerGridDivision(resolvedGridDivision()); gridSettings.mode = GridMode::fixed; break;
            case '3': gridSettings.triplet = !gridSettings.triplet; break;
            case '4': gridSettings.mode = gridSettings.mode == GridMode::off ? GridMode::adaptive : GridMode::off; break;
            case '5': gridSettings.mode = gridSettings.mode == GridMode::adaptive ? GridMode::fixed : GridMode::adaptive; break;
        }
        snap.setToggleState(gridSettings.mode != GridMode::off, juce::dontSendNotification);
        resized();
        repaint();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'G')
    {
        if (key.getModifiers().isShiftDown()) ungroupSelection();
        else groupSelectedTracks();
        return true;
    }
    // Renaming whatever card is highlighted. A band answers before the track
    // under it, because selecting a band is how you say you meant the group.
    if (key.getKeyCode() == juce::KeyPress::F2Key)
    {
        renameTrack(selectedTrack);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        // Adds a clip to the focused track at the playhead, the keyboard
        // equivalent of double-clicking the lane: it fills the grid cell the
        // playhead is in.
        const auto start = snappedDown(std::max(0.0, session.playheadSeconds()), false);
        te::EditItemID created;
        const auto result = session.createClip(selectedTrack, start, &created, newClipBeats());
        if (status) status(result.failed() ? result.getErrorMessage()
                                           : "Added a clip to " + session.trackName(selectedTrack));
        if (result.wasOk()) openCreatedClip(created);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown()) session.redo();
        else session.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        session.redo();
        return true;
    }
    // Only with a selection to move: otherwise the arrows go on to the window.
    if ((key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
        && timeSelection.active && timeSelection.isRange())
    {
        nudgeSelected(key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1, key.getModifiers().isShiftDown());
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'E')
    {
        splitAtInsertPoint();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'J')
    {
        mergeSelected();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'B')
    {
        bounceSelected();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D')
    {
        duplicateSelected();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'C')
    {
        copySelection();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'X')
    {
        cutSelection();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'V')
    {
        pasteSelection();
        return true;
    }
    if (!key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() == 'C')
    {
        const auto result = session.cycleClipColour(selected);
        if (result.failed() && status) status(result.getErrorMessage());
        return true;
    }
    if (!key.getModifiers().isAnyModifierKeyDown() && key.getKeyCode() == 'R')
    {
        reverseSelected();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::escapeKey && (dragging || timeSelection.active))
    {
        cancelDrag();
        clearTimeSelection();
        repaint();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        cancelDrag();
        // Delete acts on whatever the last click selected, and nothing when
        // that was empty space. It never reaches past the current selection.
        if (focus == Focus::automation && focusedAutomation.isValid())
        {
            // Deleting a curve leaves its lane on screen, back to the resting
            // line. Removing the lane itself is the knob menu's "Hide".
            if (!session.trackAutomationState(focusedAutomation).active)
            {
                if (status) status("That automation lane has nothing drawn on it");
                return true;
            }
            const auto result = session.clearTrackAutomationPoints(focusedAutomation);
            if (status) status(result.wasOk() ? "Automation deleted" : result.getErrorMessage());
            return true;
        }
        if (focus == Focus::track)
        {
            deleteCards(selectedTracks);
            return true;
        }
        if (focus == Focus::region)
            deleteSelection();
        return true;
    }
    return false;
}

void Arrangement::cancelDrag()
{
    dragging = false;
    dragTravelled = false;
    collapseSelectionOnRelease = false;
    collapseSelectionTo = {};
    // A sweep cut short never names its clip, and the primary it kept until
    // its release may lie outside the span by now: then it names none, and
    // says so.
    if (std::exchange(regionSelecting, false) && selected != te::EditItemID() && !isSelected(selected))
    {
        selected = {};
        if (clipSelected) clipSelected(selected);
    }
    loopGesture = LoopGesture::none;
    automationGesture = AutomationGesture::none;
    automationRow = -1;
    automationPoint = -1;
    automationPoints.clear();
}

bool Arrangement::isSelected(te::EditItemID id) const
{
    for (const auto& clip : clips)
        if (clip.id == id)
            return timeSelection.touches(clip.position, clip.track);
    return false;
}

bool Arrangement::isWhollySelected(const ClipView& clip) const
{
    return timeSelection.holdsWhole(clip.position, clip.track);
}

// Selecting clips is what makes Delete a clip operation. Callers that select
// something else set the focus themselves, after this has cleared it. The
// clips are read off the timeline's own list, which every session call has
// rebuilt by the time it returns; an id not on it selects nothing.
void Arrangement::setSelection(std::vector<te::EditItemID> ids, te::EditItemID primary)
{
    const auto previous = selected;
    TimeSelection box;
    te::EditItemID first;
    auto primaryFound = false;
    for (const auto id : ids)
        for (const auto& clip : clips)
        {
            if (clip.id != id) continue;
            if (first == te::EditItemID()) first = id;
            primaryFound = primaryFound || id == primary;
            if (!box.active)
                box = {clip.position.start, clip.position.end, clip.track, clip.track, true};
            box.start = std::min(box.start, clip.position.start);
            box.end = std::max(box.end, clip.position.end);
            box.firstTrack = std::min(box.firstTrack, clip.track);
            box.lastTrack = std::max(box.lastTrack, clip.track);
            break;
        }
    clearTimeSelection();
    if (box.active)
        setTimeSelection(box.start, box.end, box.firstTrack, box.lastTrack);
    focus = box.active ? Focus::region : Focus::none;
    selected = primaryFound ? primary : first;
    if (selected != previous && clipSelected)
        clipSelected(selected);
}

// Selecting one card collapses a gathered selection back to it, so this runs
// even when the working track is already the one under the pointer.
void Arrangement::selectTrack(int track)
{
    track = juce::jlimit(0, session.masterTrackIndex(), track);
    const auto changed = selectedTrack != track;
    selectedTrack = track;
    selectedTracks = {track};
    trackSelectionAnchor = track;
    if (changed && trackSelected) trackSelected(track);
    repaint();
}

// Making a clip is the same gesture as opening one - a double-click on the
// lane, or the Ctrl+A that stands in for it - so the clip it just made is
// selected and reported open, and lands in the editor exactly as a
// double-click on a clip already there would have put it.
void Arrangement::openCreatedClip(te::EditItemID created)
{
    if (created == te::EditItemID()) return;
    setSelection({created}, created);
    if (clipOpened) clipOpened(created);
}

// Selecting a card is what asks for its track's devices. selectTrack reports
// only a move, so a second click on the card already selected would say
// nothing at all; this says it every time, which is what lets that click bring
// the Device View back after it has been closed.
void Arrangement::focusTrack()
{
    focus = Focus::track;
    if (trackFocused) trackFocused(selectedTrack);
}

// Ctrl+E. The line is the whole instruction: it says where to cut, and what to
// cut is whatever it is drawn through. Nothing has to be selected first, which
// is the point - a click in a lane puts the line down and clears the clip
// selection in the same gesture, so requiring a selection made the obvious way
// of aiming the cut the one way that could not perform it.
void Arrangement::splitAtInsertPoint()
{
    cancelDrag();
    syncIfStale();
    const auto seconds = insertPointTime();
    splitClipsAt(clipsUnderLine(seconds), seconds);
}

void Arrangement::splitClipsAt(const std::vector<te::EditItemID>& ids, double seconds)
{
    if (ids.empty())
    {
        if (status) status("Put the line inside a clip to split it");
        return;
    }
    // Read before the cut: the left half keeps the clip's id and its name, but
    // asking afterwards would be asking about a clip that is now half as long.
    auto* clip = session.findClip(ids.front());
    const auto name = clip != nullptr ? clip->getName() : juce::String();
    int cut = 0;
    const auto result = session.splitClips(ids, seconds, &cut);
    // Reported either way: the command usually arrives from the keyboard with
    // the clip halves too close together to see, so silence on success reads
    // as nothing having happened.
    if (!status) return;
    if (result.failed())
        status(result.getErrorMessage());
    else
        status(cut == 1 ? "Split " + name.quoted() + " at the line"
                        : "Split " + juce::String(cut) + " clips at the line");
}

// Reverse is a property of the clip, so it acts on the whole selection the
// way colour and delete do rather than on whichever clip the audio editor
// happens to have open. A mixed selection is settled the way Live settles a
// mixed switch: one clip still playing forwards turns the whole selection
// round, and only a selection that is already entirely backwards turns back.
void Arrangement::reverseSelected()
{
    cancelDrag();
    // MIDI clips are simply passed over. A selection swept across both kinds
    // is the normal case, and refusing the whole thing because one lane holds
    // notes would make the key useless exactly when it is quickest to reach.
    const auto chosen = clipsToActOn();
    std::vector<te::EditItemID> audio;
    for (const auto id : chosen)
        if (session.audioClipMix(id).valid)
            audio.push_back(id);
    if (audio.empty())
    {
        if (status) status("Select an audio clip to reverse");
        return;
    }
    bool allReversed = true;
    for (const auto id : audio)
        allReversed = allReversed && session.audioClipMix(id).reversed;
    // One undo step for the selection, not one per clip: turning four clips
    // round and pressing Ctrl+Z should put all four back.
    session.beginAudioClipGesture(allReversed ? "Play clips forwards" : "Reverse clips");
    for (const auto id : audio)
        session.setAudioClipReversed(id, !allReversed);
    session.endAudioClipGesture();
    // Said either way. A reversed clip looks the same in the lane at most
    // zoom levels, so silence on success reads as the key having missed.
    const auto subject = audio.size() == 1
        ? session.audioClipMix(audio.front()).name.quoted()
        : juce::String(static_cast<int>(audio.size())) + " clips";
    if (status) status(allReversed ? subject + ": playing forwards again" : "Reversed " + subject);
    repaint();
}

// Settled the way reverse is: one clip still playing mutes the selection, and
// only a selection that is all muted comes back on.
void Arrangement::muteSelected()
{
    cancelDrag();
    const auto chosen = clipsToActOn();
    if (chosen.empty())
    {
        if (status) status("Select a clip to mute");
        return;
    }
    auto allMuted = true;
    for (const auto id : chosen)
        if (auto* clip = session.findClip(id))
            allMuted = allMuted && clip->isMuted();
    const auto outcome = session.setClipsMuted(chosen, !allMuted);
    if (!status) return;
    const auto subject = chosen.size() == 1 ? juce::String("the clip")
                                            : juce::String(static_cast<int>(chosen.size())) + " clips";
    status(outcome.failed() ? outcome.getErrorMessage()
                            : allMuted ? "Unmuted " + subject : "Muted " + subject);
}

// Merging is a command about a run of clips rather than about one, so it acts
// on the whole selection the way reverse and delete do. The selection then
// moves onto what it made: the clips it was pointing at are gone, and leaving
// it naming them would make the next key press say "select a clip first".
void Arrangement::mergeSelected()
{
    cancelDrag();
    const auto chosen = clipsToActOn();
    Session::MergeResult result;
    const auto outcome = session.mergeClips(chosen, &result);
    if (outcome.failed())
    {
        if (status) status(outcome.getErrorMessage());
        return;
    }
    const auto made = static_cast<int>(result.clips.size());
    // The selection's region is read off the list, which the merge's own
    // announcement left stale if the timeline is hidden.
    syncIfStale();
    setSelection(result.clips);
    if (status)
    {
        // The count is the clips that went in, not the ones that were picked:
        // a clip the span reached is merged whether it was selected or not,
        // and saying so is how that stops being a surprise.
        auto message = "Merged " + juce::String(result.sourceCount) + " clips into "
            + (made == 1 ? juce::String("one clip") : juce::String(made) + " clips");
        if (result.lostClipEffects)
            message += ", without the clip effects they carried";
        status(message);
    }
    repaint();
}

// The selection and its material move by one step of the grid, or a bar, in
// one undo step: Live's "nudge a selection of material". The selection stays
// what it was, one step along, so holding the key keeps nudging.
void Arrangement::nudgeSelected(int direction, bool byBar)
{
    cancelDrag();
    if (!timeSelection.active || !timeSelection.isRange()) return;
    const auto region = timeSelection;
    const auto beats = byBar ? session.beatsPerBar() : resolvedGridBeats();
    const auto startBeat = session.secondsToBeats(region.start);
    const auto target = std::max(0.0, session.beatsToSeconds(startBeat + beats * (direction < 0 ? -1.0 : 1.0)));
    if (std::abs(target - region.start) < 1.0e-7) return;
    const auto result = session.moveClipRegion(region.start, region.end, region.firstTrack, region.lastTrack,
                                               target, region.firstTrack);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    setTimeSelection(target, target + region.length(), region.firstTrack, region.lastTrack);
    focus = Focus::region;
    repaint();
}

void Arrangement::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Switched off while the DJ view shows in its place, the timeline waits
    // and catches up when it is shown, as every panel the shell hides does:
    // otherwise a booth key, a bounce landing or an edit in the lower pane
    // cost a whole rebuild of a timeline nobody could see.
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    // A change can be announced in the middle of a drag. Keep the gesture
    // snapshot stable until the pointer is released.
    if (dragging || automationGesture != AutomationGesture::none)
    {
        repaint();
        return;
    }
    sync();
}

void Arrangement::visibilityChanged()
{
    if (staleWhileHidden && !isHiddenInShell(*this))
    {
        staleWhileHidden = false;
        sync();
    }
}

// Once per command rather than once per change, so the timeline still waits
// while hidden. An undo made in the DJ view left the split it took back on
// the list, and Ctrl+E found no clip under a line drawn through a whole one.
void Arrangement::syncIfStale()
{
    if (!staleWhileHidden) return;
    staleWhileHidden = false;
    sync();
}
// The row stack indexes tracks that are about to be replaced, so it goes with
// the clips rather than surviving into the new edit.
void Arrangement::editWillChange()
{
    cancelDrag();
    endRename(false);
    clips.clear();
    trackClipCaches.clear();
    clipNameWidths.clear();
    waveforms.clear();
    rows.clear();
    groups.clear();
    selectedTracks = {0};
    trackSelectionAnchor = 0;
    trackLanes.clear();
    trackRowIndex.clear();
    rowsHeight = 0.0f;
    focusedAutomation = {};
    setSelection({});
    clearTimeSelection();
    focus = Focus::none;
}
void Arrangement::editDidChange() { sync(); fit(); }

// Each meter repaints only itself, and only when a bar or a held peak has
// moved by a pixel, so a frame of a silent song reads the session and paints
// nothing.
void Arrangement::updateMeters()
{
    if (!isShowing())
    {
        metersFedAt = 0.0;
        return;
    }
    auto levels = session.takeOutputLevels();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    auto seconds = (now - metersFedAt) / 1000.0;
    // Back from behind the DJ view: the peaks read now piled up all the while
    // it showed, and the bars still hold what they held when it opened. Both
    // are thrown away and the bars dropped to nothing.
    if (metersFedAt <= 0.0)
    {
        std::fill(levels.begin(), levels.end(), Session::OutputLevel {});
        seconds = 3600.0;
    }
    metersFedAt = now;
    const auto count = std::min(meters.size(), levels.size() - 1);
    for (size_t track = 0; track < count; ++track)
        meters[track]->feed(levels[track].leftDb, levels[track].rightDb, seconds);
    masterMeter.feed(levels.back().leftDb, levels.back().rightDb, seconds);
}

void Arrangement::updatePlayhead()
{
    float next = -1.0f;
    // Keep the logical position current even while the component is not yet
    // attached to a peer.  Zoom and fit can be invoked before the next vblank.
    const auto x = xFor(session.playheadSeconds());
    if (x >= headerWidth && x < getWidth()) next = x;
    // Rolling, the line sweeps the lanes. Parked - which is every click that
    // moves the insert line - it shows only in the bar ruler.
    const auto sweeps = session.isPlaying();
    if (!isShowing())
    {
        playhead = next;
        playheadSweepsLanes = sweeps;
        return;
    }
    const auto area = getLocalBounds().withTrimmedTop(static_cast<int>(rulerTop))
                                      .withTrimmedBottom(18 + static_cast<int>(bottomInset));
    if (sweeps != playheadSweepsLanes)
    {
        // Starting and stopping change the line's length, and stopping on the
        // line it was already on changes nothing else, so the damage cannot be
        // left to movePlayhead - it repaints nothing when the position holds.
        playheadSweepsLanes = sweeps;
        const auto damage = playheadDamage(playhead, next, area);
        if (!damage.isEmpty()) repaint(damage);
    }
    movePlayhead(*this, playhead, next, area);
    // A MIDI take has no clip until the transport stops, so what is being
    // played is read from the engine's live note fifo and drawn where the clip
    // will be. Draining it is lock-free and costs nothing when idle.
    if (session.isRecording() && session.recordingStartSeconds() >= 0.0)
    {
        session.pollRecordingNotes();
        repaintRecordingBand();
    }
    else if (recordingPaintedTo >= 0.0f)
    {
        recordingPaintedTo = -1.0f;
        paintedNoteRevision = -1;
    }
}

void Arrangement::repaintRecordingBand()
{
    const auto right = xFor(session.playheadSeconds());
    const auto revision = session.recordingNotesRevision();
    // A note arrives stamped with the time the audio thread saw it, which can
    // be slightly behind the line being drawn, and a note still held grows
    // backwards from nothing. Either way the picture changes behind the
    // playhead as well as at it, so a note starting or ending repaints the
    // whole band. That happens at the rate a person plays; every other frame
    // repaints only what the band has grown by.
    const auto notesChanged = revision != paintedNoteRevision;
    const auto from = notesChanged || recordingPaintedTo < 0.0f
                          ? xFor(session.recordingStartSeconds())
                          : recordingPaintedTo;
    paintedNoteRevision = revision;
    recordingPaintedTo = right;
    if (right < from)
        return;
    const juce::Rectangle<float> span {from - 2.0f, 0.0f, right - from + 4.0f, 0.0f};
    for (int track = 0; track < session.trackCount(); ++track)
    {
        if (!isTrackArmed(track) || isTrackHidden(track))
            continue;
        const auto row = lane(track);
        if (row.getHeight() <= 0.0f)
            continue;
        const auto damage = span.withY(row.getY()).withHeight(row.getHeight())
                                .getIntersection({headerWidth, lanesTop,
                                                  static_cast<float>(getWidth()) - headerWidth - 14.0f,
                                                  laneContentHeight()});
        if (!damage.isEmpty())
            repaint(damage.getSmallestIntegerContainer());
    }
}

GridDivision Arrangement::resolvedGridDivision() const
{
    if (gridSettings.mode == GridMode::adaptive)
        return adaptiveGridDivision(lane(0).getWidth() / std::max(0.001, viewSpan) * (60.0 / session.tempo()), session.beatsPerBar(),
                                    gridSettings.adaptiveWidth);
    return gridSettings.fixedDivision;
}

// One place that turns the grid off and on, so the toggle, the menu item and
// the readouts cannot disagree about what "on" means.
void Arrangement::setGridEnabled(bool enabled)
{
    if (enabled)
        gridSettings.mode = gridModeWhenSnapping;
    else
    {
        if (gridSettings.mode != GridMode::off) gridModeWhenSnapping = gridSettings.mode;
        gridSettings.mode = GridMode::off;
    }
    snap.setToggleState(enabled, juce::dontSendNotification);
    updateGridControl();
    repaint();
}

void Arrangement::updateGridControl()
{
    // Adaptive resolves to whatever the zoom has landed on, so both faces read
    // the division actually in force rather than the word "Adaptive" - which
    // is the number a person is trying to find out when they look.
    const auto value = gridSettings.mode == GridMode::off
        ? juce::String("Off")
        : juce::String(gridDivisionLabel(resolvedGridDivision())) + (gridSettings.triplet ? "T" : "");
    gridControl.setButtonText(value + " v");
    toolbarGrid.setButtonText("Grid " + value + " v");
}

double Arrangement::resolvedGridBeats() const
{
    return gridDivisionBeats(resolvedGridDivision(), session.beatsPerBar())
        * (gridSettings.triplet ? 2.0 / 3.0 : 1.0);
}

double Arrangement::newClipBeats() const
{
    return gridSettings.mode == GridMode::off ? 0.0 : resolvedGridBeats();
}

// The arrangement's half of the route between the two views. The clip stays
// here; a copy of it lands in the first free cell of the same track's clips,
// which a DJ console playing the track shows.
void Arrangement::showClipMenu(te::EditItemID id)
{
    juce::PopupMenu menu;
    menu.addSectionHeader("CLIP");
    menu.addItem(1, "Copy to the track's DJ clips");
    menu.addSeparator();
    menu.addItem(4, "Cut       Ctrl+X");
    menu.addItem(5, "Copy       Ctrl+C");
    menu.addItem(6, "Paste       Ctrl+V", !clipboard.isEmpty());
    menu.addItem(2, "Duplicate       Ctrl+D");
    menu.addItem(7, "Split at Line       Ctrl+E");
    // Enabled from the selection rather than from the clip the menu was opened
    // on: merging needs two, and one clip can never be merged with itself.
    menu.addItem(9, "Merge       Ctrl+J", clipsToActOn().size() > 1);
    menu.addItem(13, "Bounce to New Track       Ctrl+B");
    // Ticked from the clip the menu was opened on, which is the one under the
    // pointer whether or not the rest of the selection agrees with it.
    menu.addItem(8, "Reverse       R", session.audioClipMix(id).valid, session.audioClipMix(id).reversed);
    if (auto* clip = session.findClip(id))
        menu.addItem(14, "Mute", true, clip->isMuted());
    // Warping is where a person looks for it in Live: on the clip, not only in
    // the editor below. The mode is here too, because choosing one is the next
    // thing anyone does after switching warping on.
    if (const auto warp = session.clipWarp(id); warp.valid)
    {
        menu.addSeparator();
        menu.addItem(10, "Warp", true, warp.followsTempo);
        juce::PopupMenu modes;
        for (int i = 0; i < Session::warpModeCount; ++i)
        {
            const auto mode = static_cast<Session::WarpMode>(i);
            juce::PopupMenu::Item item {Session::warpModeName(mode)};
            item.itemID = 20 + i;
            item.isTicked = warp.mode == mode;
            item.isEnabled = warp.followsTempo;
            item.shortcutKeyDescription = Session::warpModeBlurb(mode);
            modes.addItem(std::move(item));
        }
        menu.addSubMenu("Warp mode: " + Session::warpModeName(warp.mode), modes, warp.followsTempo);
        menu.addItem(11, "Half the clip tempo   :2", warp.clipBpm > 0.0);
        menu.addItem(12, "Double the clip tempo   *2", warp.clipBpm > 0.0);
    }
    menu.addSeparator();
    menu.addItem(3, "Delete");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withMousePosition(),
        [safe = juce::Component::SafePointer<Arrangement>(this), id](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 1)
            {
                const auto outcome = safe->session.copyClipToSlot(id);
                if (safe->status)
                    safe->status(outcome.failed() ? outcome.getErrorMessage()
                                                  : "Copied into the track's first free cell in the DJ view");
                return;
            }
            // Split is the one command here that is not about the selection,
            // and it has to run before the line moves: selecting a clip puts
            // the line on that clip's start, which is the one place inside it
            // that cannot be cut. So this acts on the clip the menu was opened
            // on, at the line where it already is, and selects nothing.
            if (result == 7)
            {
                safe->splitClipsAt({id}, safe->insertPointTime());
                return;
            }
            // Warping is about the clip the menu was opened on, the way
            // Reverse is about a selection - a warp is a property of one piece
            // of audio and one tempo, and applying it to whatever else happens
            // to be selected is not what the click asked for.
            if (result >= 10 && result <= 12)
            {
                const auto warp = safe->session.clipWarp(id);
                const auto outcome = result == 10 ? safe->session.setClipFollowsTempo(id, !warp.followsTempo)
                                   : result == 11 ? safe->session.scaleClipBpm(id, 0.5)
                                                  : safe->session.scaleClipBpm(id, 2.0);
                if (safe->status && outcome.failed()) safe->status(outcome.getErrorMessage());
                else if (safe->status && result == 10)
                    safe->status(warp.followsTempo ? "This clip plays at its own speed again"
                                                   : "This clip follows the song's tempo");
                return;
            }
            if (result >= 20 && result < 20 + Session::warpModeCount)
            {
                const auto mode = static_cast<Session::WarpMode>(result - 20);
                const auto outcome = safe->session.setClipWarpMode(id, mode);
                if (safe->status)
                    safe->status(outcome.failed() ? outcome.getErrorMessage()
                                                  : "Warp mode: " + Session::warpModeName(mode)
                                                        + " - " + Session::warpModeBlurb(mode));
                return;
            }
            // The commands act on a selection, so the clip the menu was opened
            // on becomes one before any of them runs - unless a dragged
            // region runs across it, which is the selection the menu was
            // opened inside, and selecting the clip would replace it.
            if (const auto chosen = safe->clipsToActOn(); std::find(chosen.begin(), chosen.end(), id) == chosen.end())
                safe->setSelection({id}, id);
            if (result == 2) safe->duplicateSelected();
            else if (result == 3) safe->deleteSelection();
            else if (result == 4) safe->cutSelection();
            else if (result == 5) safe->copySelection();
            else if (result == 6) safe->pasteSelection();
            else if (result == 8) safe->reverseSelected();
            else if (result == 9) safe->mergeSelected();
            else if (result == 13) safe->bounceSelected();
            else if (result == 14) safe->muteSelected();
        });
}

// Which kind of track this is decides what the lane accepts for the rest of
// the project, so the button asks rather than guessing. Ctrl+T is the shortcut
// for people who already know which one they want every time.
void Arrangement::showAddTrackMenu()
{
    const auto last = Session::lastAddedTrackType();
    juce::PopupMenu menu;
    menu.addSectionHeader("NEW TRACK");
    menu.addItem(1, "MIDI Track" + juce::String(last == Session::TrackType::midi ? "       Ctrl+T" : ""));
    menu.addItem(2, "Audio Track" + juce::String(last == Session::TrackType::audio ? "       Ctrl+T" : ""));
    // Not a kind Ctrl+T repeats: a return is added now and then, not in runs.
    menu.addItem(3, "Return Track       Ctrl+Alt+T");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrack),
        [safe = juce::Component::SafePointer<Arrangement>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            if (choice == 3)
                safe->insertReturnTrack();
            else
                safe->addTrackOfType(choice == 2 ? Session::TrackType::audio : Session::TrackType::midi);
        });
}

// Choosing here is also what Ctrl+T will do next time: the menu is the only
// place the kind is ever chosen deliberately, so it is the only place that
// records it. A track made by a drop below the last lane does not count.
void Arrangement::addTrackOfType(Session::TrackType type)
{
    Session::setLastAddedTrackType(type);
    insertTrack(type);
}

void Arrangement::insertReturnTrack()
{
    auto added = -1;
    const auto result = session.addReturnTrack(&added);
    if (result.wasOk())
        selectTrack(added);
    juce::String letter;
    for (const auto& made : session.returnTracks())
        if (made.track == added)
            letter = made.letter;
    if (status)
        status(result.failed() ? result.getErrorMessage()
                               : "Added return " + letter + ": turn up the " + letter
                                     + " knob on a track to send it audio");
}

void Arrangement::insertTrack(Session::TrackType type)
{
    auto added = -1;
    const auto result = session.addTrack(type, selectedTrack, &added);
    if (result.wasOk())
        selectTrack(added);
    if (status)
        status(result.failed() ? result.getErrorMessage() : "Added " + session.trackName(added));
}

void Arrangement::showGridMenu(juce::Component* target)
{
    juce::PopupMenu menu, adaptive, fixed;
    const std::array<std::pair<AdaptiveGridWidth, const char*>, 5> widths {{{AdaptiveGridWidth::widest, "Widest"},
        {AdaptiveGridWidth::wide, "Wide"}, {AdaptiveGridWidth::medium, "Medium"},
        {AdaptiveGridWidth::narrow, "Narrow"}, {AdaptiveGridWidth::narrowest, "Narrowest"}}};
    for (int i = 0; i < static_cast<int>(widths.size()); ++i)
        adaptive.addItem(100 + i, widths[static_cast<size_t>(i)].second, true,
                         gridSettings.mode == GridMode::adaptive && gridSettings.adaptiveWidth == widths[static_cast<size_t>(i)].first);
    for (int i = 0; i < static_cast<int>(fixedGridDivisionCount); ++i)
        fixed.addItem(200 + i, gridDivisionLabel(gridDivisions[static_cast<size_t>(i)]), true,
                      gridSettings.mode == GridMode::fixed && gridSettings.fixedDivision == gridDivisions[static_cast<size_t>(i)]);
    menu.addSubMenu("Adaptive", adaptive);
    menu.addSubMenu("Fixed", fixed);
    menu.addSeparator();
    menu.addItem(1, "Triplet Grid", true, gridSettings.triplet);
    menu.addItem(2, "Show/Snap Grid", true, gridSettings.mode != GridMode::off);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
        [safe = juce::Component::SafePointer<Arrangement>(this)] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result >= 100 && result < 105)
            {
                safe->gridSettings.mode = GridMode::adaptive;
                safe->gridSettings.adaptiveWidth = static_cast<AdaptiveGridWidth>(result - 100);
            }
            else if (result >= 200 && result < 200 + static_cast<int>(fixedGridDivisionCount))
            {
                safe->gridSettings.mode = GridMode::fixed;
                safe->gridSettings.fixedDivision = gridDivisions[static_cast<size_t>(result - 200)];
            }
            else if (result == 1) safe->gridSettings.triplet = !safe->gridSettings.triplet;
            else if (result == 2) { safe->setGridEnabled(safe->gridSettings.mode == GridMode::off); return; }
            // Choosing a division turns the grid back on; toggling triplet on a
            // grid that is off leaves it off.
            if (safe->gridSettings.mode != GridMode::off) safe->gridModeWhenSnapping = safe->gridSettings.mode;
            safe->snap.setToggleState(safe->gridSettings.mode != GridMode::off, juce::dontSendNotification);
            safe->resized();
            safe->repaint();
        });
}

}
