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
    fitButton.setButtonText(L"\u26f6");
    zoomOut.setButtonText(L"\u2212");
    zoomIn.setButtonText(L"+");
    splitButton.setButtonText(L"\u2702");
    duplicateButton.setButtonText(L"\u29c9");
    addTrack.setButtonText(L"+");
    removeTrack.setButtonText(L"\u2212");
    snap.setButtonText(L"\u2317");
    automationButton.setButtonText("A");
    fitButton.setTooltip("Fit arrangement");
    zoomOut.setTooltip("Zoom out");
    zoomIn.setTooltip("Zoom in");
    splitButton.setTooltip("Split selected clip");
    duplicateButton.setTooltip("Duplicate selected clip");
    addTrack.setTooltip("Add track");
    removeTrack.setTooltip("Remove selected track");
    snap.setTooltip("Toggle clip snap");
    automationButton.setTooltip("Draw automation for the last moved device knob");
    snap.setClickingTogglesState(true);
    snap.setToggleState(true, juce::dontSendNotification);
    automationButton.setClickingTogglesState(true);
    fitButton.onClick = [this] { fit(); };
    zoomIn.onClick = [this] { zoom(0.5, viewStart + viewSpan * 0.5); };
    zoomOut.onClick = [this] { zoom(2.0, viewStart + viewSpan * 0.5); };
    splitButton.onClick = [this] { splitSelectedAtPlayhead(); };
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
    removeTrack.onClick = [this]
    {
        const auto result = session.removeAudioTrack(selectedTrack);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    snapSize.addItem("1/16", 1);
    snapSize.addItem("1/8", 2);
    snapSize.addItem("1/4", 3);
    snapSize.addItem("1 Bar", 4);
    snapSize.setSelectedId(1, juce::dontSendNotification);
    snapSize.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff262c32));
    snapSize.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff46515a));
    for (auto* control : std::initializer_list<juce::Component*>{&fitButton, &zoomIn, &zoomOut, &splitButton, &duplicateButton, &addTrack, &removeTrack, &snap, &automationButton, &scroll, &trackScrollBar})
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
    fitButton.setBounds(152, 3, 32, 26);
    zoomOut.setBounds(190, 3, 32, 26);
    zoomIn.setBounds(226, 3, 32, 26);
    splitButton.setBounds(268, 3, 34, 26);
    duplicateButton.setBounds(308, 3, 34, 26);
    addTrack.setBounds(352, 3, 34, 26);
    removeTrack.setBounds(392, 3, 34, 26);
    snap.setBounds(436, 3, 34, 26);
    automationButton.setBounds(476, 3, 34, 26);
    snapSize.setBounds(516, 3, 74, 26);
    syncTrackControls();
    for (int i = 0; i < session.trackCount(); ++i)
    {
        const auto row = lane(i);
        const auto visible = row.getBottom() >= lanesTop && row.getY() <= getHeight() - 18.0f;
        mute[static_cast<size_t>(i)]->setVisible(visible);
        solo[static_cast<size_t>(i)]->setVisible(visible);
        mute[static_cast<size_t>(i)]->setBounds(12, static_cast<int>(row.getY()) + 38, 42, 26);
        solo[static_cast<size_t>(i)]->setBounds(62, static_cast<int>(row.getY()) + 38, 42, 26);
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
    removeTrack.setEnabled(selectedTrack > 0 && session.trackCount() > 2);
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
    const auto delta = (byBar ? 60.0 / session.tempo() * 4.0 : snapUnitSeconds()) * (direction < 0 ? -1.0 : 1.0);
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

}
