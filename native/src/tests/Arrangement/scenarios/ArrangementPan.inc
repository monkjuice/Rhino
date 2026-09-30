// Holding the middle button and moving the mouse carries the view. It is the
// one gesture in the timeline that is about the view rather than about what is
// under the pointer, so what matters is that it answers first - over a card, a
// clip or the ruler alike - and that it moves the view by exactly the distance
// the pointer travelled, in both axes.
{
    const auto panEvent = [&view](juce::Point<float> down, juce::Point<float> to, bool dragged)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), to,
            juce::ModifierKeys(juce::ModifierKeys::middleButtonModifier), 1.0f, 0, 0, 0, 0,
            &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
    };
    const auto pan = [&view, &panEvent](juce::Point<float> down, juce::Point<float> to)
    {
        view.mouseDown(panEvent(down, down, false));
        view.mouseDrag(panEvent(down, to, true));
        view.mouseUp(panEvent(down, to, true));
    };

    view.sync();
    view.fit();
    // Somewhere to pan to: fit() puts the view at zero, and a view already at
    // the start of the song cannot travel left.
    view.viewStart = 2.0;
    view.updateScroll();
    const auto startedAt = view.viewStart;
    const auto secondsPerPixel = view.viewSpan / view.lane(0).getWidth();
    const auto from = juce::Point<float>(view.xFor(view.viewStart + view.viewSpan * 0.5), view.lanesTop + 20.0f);

    pan(from, from.translated(-120.0f, 0.0f));
    require(close(view.viewStart, startedAt + 120.0 * secondsPerPixel),
            "Dragging left with the middle button moves the view right by the distance travelled");
    pan(from, from.translated(120.0f, 0.0f));
    require(close(view.viewStart, startedAt),
            "Dragging the same distance back returns the view to where it started");

    // A pan is not a selection, a clip drag or a region sweep, whatever it
    // started over. Pressing the middle button on a track card used to be the
    // start of carrying that card up the stack.
    const auto selectedBefore = view.selectedTrackIndex();
    const auto overACard = juce::Point<float>(20.0f, view.lane(1).getCentreY());
    pan(overACard, overACard.translated(-40.0f, 0.0f));
    require(view.selectedTrackIndex() == selectedBefore, "Panning from a track card does not select or move it");
    require(view.movingTrack == -1 && view.resizingTrack == -1, "Panning from a card starts no card gesture");
    require(!view.regionSelecting && !view.dragging, "Panning starts no region sweep and no clip drag");
    require(!view.panning, "The pan ends when the button comes up");
    view.viewStart = startedAt;
    view.updateScroll();

    // The vertical half. It only has somewhere to go when the stack is taller
    // than the lanes, so the rows are made tall enough to scroll first.
    for (int track = 0; track < session.trackCount(); ++track)
        session.setTrackLaneHeight(track, 200.0f);
    view.sync();
    view.resized();
    require(view.rowsHeight > view.laneContentHeight() + 1.0f, "The row stack is taller than the lanes");
    view.trackScroll = 60.0;
    view.updateScroll();
    const auto scrolledTo = view.trackScroll;
    pan(from, from.translated(0.0f, -40.0f));
    require(close(view.trackScroll, scrolledTo + 40.0), "Dragging up scrolls the rows down by the same pixels");
    pan(from, from.translated(0.0f, 4000.0f));
    require(close(view.trackScroll, 0.0), "A pan past the top of the stack stops there");
    for (int track = 0; track < session.trackCount(); ++track)
        session.setTrackLaneHeight(track, 0.0f);
    view.sync();
    view.resized();
}
