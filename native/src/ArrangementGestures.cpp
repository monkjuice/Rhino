#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>
#include <utility>

// Pointer gestures: loop range and clip move/trim. Automation gestures live in
// ArrangementAutomation.cpp and are offered the pointer first.

namespace theta
{

void Arrangement::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    // A press starts a fresh gesture. One that never saw its release must not
    // go on reading this drag as a card being resized or carried.
    resizingTrack = -1;
    movingTrack = -1;
    moveDestination = -1;
    moveStarted = false;
    if (event.mods.isRightButtonDown() && loopGestureAt(event.position) != LoopGesture::none)
    {
        session.clearManualLoopRange();
        if (status) status("Loop range cleared");
        repaint();
        return;
    }
    // An automation row answers for its own lane wherever it is clicked,
    // header included, because its header is the lane's only label.
    if (event.mods.isRightButtonDown() && event.position.y >= lanesTop)
        if (const auto row = rowAt(event.position.y); row >= 0 && rows[static_cast<size_t>(row)].automation >= 0)
            if (const auto* automation = automationFor(rows[static_cast<size_t>(row)]))
            {
                setSelection({});
                focusedAutomation = automation->target;
                focus = Focus::automation;
                showAutomationMenu(automation->target);
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
    // A right-click on a card offers what belongs to the track itself, so it is
    // answered before the grid menu that empty space opens.
    if (event.mods.isRightButtonDown())
        if (const auto track = cardAt(event.position); track >= 0)
        {
            selectTrack(track);
            setSelection({});
            focus = Focus::track;
            repaint();
            showTrackMenu(track);
            return;
        }
    if (!event.mods.isLeftButtonDown()) return;
    // The master row selects but takes no clips, so it is handled before the
    // lane hit tests rather than inside them.
    if (masterLane().contains(event.position))
    {
        selectTrack(session.masterTrackIndex());
        setSelection({});
        focus = Focus::track;
        repaint();
        return;
    }
    pasteTime = snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown());
    for (int track = 0; track < session.trackCount(); ++track)
        if (lane(track).withX(0.0f).contains(event.position))
        {
            selectTrack(track);
            break;
        }
    // A card is the only place a track can be resized or carried from, and it
    // has already taken the selection a plain click on it would have made.
    if (beginCardGesture(event))
    {
        setSelection({});
        focus = Focus::track;
        repaint();
        return;
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
    // Automation takes the pointer before the clips do: its points are small
    // targets, and a lane row carries no clips of its own to compete with.
    const auto pointerRow = rowAt(event.position.y);
    if (beginAutomationGesture(event))
    {
        repaint();
        return;
    }
    if (pointerRow >= 0 && rows[static_cast<size_t>(pointerRow)].automation >= 0)
    {
        selectTrack(rows[static_cast<size_t>(pointerRow)].track);
        setSelection({});
        if (const auto* automation = automationFor(rows[static_cast<size_t>(pointerRow)]))
        {
            focusedAutomation = automation->target;
            focus = Focus::automation;
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
    if (index < 0)
    {
        setSelection({});
        // The header selects the track itself. Empty lane space selects
        // nothing, so Delete has nothing to reach for.
        focus = event.position.x < headerWidth ? Focus::track : Focus::none;
        repaint();
        return;
    }
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
    {
        selected = clip.id;
        focus = Focus::clip;
    }
    selectTrack(clip.track);
    if (clip.waveform == nullptr)
    {
        const auto result = session.selectPatternClip(selected);
        if (result.failed() && status) status(result.getErrorMessage());
    }
    repaint();
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
    if (resizingTrack >= 0 || movingTrack >= 0)
    {
        dragCardGesture(event);
        return;
    }
    if (marqueeSelecting)
    {
        marqueeBounds = {std::min(marqueeAnchor.x, event.position.x), std::min(marqueeAnchor.y, event.position.y),
                         std::abs(event.position.x - marqueeAnchor.x), std::abs(event.position.y - marqueeAnchor.y)};
        repaint();
        return;
    }
    if (automationGesture != AutomationGesture::none)
    {
        dragAutomationGesture(event);
        return;
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
    if (resizingTrack >= 0 || movingTrack >= 0)
    {
        endCardGesture();
        return;
    }
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
    if (automationGesture != AutomationGesture::none)
    {
        dragAutomationGesture(event);
        endAutomationGesture(event);
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
    if (cardResizeEdgeAt(event.position) >= 0)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (cardAt(event.position) >= 0)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    const auto index = hit(event.position);
    auto pointerStyle = juce::MouseCursor::NormalCursor;
    const auto loopHit = loopGestureAt(event.position);
    if (loopHit == LoopGesture::trimStart || loopHit == LoopGesture::trimEnd)
        pointerStyle = juce::MouseCursor::LeftRightResizeCursor;
    else if (loopHit == LoopGesture::move)
        pointerStyle = juce::MouseCursor::DraggingHandCursor;
    else if (event.y >= rulerTop && event.y < lanesTop && event.x >= headerWidth)
        pointerStyle = juce::MouseCursor::CrosshairCursor;
    else if (automationHitAt(event.position).valid())
        pointerStyle = juce::MouseCursor::UpDownResizeCursor;
    else if (index >= 0)
    {
        const auto box = bounds(clips[static_cast<size_t>(index)]);
        const auto handle = std::min(7.0f, box.getWidth() * 0.25f);
        pointerStyle = event.position.x - box.getX() < handle || box.getRight() - event.position.x < handle
            ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
    }
    setMouseCursor(pointerStyle);
}

namespace
{
// The colour grid the track menu shows. A menu item outlives the call that
// opened it, so it holds the session by reference the way the view does.
struct TrackSwatches final : public juce::PopupMenu::CustomComponent
{
    static constexpr int columns = 8, cell = 18;

    TrackSwatches(Session& s, int t, juce::Colour current) : session(s), track(t), selected(current)
    {
        setSize(columns * cell + 12, rowCount() * cell + 12);
    }

    static int rowCount()
    {
        return (static_cast<int>(Session::trackColourPalette().size()) + columns - 1) / columns;
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = columns * cell + 12;
        idealHeight = rowCount() * cell + 12;
    }

    juce::Rectangle<int> swatchBounds(int index) const
    {
        return {6 + index % columns * cell, 6 + index / columns * cell, cell - 2, cell - 2};
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = Session::trackColourPalette();
        for (int i = 0; i < static_cast<int>(palette.size()); ++i)
        {
            const auto box = swatchBounds(i);
            g.setColour(palette[static_cast<size_t>(i)]);
            g.fillRect(box);
            const auto isSelected = palette[static_cast<size_t>(i)] == selected;
            g.setColour(juce::Colour(isSelected ? 0xffe8eef2 : 0xff161b20));
            g.drawRect(box, isSelected ? 2 : 1);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto& palette = Session::trackColourPalette();
        for (int i = 0; i < static_cast<int>(palette.size()); ++i)
            if (swatchBounds(i).contains(event.getPosition()))
            {
                session.setTrackColour(track, palette[static_cast<size_t>(i)]);
                break;
            }
        triggerMenuItem();
    }

    Session& session;
    int track;
    juce::Colour selected;
};
}

// A card bottom edge is a resize handle; the rest of the card carries the
// track. Only a track's own row answers, because an automation lane is sized
// by what it draws rather than by the user.
int Arrangement::cardResizeEdgeAt(juce::Point<float> point) const
{
    if (point.x >= headerWidth || point.y < lanesTop || point.y >= masterLane().getY())
        return -1;
    constexpr auto grab = 4.0f;
    for (int index = 0; index < static_cast<int>(rows.size()); ++index)
    {
        if (rows[static_cast<size_t>(index)].automation >= 0) continue;
        const auto row = rowBounds(index);
        if (std::abs(point.y - row.getBottom()) <= grab)
            return rows[static_cast<size_t>(index)].track;
    }
    return -1;
}

int Arrangement::cardAt(juce::Point<float> point) const
{
    if (point.x >= headerWidth || point.y < lanesTop || point.y >= masterLane().getY())
        return -1;
    const auto row = rowAt(point.y);
    if (row < 0 || rows[static_cast<size_t>(row)].automation >= 0)
        return -1;
    return rows[static_cast<size_t>(row)].track;
}

bool Arrangement::beginCardGesture(const juce::MouseEvent& event)
{
    if (const auto track = cardResizeEdgeAt(event.position); track >= 0)
    {
        // Read the height before the track is marked as resizing, because from
        // that moment laneHeightFor answers with the preview instead.
        resizeStartHeight = laneHeightFor(track);
        resizePreview = resizeStartHeight;
        resizeAnchor = event.position.y;
        resizingTrack = track;
        return true;
    }
    if (const auto track = cardAt(event.position); track >= 0)
    {
        movingTrack = track;
        moveDestination = track;
        moveAnchor = event.position.y;
        moveStarted = false;
        return true;
    }
    return false;
}

void Arrangement::dragCardGesture(const juce::MouseEvent& event)
{
    if (resizingTrack >= 0)
    {
        // Dragging down grows the row and pushes the stack below it along;
        // dragging up gives height back until the card is down to its name and
        // its two buttons, which is as small as a track goes.
        resizePreview = juce::jlimit(minimumLaneHeight, maximumLaneHeight,
                                     resizeStartHeight + event.position.y - resizeAnchor);
        layoutRows();
        resized();
        repaint();
        return;
    }
    if (movingTrack < 0) return;
    if (!moveStarted && std::abs(event.position.y - moveAnchor) < 5.0f) return;
    moveStarted = true;
    // The pointer picks the destination, not the dragged card: the card would
    // answer differently depending on where along it the drag started, while
    // the row under the pointer is what the drop indicator is drawn on.
    auto destination = session.trackCount() - 1;
    for (int track = 0; track < session.trackCount(); ++track)
        if (event.position.y < lane(track).getBottom())
        {
            destination = track;
            break;
        }
    if (destination != moveDestination)
    {
        moveDestination = destination;
        repaint();
    }
}

void Arrangement::endCardGesture()
{
    if (resizingTrack >= 0)
    {
        const auto track = std::exchange(resizingTrack, -1);
        if (const auto done = session.setTrackLaneHeight(track, resizePreview); done.failed() && status)
            status(done.getErrorMessage());
    }
    else if (movingTrack >= 0 && moveStarted && moveDestination != movingTrack)
    {
        const auto from = std::exchange(movingTrack, -1);
        const auto to = moveDestination;
        if (const auto done = session.moveTrack(from, to); done.failed())
        {
            if (status) status(done.getErrorMessage());
        }
        else
        {
            selectTrack(to);
            if (status) status("Moved " + session.trackName(to) + " to position " + juce::String(to + 1));
        }
    }
    movingTrack = -1;
    moveDestination = -1;
    moveStarted = false;
    buildRows();
    resized();
    repaint();
}

// The palette is one grid rather than a list, so a colour is picked by where it
// sits, the way it is in the DAWs this borrows from.
void Arrangement::showTrackMenu(int track)
{
    juce::PopupMenu menu;
    menu.addSectionHeader(session.trackName(track));
    menu.addCustomItem(1, std::make_unique<TrackSwatches>(session, track, session.trackColour(track)), nullptr);
    menu.addItem(2, "No colour", !session.trackColour(track).isTransparent());
    menu.addSeparator();
    menu.addItem(3, "Rename...");
    const auto anchor = localPointToGlobal(lane(track).getTopLeft().toInt());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea({anchor.x, anchor.y, 1, 1}),
                       [this, track](int choice)
                       {
                           if (choice == 2) session.setTrackColour(track, {});
                           else if (choice == 3) renameTrack(track);
                       });
}

// The name is asked for where the rest of the app asks for text, rather than
// turned into an editor on the card: a card can be inches tall, and the name
// column is the narrowest thing on it.
void Arrangement::renameTrack(int track)
{
    auto* window = new juce::AlertWindow("Rename track", "New name for " + session.trackName(track) + ":",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", session.trackName(track), {});
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, track, window](int result)
    {
        const auto name = window->getTextEditorContents("name");
        delete window;
        if (result != 1) return;
        if (const auto done = session.setTrackName(track, name); done.failed() && status)
            status(done.getErrorMessage());
        else if (status)
            status("Renamed track " + juce::String(track + 1) + " to " + name.trim());
    }), false);
}

// What the Info View says while the pointer rests on a header control. The
// controls are the same objects the session view drives, so the text names the
// track rather than leaving the reader to work out which card it came from.
juce::String Arrangement::controlDescription(juce::Component* component) const
{
    if (component == &masterVolume)
        return "Main volume - the level of everything the arrangement plays. Drag to set it, double-click for 0.0 dB.";
    if (component == &masterPan)
        return "Main pan - where the whole mix sits between the speakers. Drag to move it, double-click to centre it.";
    for (int track = 0; track < static_cast<int>(mute.size()); ++track)
    {
        const auto index = static_cast<size_t>(track);
        const auto name = session.trackName(track);
        if (component == mute[index].get())
            return "Mute " + name + " - silences this track while the rest keeps playing.";
        if (component == solo[index].get())
            return "Solo " + name + " - silences every track that is not soloed.";
        if (component == volume[index].get())
            return "Volume of " + name + " - drag to set the level, double-click for 0.0 dB.";
        if (component == pan[index].get())
            return "Pan of " + name + " - drag to place it between the speakers, double-click to centre it.";
    }
    return {};
}

void Arrangement::mouseEnter(const juce::MouseEvent& event)
{
    if (status)
        if (const auto text = controlDescription(event.eventComponent); text.isNotEmpty())
            status(text);
}

void Arrangement::mouseExit(const juce::MouseEvent&)
{
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
             && rowsHeight > laneContentHeight() + 1.0f)
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
