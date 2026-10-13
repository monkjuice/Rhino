#include "ArrangementInternal.h"
#include <algorithm>

// Track automation in the arrangement: the row stack, where a curve sits in a
// row, what the pointer is over, and the gestures that edit it. The drawing is
// next door in ArrangementAutomationPaint.cpp.
//
// Automation runs the length of the timeline, not the length of a clip. Every
// track shows the lanes the user has revealed on it; a lane sent to its own row
// gets a dimmed clone of the track stacked underneath, so a busy track can be
// read one parameter at a time.

namespace rhino
{
namespace
{
constexpr float pointGrabRadius = 7.0f;
// How close to the line counts as on it. Deliberately small: the snap is there
// so a node meant for the curve lands exactly on it, not so that the lane
// resists being drawn on a few pixels away.
constexpr float curveSnapDistance = 5.0f;
}

// Rows are rebuilt whenever the edit changes, because revealing a lane changes
// what is below it. Their pixel geometry is a separate step: lane height
// follows the component height, so a resize must reflow the stack without
// re-reading the edit.
void Arrangement::buildRows()
{
    // The hover addresses a lane by its index in the stack being thrown away
    // here, so it cannot survive the rebuild. The next pointer move answers
    // against the new stack.
    automationHover = {};
    rows.clear();
    groups = session.trackGroups();
    trackRowIndex.assign(static_cast<size_t>(std::max(0, session.trackCount())), -1);
    trackLanes.assign(static_cast<size_t>(std::max(0, session.trackCount())), {});
    for (int track = 0; track < session.trackCount(); ++track)
    {
        trackLanes[static_cast<size_t>(track)] = session.trackAutomations(track);
        trackRowIndex[static_cast<size_t>(track)] = static_cast<int>(rows.size());
        rows.push_back({track, -1, 0.0f, 0.0f});
        const auto& lanes = trackLanes[static_cast<size_t>(track)];
        for (int i = 0; i < static_cast<int>(lanes.size()); ++i)
            if (lanes[static_cast<size_t>(i)].ownLane)
                rows.push_back({track, i, 0.0f, 0.0f});
    }
    layoutRows();
}

// A row inside a collapsed group is laid out at no height rather than left out.
// Everything that reaches a track through the stack - its lane, its clips, its
// header controls - then answers with an empty rectangle and draws nothing,
// which is one rule instead of a hidden case in each of them.
void Arrangement::layoutRows()
{
    auto top = 0.0f;
    for (auto& row : rows)
    {
        row.top = top;
        row.height = isTrackHidden(row.track) ? 0.0f
            : row.automation < 0 ? laneHeightFor(row.track) : automationRowHeight;
        top += row.height;
    }
    rowsHeight = top;
}

juce::Rectangle<float> Arrangement::rowBounds(int row) const
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(rows.size())))
        return {};
    const auto& entry = rows[static_cast<size_t>(row)];
    return {headerWidth, lanesTop + entry.top - static_cast<float>(trackScroll),
            std::max(1.0f, getWidth() - headerWidth - 14.0f), entry.height};
}

// Deliberately unclamped: a clip can be dragged onto a lane that is scrolled
// out of view, so this answers for the whole stack. Callers that need the row
// to be on screen, such as the curve hit test, check that themselves.
int Arrangement::rowAt(float y) const
{
    if (y < lanesTop)
        return -1;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
    {
        const auto bounds = rowBounds(row);
        if (y >= bounds.getY() && y < bounds.getBottom())
            return row;
    }
    return -1;
}

const Session::TrackAutomation* Arrangement::automationFor(const LaneRow& row) const
{
    if (row.automation < 0 || !juce::isPositiveAndBelow(row.track, static_cast<int>(trackLanes.size())))
        return nullptr;
    const auto& lanes = trackLanes[static_cast<size_t>(row.track)];
    if (!juce::isPositiveAndBelow(row.automation, static_cast<int>(lanes.size())))
        return nullptr;
    return &lanes[static_cast<size_t>(row.automation)];
}

// A track's own row shares its height with the clips drawn under it, so the
// curve keeps a small margin rather than running into the clip name bar.
juce::Rectangle<float> Arrangement::automationArea(int row) const
{
    const auto bounds = rowBounds(row);
    if (bounds.isEmpty())
        return {};
    if (rows[static_cast<size_t>(row)].automation < 0)
        return bounds.reduced(0.0f, 8.0f).withTrimmedTop(10.0f);
    return bounds.reduced(0.0f, 7.0f);
}

float Arrangement::automationYFor(int row, const Session::TrackAutomation& automation, float value) const
{
    const auto area = automationArea(row);
    const auto minimum = automation.maximum > automation.minimum ? automation.minimum : 0.0f;
    const auto maximum = automation.maximum > automation.minimum ? automation.maximum : 1.0f;
    const auto amount = std::clamp((value - minimum) / std::max(0.0001f, maximum - minimum), 0.0f, 1.0f);
    return area.getBottom() - amount * area.getHeight();
}

float Arrangement::automationValueForY(int row, const Session::TrackAutomation& automation, float y) const
{
    const auto area = automationArea(row);
    const auto minimum = automation.maximum > automation.minimum ? automation.minimum : 0.0f;
    const auto maximum = automation.maximum > automation.minimum ? automation.maximum : 1.0f;
    const auto amount = 1.0f - std::clamp((y - area.getY()) / std::max(1.0f, area.getHeight()), 0.0f, 1.0f);
    return minimum + (maximum - minimum) * amount;
}

// Lifting a never-drawn lane turns its dotted resting line into a real, flat
// curve. Two points is the minimum that counts as drawn.
std::vector<Session::AutomationPoint> Arrangement::defaultAutomationPoints(const Session::TrackAutomation& automation) const
{
    const auto end = std::max({songEnd, viewStart + viewSpan, 1.0});
    return {{0.0, automation.restingValue}, {end, automation.restingValue}};
}

Arrangement::AutomationHover Arrangement::automationHoverAt(juce::Point<float> point, bool bypassSnap) const
{
    AutomationHover hover;
    if (point.x < headerWidth || point.x > static_cast<float>(getWidth()) - 14.0f
        || point.y < lanesTop || point.y >= lanesTop + laneContentHeight())
        return hover;
    const auto row = rowAt(point.y);
    if (row < 0)
        return hover;
    const auto& entry = rows[static_cast<size_t>(row)];
    // A track's own row belongs to its clips: automation there is read-only
    // until the A button puts the arrangement in automation mode. A lane row
    // holds nothing else, so it is always editable.
    if (entry.automation < 0 && !automationButton.getToggleState())
        return hover;
    if (automationArea(row).isEmpty() || !juce::isPositiveAndBelow(entry.track, static_cast<int>(trackLanes.size())))
        return hover;

    // A lane row shows exactly one curve; a track row shows every curve that
    // was not sent away, tested newest first so the topmost drawn wins.
    const auto& lanes = trackLanes[static_cast<size_t>(entry.track)];
    std::vector<int> candidates;
    if (entry.automation >= 0)
        candidates.push_back(entry.automation);
    else
        for (int i = static_cast<int>(lanes.size()); --i >= 0;)
            if (!lanes[static_cast<size_t>(i)].ownLane)
                candidates.push_back(i);
    if (candidates.empty())
        return hover;

    hover.row = row;
    hover.pointer = point;

    // A node under the pointer wins over the line it sits on: it is the
    // smaller target, and it is the one being reached for.
    for (const auto index : candidates)
    {
        const auto& automation = lanes[static_cast<size_t>(index)];
        for (int i = 0; i < static_cast<int>(automation.points.size()); ++i)
        {
            const auto& stored = automation.points[static_cast<size_t>(i)];
            const juce::Point<float> handle {xFor(stored.timeSeconds), automationYFor(row, automation, stored.value)};
            if (std::abs(handle.x - point.x) <= pointGrabRadius && std::abs(handle.y - point.y) <= pointGrabRadius)
            {
                hover.automation = index;
                hover.point = i;
                hover.onCurve = true;
                hover.timeSeconds = stored.timeSeconds;
                hover.value = stored.value;
                return hover;
            }
        }
    }

    // Otherwise the pointer belongs to the nearest curve in the row, wherever
    // in the lane it is: a lane is a value editor, so every position in one
    // says something about one of the curves drawn on it.
    const auto pointerTime = std::max(0.0, timeAt(point.x));
    auto nearest = -1;
    auto nearestDistance = 0.0f;
    for (const auto index : candidates)
    {
        const auto& automation = lanes[static_cast<size_t>(index)];
        const auto distance = std::abs(automationYFor(row, automation, automation.valueAt(pointerTime)) - point.y);
        if (nearest < 0 || distance < nearestDistance)
        {
            nearest = index;
            nearestDistance = distance;
        }
    }

    const auto& automation = lanes[static_cast<size_t>(nearest)];
    // The same snap a dragged node takes, so where the ghost is drawn is where
    // the node lands rather than somewhere near it.
    hover.automation = nearest;
    hover.timeSeconds = std::max(0.0, snapped(pointerTime, bypassSnap));
    const auto onLine = automation.valueAt(hover.timeSeconds);
    hover.onCurve = std::abs(automationYFor(row, automation, onLine) - point.y) <= curveSnapDistance;
    hover.value = hover.onCurve ? onLine : automationValueForY(row, automation, point.y);
    return hover;
}

void Arrangement::clearAutomationHover()
{
    updateAutomationHover({-1.0f, -1.0f}, false);
}

// Repaints only what the change touched. A move over a lane fires at pointer
// rate, and repainting the whole arrangement for a four-pixel ghost would put
// the timeline's frame cost on the mouse.
void Arrangement::updateAutomationHover(juce::Point<float> point, bool bypassSnap)
{
    // A gesture speaks for the pointer while it runs: the drag keeps the
    // reading pinned to the node it is carrying.
    if (automationGesture != AutomationGesture::none)
        return;
    const auto next = automationHoverAt(point, bypassSnap);
    const auto unchanged = next.valid() == automationHover.valid()
        && next.row == automationHover.row && next.automation == automationHover.automation
        && next.point == automationHover.point && next.onCurve == automationHover.onCurve
        && std::abs(next.timeSeconds - automationHover.timeSeconds) < 1.0e-9
        && std::abs(next.value - automationHover.value) < 1.0e-6f
        && next.pointer.getDistanceFrom(automationHover.pointer) < 0.5f;
    if (unchanged)
        return;
    const auto damage = [this] (const AutomationHover& hover)
    {
        if (!hover.valid())
            return juce::Rectangle<int>();
        // The reading floats above the pointer, which puts it over the row
        // above whenever the pointer is near the top of its own.
        return rowBounds(hover.row).getSmallestIntegerContainer()
            .getUnion({static_cast<int>(hover.pointer.x) - 200, static_cast<int>(hover.pointer.y) - 48, 400, 96});
    };
    const auto before = damage(automationHover);
    automationHover = next;
    const auto after = damage(automationHover);
    if (!before.isEmpty())
        repaint(before);
    if (!after.isEmpty())
        repaint(after);
}

float Arrangement::automationNeighbourValue(const std::vector<Session::AutomationPoint>& points,
                                            int index, double seconds) const
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(points.size())))
        return 0.0f;
    const auto hasPrevious = index > 0;
    const auto hasNext = index + 1 < static_cast<int>(points.size());
    if (hasPrevious && hasNext)
    {
        const auto& previous = points[static_cast<size_t>(index - 1)];
        const auto& next = points[static_cast<size_t>(index + 1)];
        const auto span = next.timeSeconds - previous.timeSeconds;
        if (span <= 0.0)
            return next.value;
        const auto amount = static_cast<float>(std::clamp((seconds - previous.timeSeconds) / span, 0.0, 1.0));
        return previous.value + (next.value - previous.value) * amount;
    }
    // An end node has one neighbour, and the curve runs flat past it, so that
    // neighbour's value is the line the snap pulls it back onto.
    if (hasPrevious)
        return points[static_cast<size_t>(index - 1)].value;
    if (hasNext)
        return points[static_cast<size_t>(index + 1)].value;
    return points[static_cast<size_t>(index)].value;
}

int Arrangement::insertAutomationPoint(double seconds, float value)
{
    auto index = 0;
    while (index < static_cast<int>(automationPoints.size())
           && automationPoints[static_cast<size_t>(index)].timeSeconds < seconds)
        ++index;
    constexpr auto sameTime = 1.0e-6;
    if (index > 0 && seconds - automationPoints[static_cast<size_t>(index - 1)].timeSeconds < sameTime)
    {
        automationPoints[static_cast<size_t>(index - 1)].value = value;
        return index - 1;
    }
    if (index < static_cast<int>(automationPoints.size())
        && automationPoints[static_cast<size_t>(index)].timeSeconds - seconds < sameTime)
    {
        automationPoints[static_cast<size_t>(index)].value = value;
        return index;
    }
    automationPoints.insert(automationPoints.begin() + index, {seconds, value});
    return index;
}

// Only the lane Delete would act on is drawn as focused, so the highlight and
// the keyboard agree about what is selected.
bool Arrangement::isFocusedAutomation(Session::DeviceTarget target) const
{
    return focus == Focus::automation && focusedAutomation.track == target.track
        && focusedAutomation.slot == target.slot && focusedAutomation.parameter == target.parameter;
}

bool Arrangement::beginAutomationGesture(const juce::MouseEvent& event)
{
    const auto hover = automationHoverAt(event.position, event.mods.isAltDown());
    if (!hover.valid())
        return false;
    const auto& entry = rows[static_cast<size_t>(hover.row)];
    const auto& automation = trackLanes[static_cast<size_t>(entry.track)][static_cast<size_t>(hover.automation)];

    automationRow = hover.row;
    automationTarget = automation.target;
    setSelection({});
    focusedAutomation = automation.target;
    focus = Focus::automation;
    automationPoints = automation.points.empty() ? defaultAutomationPoints(automation) : automation.points;
    automationInserted = false;
    automationHover = hover;
    automationHover.dragging = true;

    // Double-clicking a node takes it out again. Adding one is a single click
    // anywhere on the lane, so without this a curve could only ever gain
    // nodes, and the only way back would be clearing the whole thing.
    if (hover.point >= 0 && event.getNumberOfClicks() == 2 && automationPoints.size() > 2)
    {
        automationPoints.erase(automationPoints.begin() + hover.point);
        auto points = std::move(automationPoints);
        automationPoints.clear();
        automationGesture = AutomationGesture::none;
        automationRow = -1;
        automationPoint = -1;
        automationHover = {};
        selectTrack(entry.track);
        const auto result = session.setTrackAutomationPoints(automationTarget, std::move(points));
        if (status)
            status(result.wasOk() ? "Automation point removed" : result.getErrorMessage());
        repaint();
        return true;
    }

    if (hover.point >= 0)
    {
        automationGesture = AutomationGesture::movePoint;
        automationPoint = hover.point;
    }
    else if (!automation.active())
    {
        // A lane that has never been drawn has no curve to add to, so the
        // press lifts its resting line into one instead.
        automationGesture = AutomationGesture::moveLine;
        automationPoint = -1;
    }
    else
    {
        // Clicking the lane drops a node and starts carrying it, so the one
        // gesture both adds and places. Near the line the value has already
        // been pulled onto it, which is what keeps a node added to a ramp
        // from denting the ramp.
        automationGesture = AutomationGesture::movePoint;
        automationPoint = insertAutomationPoint(hover.timeSeconds, hover.value);
        automationInserted = true;
        automationHover.point = automationPoint;
    }
    selectTrack(entry.track);
    // An existing node is left exactly where it was until the pointer moves,
    // so clicking one focuses its lane the way clicking a lane selects it.
    return true;
}

void Arrangement::dragAutomationGesture(const juce::MouseEvent& event)
{
    if (automationGesture == AutomationGesture::none || automationRow < 0)
        return;
    const auto& entry = rows[static_cast<size_t>(automationRow)];
    if (!juce::isPositiveAndBelow(entry.track, static_cast<int>(trackLanes.size())))
        return;
    const Session::TrackAutomation* automation = nullptr;
    auto automationIndex = -1;
    const auto& lanes = trackLanes[static_cast<size_t>(entry.track)];
    for (int index = 0; index < static_cast<int>(lanes.size()); ++index)
        if (lanes[static_cast<size_t>(index)].target.track == automationTarget.track
            && lanes[static_cast<size_t>(index)].target.slot == automationTarget.slot
            && lanes[static_cast<size_t>(index)].target.parameter == automationTarget.parameter)
        {
            automation = &lanes[static_cast<size_t>(index)];
            automationIndex = index;
        }
    if (automation == nullptr)
        return;

    auto value = automationValueForY(automationRow, *automation, event.position.y);
    if (automationGesture == AutomationGesture::moveLine)
    {
        for (auto& point : automationPoints)
            point.value = value;
    }
    else if (juce::isPositiveAndBelow(automationPoint, static_cast<int>(automationPoints.size())))
    {
        auto& point = automationPoints[static_cast<size_t>(automationPoint)];
        // End points anchor the curve to the start and end of the timeline;
        // moving them sideways would leave a gap the next milestone has to fill.
        if (automationPoint > 0 && automationPoint + 1 < static_cast<int>(automationPoints.size()))
        {
            const auto previous = automationPoints[static_cast<size_t>(automationPoint - 1)].timeSeconds;
            const auto next = automationPoints[static_cast<size_t>(automationPoint + 1)].timeSeconds;
            point.timeSeconds = std::clamp(snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown()),
                                           previous, next);
        }
        else if (automationPoint > 0)
        {
            point.timeSeconds = std::max(automationPoints[static_cast<size_t>(automationPoint - 1)].timeSeconds,
                                         snapped(std::max(0.0, timeAt(event.position.x)), event.mods.isAltDown()));
        }
        // The subtle snap, applied to the drag as well as to the click that
        // started it: within a few pixels of the line its neighbours draw, a
        // node sits exactly on that line. Measured after the time has settled,
        // because on a ramp the line to snap to depends on where along it the
        // node now is.
        const auto neighbour = automationNeighbourValue(automationPoints, automationPoint, point.timeSeconds);
        if (std::abs(automationYFor(automationRow, *automation, neighbour)
                     - automationYFor(automationRow, *automation, value)) <= curveSnapDistance)
            value = neighbour;
        point.value = value;
    }
    // The reading follows the node rather than the pointer while one is being
    // carried, so it says what the curve is about to become.
    automationHover.row = automationRow;
    automationHover.automation = automationIndex;
    automationHover.point = automationPoint;
    automationHover.dragging = true;
    automationHover.onCurve = false;
    automationHover.pointer = event.position;
    automationHover.value = value;
    automationHover.timeSeconds = juce::isPositiveAndBelow(automationPoint, static_cast<int>(automationPoints.size()))
        ? automationPoints[static_cast<size_t>(automationPoint)].timeSeconds
        : std::max(0.0, timeAt(event.position.x));
    repaint();
}

void Arrangement::endAutomationGesture(const juce::MouseEvent& event)
{
    if (automationGesture == AutomationGesture::none)
        return;
    const auto target = automationTarget;
    // A click that put a node down or took one out has already changed the
    // curve, so travel is not what decides whether there is anything to write.
    const auto moved = event.getDistanceFromDragStart() >= 3;
    const auto edited = automationInserted;
    auto points = automationPoints;
    automationGesture = AutomationGesture::none;
    automationRow = -1;
    automationPoint = -1;
    automationInserted = false;
    automationPoints.clear();
    automationHover = {};
    if (!moved && !edited)
    {
        updateAutomationHover(event.position, event.mods.isAltDown());
        repaint();
        return;
    }

    const auto result = session.setTrackAutomationPoints(target, std::move(points));
    if (status)
        status(result.wasOk() ? (edited && !moved ? "Automation point added" : "Automation updated")
                              : result.getErrorMessage());
    updateAutomationHover(event.position, event.mods.isAltDown());
    repaint();
}

void Arrangement::showAutomationMenu(Session::DeviceTarget target)
{
    const auto lane = session.trackAutomationState(target);
    juce::PopupMenu menu;
    menu.addSectionHeader("AUTOMATION");
    menu.addItem(1, "Show automation", true, lane.visible && !lane.ownLane);
    menu.addItem(2, "Show automation on new lane", true, lane.visible && lane.ownLane);
    menu.addSeparator();
    menu.addItem(3, "Hide automation", lane.visible);
    menu.addItem(4, "Delete automation", lane.active);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<Arrangement>(this), target] (int result)
        {
            if (safe == nullptr || result == 0) return;
            const auto outcome = result == 1 ? safe->session.showTrackAutomation(target, false)
                : result == 2 ? safe->session.showTrackAutomation(target, true)
                : result == 3 ? safe->session.hideTrackAutomation(target)
                : safe->session.clearTrackAutomationPoints(target);
            if (result == 3)
            {
                safe->focusedAutomation = {};
                safe->focus = Focus::none;
            }
            else
            {
                safe->focusedAutomation = target;
                safe->focus = Focus::automation;
            }
            if (safe->status && outcome.failed())
                safe->status(outcome.getErrorMessage());
        });
}

}
