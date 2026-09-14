#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

namespace theta
{

Arrangement::Arrangement(Session& s) : session(s), vblank(this, [this] { updatePlayhead(); })
{
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
    addTrack.setTooltip("Add track");
    snap.setTooltip("Toggle clip snap");
    gridControl.setTooltip("Arrangement grid settings");
    automationButton.setTooltip("Draw automation for the last moved device knob");
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
        if (automationButton.getToggleState() && status)
            status(session.lastTouchedDeviceParameter().isValid()
                ? "Automation draw: drag across a clip to write the last moved knob"
                : "Move a device knob first, then draw automation");
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
    sync();
}

Arrangement::~Arrangement()
{
    session.removeChangeListener(this);
    session.listeners.remove(this);
    scroll.removeListener(this);
    trackScrollBar.removeListener(this);
}

void Arrangement::resized()
{
    addTrack.setBounds(10, 3, 34, 26);
    duplicateButton.setBounds(48, 3, 34, 26);
    snap.setBounds(86, 3, 34, 26);
    automationButton.setBounds(124, 3, 34, 26);
    snapSize.setBounds(164, 3, 74, 26);
    gridControl.setBounds(getWidth() - 104, getHeight() - 34, 76, 20);
    updateGridControl();
    syncTrackControls();
    const auto mixerVisible = showTrackMixer();
    for (int i = 0; i < session.trackCount(); ++i)
    {
        const auto row = lane(i);
        const auto index = static_cast<size_t>(i);
        const auto visible = row.getBottom() >= lanesTop && row.getY() <= getHeight() - 18.0f;
        // One control row pinned to the bottom of the lane, so short lanes keep
        // the track name legible rather than overlapping it.
        const auto controlsY = static_cast<int>(row.getBottom()) - 26;
        mute[index]->setVisible(visible);
        solo[index]->setVisible(visible);
        volume[index]->setVisible(visible && mixerVisible);
        pan[index]->setVisible(visible && mixerVisible);
        mute[index]->setBounds(10, controlsY, 26, 22);
        solo[index]->setBounds(40, controlsY, 26, 22);
        volume[index]->setBounds(72, controlsY, 76, 22);
        pan[index]->setBounds(152, controlsY, 38, 22);
    }
    scroll.setBounds(static_cast<int>(headerWidth), getHeight() - 14, getWidth() - static_cast<int>(headerWidth) - 14, 14);
    trackScrollBar.setBounds(getWidth() - 12, static_cast<int>(lanesTop), 12, getHeight() - static_cast<int>(lanesTop) - 18);
    updateScroll();
    updatePlayhead();
}

// Live shows the mixer in the arrangement too, but only where it fits. Below
// this lane height the fader and pan would collide with the track name.
bool Arrangement::showTrackMixer() const
{
    return laneHeight() >= 56.0f;
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
        if (activeAutomationClip == selected && activeAutomationTarget.isValid())
        {
            const auto result = session.deleteClipAutomation(selected, activeAutomationTarget);
            if (result.wasOk())
            {
                activeAutomationClip = {};
                activeAutomationTarget = {};
            }
            if (status) status(result.wasOk() ? "Automation lane deleted" : result.getErrorMessage());
            return true;
        }
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
}

bool Arrangement::isSelected(te::EditItemID id) const
{
    return std::find(selectedClips.begin(), selectedClips.end(), id) != selectedClips.end();
}

void Arrangement::setSelection(std::vector<te::EditItemID> ids, te::EditItemID primary)
{
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

void Arrangement::selectTrack(int track)
{
    track = juce::jlimit(0, std::max(0, session.trackCount() - 1), track);
    if (selectedTrack == track) return;
    selectedTrack = track;
    if (trackSelected) trackSelected(track);
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
    if (dragging || automationDragging)
    {
        repaint();
        return;
    }
    sync();
}
void Arrangement::editWillChange() { cancelDrag(); clips.clear(); waveforms.clear(); setSelection({}); }
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
