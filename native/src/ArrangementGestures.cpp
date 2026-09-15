#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

// Pointer gestures: loop range, clip move/trim, and automation drawing.

namespace theta
{

void Arrangement::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.mods.isRightButtonDown() && loopGestureAt(event.position) != LoopGesture::none)
    {
        session.clearManualLoopRange();
        if (status) status("Loop range cleared");
        repaint();
        return;
    }
    if (event.mods.isRightButtonDown() && event.position.x >= headerWidth && event.position.y >= rulerTop)
    {
        // A right-click on a clip acts on that clip; empty lane space still
        // opens the grid menu.
        if (const auto index = hit(event.position); index >= 0)
        {
            showClipMenu(clips[static_cast<size_t>(index)].id);
            return;
        }
        showGridMenu();
        return;
    }
    if (!event.mods.isLeftButtonDown()) return;
    pasteTime = snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown());
    for (int track = 0; track < session.trackCount(); ++track)
        if (lane(track).withX(0.0f).contains(event.position))
        {
            selectTrack(track);
            break;
        }
    if (event.y >= rulerTop && event.y < lanesTop && event.x >= headerWidth)
    {
        const auto loopRange = session.edit->getTransport().getLoopRange();
        loopOriginalStart = loopRange.getStart().inSeconds();
        loopOriginalEnd = loopRange.getEnd().inSeconds();
        loopPreviewStart = loopOriginalStart;
        loopPreviewEnd = loopOriginalEnd;
        loopAnchor = timeAt(event.position.x);
        loopGesture = loopGestureAt(event.position);
        if (loopGesture == LoopGesture::none)
        {
            loopGesture = LoopGesture::create;
            loopAnchor = snapped(std::max(0.0, loopAnchor), event.mods.isAltDown());
            loopPreviewStart = loopPreviewEnd = loopAnchor;
        }
        repaint();
        return;
    }
    // Double-clicking empty lane space creates a clip that starts where the
    // pointer is, which is the only way to add one now that instrument drops
    // change the track instead.
    if (event.getNumberOfClicks() == 2 && event.position.x >= headerWidth && event.position.y >= lanesTop
        && hit(event.position) < 0)
    {
        const auto track = trackAt(event.position.y);
        if (track >= 0)
        {
            selectTrack(track);
            const auto start = snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown());
            const auto result = session.createClip(track, start);
            if (status) status(result.failed() ? result.getErrorMessage() : "Added a clip to " + session.trackName(track));
        }
        return;
    }
    if (juce::KeyPress::isKeyCurrentlyDown('S') && event.position.x >= headerWidth && event.position.y >= lanesTop)
    {
        marqueeSelecting = true;
        marqueeAnchor = event.position;
        marqueeBounds = {event.position.x, event.position.y, 0.0f, 0.0f};
        repaint();
        return;
    }
    const auto index = hit(event.position);
    if (index < 0) { setSelection({}); repaint(); return; }
    const auto& clip = clips[static_cast<size_t>(index)];
    if (event.mods.isShiftDown())
    {
        auto next = selectedClips;
        if (isSelected(clip.id))
            next.erase(std::remove(next.begin(), next.end(), clip.id), next.end());
        else
            next.push_back(clip.id);
        setSelection(std::move(next), clip.id);
    }
    else if (!isSelected(clip.id))
        setSelection({clip.id}, clip.id);
    else
        selected = clip.id;
    selectTrack(clip.track);
    if (clip.waveform == nullptr)
    {
        const auto result = session.selectPatternClip(selected);
        if (result.failed() && status) status(result.getErrorMessage());
    }
    repaint();
    if (automationButton.getToggleState())
    {
        automationTarget = session.lastTouchedDeviceParameter();
        if (!automationTarget.isValid())
        {
            if (status) status("Move a device knob first, then draw automation.");
            return;
        }
        const auto parameters = session.deviceParameters(automationTarget.track, automationTarget.slot);
        if (!juce::isPositiveAndBelow(automationTarget.parameter, parameters.size()))
        {
            if (status) status("The last moved knob is no longer available.");
            return;
        }
        activeAutomationClip = clip.id;
        activeAutomationTarget = automationTarget;
        automationDragging = true;
        automationStartTime = automationEndTime = snapped(std::clamp(timeAt(event.position.x), clip.position.start, clip.position.end), event.mods.isAltDown());
        automationStartValue = automationEndValue = automationValueForY(clip, event.position.y, automationTarget);
        repaint(bounds(clip).getSmallestIntegerContainer());
        return;
    }
    original = preview = clip.position;
    originalTrack = previewTrack = clip.track;
    sourceDuration = clip.sourceDuration;
    const auto box = bounds(clip);
    const auto handleWidth = std::min(7.0f, box.getWidth() * 0.25f);
    gesture = event.position.x - box.getX() < handleWidth ? ClipGesture::trimLeft
        : box.getRight() - event.position.x < handleWidth ? ClipGesture::trimRight : ClipGesture::move;
    dragTime = timeAt(event.position.x);
    dragging = true;
}

void Arrangement::mouseDrag(const juce::MouseEvent& event)
{
    if (marqueeSelecting)
    {
        marqueeBounds = {std::min(marqueeAnchor.x, event.position.x), std::min(marqueeAnchor.y, event.position.y),
                         std::abs(event.position.x - marqueeAnchor.x), std::abs(event.position.y - marqueeAnchor.y)};
        repaint();
        return;
    }
    if (automationDragging)
    {
        for (const auto& clip : clips)
            if (clip.id == selected)
            {
                automationEndTime = snapped(std::clamp(timeAt(event.position.x), clip.position.start, clip.position.end), event.mods.isAltDown());
                automationEndValue = automationValueForY(clip, event.position.y, automationTarget);
                repaint(bounds(clip).getSmallestIntegerContainer());
                return;
            }
    }
    if (loopGesture != LoopGesture::none)
    {
        constexpr auto minimumLoopSeconds = 0.02;
        const auto t = std::max(0.0, timeAt(event.position.x));
        if (loopGesture == LoopGesture::create)
        {
            const auto edge = snapped(t, event.mods.isAltDown());
            loopPreviewStart = std::min(loopAnchor, edge);
            loopPreviewEnd = std::max(loopAnchor, edge);
        }
        else if (loopGesture == LoopGesture::move)
        {
            const auto length = loopOriginalEnd - loopOriginalStart;
            auto start = snapped(loopOriginalStart + t - loopAnchor, event.mods.isAltDown());
            start = std::max(0.0, start);
            loopPreviewStart = start;
            loopPreviewEnd = start + length;
        }
        else if (loopGesture == LoopGesture::trimStart)
        {
            loopPreviewStart = std::min(snapped(t, event.mods.isAltDown()), loopOriginalEnd - minimumLoopSeconds);
            loopPreviewStart = std::max(0.0, loopPreviewStart);
            loopPreviewEnd = loopOriginalEnd;
        }
        else if (loopGesture == LoopGesture::trimEnd)
        {
            loopPreviewStart = loopOriginalStart;
            loopPreviewEnd = std::max(snapped(t, event.mods.isAltDown()), loopOriginalStart + minimumLoopSeconds);
        }
        repaint();
        return;
    }
    if (!dragging) return;
    const auto anchor = gesture == ClipGesture::trimRight ? original.end : original.start;
    auto targetTrack = previewTrack;
    if (gesture == ClipGesture::move)
    {
        if (const auto target = trackAt(event.position.y); target >= 0)
            targetTrack = target;
        else if (session.trackCount() > 0 && event.position.y > lane(session.trackCount() - 1).getBottom())
            targetTrack = session.trackCount();
        else
            targetTrack = originalTrack;
        auto lowestSelectedTrack = originalTrack;
        for (const auto& selectedClip : clips)
            if (isSelected(selectedClip.id)) lowestSelectedTrack = std::min(lowestSelectedTrack, selectedClip.track);
        targetTrack = std::max(targetTrack, originalTrack - lowestSelectedTrack);
    }
    const auto rawStart = anchor + timeAt(event.position.x) - dragTime;
    const auto editTime = gesture == ClipGesture::move
        ? snappedClipMoveStart(rawStart, original.end - original.start, targetTrack, event.mods.isAltDown())
        : snapped(rawStart, event.mods.isAltDown());
    preview = previewClipEdit(original, gesture, editTime, sourceDuration);
    previewTrack = targetTrack;
    repaint();
}

void Arrangement::mouseUp(const juce::MouseEvent& event)
{
    if (marqueeSelecting)
    {
        mouseDrag(event);
        std::vector<te::EditItemID> hits;
        for (const auto& clip : clips)
            if (bounds(clip).intersects(marqueeBounds)) hits.push_back(clip.id);
        marqueeSelecting = false;
        setSelection(std::move(hits));
        repaint();
        return;
    }
    if (automationDragging)
    {
        mouseDrag(event);
        automationDragging = false;
        const auto result = session.setClipAutomationRamp(selected, automationTarget, automationStartTime, automationEndTime,
                                                          automationStartValue, automationEndValue);
        if (status)
        {
            const auto parameters = session.deviceParameters(automationTarget.track, automationTarget.slot);
            const auto name = juce::isPositiveAndBelow(automationTarget.parameter, parameters.size())
                ? parameters[static_cast<size_t>(automationTarget.parameter)].name
                : juce::String("parameter");
            status(result.wasOk() ? "Clip automation: " + name : result.getErrorMessage());
        }
        if (result.wasOk())
        {
            activeAutomationClip = selected;
            activeAutomationTarget = automationTarget;
            // Draw mode is one-shot so the next drag returns to the normal
            // clip move/trim gesture without requiring an extra toggle.
            automationButton.setToggleState(false, juce::dontSendNotification);
        }
        repaint();
        return;
    }
    if (loopGesture != LoopGesture::none)
    {
        mouseDrag(event);
        const auto completedGesture = loopGesture;
        loopGesture = LoopGesture::none;
        if (event.getDistanceFromDragStart() >= 3)
        {
            const auto result = session.setLoopRange(loopPreviewStart, loopPreviewEnd);
            if (result.failed() && status) status(result.getErrorMessage());
            else if (status) status(completedGesture == LoopGesture::create ? "Loop range selected" : "Loop range updated");
        }
        else
        {
            session.edit->getTransport().setPosition(tracktion::core::TimePosition::fromSeconds(std::max(0.0, timeAt(event.position.x))));
            updatePlayhead();
        }
        repaint();
        return;
    }
    if (!dragging) return;
    if (event.getDistanceFromDragStart() >= 3)
    {
        mouseDrag(event);
        dragging = false;
        if (gesture == ClipGesture::move && selectedClips.size() > 1)
        {
            struct Move { te::EditItemID id; ClipGeometry position; int track; };
            std::vector<Move> moves;
            for (const auto& clip : clips)
                if (isSelected(clip.id)) moves.push_back({clip.id, clip.position, clip.track});
            std::sort(moves.begin(), moves.end(), [this] (const auto& a, const auto& b)
            {
                return a.track + previewTrack - originalTrack < b.track + previewTrack - originalTrack;
            });
            const auto timeDelta = preview.start - original.start;
            const auto trackDelta = previewTrack - originalTrack;
            juce::Result result = juce::Result::ok();
            for (const auto& move : moves)
            {
                const auto length = move.position.end - move.position.start;
                result = session.editClip(move.id, {std::max(0.0, move.position.start + timeDelta),
                                                    std::max(0.0, move.position.start + timeDelta) + length,
                                                    move.position.offset}, ClipGesture::move, move.track + trackDelta);
                if (result.failed()) break;
            }
            if (result.failed() && status) status(result.getErrorMessage());
            else selectTrack(juce::jlimit(0, std::max(0, session.trackCount() - 1), previewTrack));
        }
        else
        {
            const auto result = session.editClip(selected, preview, gesture, gesture == ClipGesture::move ? previewTrack : -1);
            if (result.failed() && status) status(result.getErrorMessage());
            else if (gesture == ClipGesture::move)
                selectTrack(juce::jlimit(0, std::max(0, session.trackCount() - 1), previewTrack));
        }
    }
    cancelDrag();
    repaint();
}

void Arrangement::mouseMove(const juce::MouseEvent& event)
{
    const auto index = hit(event.position);
    auto pointerStyle = juce::MouseCursor::NormalCursor;
    const auto loopHit = loopGestureAt(event.position);
    if (loopHit == LoopGesture::trimStart || loopHit == LoopGesture::trimEnd)
        pointerStyle = juce::MouseCursor::LeftRightResizeCursor;
    else if (loopHit == LoopGesture::move)
        pointerStyle = juce::MouseCursor::DraggingHandCursor;
    else if (event.y >= rulerTop && event.y < lanesTop && event.x >= headerWidth)
        pointerStyle = juce::MouseCursor::CrosshairCursor;
    else if (index >= 0)
    {
        if (automationButton.getToggleState())
            pointerStyle = juce::MouseCursor::CrosshairCursor;
        else if (automationLaneAt(clips[static_cast<size_t>(index)], event.position) >= 0)
            pointerStyle = juce::MouseCursor::DraggingHandCursor;
        else
        {
            const auto box = bounds(clips[static_cast<size_t>(index)]);
            const auto handle = std::min(7.0f, box.getWidth() * 0.25f);
            pointerStyle = event.position.x - box.getX() < handle || box.getRight() - event.position.x < handle
                ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
        }
    }
    setMouseCursor(pointerStyle);
}

void Arrangement::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (dragging) return;
    if (event.mods.isShiftDown())
    {
        const auto wheelDelta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        zoom(std::exp(-wheelDelta * 2.0f), timeAt(event.position.x));
    }
    else if (std::abs(wheel.deltaY) > std::abs(wheel.deltaX)
             && static_cast<float>(session.trackCount()) * laneHeight() > laneContentHeight() + 1.0f)
    {
        trackScroll += -wheel.deltaY * laneHeight() * 1.5;
        updateScroll();
        resized();
        repaint();
    }
    else
    {
        viewStart -= (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * viewSpan * 0.3;
        updateScroll();
        updatePlayhead();
        repaint();
    }
}

}
