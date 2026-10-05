#include "ArrangementInternal.h"
#include <algorithm>

// How track automation is drawn: the curves over a row, the node under the
// pointer and the ghost beside it, the reading that says what a level is
// worth, and the dimmed clone of a track that sits under a lane of its own.
//
// Split from ArrangementAutomation.cpp, which keeps the row stack, the
// geometry and the gestures. Same class, two translation units - no header,
// signature or call site changes.

namespace rhino
{
namespace
{
const juce::Colour activeCurve {0xffe2564f};
const juce::Colour restingCurve {0xffb9524d};
const juce::Colour hoveredCurve = activeCurve.brighter(0.4f);
const juce::Colour hoveredResting = restingCurve.brighter(0.4f);

// A curve is drawn on a track's lane and on a lane of its own, and the two
// grounds are different shades, so anything that has to disappear into the row
// it sits on asks for the row's own.
juce::Colour groundFor(int automationIndex)
{
    return automationIndex >= 0 ? palette::automationLane : palette::arrangement;
}

void drawDashedLine(juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    const float dashes[] {5.0f, 4.0f};
    g.drawDashedLine({x1, y1, x2, y2}, dashes, 2, 1.4f);
}
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

    // A segment or a node wholly outside the columns being repainted is left
    // out: a curve runs the width of the view, and a playhead strip needs the
    // one or two segments that cross it. The margin covers the line's
    // thickness and the widest node.
    const auto dirty = repaintArea(g);
    const auto reaches = [&dirty](float x1, float x2)
    {
        return std::max(x1, x2) >= dirty.getX() - 6.0f && std::min(x1, x2) <= dirty.getRight() + 6.0f;
    };
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
        // The pointer lightens the curve it is working on, which is the whole
        // of the feedback that a lane is live before anything is clicked.
        const auto hovered = automationHover.valid() && automationHover.row == row
            && automationHover.automation == index;
        const auto curveColour = hovered ? hoveredCurve : activeCurve;

        if (points.size() < 2)
        {
            // Never drawn: a dotted line at the knob's current value, which
            // says "this parameter is free" rather than "this is its curve".
            const auto y = automationYFor(row, automation, automation.restingValue);
            g.setColour((hovered ? hoveredResting : restingCurve).withAlpha(0.85f));
            drawDashedLine(g, left, y, right, y);
            if (hovered && !automationHover.dragging)
                paintAutomationGhost(g, row, automation);
            continue;
        }

        g.setColour(curveColour);
        auto previous = juce::Point<float>(left, automationYFor(row, automation, points.front().value));
        for (const auto& point : points)
        {
            const juce::Point<float> next {xFor(point.timeSeconds), automationYFor(row, automation, point.value)};
            if (reaches(previous.x, next.x))
                g.drawLine(previous.x, previous.y, next.x, next.y, 1.6f);
            previous = next;
        }
        if (reaches(previous.x, right))
            g.drawLine(previous.x, previous.y, right, previous.y, 1.6f);
        // Nothing is proposed while a node is actually being carried: the node
        // itself is already drawn where the ghost would go.
        if (hovered && automationHover.point < 0 && !automationHover.dragging)
            paintAutomationGhost(g, row, automation);
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
        {
            const auto& point = points[static_cast<size_t>(i)];
            const juce::Point<float> handle {xFor(point.timeSeconds), automationYFor(row, automation, point.value)};
            if (handle.x < left - 6.0f || handle.x > right + 6.0f || !reaches(handle.x, handle.x))
                continue;
            // The node under the pointer is filled rather than outlined: it is
            // the one a press would pick up, and at six pixels across a ring
            // and a disc tell apart far better than two sizes of ring.
            const auto active = hovered && automationHover.point == i;
            const auto size = active ? 9.0f : focused ? 8.0f : 6.0f;
            const auto box = juce::Rectangle<float>(size, size).withCentre(handle);
            // A resting node is a hole punched in the row it sits on, so it is
            // filled with that row's own ground. A curve is drawn both on a
            // track's lane and on a lane of its own, and the two grounds are
            // different shades.
            g.setColour(active ? curveColour : groundFor(rows[static_cast<size_t>(row)].automation));
            g.fillEllipse(box);
            g.setColour(active ? hoveredCurve.brighter(0.5f) : curveColour);
            g.drawEllipse(box, 1.6f);
        }
    }
}

// Where a click would put a node, drawn before the click so the lane answers
// the pointer rather than only the press. Hollow and half lit: it has to read
// as a proposal next to the solid nodes that are really there.
void Arrangement::paintAutomationGhost(juce::Graphics& g, int row, const Session::TrackAutomation& automation) const
{
    const auto area = automationArea(row);
    const juce::Point<float> centre {xFor(automationHover.timeSeconds),
                                     automationYFor(row, automation, automationHover.value)};
    if (centre.x < area.getX() - 6.0f || centre.x > std::min(static_cast<float>(getWidth()) - 14.0f, area.getRight()) + 6.0f)
        return;
    const auto box = juce::Rectangle<float>(9.0f, 9.0f).withCentre(centre);
    g.setColour(groundFor(rows[static_cast<size_t>(row)].automation).withAlpha(0.75f));
    g.fillEllipse(box);
    // On the line the ghost is solid-edged, because that is where it will
    // land; off it the edge is dashed to say the value is the pointer's own.
    g.setColour(hoveredCurve.withAlpha(automationHover.onCurve ? 0.95f : 0.55f));
    g.drawEllipse(box, automationHover.onCurve ? 1.8f : 1.2f);
}

// The level under the pointer, in the units the knob itself prints: "2.0 dB",
// "12%". A lane draws a curve between two bare numbers, so without this the
// only way to know what a point is worth is to play it.
void Arrangement::paintAutomationReadout(juce::Graphics& g)
{
    if (!automationHover.valid() || !juce::isPositiveAndBelow(automationHover.row, static_cast<int>(rows.size())))
        return;
    const auto& entry = rows[static_cast<size_t>(automationHover.row)];
    if (!juce::isPositiveAndBelow(entry.track, static_cast<int>(trackLanes.size())))
        return;
    const auto& lanes = trackLanes[static_cast<size_t>(entry.track)];
    if (!juce::isPositiveAndBelow(automationHover.automation, static_cast<int>(lanes.size())))
        return;
    const auto& automation = lanes[static_cast<size_t>(automationHover.automation)];

    auto text = session.automationValueText(automation.target, automationHover.value);
    if (text.isEmpty())
        text = juce::String(automationHover.value, 2);
    // A lane row is labelled by its own header, so the reading is just the
    // level. A track's own row stacks every curve that was not sent away, so
    // there the reading has to say which of them it belongs to.
    if (entry.automation < 0)
        text = automation.parameterName + "  " + text;

    g.setFont(uiFont(10.0f));
    const auto width = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text)) + 14.0f;
    constexpr auto height = 18.0f;
    const auto laneBottom = lanesTop + laneContentHeight();
    // Up and to the right of the pointer, and inside the lanes wherever the
    // pointer is: against the right edge or the top row it folds back rather
    // than being cropped to a stub.
    const juce::Rectangle<float> box {
        std::clamp(automationHover.pointer.x + 14.0f, headerWidth + 4.0f,
                   std::max(headerWidth + 4.0f, static_cast<float>(getWidth()) - 18.0f - width)),
        std::clamp(automationHover.pointer.y - 26.0f, lanesTop + 3.0f,
                   std::max(lanesTop + 3.0f, laneBottom - height - 3.0f)),
        width, height};

    g.setColour(palette::appBackground.withAlpha(0.94f));
    g.fillRect(box);
    g.setColour(hoveredCurve.withAlpha(0.7f));
    g.drawRect(box, 1.0f);
    g.setColour(palette::text);
    drawSnappedText(g, text, box.getSmallestIntegerContainer(), juce::Justification::centred, true);
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
    // Lit to the same degree the lanes are, and cooled rather than tinted
    // darker: the row still has to read as not-a-track at a glance, and the
    // timeline's grid is drawn through it in the same dark lines.
    g.setColour(palette::automationLane);
    g.fillRect(full);
    g.setColour(palette::barGrid);
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
    g.setFont(uiFont(9.0f));
    drawSnappedText(g, automation->deviceName, {22, static_cast<int>(area.getY()) + 3,
                    static_cast<int>(headerWidth) - 32, 15}, juce::Justification::centredLeft, true);
    g.setColour(juce::Colour(0xffd6dde2));
    drawSnappedText(g, automation->parameterName, {22, static_cast<int>(area.getY()) + 18,
                    static_cast<int>(headerWidth) - 32, 15}, juce::Justification::centredLeft, true);
    g.setColour(activeCurve.withAlpha(automation->active() ? 1.0f : 0.5f));
    g.fillRect(10.0f, area.getY() + 8.0f, 3.0f, area.getHeight() - 16.0f);
}

}
