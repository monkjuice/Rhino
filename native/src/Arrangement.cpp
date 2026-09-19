#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

namespace rhino
{

Arrangement::Arrangement(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
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
    addTrack.setTooltip("Add track (Ctrl+T)");
    snap.setTooltip("Toggle clip snap");
    gridControl.setTooltip("Arrangement grid settings");
    automationButton.setTooltip("Automation edit mode: drag lanes instead of clips");
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    gridControl.onClick = [this] { showGridMenu(); };
    snap.onClick = [this]
    {
        gridSettings.mode = snap.getToggleState() ? GridMode::fixed : GridMode::off;
        repaint();
    };
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
    addTrack.onClick = [this]
    {
        const auto result = session.addAudioTrack();
        if (result.failed() && status) status(result.getErrorMessage());
    };
    snapSize.addItem("1/16", 1);
    snapSize.addItem("1/8", 2);
    snapSize.addItem("1/4", 3);
    snapSize.addItem("1 Bar", 4);
    snapSize.setSelectedId(1, juce::dontSendNotification);
    snapSize.onChange = [this]
    {
        gridSettings.mode = snap.getToggleState() ? GridMode::fixed : GridMode::off;
        switch (snapSize.getSelectedId())
        {
            case 2: gridSettings.fixedDivision = GridDivision::eighth; break;
            case 3: gridSettings.fixedDivision = GridDivision::quarter; break;
            case 4: gridSettings.fixedDivision = GridDivision::bar; break;
            default: gridSettings.fixedDivision = GridDivision::sixteenth; break;
        }
        repaint();
    };
    snapSize.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262c32));
    snapSize.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff46515a));
    for (auto* control : std::initializer_list<juce::Component*>{&duplicateButton, &addTrack, &snap, &automationButton, &gridControl, &scroll, &trackScrollBar})
        addAndMakeVisible(control);
    addAndMakeVisible(snapSize);
    laneHeaders.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(laneHeaders);
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
    masterVolume.setColour(juce::Slider::trackColourId, juce::Colour(0xff45d0d4));
    masterVolume.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff161c21));
    masterVolume.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff0e1317));
    masterVolume.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
    masterPan.setColour(juce::Slider::trackColourId, juce::Colour(0xffd4564e));
    masterPan.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff161c21));
    masterPan.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff0e1317));
    masterPan.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x33000000));
    masterVolume.addMouseListener(this, false);
    masterPan.addMouseListener(this, false);
    addAndMakeVisible(masterVolume);
    addAndMakeVisible(masterPan);
}

void Arrangement::resized()
{
    addTrack.setBounds(10, 3, 34, 26);
    duplicateButton.setBounds(48, 3, 34, 26);
    snap.setBounds(86, 3, 34, 26);
    automationButton.setBounds(124, 3, 34, 26);
    snapSize.setBounds(164, 3, 74, 26);
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
        // A row folded into a collapsed group is laid out at no height, so its
        // controls go with it rather than piling up under the band above.
        const auto visible = row.getHeight() > 1.0f && row.getBottom() > lanesTop && row.getY() < lanesBottom;
        mute[index]->setVisible(visible);
        solo[index]->setVisible(visible);
        volume[index]->setVisible(visible && mixerVisible);
        pan[index]->setVisible(visible && mixerVisible);
        mute[index]->setBounds(left, top, cardControlWidth, 18);
        solo[index]->setBounds(right, top, cardControlWidth, 18);
        volume[index]->setBounds(left, top + 23, cardControlWidth, 16);
        pan[index]->setBounds(right, top + 23, cardControlWidth, 16);
    }
    {
        // Everything the main row carries sits on its single line, and its two
        // faders are the same pair of widths the cards use.
        const auto controlsY = static_cast<int>(masterLane().getY()) + 5;
        masterVolume.setBounds(56, controlsY, 62, 16);
        masterPan.setBounds(122, controlsY, 62, 16);
    }
    scroll.setBounds(static_cast<int>(headerWidth), getHeight() - 14, getWidth() - static_cast<int>(headerWidth) - 14, 14);
    trackScrollBar.setBounds(getWidth() - 12, static_cast<int>(lanesTop), 12, getHeight() - static_cast<int>(lanesTop) - 18);
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
        if (focus == Focus::group && selectedGroup > 0) renameGroup(selectedGroup);
        else if (session.isMasterTrack(selectedTrack))
        {
            if (status) status("The main row keeps its name");
        }
        else renameTrack(selectedTrack);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
    {
        // Adds a clip to the focused track at the playhead, the keyboard
        // equivalent of double-clicking the lane.
        const auto start = snapped(std::max(0.0, playheadTime(session.edit->getTransport())), false);
        const auto result = session.createClip(selectedTrack, start);
        if (status) status(result.failed() ? result.getErrorMessage()
                                           : "Added a clip to " + session.trackName(selectedTrack));
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
        splitSelectedAtPlayhead();
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
    if (key.getKeyCode() == juce::KeyPress::escapeKey && dragging)
    {
        cancelDrag();
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
        // Delete on a band takes the band away and leaves every track it held,
        // because a group is a way of reading the stack rather than a thing the
        // tracks live inside.
        if (focus == Focus::group)
        {
            ungroupSelection();
            return true;
        }
        if (focus == Focus::track)
        {
            const auto name = session.trackName(selectedTrack);
            const auto result = session.removeAudioTrack(selectedTrack);
            if (status) status(result.wasOk() ? "Deleted " + name : result.getErrorMessage());
            return true;
        }
        if (focus == Focus::clip)
            deleteSelection();
        return true;
    }
    return false;
}

void Arrangement::cancelDrag()
{
    dragging = false;
    marqueeSelecting = false;
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
    focus = ids.empty() ? Focus::none : Focus::clip;
    selectedClips.clear();
    for (const auto id : ids)
        if (!isSelected(id)) selectedClips.push_back(id);
    selected = primary;
    if (selected == te::EditItemID() || !isSelected(selected))
        selected = selectedClips.empty() ? te::EditItemID() : selectedClips.front();
}

void Arrangement::copySelection()
{
    if (selectedClips.empty() && selected != te::EditItemID()) selectedClips = {selected};
    clipboard = selectedClips;
    if (status) status(clipboard.empty() ? "Select clips to copy" : "Copied " + juce::String(clipboard.size()) + " clip" + (clipboard.size() == 1 ? "" : "s"));
}

void Arrangement::pasteSelection()
{
    if (clipboard.empty())
    {
        if (status) status("Copy one or more clips first");
        return;
    }
    std::vector<te::EditItemID> pasted;
    const auto result = session.pasteClips(clipboard, std::max(0.0, pasteTime), selectedTrack, pasted);
    if (result.failed())
    {
        if (status) status(result.getErrorMessage());
        return;
    }
    setSelection(std::move(pasted));
    if (status) status("Pasted " + juce::String(selectedClips.size()) + " clip" + (selectedClips.size() == 1 ? "" : "s"));
}

void Arrangement::deleteSelection()
{
    if (selectedClips.empty() && selected != te::EditItemID()) selectedClips = {selected};
    for (const auto id : selectedClips) session.deleteClip(id);
    setSelection({});
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
    selectedGroup = -1;
    if (changed && trackSelected) trackSelected(track);
    repaint();
}

void Arrangement::splitSelectedAtPlayhead()
{
    cancelDrag();
    const auto result = session.splitClip(selected, playheadTime(session.edit->getTransport()));
    if (result.failed() && status) status(result.getErrorMessage());
}

void Arrangement::duplicateSelected()
{
    cancelDrag();
    if (selectedClips.size() > 1)
    {
        copySelection();
        pasteTime = std::max(pasteTime, [&]
        {
            double end = 0.0;
            for (const auto& clip : clips) if (isSelected(clip.id)) end = std::max(end, clip.position.end);
            return end;
        }());
        pasteSelection();
        return;
    }
    const auto result = session.duplicateClip(selected);
    if (result.failed() && status) status(result.getErrorMessage());
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
    clips.clear();
    waveforms.clear();
    rows.clear();
    groups.clear();
    selectedTracks = {0};
    trackSelectionAnchor = 0;
    selectedGroup = -1;
    trackLanes.clear();
    trackRowIndex.clear();
    rowsHeight = 0.0f;
    focusedAutomation = {};
    setSelection({});
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
    if (!isShowing())
    {
        playhead = next;
        return;
    }
    movePlayhead(*this, playhead, next,
                 getLocalBounds().withTrimmedTop(static_cast<int>(rulerTop)).withTrimmedBottom(18));
}

GridDivision Arrangement::resolvedGridDivision() const
{
    if (gridSettings.mode == GridMode::adaptive)
        return adaptiveGridDivision(lane(0).getWidth() / std::max(0.001, viewSpan) * (60.0 / session.tempo()), session.beatsPerBar(),
                                    gridSettings.adaptiveWidth);
    return gridSettings.fixedDivision;
}

void Arrangement::updateGridControl()
{
    gridControl.setButtonText(juce::String(gridDivisionLabel(resolvedGridDivision()))
                              + (gridSettings.triplet ? "T" : "") + " v");
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
    menu.addItem(2, "Duplicate");
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
            }
            else if (result == 2)
            {
                safe->selected = id;
                safe->duplicateSelected();
            }
            else if (result == 3)
            {
                safe->setSelection({id}, id);
                safe->deleteSelection();
            }
        });
}

void Arrangement::showGridMenu()
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(gridControl),
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
            else if (result == 2) safe->gridSettings.mode = safe->gridSettings.mode == GridMode::off ? GridMode::adaptive : GridMode::off;
            safe->snap.setToggleState(safe->gridSettings.mode != GridMode::off, juce::dontSendNotification);
            safe->resized();
            safe->repaint();
        });
}

}
