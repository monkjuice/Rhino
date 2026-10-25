#include "StepGridInternal.h"
#include <algorithm>
#include <cmath>
#include <optional>

// Painting the velocity lane. Markers are drawn with whole-pixel rectangles
// and nothing else, as the cells are, so a partial repaint draws exactly what
// a full one does.

namespace rhino
{

namespace
{
// A dot of whole pixels: each row of the disc is one rectangle, and rows the
// same width are one rectangle together.
void fillDot(juce::Graphics& g, int cx, int cy, bool large)
{
    if (large)
    {
        g.fillRect(cx - 2, cy - 4, 5, 1);
        g.fillRect(cx - 3, cy - 3, 7, 1);
        g.fillRect(cx - 4, cy - 2, 9, 5);
        g.fillRect(cx - 3, cy + 3, 7, 1);
        g.fillRect(cx - 2, cy + 4, 5, 1);
        return;
    }
    g.fillRect(cx - 1, cy - 3, 3, 1);
    g.fillRect(cx - 2, cy - 2, 5, 1);
    g.fillRect(cx - 3, cy - 1, 7, 3);
    g.fillRect(cx - 2, cy + 2, 5, 1);
    g.fillRect(cx - 1, cy + 3, 3, 1);
}
}

void StepGrid::paintVelocityLane(juce::Graphics& g, juce::Rectangle<float> dirty, float stepWidth,
                                 int firstStep, int lastStep)
{
    const auto lane = velocityLaneBounds();
    if (lane.isEmpty())
        return;
    const auto divider = laneDividerBounds();
    if (!dirty.intersects(divider.getUnion(lane)))
        return;
    if (dirty.intersects(divider))
    {
        g.setColour(palette::sideSurface);
        g.fillRect(divider);
        g.setColour(dividerHot || gesture == Gesture::laneResize ? palette::activeNeutral : palette::border);
        g.fillRect(divider.withTrimmedTop(2.0f).withHeight(1.0f));
    }
    const auto plot = velocityPlotBounds();
    const auto right = gridRight();
    const auto xFor = [this, stepWidth](double step) { return labelWidth + static_cast<float>(step - stepScroll) * stepWidth; };
    if (dirty.getRight() > labelWidth)
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<float>(labelWidth, lane.getY(), right - labelWidth, lane.getHeight())
                               .getSmallestIntegerContainer());
        // The beats shade as the cells above them do, a little darker.
        const auto perBeat = stepsPerBeat();
        for (int step = firstStep; step <= lastStep; ++step)
        {
            const juce::Rectangle<float> column {xFor(step), lane.getY(), stepWidth, lane.getHeight()};
            if (!dirty.intersects(column))
                continue;
            g.setColour(step / perBeat % 2 == 0 ? palette::velocityLane : palette::velocityLaneAlternate);
            g.fillRect(column);
        }
        for (int step = firstStep; step <= lastStep + 1; ++step)
        {
            const auto x = juce::roundToInt(xFor(step));
            if (x < dirty.getX() - 1.0f || x > dirty.getRight() + 1.0f)
                continue;
            g.setColour(step % perBeat == 0 ? palette::velocityLaneBarRule : palette::velocityLaneStepRule);
            g.drawVerticalLine(x, lane.getY(), lane.getBottom());
        }
        g.setColour(palette::velocityGuide);
        g.fillRect(juce::Rectangle<float>(labelWidth, std::floor(yForVelocity(64.0f)), gridWidth(), 1.0f));

        // In layers, so the marker that matters is never under another one:
        // notes out of the pitch window, then the rest, then the selection,
        // then the one under the pointer. Inside a layer the later note is on
        // top, which is the marker pickMarker takes from a stack.
        struct Placed { const LaneNote* note; float x0, x1, y; bool selected; };
        std::vector<Placed> dim, plain, chosen;
        std::optional<Placed> hovered;
        for (const auto& note : laneNotes)
        {
            const auto x0 = xFor(note.start);
            if (x0 >= right)
                continue;
            const auto x1 = std::min(right, x0 + static_cast<float>(note.length) * stepWidth);
            const auto y = velocitylane::yForVelocity(static_cast<float>(note.velocity), plot.getY(), plot.getHeight());
            const juce::Rectangle<float> reach {x0 - 7.0f, y - 7.0f, std::max(14.0f, x1 - x0 + 8.0f), 14.0f};
            if (!dirty.intersects(reach))
                continue;
            const auto selected = isSelected(note.state);
            const Placed placed {&note, x0, x1, y, selected};
            if (note.state == hoveredNoteState) hovered = placed;
            else if (selected) chosen.push_back(placed);
            else if (!note.inWindow) dim.push_back(placed);
            else plain.push_back(placed);
        }
        const auto drawMarker = [&g](const Placed& placed, bool isHovered)
        {
            auto colour = placed.selected ? palette::noteSelected : palette::noteBody;
            // Opaque, so nothing is blended and a partial repaint matches.
            if (!placed.note->inWindow)
                colour = colour.interpolatedWith(palette::velocityLane, 0.55f);
            const auto cx = static_cast<int>(std::floor(placed.x0));
            const auto cy = static_cast<int>(std::floor(placed.y));
            g.setColour(colour);
            const auto barLeft = static_cast<float>(cx + 4);
            if (placed.x1 - barLeft >= 1.0f)
                g.fillRect(juce::Rectangle<float>(barLeft, static_cast<float>(isHovered ? cy - 1 : cy),
                                                  placed.x1 - barLeft, isHovered ? 3.0f : 1.0f));
            if (isHovered)
            {
                g.setColour(palette::text);
                fillDot(g, cx, cy, true);
                g.setColour(colour);
            }
            fillDot(g, cx, cy, false);
        };
        for (const auto* layer : {&dim, &plain, &chosen})
            for (const auto& placed : *layer) drawMarker(placed, false);
        if (hovered) drawMarker(*hovered, true);
    }

    if (dirty.getX() >= labelWidth)
        return;
    const auto gutter = lane.withWidth(labelWidth);
    g.setColour(palette::sideSurface);
    g.fillRect(gutter);
    g.setColour(palette::border);
    g.fillRect(gutter.withX(labelWidth - 1.0f).withWidth(1.0f));
    const auto [value, name] = velocityGutterText();
    const auto tall = lane.getHeight() >= velocitylane::midLabelHeight;
    if (value.isEmpty())
    {
        g.setFont(uiFont(8.5f));
        g.setColour(palette::textDim);
        const auto label = [&g](const juce::String& text, float centreY)
        {
            drawSnappedText(g, text, juce::Rectangle<float>(0.0f, centreY - 5.0f, labelWidth - 6.0f, 10.0f).toNearestInt(),
                            juce::Justification::centredRight);
        };
        label("127", plot.getY());
        if (tall)
            label("64", yForVelocity(64.0f));
        label("1", plot.getBottom());
        return;
    }
    g.setFont(uiFontBold(10.5f));
    g.setColour(palette::text);
    if (!tall || name.isEmpty())
    {
        drawSnappedText(g, value, juce::Rectangle<float>(4.0f, lane.getCentreY() - 7.0f, labelWidth - 10.0f, 14.0f).toNearestInt(),
                        juce::Justification::centredRight);
        return;
    }
    drawSnappedText(g, value, juce::Rectangle<float>(4.0f, lane.getCentreY() - 13.0f, labelWidth - 10.0f, 14.0f).toNearestInt(),
                    juce::Justification::centredRight);
    g.setFont(uiFont(8.5f));
    g.setColour(palette::textDim);
    drawSnappedText(g, name, juce::Rectangle<float>(4.0f, lane.getCentreY() + 1.0f, labelWidth - 10.0f, 12.0f).toNearestInt(),
                    juce::Justification::centredRight, true);
}

}
