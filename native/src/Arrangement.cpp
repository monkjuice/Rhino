#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

namespace rhino
{

// One frame clock for both: a drag that has reached the edge pulls the view
// after it on the same tick the playhead is advanced on.
Arrangement::Arrangement(Session& s)
    : session(s), vblank(this, [this] { autoScrollDrag(); updatePlayhead(); })
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
    automationButton.setButtonText("A");
    duplicateButton.setTooltip("Duplicate selected clip");
    addTrack.setTooltip("Add a track: pick audio or MIDI (Ctrl+T repeats the last kind)");
    snap.setTooltip("Toggle clip snap");
    gridControl.setTooltip("Grid spacing: how far apart the lines are, and what a clip snaps to");
    toolbarGrid.setTooltip("Grid spacing: how far apart the lines are, and what a clip snaps to");
    automationButton.setTooltip("Automation edit mode: drag lanes instead of clips");
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    gridControl.onClick = [this] { showGridMenu(&gridControl); };
    toolbarGrid.onClick = [this] { showGridMenu(&toolbarGrid); };
    snap.onClick = [this] { setGridEnabled(snap.getToggleState()); };
    automationButton.setClickingTogglesState(true);
    duplicateButton.onClick = [this] { duplicateSelected(); };
    automationButton.onClick = [this]
    {
        if (status)
            status(automationButton.getToggleState()
                ? "Automation edit: drags in a track lane move automation, not clips"
                : "Clip edit: right-click a device knob to show its automation");
        repaint();
    };
    addTrack.onClick = [this] { showAddTrackMenu(); };
    for (auto* control : std::initializer_list<juce::Component*>{&duplicateButton, &addTrack, &snap, &automationButton, &gridControl, &toolbarGrid, &scroll, &trackScrollBar})
        addAndMakeVisible(control);
    laneHeaders.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(laneHeaders);
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
    masterVolume.setTooltip("Main output volume");
    masterVolume.onDragStart = [this] { session.beginMasterVolumeGesture(); };
    masterVolume.onDragEnd = [this] { session.endMasterVolumeGesture(); };
    masterVolume.onValueChange = [this] { session.setMasterVolumeDb(static_cast<float>(masterVolume.getValue())); };
    masterPan.setSliderStyle(juce::Slider::LinearBar);
    masterPan.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    masterPan.setRange(-1.0, 1.0, 0.01);
    masterPan.setDoubleClickReturnValue(true, 0.0);
    masterPan.setTooltip("Main output pan");
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
    masterVolume.addMouseListener(this, false);
    masterPan.addMouseListener(this, false);
    addAndMakeVisible(masterVolume);
    addAndMakeVisible(masterPan);
}

void Arrangement::resized()
{
    addTrack.setBounds(10, 5, 34, 26);
    duplicateButton.setBounds(48, 5, 34, 26);
    snap.setBounds(86, 5, 34, 26);
    automationButton.setBounds(124, 5, 34, 26);
    toolbarGrid.setBounds(164, 5, 92, 26);
    // Above the main row rather than inside it.
    gridControl.setBounds(getWidth() - 104, static_cast<int>(masterLane().getY()) - 26, 76, 20);
    updateGridControl();
    syncTrackControls();
    // Lane height follows the component height, so the row stack has to reflow
    // before anything is positioned against it. A track added since the last
    // sync needs the full rebuild; a plain resize only needs the geometry.
    if (static_cast<int>(trackRowIndex.size()) != session.trackCount())
        buildRows();
    else
        layoutRows();
    const auto lanesBottom = masterLane().getY();
    laneHeaders.setBounds(0, static_cast<int>(lanesTop), static_cast<int>(headerWidth),
                          std::max(1, static_cast<int>(lanesBottom - lanesTop)));
    for (int i = 0; i < session.trackCount() && i < static_cast<int>(mute.size()); ++i)
    {
        const auto row = lane(i);
        const auto index = static_cast<size_t>(i);
        // Mute and solo sit on the name's line; the two faders share the line
        // under them at matching widths. A row dragged to its shortest gives up
        // the faders and keeps the buttons. Positions are inside the header
        // container, which crops whatever leaves the lanes.
        const auto top = static_cast<int>(row.getY() - lanesTop) + cardControlsTop;
        // A card inside a group is pushed right, controls and all, so the step
        // in the left edge is unbroken down the whole group.
        const auto left = cardControlLeft + static_cast<int>(trackIndent(i));
        const auto right = left + cardControlWidth + cardControlGap;
        const auto mixerVisible = row.getHeight() >= mixerLaneHeight;
        // Only a MIDI track has an input to choose, and the line only appears
        // once the row is tall enough to hold it -- the same rule the faders
        // already follow one line up.
        const auto inputVisible = row.getHeight() >= inputLaneHeight
            && session.trackRecordInput(i) == Session::RecordInput::midi;
        // A row folded into a collapsed group is laid out at no height, so its
        // controls go with it rather than piling up under the band above.
        const auto visible = row.getHeight() > 1.0f && row.getBottom() > lanesTop && row.getY() < lanesBottom;
        mute[index]->setVisible(visible);
        solo[index]->setVisible(visible);
        // A group bus records nothing of its own, so it is given no dot to
        // press rather than one that refuses.
        const auto armable = session.trackRecordInput(i) != Session::RecordInput::none;
        arm[index]->setVisible(visible && armable);
        volume[index]->setVisible(visible && mixerVisible);
        pan[index]->setVisible(visible && mixerVisible);
        midiInput[index]->setVisible(visible && inputVisible);
        const auto buttonStep = cardButtonWidth + cardControlGap;
        mute[index]->setBounds(left, top, cardButtonWidth, 18);
        solo[index]->setBounds(left + buttonStep, top, cardButtonWidth, 18);
        arm[index]->setBounds(left + 2 * buttonStep, top, cardButtonWidth, 18);
        volume[index]->setBounds(left, top + 23, cardControlWidth, 16);
        pan[index]->setBounds(right, top + 23, cardControlWidth, 16);
        // The chooser spans both fader columns, because a device name needs
        // the room and there is nothing to sit beside it.
        midiInput[index]->setBounds(left, top + 42, cardControlWidth * 2 + cardControlGap, 16);
    }
    {
        // Everything the main row carries sits on its single line, and its two
        // faders are the same pair of widths the cards use.
        const auto controlsY = static_cast<int>(masterLane().getY()) + 5;
        masterVolume.setBounds(56, controlsY, 62, 16);
        masterPan.setBounds(122, controlsY, 62, 16);
    }
    layoutNameEditor();
    // Both scrollbars stop at the top of whatever covers the foot of the panel,
    // so the pane floating over the arrangement never buries them.
    const auto foot = getHeight() - static_cast<int>(bottomInset);
    scroll.setBounds(static_cast<int>(headerWidth), foot - static_cast<int>(scrollBarHeight),
                     getWidth() - static_cast<int>(headerWidth) - 14, static_cast<int>(scrollBarHeight));
    trackScrollBar.setBounds(getWidth() - 12, static_cast<int>(lanesTop), 12,
                             std::max(1, foot - static_cast<int>(lanesTop) - 18));
    updateScroll();
    updatePlayhead();
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
    viewSpan = std::clamp(viewSpan * factor, 0.25, std::max(60.0, songEnd * 2.0));
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
        // equivalent of double-clicking the lane.
        const auto start = snapped(std::max(0.0, playheadTime(session.edit->getTransport())), false);
        te::EditItemID created;
        const auto result = session.createClip(selectedTrack, start, &created);
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
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
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
            const auto name = session.trackName(selectedTrack);
            const auto result = session.removeAudioTrack(selectedTrack);
            if (status) status(result.wasOk() ? "Deleted " + name : result.getErrorMessage());
            return true;
        }
        if (focus == Focus::clip || focus == Focus::region)
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
    regionSelecting = false;
    loopGesture = LoopGesture::none;
    automationGesture = AutomationGesture::none;
    automationRow = -1;
    automationPoint = -1;
    automationPoints.clear();
}

bool Arrangement::isSelected(te::EditItemID id) const
{
    return std::find(selectedClips.begin(), selectedClips.end(), id) != selectedClips.end();
}

// Selecting clips is what makes Delete a clip operation. Callers that select
// something else set the focus themselves, after this has cleared it.
void Arrangement::setSelection(std::vector<te::EditItemID> ids, te::EditItemID primary)
{
    const auto previous = selected;
    focus = ids.empty() ? Focus::none : Focus::clip;
    selectedClips.clear();
    for (const auto id : ids)
        if (!isSelected(id)) selectedClips.push_back(id);
    selected = primary;
    if (selected == te::EditItemID() || !isSelected(selected))
        selected = selectedClips.empty() ? te::EditItemID() : selectedClips.front();
    // Selecting clips is also a time selection - that is what makes Ctrl+D on
    // a clip and Ctrl+D on its span the same command - unless the region is
    // what changed the selection in the first place.
    if (!syncingSelection)
        setRegionFromSelectedClips();
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
    auto chosen = selectedClips;
    if (chosen.empty() && selected != te::EditItemID()) chosen = {selected};
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

// Merging is a command about a run of clips rather than about one, so it acts
// on the whole selection the way reverse and delete do. The selection then
// moves onto what it made: the clips it was pointing at are gone, and leaving
// it naming them would make the next key press say "select a clip first".
void Arrangement::mergeSelected()
{
    cancelDrag();
    auto chosen = selectedClips;
    if (chosen.empty() && selected != te::EditItemID()) chosen = {selected};
    Session::MergeResult result;
    const auto outcome = session.mergeClips(chosen, &result);
    if (outcome.failed())
    {
        if (status) status(outcome.getErrorMessage());
        return;
    }
    const auto made = static_cast<int>(result.clips.size());
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

void Arrangement::nudgeSelected(int direction, bool byBar)
{
    cancelDrag();
    auto* clip = session.findClip(selected);
    if (!clip) return;
    const auto old = clip->getPosition();
    const auto beats = byBar ? session.beatsPerBar() : resolvedGridBeats();
    const auto startBeat = session.edit->tempoSequence.toBeats(old.time.getStart()).inBeats();
    const auto target = session.edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(startBeat + beats * (direction < 0 ? -1.0 : 1.0)));
    const auto delta = target.inSeconds() - old.time.getStart().inSeconds();
    const auto length = old.time.getLength().inSeconds();
    const auto start = std::max(0.0, old.time.getStart().inSeconds() + delta);
    const auto result = session.editClip(selected, {start, start + length, old.offset.inSeconds()}, ClipGesture::move);
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Playback automation can publish parameter changes every block. Keep the
    // gesture snapshot stable until the pointer is released.
    if (dragging || automationGesture != AutomationGesture::none)
    {
        repaint();
        return;
    }
    sync();
}
// The row stack indexes tracks that are about to be replaced, so it goes with
// the clips rather than surviving into the new edit.
void Arrangement::editWillChange()
{
    cancelDrag();
    endRename(false);
    clips.clear();
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

void Arrangement::updatePlayhead()
{
    float next = -1.0f;
    // Keep the logical position current even while the component is not yet
    // attached to a peer.  Zoom and fit can be invoked before the next vblank.
    const auto x = xFor(playheadTime(session.edit->getTransport()));
    if (x >= headerWidth && x < getWidth()) next = x;
    // Rolling, the line sweeps the lanes. Parked - which is every click that
    // moves the insert line - it shows only in the bar ruler.
    const auto sweeps = session.edit->getTransport().isPlaying();
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
    const auto right = xFor(playheadTime(session.edit->getTransport()));
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

// The arrangement's half of the route between the two views. The clip stays
// here; a copy of it lands in a session slot on the same track.
void Arrangement::showClipMenu(te::EditItemID id)
{
    juce::PopupMenu menu;
    menu.addSectionHeader("CLIP");
    menu.addItem(1, "Copy to session slot");
    menu.addSeparator();
    menu.addItem(4, "Cut       Ctrl+X");
    menu.addItem(5, "Copy       Ctrl+C");
    menu.addItem(6, "Paste       Ctrl+V", !clipboard.isEmpty());
    menu.addItem(2, "Duplicate       Ctrl+D");
    menu.addItem(7, "Split at Line       Ctrl+E");
    // Enabled from the selection rather than from the clip the menu was opened
    // on: merging needs two, and one clip can never be merged with itself.
    menu.addItem(9, "Merge       Ctrl+J", selectedClips.size() > 1);
    // Ticked from the clip the menu was opened on, which is the one under the
    // pointer whether or not the rest of the selection agrees with it.
    menu.addItem(8, "Reverse       R", session.audioClipMix(id).valid, session.audioClipMix(id).reversed);
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
                                                  : "Copied the clip into a session slot");
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
            // on becomes one before any of them runs.
            if (!safe->isSelected(id))
                safe->setSelection({id}, id);
            if (result == 2) safe->duplicateSelected();
            else if (result == 3) safe->deleteSelection();
            else if (result == 4) safe->cutSelection();
            else if (result == 5) safe->copySelection();
            else if (result == 6) safe->pasteSelection();
            else if (result == 8) safe->reverseSelected();
            else if (result == 9) safe->mergeSelected();
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrack),
        [safe = juce::Component::SafePointer<Arrangement>(this)](int choice)
        {
            if (safe == nullptr || choice == 0) return;
            safe->addTrackOfType(choice == 2 ? Session::TrackType::audio : Session::TrackType::midi);
        });
}

// Choosing here is also what Ctrl+T will do next time: the menu is the only
// place the kind is ever chosen deliberately, so it is the only place that
// records it. A track made by a drop below the last lane does not count.
void Arrangement::addTrackOfType(Session::TrackType type)
{
    Session::setLastAddedTrackType(type);
    const auto result = session.addTrack(type);
    if (status)
        status(result.failed() ? result.getErrorMessage()
                               : "Added " + session.trackName(session.trackCount() - 1));
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
    for (int i = 0; i < static_cast<int>(gridDivisions.size()); ++i)
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
            else if (result >= 200 && result < 210)
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
