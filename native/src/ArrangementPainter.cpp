#include "ArrangementInternal.h"
#include "Playhead.h"
#include "Theme.h"
#include <optional>
#include <set>

// Arrangement rendering.

namespace rhino
{

void Arrangement::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    g.setFont(uiFont(10.0f));
    g.setColour(juce::Colour(0xff8a969f));
    drawSnappedText(g, "Drop browser items or files / drag clips to move / trim edges",
                    {360, 0, getWidth() - 370, 30});
    for (int index = 0; index < static_cast<int>(rows.size()); ++index)
    {
        const auto row = rowBounds(index);
        // A row folded into a collapsed group is laid out at no height, so it
        // draws nothing here and the band above it speaks for it.
        if (row.getHeight() <= 0.0f) continue;
        if (row.getBottom() < lanesTop || row.getY() > lanesTop + laneContentHeight()) continue;
        if (rows[static_cast<size_t>(index)].automation >= 0)
        {
            paintGhostRow(g, index);
            continue;
        }
        const auto track = rows[static_cast<size_t>(index)].track;
        g.setColour(juce::Colour(track == 0 ? 0xff262e36 : 0xff222a31));
        g.fillRect(row.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f));
        // The card is two columns with the panel grey between them: the
        // controls keep the panel background, and the name sits on the track
        // colour. The clips on the track keep whatever colours they were given.
        // A card inside a group starts at its indent instead of at the edge.
        const auto indent = trackIndent(track);
        const auto nameColumn = juce::Rectangle<float>(indent + cardControlsWidth + cardDividerWidth, row.getY(),
                                                       headerWidth - indent - cardControlsWidth - cardDividerWidth,
                                                       row.getHeight());
        const auto colour = session.trackColour(track);
        const auto cardColour = colour.isTransparent() ? juce::Colour(0xff41505d) : colour;
        g.setColour(cardColour);
        g.fillRect(nameColumn);
        g.setColour(juce::Colour(0xff39434b));
        g.fillRect(indent + cardControlsWidth, row.getY(), cardDividerWidth, row.getHeight());
        paintGroupGutter(g, track, row);
        if (isTrackSelected(track))
        {
            // Highlighted and selected are the same state: every card in the
            // selection is washed whatever the last click was, and the strip
            // only says which of them the rest of the app is working on.
            g.setColour(juce::Colour(0x12ffffff));
            g.fillRect(row.withX(0.0f).withWidth(headerWidth));
            if (track == selectedTrack)
            {
                g.setColour(juce::Colour(0xffc6d58c));
                g.fillRect(row.withX(0.0f).withWidth(3.0f));
            }
        }
        // The name holds the top line of the card whatever height the row is
        // dragged to, level with the two buttons beside it. Dark text on a
        // light card, light on a dark one, so every colour stays readable. A
        // card being renamed gives that line to the editor instead.
        g.setColour(cardColour.contrasting(0.8f));
        g.setFont(uiFontBold(10.0f));
        if (renamingTrack != track)
        {
            // Clipped rather than shrunk to fit: a scaled-down line lands on a
            // fractional em again, which is the blur this is avoiding.
            const auto nameArea = trackNameBounds(track);
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(nameArea);
            drawSnappedText(g, juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track), nameArea);
        }
        g.setFont(uiFont(10.0f));
    }

    // Every row is bounded, header and timeline alike, so a track reads as one
    // band across the whole panel rather than as a card beside loose lanes.
    {
        const auto laneBottom = masterLane().getY();
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, static_cast<int>(lanesTop), getWidth() - 14,
                                                std::max(1, static_cast<int>(laneContentHeight()))));
        for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        {
            const auto row = rowBounds(index);
            if (row.getHeight() <= 0.0f) continue;
            if (row.getBottom() < lanesTop || row.getY() > laneBottom) continue;
            const auto ownRow = rows[static_cast<size_t>(index)].automation < 0;
            g.setColour(juce::Colour(ownRow ? 0xff39434b : 0xff2c353c));
            g.drawHorizontalLine(static_cast<int>(row.getBottom()) - 1, 0.0f, static_cast<float>(getWidth()) - 14.0f);
        }
        g.setColour(juce::Colour(0xff39434b));
        g.drawVerticalLine(static_cast<int>(headerWidth) - 1, lanesTop, laneBottom);
    }

    // The master row is pinned below the lanes. It takes no clips, so its lane
    // is empty; only its header carries anything.
    {
        const auto master = masterLane();
        g.setColour(juce::Colour(0xff191f24));
        g.fillRect(master);
        g.setColour(juce::Colour(0xff3a434b));
        g.drawHorizontalLine(static_cast<int>(master.getY()), 0.0f, master.getRight());
        g.setColour(juce::Colour(isMasterSelected() ? 0xff343f47 : 0xff222930));
        g.fillRect(master.withWidth(headerWidth));
        if (isMasterSelected())
        {
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(master.withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.setFont(uiFontBold(9.0f));
        drawSnappedText(g, "MAIN", {10, static_cast<int>(master.getY()) + 5, 44, 16});
    }

    const auto firstBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(viewStart)).inBeats();
    const auto lastBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(viewStart + viewSpan)).inBeats();
    const auto gridBeat = resolvedGridBeats();
    const auto firstGrid = std::floor(firstBeat / gridBeat) * gridBeat;
    int paintedTicks = 0;
    for (auto beat = firstGrid; beat <= lastBeat + gridBeat && paintedTicks++ < 2000; beat += gridBeat)
    {
        const auto time = session.edit->tempoSequence.toTime(tracktion::core::BeatPosition::fromBeats(beat)).inSeconds();
        const auto x = xFor(time);
        const auto bar = isGridLine(beat, session.beatsPerBar());
        const auto wholeBeat = isGridLine(beat, 1.0);
        // The ruler's divisions carry on through the main row, which is what
        // ties it to the timeline above it. They start at the lanes rather than
        // in the track headers, which the row before them has already painted.
        if ((gridSettings.mode != GridMode::off || bar) && x >= headerWidth)
        {
            g.setColour(bar ? juce::Colour(0xff42515c) : wholeBeat ? juce::Colour(0xff35404a) : juce::Colour(0xff29323a));
            g.drawVerticalLine(static_cast<int>(x), static_cast<int>(lanesTop), masterLane().getBottom());
        }
        if (bar)
        {
            const auto barNumber = static_cast<int>(std::floor(beat / session.beatsPerBar())) + 1;
            g.setColour(juce::Colour(0xff8c99a4));
            g.setFont(uiFont(10.0f));
            drawSnappedText(g, juce::String(barNumber) + ".1",
                            {static_cast<int>(x) + 4, static_cast<int>(rulerTop), 64, 24});
        }
    }
    {
        const auto loopRange = session.edit->getTransport().getLoopRange();
        auto start = loopGesture != LoopGesture::none ? loopPreviewStart : loopRange.getStart().inSeconds();
        auto end = loopGesture != LoopGesture::none ? loopPreviewEnd : loopRange.getEnd().inSeconds();
        if (end < start) std::swap(start, end);
        if ((loopGesture != LoopGesture::none || session.hasManualLoopRange()) && end - start > 0.02)
        {
            const auto x1 = xFor(start);
            const auto x2 = xFor(end);
            juce::Rectangle<float> loopBounds {std::max(headerWidth, std::min(x1, x2)), rulerTop,
                                               std::max(0.0f, std::min(std::max(x1, x2), static_cast<float>(getWidth() - 14)) - std::max(headerWidth, std::min(x1, x2))),
                                               getHeight() - rulerTop - 18.0f};
            if (!loopBounds.isEmpty())
            {
                g.setColour(juce::Colour(0x245ab9d6));
                g.fillRect(loopBounds);
                g.setColour(juce::Colour(0xff5ab9d6));
                g.fillRect(loopBounds.withHeight(3.0f));
                g.drawVerticalLine(static_cast<int>(loopBounds.getX()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
                g.drawVerticalLine(static_cast<int>(loopBounds.getRight()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
            }
        }
    }
    const auto dirty = g.getClipBounds().toFloat();
    std::set<int> tracksWithClips;
    for (const auto& clip : clips)
    {
        tracksWithClips.insert(clip.track);
        const auto paintTrack = displayedTrack(clip);
        const auto box = bounds(clip);
        const auto visible = box.getIntersection(lane(paintTrack))
            .getIntersection({0.0f, lanesTop, static_cast<float>(getWidth() - 14), laneContentHeight()});
        if (visible.isEmpty() || !visible.intersects(dirty)) continue;
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, static_cast<int>(lanesTop), getWidth() - 14,
                                                std::max(1, static_cast<int>(laneContentHeight()))));
        const auto fallback = juce::Colour(clip.track == 0 ? 0xff414c34 : 0xff284b59);
        const auto label = clip.colour.isTransparent() ? fallback : clip.colour;
        g.setColour(label.withAlpha(isSelected(clip.id) ? 0.82f : 0.68f));
        g.fillRect(box);
        g.setColour(label.brighter(0.55f));
        g.fillRect(box.withHeight(4.0f));
        g.setColour(juce::Colour(isSelected(clip.id) ? 0xffdce9b1 : 0xff617985));
        g.drawRect(box.reduced(0.5f), isSelected(clip.id) ? 2.0f : 1.0f);
        g.setColour(juce::Colour(0xffe0e7ec));
        if (visible.getWidth() >= 24.0f)
            g.drawText(clip.name, visible.reduced(6.0f, 0).withHeight(23.0f), juce::Justification::centredLeft, true);
        if (clip.clipPlugins > 0)
        {
            const auto badge = visible.withSizeKeepingCentre(28.0f, 16.0f).withRightX(visible.getRight() - 5.0f).withY(visible.getY() + 5.0f);
            g.setColour(juce::Colour(0xcc15191d));
            g.fillRect(badge);
            g.setColour(label.brighter(0.75f));
            g.drawRect(badge.reduced(0.5f), 1.0f);
            g.setColour(juce::Colour(0xffeaf0f3));
            g.drawText("FX" + juce::String(clip.clipPlugins), badge, juce::Justification::centred, true);
        }
        const auto position = displayedPosition(clip);
        if (clip.waveform)
        {
            auto waveArea = visible.withTop(box.getY() + 26.0f).reduced(0, 5).getSmallestIntegerContainer();
            if (clip.waveform->thumbnail.getTotalLength() > 0.0)
            {
                const auto start = (position.offset + std::max(0.0, timeAt(visible.getX()) - position.start)) * clip.speed;
                const auto end = start + visible.getWidth() / lane(0).getWidth() * viewSpan * clip.speed;
                g.setColour(juce::Colour(0xff8cc5d2));
                // A modest display-only lift keeps low-amplitude and steady tones legible
                // at arrangement zoom without changing the source audio or clip gain.
                clip.waveform->thumbnail.drawChannels(g, waveArea, start, end, 1.45f);
            }
            else
            {
                g.setColour(juce::Colour(0xffa1b1b9));
                g.drawText(clip.waveform->readable ? "Reading waveform..." : "Missing or unreadable audio",
                           waveArea.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            juce::Graphics::ScopedSaveState clipContentScope(g);
            g.reduceClipRegion(visible.getSmallestIntegerContainer());
            const auto noteArea = box.withTop(box.getY() + 28.0f).reduced(6.0f, 5.0f);
            g.setColour(juce::Colour(0x553f4837));
            for (int step = 1; step < Session::steps; ++step)
            {
                const auto x = xFor(position.start + step * (position.end - position.start) / Session::steps);
                if (x > noteArea.getX() && x < noteArea.getRight())
                    g.drawVerticalLine(static_cast<int>(x), noteArea.getY(), noteArea.getBottom());
            }
            auto lowPitch = Session::lowestNote;
            auto highPitch = Session::lowestNote + Session::pitches - 1;
            for (const auto& note : clip.midiNotes)
            {
                lowPitch = std::min(lowPitch, note.pitch);
                highPitch = std::max(highPitch, note.pitch);
            }
            for (const auto& note : clip.midiNotes)
            {
                const auto x1 = xFor(position.start + note.start - clip.position.start);
                const auto x2 = xFor(position.start + note.end - clip.position.start);
                const auto w = std::max(3.0f, x2 - x1);
                const auto pitchScale = static_cast<float>(note.pitch - lowPitch)
                    / static_cast<float>(std::max(1, highPitch - lowPitch));
                const auto h = std::max(4.0f, noteArea.getHeight() / Session::pitches - 1.0f);
                const auto y = noteArea.getBottom() - h - pitchScale * (noteArea.getHeight() - h);
                const juce::Rectangle<float> noteBox {x1, y, w, h};
                if (!noteBox.intersects(visible)) continue;
                g.setColour(juce::Colour(0xffc6d58c));
                g.fillRect(noteBox);
                g.setColour(juce::Colour(0xffe8f1bd));
                g.drawRect(noteBox.reduced(0.5f), 1.0f);
            }
            if (clip.midiNotes.empty())
            {
                g.setColour(juce::Colour(0xff9daa7e));
                g.drawText("Edit notes below", noteArea, juce::Justification::centredLeft, true);
            }
        }
    }
    // The hint belongs on the first track that could take audio, which is any
    // empty track without an instrument rather than a fixed lane index.
    for (int track = 0; track < session.trackCount(); ++track)
        if (!tracksWithClips.contains(track) && !session.trackHasInstrument(track) && !isTrackHidden(track)
            && !session.isGroupBusTrack(track))
        {
            g.setColour(juce::Colour(0xff75828e));
            g.drawText("Drop audio here", lane(track).reduced(16, 0), juce::Justification::centredLeft);
            break;
        }
    // Curves sit on top of the clips they modulate, and are clipped to the
    // scrolling lane area so a scrolled-off row cannot draw into the ruler.
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(headerWidth), static_cast<int>(lanesTop),
                                                std::max(1, getWidth() - static_cast<int>(headerWidth) - 14),
                                                std::max(1, static_cast<int>(laneContentHeight()))));
        for (int index = 0; index < static_cast<int>(rows.size()); ++index)
        {
            const auto row = rowBounds(index);
            if (row.getHeight() <= 0.0f) continue;
            if (row.getBottom() < lanesTop || row.getY() > lanesTop + laneContentHeight()) continue;
            paintAutomationRow(g, index);
        }
    }
    // Where a carried track would land if it were dropped now.
    if (movingTrack >= 0 && moveStarted && moveDestination >= 0)
    {
        const auto target = lane(moveDestination);
        const auto y = moveDestination > movingTrack ? target.getBottom() : target.getY();
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(0.0f, y - 1.0f, static_cast<float>(getWidth()) - 14.0f, 2.0f);
    }
    if (marqueeSelecting)
    {
        g.setColour(juce::Colour(0x285ab9d6));
        g.fillRect(marqueeBounds);
        g.setColour(juce::Colour(0xff5ab9d6));
        g.drawRect(marqueeBounds, 1.0f);
    }
    if (playhead >= headerWidth)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, rulerTop, 2.0f, getHeight() - rulerTop - 18.0f);
    }
}

}
