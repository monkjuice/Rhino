#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

// Arrangement rendering.

namespace theta
{

void Arrangement::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(juce::Colour(0xff8a969f));
    g.drawText("Drop browser items or files / drag clips to move / trim edges",
               360, 0, getWidth() - 370, 30, juce::Justification::centredLeft);
    for (int index = 0; index < static_cast<int>(rows.size()); ++index)
    {
        const auto row = rowBounds(index);
        if (row.getBottom() < lanesTop || row.getY() > lanesTop + laneContentHeight()) continue;
        if (rows[static_cast<size_t>(index)].automation >= 0)
        {
            paintGhostRow(g, index);
            continue;
        }
        const auto track = rows[static_cast<size_t>(index)].track;
        g.setColour(juce::Colour(track == 0 ? 0xff242b31 : 0xff20272e));
        g.fillRect(row.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f));
        if (track == selectedTrack)
        {
            // Two different things. The strip marks the track the rest of the
            // app is working on, which follows a clip click. The wash marks the
            // card itself as the selected object, which Delete acts on, and a
            // clip and a track card are never selected together.
            if (focus == Focus::track)
            {
                g.setColour(juce::Colour(0xff343f47));
                g.fillRect(row.withX(0.0f).withWidth(headerWidth));
            }
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(row.withX(0.0f).withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.drawText(juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                   10, static_cast<int>(row.getY()) + 4, static_cast<int>(headerWidth) - 20, 20,
                   juce::Justification::centredLeft);
    }

    // The master row is pinned below the lanes. It takes no clips, so its lane
    // is empty; only its header carries anything.
    {
        const auto master = masterLane();
        g.setColour(juce::Colour(0xff191f24));
        g.fillRect(master);
        g.setColour(juce::Colour(0xff3a434b));
        g.drawHorizontalLine(static_cast<int>(master.getY()), 0.0f, master.getRight());
        g.setColour(juce::Colour(isMasterSelected() && focus == Focus::track ? 0xff343f47 : 0xff222930));
        g.fillRect(master.withWidth(headerWidth));
        if (isMasterSelected())
        {
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(master.withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("MAIN", 10, static_cast<int>(master.getY()) + 2, 120, 16, juce::Justification::centredLeft);
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
        if (gridSettings.mode != GridMode::off || bar)
        {
            g.setColour(bar ? juce::Colour(0xff42515c) : wholeBeat ? juce::Colour(0xff35404a) : juce::Colour(0xff29323a));
            g.drawVerticalLine(static_cast<int>(x), static_cast<int>(lanesTop), masterLane().getY());
        }
        if (bar)
        {
            const auto barNumber = static_cast<int>(std::floor(beat / session.beatsPerBar())) + 1;
            g.setColour(juce::Colour(0xff8c99a4));
            g.drawText(juce::String(barNumber) + ".1", static_cast<int>(x) + 4, static_cast<int>(rulerTop),
                       64, 24, juce::Justification::centredLeft);
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
                                                std::max(1, getHeight() - static_cast<int>(lanesTop) - 18)));
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
        if (!tracksWithClips.contains(track) && !session.trackHasInstrument(track))
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
            if (row.getBottom() < lanesTop || row.getY() > lanesTop + laneContentHeight()) continue;
            paintAutomationRow(g, index);
        }
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
