#include "StepGridInternal.h"
#include "Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Grid rendering.

namespace rhino
{

// The lanes are one row per semitone, so the keys are drawn the way a piano
// looks from directly above rather than in true piano geometry: a black key
// keeps its own full-height row and is only narrower than its neighbours.
void StepGrid::paintKeyboard(juce::Graphics& g)
{
    const auto rows = visiblePitchRows();
    const auto height = rowHeight();
    const auto width = labelWidth - 4.0f;
    const auto drums = session.isPatternDrums();
    const auto blackKeyWidth = std::floor(width * 0.6f);
    // A name needs a row tall enough to hold it. Once the lanes are squeezed
    // past that only the octaves stay named, and past that nothing does.
    const auto labelEvery = height >= 15.0f ? 1 : height >= 8.0f ? 12 : 0;
    g.setFont(juce::FontOptions(std::clamp(height - 3.0f, 7.5f, 11.0f)));
    for (int row = 0; row < rows; ++row)
    {
        const auto pitch = lowestVisiblePitch + rows - 1 - row;
        const auto key = cell(0, row).withX(0.0f).withWidth(width);
        const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
        if (drums)
        {
            const bool namedDrum = pitch == 48 || pitch == 50 || pitch == 52 || pitch == 53
                                || pitch == 54 || pitch == 56 || pitch == 58 || pitch == 59;
            g.setColour(juce::Colour(namedDrum ? 0xff3a3325 : black ? 0xff15191e : 0xff30373e));
            g.fillRect(key.reduced(0.0f, 1.0f));
            if (labelEvery > 0)
            {
                g.setColour(juce::Colour(namedDrum ? 0xffffc16a : 0xffbac2ca));
                g.drawText(drumLaneName(pitch), key, juce::Justification::centred);
            }
            continue;
        }
        // A black key is laid over the white surface rather than replacing it:
        // what shows to its right is the white key it sits between.
        g.setColour(juce::Colour(0xffcdd3c8));
        g.fillRect(key);
        if (black)
        {
            g.setColour(juce::Colour(0xff15191e));
            g.fillRect(key.withWidth(blackKeyWidth));
        }
        // The seam between two keys: it reads on a white key and disappears
        // into a black one, which is what the eye expects from above.
        g.setColour(juce::Colour(0xff2b3138));
        g.fillRect(key.withTop(key.getBottom() - 1.0f));
        if (labelEvery == 0 || black || (labelEvery == 12 && pitchClassOf(pitch) != 0))
            continue;
        g.setColour(juce::Colour(pitchClassOf(pitch) == 0 ? 0xff2f353c : 0xff70767d));
        // Middle C is C3 here, matching drumLaneName and Forge's keyboard — see
        // StepGridInternal.h for why the three of them have to agree.
        g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 3),
                   key.reduced(3.0f, 0.0f), juce::Justification::centredRight);
    }
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    if (hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(0xff55c7eb).withAlpha(0.12f));
        g.fillRect(getLocalBounds().removeFromTop(static_cast<int>(headerHeight)));
    }
    g.setFont(juce::FontOptions(12.0f));
    const auto dirty = g.getClipBounds().toFloat();
    const auto steps = session.editorStepCount();
    const auto firstVisibleStep = std::max(0, static_cast<int>(std::floor(stepScroll)));
    const auto lastVisibleStep = std::min(steps - 1, static_cast<int>(std::ceil(stepScroll + visibleStepSpan())));
    for (int step = 0; step < steps; ++step)
    {
        const auto headerCell = cell(step, 0).withY(0).withHeight(headerHeight);
        if (headerCell.getRight() < labelWidth || headerCell.getX() > gridRight())
            continue;
        g.setColour(juce::Colour(step % 4 == 0 ? 0xffd4dacd : 0xff78818a));
        g.drawText(juce::String(step + 1), headerCell, juce::Justification::centred);
    }
    paintKeyboard(g);
    const auto rows = visiblePitchRows();
    const auto scaleIndex = scaleHighlight - 2;
    const bool scaleEnabled = !session.isPatternDrums() && scaleIndex >= 0;
    const auto root = scaleEnabled ? scaleIndex % 12 : 0;
    const bool minor = scaleEnabled && scaleIndex >= 12;
    static constexpr std::array<int, 7> major {0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<int, 7> naturalMinor {0, 2, 3, 5, 7, 8, 10};
    const auto& scale = minor ? naturalMinor : major;
    for (int row = 0; row < rows; ++row)
    {
        const auto pitch = lowestVisiblePitch + rows - 1 - row;
        const auto pitchClass = pitchClassOf(pitch);
        const bool inScale = !scaleEnabled || std::find(scale.begin(), scale.end(), (pitchClass - root + 12) % 12) != scale.end();
        for (int step = firstVisibleStep; step <= lastVisibleStep; ++step)
        {
            const auto bounds = cell(step, row);
            if (!dirty.intersects(bounds)) continue;
            const auto barColour = step / 4 % 2 == 0 ? juce::Colour(0xff46515a) : juce::Colour(0xff3b4650);
            g.setColour(barColour);
            g.fillRect(bounds);
            if (scaleEnabled && inScale)
            {
                g.setColour(juce::Colour(0x123b8d9e));
                g.fillRect(bounds);
            }
        }
        for (const auto& note : visibleNotes)
        {
            if (note.row != row)
                continue;
            auto bounds = boundsFor(note);
            if (bounds.getRight() < labelWidth || !dirty.intersects(bounds))
                continue;

            const auto selected = isSelected(note.state);
            g.setColour(selected ? juce::Colour(0xffe9a84a) : juce::Colour(0xffc6d58c));
            g.fillRect(bounds);
            g.setColour(selected ? juce::Colour(0x77482d15) : juce::Colour(0x55363f46));
            g.fillRect(bounds.withWidth(1.0f));
            g.setColour(selected ? juce::Colour(0xffffe3a3) : juce::Colour(0xffe8f2aa));
            g.fillRect(bounds.withX(bounds.getRight() - 2.0f).withWidth(2.0f));
            if (selected)
            {
                g.setColour(juce::Colour(0xfffff0c2));
                g.drawRect(bounds.reduced(1.0f), 2.0f);
            }
        }
    }
    // Draw the grid after all cells. This avoids the next row's fractional
    // fill covering the preceding row separator on high-DPI displays.
    g.setColour(juce::Colour(0xff202930));
    for (int row = 0; row <= rows; ++row)
    {
        const auto y = headerHeight + row * rowHeight();
        g.fillRect(juce::Rectangle<float>(labelWidth, std::floor(y), gridWidth(), 1.0f));
    }
    for (int row = 0; row < rows; ++row)
    {
        std::array<int, Session::steps + 2> sustainedBoundaryDeltas {};
        for (const auto& note : visibleNotes)
        {
            if (note.row != row)
                continue;
            const auto noteStart = note.start;
            const auto noteEnd = noteStart + std::max(0.0625, note.length);
            constexpr float boundaryTolerance = 0.0001f;
            const auto firstCovered = std::clamp(static_cast<int>(std::floor(noteStart + boundaryTolerance)) + 1,
                                                 0, steps + 1);
            const auto afterLastCovered = std::clamp(static_cast<int>(std::ceil(noteEnd - boundaryTolerance)),
                                                     0, steps + 1);
            if (firstCovered < afterLastCovered)
            {
                ++sustainedBoundaryDeltas[static_cast<size_t>(firstCovered)];
                --sustainedBoundaryDeltas[static_cast<size_t>(afterLastCovered)];
            }
        }

        int sustainedNotes = 0;
        const auto rowTop = headerHeight + row * rowHeight();
        const auto rowBottom = headerHeight + (row + 1) * rowHeight();
        for (int step = 0; step <= lastVisibleStep + 1; ++step)
        {
            sustainedNotes += sustainedBoundaryDeltas[static_cast<size_t>(step)];
            if (step >= firstVisibleStep && sustainedNotes == 0)
            {
                const auto x = cell(step, 0).getX();
                g.setColour(juce::Colour(step % 4 == 0 ? 0xff252d35 : 0xff303941));
                g.drawVerticalLine(juce::roundToInt(x), rowTop, rowBottom);
            }
        }
    }
    {
        auto loopStartX = -1.0f;
        auto loopEndX = -1.0f;
        if (loopDragActive)
        {
            loopStartX = labelWidth + static_cast<float>(loopPreviewStartStep - stepScroll) * cellWidth();
            loopEndX = labelWidth + static_cast<float>(loopPreviewEndStep - stepScroll) * cellWidth();
        }
        else if (session.hasManualLoopRange())
        {
            const auto range = session.edit->getTransport().getLoopRange();
            loopStartX = loopXForTimelineTime(range.getStart().inSeconds());
            loopEndX = loopXForTimelineTime(range.getEnd().inSeconds());
        }
        const auto left = std::max(labelWidth, std::min(loopStartX, loopEndX));
        const auto right = std::min(gridRight(), std::max(loopStartX, loopEndX));
        if (right > left)
        {
            const juce::Rectangle<float> loopBounds {left, 0.0f, right - left, headerHeight + rowAreaHeight()};
            g.setColour(juce::Colour(0x245ab9d6));
            g.fillRect(loopBounds);
            g.setColour(juce::Colour(0xff5ab9d6));
            g.fillRect(loopBounds.withHeight(3.0f));
            g.drawVerticalLine(juce::roundToInt(left), 0.0f, headerHeight);
            g.drawVerticalLine(juce::roundToInt(right), 0.0f, headerHeight);
        }
    }
    if (!session.isPatternDrums())
    {
        const auto maxLowest = 127 - rows + 1;
        const auto thumbHeight = std::max(18.0f, rowAreaHeight() * (static_cast<float>(rows) / 128.0f));
        const auto thumbTravel = std::max(1.0f, rowAreaHeight() - thumbHeight);
        const auto thumbY = headerHeight + (maxLowest - lowestVisiblePitch) / static_cast<float>(maxLowest) * thumbTravel;
        const auto right = gridRight();
        const juce::Rectangle<float> thumb(right - 5.0f, thumbY, 3.0f, thumbHeight);
        g.setColour(juce::Colour(0x55313b44));
        g.fillRect(juce::Rectangle<float>(right - 6.0f, headerHeight + 2.0f, 4.0f, rowAreaHeight() - 4.0f));
        g.setColour(juce::Colour(0xaa8cc5d2));
        g.fillRoundedRectangle(thumb, 1.5f);
    }
    if (playhead >= 0)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, headerHeight, 2.0f, rowAreaHeight());
    }
    if (hasKeyboardFocus(true))
    {
        g.setColour(juce::Colour(0xff55c7eb));
        g.drawRect(getLocalBounds().toFloat().reduced(1.0f), 2.0f);
    }
    // Over the notes and under the marquee: the region is what the clipboard
    // commands act on, so it has to read as covering what it contains.
    paintStepSelection(g);
    if (gesture == Gesture::select && !selectionBox.isEmpty())
    {
        g.setColour(juce::Colour(0x3355c7eb));
        g.fillRect(selectionBox);
        g.setColour(juce::Colour(0xff55c7eb));
        static constexpr float dash[] {4.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(selectionBox.getTopLeft(), selectionBox.getTopRight()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getTopRight(), selectionBox.getBottomRight()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getBottomRight(), selectionBox.getBottomLeft()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(selectionBox.getBottomLeft(), selectionBox.getTopLeft()), dash, 2, 1.0f);
    }
    if (subdivisionActive && subdivisionCount >= 2)
    {
        const auto centre = subdivisionSourceBounds.getCentre();
        const auto badge = juce::Rectangle<float>(centre.x - 17.0f, subdivisionSourceBounds.getY() - 28.0f, 34.0f, 22.0f);
        g.setColour(juce::Colour(0xffe5e8df));
        g.fillRoundedRectangle(badge, 3.0f);
        g.setColour(juce::Colour(0xff252a30));
        g.drawRoundedRectangle(badge, 3.0f, 1.0f);
        g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
        g.drawText(juce::String(subdivisionCount), badge, juce::Justification::centred);
    }
    const auto footer = footerBounds();
    if (dirty.intersects(footer))
    {
        g.setColour(juce::Colour(velocityAdjustActive ? 0xff29343b : 0xff171c21));
        g.fillRect(footer);
        g.setColour(juce::Colour(0xff3b4650));
        g.fillRect(footer.withHeight(1.0f));
        const auto velocity = selectedVelocityPercent();
        const auto value = velocity >= 0 ? juce::String(velocity) + "%"
                                         : velocity == -1 ? juce::String("MIXED") : juce::String("-");
        g.setFont(juce::FontOptions(11.5f).withStyle("Bold"));
        g.setColour(velocityAdjustActive ? juce::Colour(0xffe9a84a) : juce::Colour(0xffb8c4aa));
        g.drawText("VELOCITY  " + value, footer.reduced(9.0f, 2.0f), juce::Justification::centredRight);
        // The draw toggle sits at the left of the footer, so the hint starts
        // past it rather than underneath it.
        const auto hint = footer.withTrimmedLeft(68.0f).reduced(9.0f, 2.0f);
        g.setFont(juce::FontOptions(10.5f));
        g.setColour(juce::Colour(0xff78818a));
        if (drawMode)
            g.drawText("Drag paints notes  -  B to select", hint, juce::Justification::centredLeft);
        else if (velocity >= -1)
            g.drawText("Hold V + Up/Down or wheel", hint, juce::Justification::centredLeft);
        else
            g.drawText("Drag to select  -  double-click a cell for a note", hint, juce::Justification::centredLeft);
    }
}

}
