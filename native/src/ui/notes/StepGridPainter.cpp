#include "StepGridInternal.h"
#include "ui/shared/Playhead.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Grid rendering.

namespace rhino
{

juce::Rectangle<float> StepGrid::repaintArea(const juce::Graphics& g) const
{
    return cullRepaints ? g.getClipBounds().toFloat() : getLocalBounds().toFloat();
}

// The lanes are one row per semitone, so the keys are drawn the way a piano
// looks from directly above rather than in true piano geometry: a black key
// keeps its own full-height row and is only narrower than its neighbours.
//
// The keys are narrow, and the names stand in a column of their own to their
// left, the octaves' Cs only, so a run of keys reads at a glance; the key
// under the pointer is named there too. Until 2026-10-25 the names were drawn
// on keys twice as wide, every one of them once a row was 15 px tall. A drum
// clip's rows are pads, and each is named across the whole strip.
void StepGrid::paintKeyboard(juce::Graphics& g)
{
    const auto rows = visiblePitchRows();
    const auto height = rowHeight();
    const auto drums = session.isPatternDrums();
    const auto keysLeft = drums ? 0.0f : keyLabelWidth;
    const auto width = labelWidth - 2.0f - keysLeft;
    const auto blackKeyWidth = std::floor(width * 0.6f);
    // A name needs a row tall enough to hold it.
    const auto named = height >= octaveLabelRowHeight;
    // Rounded to a whole pixel per em rather than following the row height
    // continuously: a lane height dragged between two integers would otherwise
    // rasterise every pad name at a fractional em, which is the one thing
    // uiFont exists to avoid. The octaves' names keep one size.
    g.setFont(drums ? uiFont(std::round(std::clamp(height - 3.0f, 7.5f, 11.0f) * 0.825f)) : uiFont(9.0f));
    const auto dirty = repaintArea(g);
    // The top row, cut short, reaches up behind the loop and preview buttons,
    // and a hovered key's name past the rows either side of it.
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<float>(0.0f, headerHeight, labelWidth, rowAreaHeight()).getSmallestIntegerContainer());
    const auto rowsStart = rowsTop();
    for (int row = 0; row < rows; ++row)
    {
        const auto pitch = pitchOfRow(row);
        const auto top = rowsStart + static_cast<float>(row) * height;
        const auto key = juce::Rectangle<float>(keysLeft, top, width, height);
        // A name can stand a little taller than a squeezed row.
        if (!dirty.intersects(juce::Rectangle<float>(0.0f, top, labelWidth, height).expanded(0.0f, height)))
            continue;
        const bool black = juce::MidiMessage::isMidiNoteBlack(pitch);
        // A key the track is playing is lit in the playhead's blue, as the
        // playhead marks the time now: a black key on its own width, so it
        // never reads as the white key beside it.
        const bool sounding = soundingKeys.test(static_cast<size_t>(pitch));
        if (drums)
        {
            // A row is named for the sound on its pad of the Drum Rack, and
            // a row whose pad is empty reads as its note; a pad being played
            // is lit as a key is.
            const auto padName = session.patternNoteName(pitch);
            const bool namedDrum = padName.has_value();
            g.setColour(sounding ? palette::keySounding
                                 : juce::Colour(namedDrum ? 0xff3a3325 : black ? 0xff15191e : 0xff30373e));
            g.fillRect(key.reduced(0.0f, 1.0f));
            if (named)
            {
                g.setColour(sounding ? palette::keyHoverText : juce::Colour(namedDrum ? 0xffffc16a : 0xffbac2ca));
                drawSnappedText(g, padName.value_or(pitchName(pitch)), key.toNearestInt(),
                                juce::Justification::centred, true);
            }
            continue;
        }
        // A black key is laid over the white surface rather than replacing it:
        // what shows to its right is the white key it sits between.
        g.setColour(!black && sounding ? palette::keySounding : juce::Colour(0xffcdd3c8));
        g.fillRect(key);
        if (black)
        {
            g.setColour(sounding ? palette::keySounding : juce::Colour(0xff15191e));
            g.fillRect(key.withWidth(blackKeyWidth));
        }
        // The seam between two keys: it reads on a white key and disappears
        // into a black one, which is what the eye expects from above.
        g.setColour(juce::Colour(0xff2b3138));
        g.fillRect(key.withTop(key.getBottom() - 1.0f));
        if (!named || pitchClassOf(pitch) != 0)
            continue;
        // Middle C is C3 here, matching the drum rows and Forge's keyboard —
        // see StepGridInternal.h for why they have to agree.
        g.setColour(palette::textDim);
        drawSnappedText(g, pitchName(pitch), juce::Rectangle<float>(0.0f, top, keyLabelWidth - 3.0f, height).toNearestInt(),
                        juce::Justification::centredRight);
    }
    // The key under the pointer, or of the note in hand, is lit in the note
    // colour and named however squeezed the rows are, so the pitch of any
    // cell is read by pointing at it: on the piano the key lights and its name
    // stands in the names' column, on a drum clip the pad's row lights with
    // its name. Last, so that on a thin row the name can stand over the rows
    // either side. The column is as wide as the longest name, C#-2, needs.
    const auto hoveredRow = rowOfPitch(hoveredPitch);
    if (hoveredPitch < 0 || hoveredRow < 0 || hoveredRow >= rows)
        return;
    const auto hoveredKey = juce::Rectangle<float>(keysLeft, rowsStart + static_cast<float>(hoveredRow) * height, width, height);
    const auto tag = (drums ? hoveredKey : hoveredKey.withX(0.0f).withWidth(keyLabelWidth - 1.0f))
                         .withSizeKeepingCentre(drums ? width : keyLabelWidth - 1.0f, std::max(height, keyTagHeight));
    if (!dirty.intersects(tag.getUnion(hoveredKey)))
        return;
    g.setColour(palette::noteBody);
    if (!drums)
        g.fillRect(hoveredKey);
    g.fillRect(tag);
    g.setColour(palette::keyHoverText);
    g.setFont(uiFontBold(9.0f));
    drawSnappedText(g, rowName(hoveredPitch), tag.reduced(2.0f, 0.0f).toNearestInt(),
                    drums ? juce::Justification::centred : juce::Justification::centredRight, true);
}

juce::String StepGrid::rowName(int pitch) const
{
    return session.isPatternDrums() ? session.patternNoteName(pitch).value_or(pitchName(pitch)) : pitchName(pitch);
}

// A beat is the signature's own note value - a quarter in 4/4, an eighth in
// 6/8 - and the resolution is steps to a whole note, a power of two from 16,
// so it always divides by a denominator of up to 16.
int StepGrid::stepsPerBeat() const
{
    return std::max(1, session.editorStepResolution() / std::max(1, session.timeSignature().denominator));
}

int StepGrid::stepsPerBar() const
{
    return stepsPerBeat() * std::max(1, session.timeSignature().numerator);
}

// Bars and beats, counted from the clip's own start as Live's clip ruler is:
// a bar's number at its line, its beats as bar.beat while each has room for
// one, and a tick under every beat, taller at a bar. Bars are numbered every
// one, two, four... as the zoom closes them up. Zoomed in until a beat is
// wider than half the view, the steps between are named as bar.beat.step too,
// so the ruler never stands empty. A label either edge would cut is left out,
// because the tail of a number reads as another number.
void StepGrid::paintRuler(juce::Graphics& g, juce::Rectangle<float> dirty)
{
    const auto steps = session.editorStepCount();
    const auto perBeat = stepsPerBeat();
    const auto beatsInBar = std::max(1, session.timeSignature().numerator);
    const auto beatWidth = cellWidth() * static_cast<float>(perBeat);
    auto barStride = 1;
    while (beatWidth * static_cast<float>(beatsInBar * barStride) < 30.0f && barStride < 4096)
        barStride *= 2;
    const auto beatsNamed = beatWidth >= 36.0f;
    const auto stepsNamed = perBeat > 1 && beatWidth > gridWidth() * 0.5f && cellWidth() >= 36.0f;
    const auto stride = stepsNamed ? 1 : perBeat;
    const auto firstStep = std::max(0, static_cast<int>(std::floor(stepScroll / stride)) * stride);
    const auto lastStep = std::min(steps, static_cast<int>(std::ceil(stepScroll + visibleStepSpan())));
    g.setFont(uiFont(10.0f));
    for (int step = firstStep; step <= lastStep; step += stride)
    {
        // Rounded as the grid's own lines are, so a tick stands on its line.
        const auto x = static_cast<float>(juce::roundToInt(xForStep(static_cast<double>(step))));
        const auto beat = step / perBeat;
        const auto bar = beat / beatsInBar;
        const auto onBeat = step % perBeat == 0;
        const auto onBar = onBeat && beat % beatsInBar == 0;
        const auto tickTop = headerHeight - (onBar ? 8.0f : onBeat ? 4.0f : 2.0f);
        if (dirty.intersects(juce::Rectangle<float>(x - 1.0f, tickTop, 3.0f, headerHeight - tickTop)))
        {
            g.setColour(onBar ? juce::Colour(0xff78818a) : palette::border);
            g.fillRect(juce::Rectangle<float>(x, tickTop, 1.0f, headerHeight - tickTop));
        }
        if (onBar ? bar % barStride != 0 : onBeat && !beatsNamed)
            continue;
        const auto text = onBar ? juce::String(bar + 1)
                        : juce::String(bar + 1) + "." + juce::String(beat % beatsInBar + 1)
                              + (onBeat ? juce::String() : "." + juce::String(step % perBeat + 1));
        const juce::Rectangle<float> label {x + 3.0f, 0.0f,
                                            juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 1.0f,
                                            headerHeight - 6.0f};
        if (label.getX() < labelWidth || label.getRight() > gridRight() || !dirty.intersects(label))
            continue;
        g.setColour(juce::Colour(onBar ? 0xffd4dacd : 0xff78818a));
        drawSnappedText(g, text, label.toNearestInt(), juce::Justification::centredLeft);
    }
}

// The loop button's box, as a TextButton paints it, with headphones in place
// of its text: a band over two cups, in the colour the box's text would take.
void StepGrid::PreviewButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground(g, *this, findColour(getToggleState() ? buttonOnColourId : buttonColourId),
                                          highlighted, down);
    const auto box = getLocalBounds().toFloat();
    const auto side = std::min(box.getWidth(), box.getHeight()) * 0.58f;
    const auto area = box.withSizeKeepingCentre(side, side);
    const auto centreX = area.getCentreX(), centreY = area.getY() + side * 0.55f, radius = side * 0.42f;
    juce::Path band;
    band.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                       -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour(findColour(getToggleState() ? textColourOnId : textColourOffId));
    g.strokePath(band, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (const auto cup : {centreX - radius, centreX + radius})
        g.fillRoundedRectangle({cup - side * 0.13f, centreY - side * 0.04f, side * 0.26f, side * 0.45f}, side * 0.08f);
}

void StepGrid::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setFont(uiFont(10.0f));
    // What this repaint has to cover. The playhead invalidates a strip a few
    // pixels wide every display refresh, below the header and right of the
    // keys, and everything outside it is skipped rather than drawn and thrown
    // away: the clip region discards the pixels, but not the text layout spent
    // producing them.
    const auto dirty = repaintArea(g);
    const auto steps = session.editorStepCount();
    const auto firstVisibleStep = std::max(0, static_cast<int>(std::floor(stepScroll)));
    const auto lastVisibleStep = std::min(steps - 1, static_cast<int>(std::ceil(stepScroll + visibleStepSpan())));
    // cell() works the clip's length out through the tempo map on every call -
    // four conversions - and a paint asks for thousands of cells. The same
    // arithmetic on sizes read once gives the same rectangles.
    const auto stepWidth = cellWidth();
    const auto laneHeight = rowHeight();
    const auto top = rowsTop();
    const auto cellAt = [this, stepWidth, laneHeight, top](int step, int row)
    {
        return juce::Rectangle<float> {labelWidth + static_cast<float>(step - stepScroll) * stepWidth,
                                       top + row * laneHeight, stepWidth, laneHeight};
    };
    // The steps whose cells can touch the repainted columns, one either side
    // to be sure of a cell the rounding puts on the boundary.
    const auto dirtyFirstStep = std::max(firstVisibleStep,
        static_cast<int>(std::floor(stepScroll + (dirty.getX() - labelWidth) / stepWidth)) - 1);
    const auto dirtyLastStep = std::min(lastVisibleStep,
        static_cast<int>(std::ceil(stepScroll + (dirty.getRight() - labelWidth) / stepWidth)) + 1);
    const auto rows = visiblePitchRows();
    // Everything on the time axis is cut at the keys' edge, up to the velocity
    // lane, which cuts its own. A step scrolled part-way into view starts left
    // of it, and zoomed in, one step is wider than the keys: its cells, notes
    // and number were painted over the whole keyboard and under the loop
    // button, which is how the keys vanished.
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<float>(labelWidth, 0.0f, gridWidth(), timeAreaBottom())
                           .getSmallestIntegerContainer());
    if (dirty.getY() < headerHeight)
        paintRuler(g, dirty);
    // And the rows are cut at the ruler, which the top one reaches up behind
    // when the panel cuts it short.
    g.reduceClipRegion(juce::Rectangle<float>(labelWidth, headerHeight, gridWidth(), rowAreaHeight())
                           .getSmallestIntegerContainer());
    const auto perBeat = stepsPerBeat();
    const auto perBar = stepsPerBar();
    const auto drums = session.isPatternDrums();
    const bool scaleEnabled = !drums && scaleShown;
    // The same mask Rhino Tune corrects to and the Push lays its pads in.
    const auto scale = maskForScale(scaleKind, scaleRoot);
    for (int row = 0; row < rows; ++row)
    {
        const auto pitch = pitchOfRow(row);
        const bool inScale = !scaleEnabled || scale[static_cast<size_t>(pitchClassOf(pitch))];
        // Cells and notes stay inside their row's height, so a row the repaint
        // misses has nothing to draw.
        const auto rowTop = top + row * laneHeight;
        if (rowTop + laneHeight < dirty.getY() || rowTop > dirty.getBottom())
            continue;
        // A black key's row is darker, as the keys beside it are; a drum
        // clip's rows are pads, not keys.
        const auto blackKey = !drums && juce::MidiMessage::isMidiNoteBlack(pitch);
        for (int step = dirtyFirstStep; step <= dirtyLastStep; ++step)
        {
            const auto bounds = cellAt(step, row);
            if (!dirty.intersects(bounds)) continue;
            const auto lit = step / perBeat % 2 == 0;
            g.setColour(blackKey ? (lit ? palette::noteCellBlackKey : palette::noteCellBlackKeyAlternate)
                                 : (lit ? palette::noteCell : palette::noteCellAlternate));
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
            // Live's velocity shading: a quieter note sinks towards the cells,
            // so a phrase's dynamics read without opening the lane; 127 is the
            // full colour, and 1 still stands clear of the grid.
            const auto quiet = 0.6f * (1.0f - static_cast<float>(std::clamp(note.velocity, 1, 127) - 1) / 126.0f);
            const auto shade = [quiet](juce::Colour colour) { return colour.interpolatedWith(palette::noteCell, quiet); };
            g.setColour(shade(selected ? palette::noteSelected : palette::noteBody));
            g.fillRect(bounds);
            g.setColour(selected ? juce::Colour(0x77482d15) : juce::Colour(0x55363f46));
            g.fillRect(bounds.withWidth(1.0f));
            g.setColour(shade(selected ? juce::Colour(0xffffe3a3) : juce::Colour(0xffe8f2aa)));
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
        // A top row cut short keeps its line at the ruler's edge.
        const auto y = std::max(headerHeight, top + row * laneHeight);
        g.fillRect(juce::Rectangle<float>(labelWidth, std::floor(y), gridWidth(), 1.0f));
    }
    for (int row = 0; row < rows; ++row)
    {
        const auto rowTop = top + row * laneHeight;
        const auto rowBottom = top + (row + 1) * laneHeight;
        if (rowBottom < dirty.getY() || rowTop > dirty.getBottom())
            continue;
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

        // Counted from the first step whatever the repaint covers, because
        // whether a line is under a note depends on every note before it.
        int sustainedNotes = 0;
        for (int step = 0; step <= dirtyLastStep + 1; ++step)
        {
            sustainedNotes += sustainedBoundaryDeltas[static_cast<size_t>(step)];
            if (step >= dirtyFirstStep && sustainedNotes == 0)
            {
                const auto x = cellAt(step, 0).getX();
                g.setColour(step % perBar == 0 ? palette::noteBarLine
                          : step % perBeat == 0 ? palette::noteBeatLine : palette::noteStepLine);
                g.drawVerticalLine(juce::roundToInt(x), rowTop, rowBottom);
            }
        }
    }
    g.restoreState();
    // After the header's numbers, which are drawn in the font set above: the
    // keyboard sets its own.
    if (dirty.getX() < labelWidth)
        paintKeyboard(g);
    // Under everything that crosses the whole time area: the loop band, the
    // playhead, the insert line and the marquee all run through the lane.
    paintVelocityLane(g, dirty, stepWidth, dirtyFirstStep, dirtyLastStep);
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
            const auto range = session.loopRange();
            loopStartX = loopXForTimelineTime(range.getStart().inSeconds());
            loopEndX = loopXForTimelineTime(range.getEnd().inSeconds());
        }
        const auto left = std::max(labelWidth, std::min(loopStartX, loopEndX));
        const auto right = std::min(gridRight(), std::max(loopStartX, loopEndX));
        if (right > left)
        {
            const juce::Rectangle<float> loopBounds {left, 0.0f, right - left, timeAreaBottom()};
            g.setColour(juce::Colour(0x245ab9d6));
            g.fillRect(loopBounds);
            g.setColour(juce::Colour(0xff5ab9d6));
            g.fillRect(loopBounds.withHeight(3.0f));
            g.drawVerticalLine(juce::roundToInt(left), 0.0f, headerHeight);
            g.drawVerticalLine(juce::roundToInt(right), 0.0f, headerHeight);
        }
    }
    if (playhead >= 0)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, headerHeight, 2.0f, timeAreaBottom() - headerHeight);
    }
    // Over the notes and under the marquee: where a paste will land.
    paintInsertLine(g);
    if ((gesture == Gesture::select || gesture == Gesture::velocitySelect) && !selectionBox.isEmpty())
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
    if (subdivisionActive && subdivisionCount >= 1)
    {
        // Over the note, but never over the keys when the note starts off the
        // left of the view.
        const auto centre = std::max(subdivisionSourceBounds.getCentreX(), labelWidth + 19.0f);
        const auto badge = juce::Rectangle<float>(centre - 17.0f, subdivisionSourceBounds.getY() - 28.0f, 34.0f, 22.0f);
        g.setColour(juce::Colour(0xffe5e8df));
        g.fillRoundedRectangle(badge, 3.0f);
        g.setColour(juce::Colour(0xff252a30));
        g.drawRoundedRectangle(badge, 3.0f, 1.0f);
        g.setFont(uiFontBold(10.5f));
        drawSnappedText(g, juce::String(subdivisionCount), badge.toNearestInt(), juce::Justification::centred);
    }
    const auto footer = footerBounds();
    if (dirty.intersects(footer))
    {
        g.setColour(juce::Colour(velocityAdjustActive ? 0xff29343b : 0xff171c21));
        g.fillRect(footer);
        g.setColour(juce::Colour(0xff3b4650));
        g.fillRect(footer.withHeight(1.0f));
        const auto velocity = selectedVelocity();
        const auto value = velocity >= 0 ? juce::String(velocity)
                                         : velocity == -1 ? juce::String("MIXED") : juce::String("-");
        g.setFont(uiFontBold(9.5f));
        g.setColour(velocityAdjustActive ? juce::Colour(0xffe9a84a) : juce::Colour(0xffb8c4aa));
        drawSnappedText(g, "VELOCITY  " + value, footer.reduced(9.0f, 2.0f).toNearestInt(),
                        juce::Justification::centredRight);
        // The draw, velocity and loop toggles sit at the left of the footer,
        // so the hint starts past them rather than underneath them.
        const auto hint = footer.withTrimmedLeft(178.0f).reduced(9.0f, 2.0f);
        g.setFont(uiFont(8.5f));
        g.setColour(juce::Colour(0xff78818a));
        // The lane is named only while it is on screen.
        const auto laneShowing = velocityLaneHeight() > 0.0f;
        if (drawMode)
            drawSnappedText(g, laneShowing ? "Drag paints notes, or velocities in the lane  -  B to select"
                                           : "Drag paints notes  -  B to select",
                            hint.toNearestInt(), juce::Justification::centredLeft, true);
        else if (velocity >= -1)
            drawSnappedText(g, laneShowing ? "Drag a marker, Ctrl+Up/Down, or hold V + wheel"
                                           : "Ctrl+Up/Down, or hold V + Up/Down or wheel",
                            hint.toNearestInt(), juce::Justification::centredLeft, true);
        else
            drawSnappedText(g, "Drag to select  -  double-click a cell for a note  -  right-click for options",
                            hint.toNearestInt(), juce::Justification::centredLeft, true);
    }
}

}
