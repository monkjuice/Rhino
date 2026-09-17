#include "ArrangementInternal.h"
#include <algorithm>

// Track automation in the arrangement: the row stack, the curves drawn over it,
// and the pointer gestures that edit them.
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
const juce::Colour activeCurve {0xffe2564f};
const juce::Colour restingCurve {0xffb9524d};

void drawDashedLine(juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    const float dashes[] {5.0f, 4.0f};
    g.drawDashedLine({x1, y1, x2, y2}, dashes, 2, 1.4f);
}
}

// Rows are rebuilt whenever the edit changes, because revealing a lane changes
// what is below it. Their pixel geometry is a separate step: lane height
// follows the component height, so a resize must reflow the stack without
// re-reading the edit.
void Arrangement::buildRows()
{
    rows.clear();
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

void Arrangement::layoutRows()
{
    auto top = 0.0f;
    for (auto& row : rows)
    {
        row.top = top;
        row.height = row.automation < 0 ? laneHeightFor(row.track) : automationRowHeight;
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

Arrangement::AutomationHit Arrangement::automationHitAt(juce::Point<float> point) const
{
    if (point.x < headerWidth || point.y >= lanesTop + laneContentHeight())
        return {};
    const auto row = rowAt(point.y);
    if (row < 0)
        return {};
    const auto& entry = rows[static_cast<size_t>(row)];
    // A track's own row belongs to its clips: automation there is read-only
    // until the A button puts the arrangement in automation mode. A lane row
    // holds nothing else, so it is always editable.
    if (entry.automation < 0 && !automationButton.getToggleState())
        return {};

    // A lane row shows exactly one curve; a track row shows every curve that
    // was not sent away, tested newest first so the topmost drawn wins.
    std::vector<int> candidates;
    if (entry.automation >= 0)
        candidates.push_back(entry.automation);
    else if (juce::isPositiveAndBelow(entry.track, static_cast<int>(trackLanes.size())))
        for (int i = static_cast<int>(trackLanes[static_cast<size_t>(entry.track)].size()); --i >= 0;)
            if (!trackLanes[static_cast<size_t>(entry.track)][static_cast<size_t>(i)].ownLane)
                candidates.push_back(i);

    for (const auto index : candidates)
    {
        const auto& automation = trackLanes[static_cast<size_t>(entry.track)][static_cast<size_t>(index)];
        for (int i = 0; i < static_cast<int>(automation.points.size()); ++i)
        {
            const auto& stored = automation.points[static_cast<size_t>(i)];
            const juce::Point<float> handle {xFor(stored.timeSeconds), automationYFor(row, automation, stored.value)};
            if (std::abs(handle.x - point.x) <= pointGrabRadius && std::abs(handle.y - point.y) <= pointGrabRadius)
                return {row, index, i};
        }
    }

    // Lifting the resting line is the only way to activate a lane until the
    // next milestone adds points anywhere along an existing curve.
    for (const auto index : candidates)
    {
        const auto& automation = trackLanes[static_cast<size_t>(entry.track)][static_cast<size_t>(index)];
        if (!automation.active())
            return {row, index, -1};
    }
    return {};
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
    const auto target = automationHitAt(event.position);
    if (!target.valid())
        return false;
    const auto& entry = rows[static_cast<size_t>(target.row)];
    const auto& automation = trackLanes[static_cast<size_t>(entry.track)][static_cast<size_t>(target.automation)];

    automationRow = target.row;
    automationTarget = automation.target;
    setSelection({});
    focusedAutomation = automation.target;
    focus = Focus::automation;
    automationPoints = automation.points.empty() ? defaultAutomationPoints(automation) : automation.points;
    if (target.point >= 0)
    {
        automationGesture = AutomationGesture::movePoint;
        automationPoint = target.point;
    }
    else
    {
        automationGesture = AutomationGesture::moveLine;
        automationPoint = -1;
    }
    selectTrack(entry.track);
    // The curve is left alone until the pointer actually moves, so clicking a
    // lane row focuses it the way clicking a track lane selects the track.
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
    for (const auto& lane : trackLanes[static_cast<size_t>(entry.track)])
        if (lane.target.track == automationTarget.track && lane.target.slot == automationTarget.slot
            && lane.target.parameter == automationTarget.parameter)
            automation = &lane;
    if (automation == nullptr)
        return;

    const auto value = automationValueForY(automationRow, *automation, event.position.y);
    if (automationGesture == AutomationGesture::moveLine)
    {
        for (auto& point : automationPoints)
            point.value = value;
    }
    else if (juce::isPositiveAndBelow(automationPoint, static_cast<int>(automationPoints.size())))
    {
        auto& point = automationPoints[static_cast<size_t>(automationPoint)];
        point.value = value;
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
    }
    repaint();
}

void Arrangement::endAutomationGesture(const juce::MouseEvent& event)
{
    if (automationGesture == AutomationGesture::none)
        return;
    const auto target = automationTarget;
    const auto moved = event.getDistanceFromDragStart() >= 3;
    auto points = automationPoints;
    automationGesture = AutomationGesture::none;
    automationRow = -1;
    automationPoint = -1;
    automationPoints.clear();
    if (!moved)
    {
        repaint();
        return;
    }

    const auto result = session.setTrackAutomationPoints(target, std::move(points));
    if (status)
        status(result.wasOk() ? "Automation updated" : result.getErrorMessage());
    repaint();
}

void Arrangement::paintAutomationRow(juce::Graphics& g, int row)
{
    const auto& entry = rows[static_cast<size_t>(row)];
    if (!juce::isPositiveAndBelow(entry.track, static_cast<int>(trackLanes.size())))
        return;
    const auto area = automationArea(row);
    if (area.isEmpty())
        return;
    const auto left = std::max(headerWidth, area.getX());
    const auto right = std::min(static_cast<float>(getWidth()) - 14.0f, area.getRight());
    if (right <= left)
        return;

    const auto& lanes = trackLanes[static_cast<size_t>(entry.track)];
    for (int index = 0; index < static_cast<int>(lanes.size()); ++index)
    {
        if (entry.automation >= 0 ? index != entry.automation : lanes[static_cast<size_t>(index)].ownLane)
            continue;
        const auto& automation = lanes[static_cast<size_t>(index)];
        const auto beingDragged = automationGesture != AutomationGesture::none && automationRow == row
            && automation.target.track == automationTarget.track && automation.target.slot == automationTarget.slot
            && automation.target.parameter == automationTarget.parameter;
        const auto points = beingDragged ? automationPoints : automation.points;
        const auto focused = isFocusedAutomation(automation.target);

        if (points.size() < 2)
        {
            // Never drawn: a dotted line at the knob's current value, which
            // says "this parameter is free" rather than "this is its curve".
            const auto y = automationYFor(row, automation, automation.restingValue);
            g.setColour(restingCurve.withAlpha(0.85f));
            drawDashedLine(g, left, y, right, y);
            continue;
        }

        g.setColour(activeCurve);
        auto previous = juce::Point<float>(left, automationYFor(row, automation, points.front().value));
        for (const auto& point : points)
        {
            const juce::Point<float> next {xFor(point.timeSeconds), automationYFor(row, automation, point.value)};
            g.drawLine(previous.x, previous.y, next.x, next.y, 1.6f);
            previous = next;
        }
        g.drawLine(previous.x, previous.y, right, previous.y, 1.6f);
        for (const auto& point : points)
        {
            const juce::Point<float> handle {xFor(point.timeSeconds), automationYFor(row, automation, point.value)};
            if (handle.x < left - 6.0f || handle.x > right + 6.0f)
                continue;
            const auto box = juce::Rectangle<float>(focused ? 8.0f : 6.0f, focused ? 8.0f : 6.0f).withCentre(handle);
            g.setColour(juce::Colour(0xff1b2126));
            g.fillEllipse(box);
            g.setColour(activeCurve);
            g.drawEllipse(box, 1.6f);
        }
    }
}

// The ghost row: the track repeated dimly so the curve above it lines up with
// something recognisable, plus the lane's name in the header.
void Arrangement::paintGhostRow(juce::Graphics& g, int row)
{
    const auto& entry = rows[static_cast<size_t>(row)];
    const auto* automation = automationFor(entry);
    const auto area = rowBounds(row);
    if (automation == nullptr || area.isEmpty())
        return;

    const auto full = area.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f);
    g.setColour(juce::Colour(0xff1b2126));
    g.fillRect(full);
    g.setColour(juce::Colour(0xff2a323a));
    g.drawHorizontalLine(static_cast<int>(area.getY()), 0.0f, full.getRight());

    for (const auto& clip : clips)
    {
        if (clip.track != entry.track)
            continue;
        const auto box = bounds(clip).withY(area.getY() + 3.0f).withHeight(area.getHeight() - 6.0f);
        const auto visible = box.getIntersection(area);
        if (visible.isEmpty())
            continue;
        const auto fallback = juce::Colour(clip.track == 0 ? 0xff414c34 : 0xff284b59);
        g.setColour((clip.colour.isTransparent() ? fallback : clip.colour).withAlpha(0.22f));
        g.fillRect(visible);
    }

    const auto focused = isFocusedAutomation(automation->target);
    g.setColour(juce::Colour(focused ? 0xff2e3840 : 0xff222930));
    g.fillRect(area.withX(0.0f).withWidth(headerWidth));
    g.setColour(juce::Colour(0xff9aa6af));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(automation->deviceName, 22, static_cast<int>(area.getY()) + 3,
               static_cast<int>(headerWidth) - 32, 15, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffd6dde2));
    g.drawText(automation->parameterName, 22, static_cast<int>(area.getY()) + 18,
               static_cast<int>(headerWidth) - 32, 15, juce::Justification::centredLeft, true);
    g.setColour(activeCurve.withAlpha(automation->active() ? 1.0f : 0.5f));
    g.fillRect(10.0f, area.getY() + 8.0f, 3.0f, area.getHeight() - 16.0f);
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
