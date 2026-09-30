#include "ArrangementInternal.h"
#include "Playhead.h"
#include "Theme.h"
#include "WaveformLanes.h"
#include <optional>
#include <set>

// Arrangement rendering.

namespace rhino
{

// Bar numbers, drawn from the bars themselves rather than from whichever grid
// line happened to land on one. Stepping by the snap division made the ruler
// read in whatever the grid was set to; stepping by bars keeps the numbering
// consecutive and the reading musical at every zoom.
// Bars are grouped and every other group is washed, which is what makes a 3/4
// project read as threes and a 4/4 as fours - the beat lines alone look the
// same in both. The wash goes on before the clips rather than over them: a
// clip is filled translucent, so it picks the band up through its own colour.
int Arrangement::paintBarBands(juce::Graphics& g, double firstBeat, double lastBeat)
{
    const auto barLength = std::max(0.25, session.beatsPerBar());
    const auto timeOfBar = [this, barLength](double bar)
    {
        return session.edit->tempoSequence
            .toTime(tracktion::core::BeatPosition::fromBeats(bar * barLength)).inSeconds();
    };
    // Counted from bar zero rather than from the left edge of the view, so
    // which group is washed is a property of the music and does not flip as
    // the arrangement is scrolled.
    const auto firstBar = std::floor(firstBeat / barLength);
    const auto lastBar = std::floor(lastBeat / barLength);
    // A tempo ramp makes bars unequal in pixels; the group size is picked from
    // the first bar on screen, exactly as the bar numbering's step is.
    const auto pixelsPerBar = static_cast<double>(xFor(timeOfBar(firstBar + 1.0)) - xFor(timeOfBar(firstBar)));
    const auto band = static_cast<double>(barsPerBand(pixelsPerBar));
    const auto pair = band * 2.0;
    const auto right = static_cast<float>(getWidth()) - 14.0f;
    const auto top = lanesTop;
    const auto bottom = masterLane().getBottom();
    if (right <= headerWidth || bottom <= top) return static_cast<int>(band);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<float>(headerWidth, top, right - headerWidth, bottom - top)
                           .getSmallestIntegerContainer());
    g.setColour(juce::Colours::white.withAlpha(barBandWash));
    int painted = 0;
    for (auto bar = std::floor(firstBar / pair) * pair; bar <= lastBar + pair && painted < 512; bar += pair)
    {
        const auto x1 = xFor(timeOfBar(bar + band));
        const auto x2 = xFor(timeOfBar(bar + pair));
        ++painted;
        if (x2 < headerWidth) continue;
        if (x1 > right) break;
        g.fillRect(juce::Rectangle<float>(x1, top, x2 - x1, bottom - top));
    }
    return static_cast<int>(band);
}

void Arrangement::paintBarNumbers(juce::Graphics& g, double firstBeat, double lastBeat)
{
    const auto barLength = std::max(0.25, session.beatsPerBar());
    const auto timeOfBar = [this, barLength](double bar)
    {
        return session.edit->tempoSequence
            .toTime(tracktion::core::BeatPosition::fromBeats((bar - 1.0) * barLength)).inSeconds();
    };
    const auto firstBar = std::max(1.0, std::floor(firstBeat / barLength) + 1.0);
    const auto lastBar = std::floor(lastBeat / barLength) + 1.0;
    if (lastBar < firstBar) return;
    // A tempo ramp makes bars unequal in pixels, so the step is picked from the
    // width of the first one on screen and the labels simply thin out or crowd
    // a little where the tempo moves.
    const auto pixelsPerBar = std::max(0.01f, xFor(timeOfBar(firstBar + 1.0)) - xFor(timeOfBar(firstBar)));
    double step = 1.0;
    while (step * pixelsPerBar < 44.0f && step < 4096.0) step *= 2.0;
    const auto start = std::floor((firstBar - 1.0) / step) * step + 1.0;
    const auto right = static_cast<float>(getWidth()) - 14.0f;
    int painted = 0;
    for (auto bar = start; bar <= lastBar + step && painted < 512; bar += step)
    {
        const auto x = xFor(timeOfBar(bar));
        if (x < headerWidth - 1.0f) continue;
        if (x > right) break;
        ++painted;
        g.setColour(palette::border.brighter(0.2f));
        g.drawVerticalLine(static_cast<int>(x), rulerTop + 2.0f, lanesTop);
        g.setColour(palette::textDim);
        g.setFont(uiFont(10.0f));
        drawSnappedText(g, juce::String(static_cast<juce::int64>(bar)),
                        {static_cast<int>(x) + 4, static_cast<int>(rulerTop), 64, 24});
    }
}

// Every row's band and its card. The clip region is the whole point: a row
// dragged tall enough to reach past the bottom of the lanes used to paint its
// card the full height it was given, so the track colour ran on down through
// the main row and the scrollbar strip under it.
void Arrangement::paintTrackCards(juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(juce::Rectangle<int>(0, static_cast<int>(lanesTop), getWidth() - 14,
                                            std::max(1, static_cast<int>(laneContentHeight()))));
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
        // One ground for every lane. The first track used to be lifted a shade,
        // which under the neutral palette landed it on exactly palette::minorGrid
        // - so the subdivisions drawn over it were the colour of the lane they
        // were drawn on and track one had no grid at all at any zoom the beat
        // lines did not already cover.
        g.setColour(palette::arrangement);
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
        const auto cardColour = colour.isTransparent() ? palette::control.brighter(0.18f) : colour;
        g.setColour(cardColour);
        g.fillRect(nameColumn);
        g.setColour(palette::border);
        g.fillRect(indent + cardControlsWidth, row.getY(), cardDividerWidth, row.getHeight());
        paintGroupGutter(g, track, row);
        if (isTrackArmed(track))
        {
            // A card too short to show its buttons still has to say it is
            // armed, so the edge of the header carries it too.
            g.setColour(palette::recordAccent.darker(0.2f));
            g.fillRect(row.withX(headerWidth - 3.0f).withWidth(3.0f));
        }
        if (isTrackSelected(track))
        {
            // Highlighted and selected are the same state: every card in the
            // selection is washed whatever the last click was, and the strip
            // only says which of them the rest of the app is working on.
            g.setColour(juce::Colour(0x12ffffff));
            g.fillRect(row.withX(0.0f).withWidth(headerWidth));
            if (track == selectedTrack)
            {
                g.setColour(palette::selection);
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
            // One pass of the SemiBold cut, exactly as every other label in the
            // app is drawn. It used to be struck twice to make the name heavier
            // than the card's other text, and that is what made it the one
            // string in the interface that looked soft: a second composite over
            // the same glyphs takes a half-covered edge pixel from 50% to 75%,
            // so the antialiasing fringe darkens into the stem and the whole
            // line reads as blurred rather than bold. Weight, if the name needs
            // more of it, comes from the em size.
            // Elided rather than shrunk to fit: a scaled-down line lands on a
            // fractional em again, which is the blur this is avoiding.
            const auto nameArea = trackNameBounds(track);
            drawSnappedText(g, juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                            nameArea, juce::Justification::centredLeft, true);
        }
        g.setFont(uiFont(10.0f));
    }
}

void Arrangement::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setFont(uiFont(10.0f));
    g.setColour(palette::textDim);
    drawSnappedText(g, "Drop browser items or files / drag clips to move / trim edges",
                    {360, 0, getWidth() - 370, 30});
    paintTrackCards(g);

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
            g.setColour(ownRow ? palette::border : palette::minorGrid);
            // Two pixels rather than one: at a single pixel the divisions read
            // as a tone change between lanes rather than as a line ruled
            // between them, and the bands stopped separating at a glance.
            g.fillRect(0.0f, row.getBottom() - trackDividerThickness,
                       static_cast<float>(getWidth()) - 14.0f, trackDividerThickness);
        }
        g.setColour(palette::border);
        g.fillRect(headerWidth - trackDividerThickness, lanesTop, trackDividerThickness, laneBottom - lanesTop);
    }

    // The master row is pinned below the lanes. It takes no clips, so its lane
    // is empty; only its header carries anything.
    {
        const auto master = masterLane();
        // The row owns everything from its top edge to the foot of the panel,
        // scrollbar strip included, and fills all of it. Filling only the row
        // itself left an eighteen pixel gap that nothing else painted, so
        // whatever the last lane happened to be showing came through it.
        const juce::Rectangle<float> band {0.0f, master.getY(), static_cast<float>(getWidth()),
                                           std::max(master.getHeight(),
                                                    getHeight() - bottomInset - master.getY())};
        g.setColour(palette::appBackground);
        g.fillRect(band);
        g.setColour(isMasterSelected() ? palette::hover : palette::sideSurface);
        g.fillRect(band.withWidth(headerWidth));
        if (isMasterSelected())
        {
            g.setColour(palette::selection);
            g.fillRect(band.withWidth(3.0f));
        }
        // Last, so it rules the whole width: drawn before the header column it
        // stopped at the cards and the main row read as closed off over the
        // lanes and open beside them.
        g.setColour(palette::border);
        g.fillRect(band.withHeight(trackDividerThickness));
        g.setColour(palette::text);
        g.setFont(uiFontBold(9.0f));
        drawSnappedText(g, "MAIN", {10, static_cast<int>(master.getY()) + 5, 44, 16});
    }

    const auto firstBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(viewStart)).inBeats();
    const auto lastBeat = session.edit->tempoSequence.toBeats(tracktion::core::TimePosition::fromSeconds(viewStart + viewSpan)).inBeats();
    const auto bandBars = paintBarBands(g, firstBeat, lastBeat);
    const auto barLength = std::max(0.25, session.beatsPerBar());
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
            // A line inside a washed band is lifted by the wash, so it keeps
            // the contrast it was picked for. Held flat, the subdivisions
            // matched the washed lane almost exactly and the alternating bands
            // read as columns with nothing in them. The epsilon keeps a line
            // sitting on a bar boundary in the bar it opens rather than in the
            // one before it, which floating point otherwise decides at random.
            const auto line = bar ? palette::border.brighter(0.12f) : wholeBeat ? palette::border : palette::minorGrid;
            g.setColour(isWashedBar(std::floor(beat / barLength + 0.000001), bandBars)
                            ? line.interpolatedWith(juce::Colours::white, barBandWash)
                            : line);
            g.drawVerticalLine(static_cast<int>(x), static_cast<int>(lanesTop), masterLane().getBottom());
        }
    }
    paintBarNumbers(g, firstBeat, lastBeat);
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
                                               getHeight() - bottomInset - rulerTop - 18.0f};
            if (!loopBounds.isEmpty())
            {
                g.setColour(palette::activeNeutral.withAlpha(0.1f));
                g.fillRect(loopBounds);
                g.setColour(palette::activeNeutral);
                g.fillRect(loopBounds.withHeight(3.0f));
                g.drawVerticalLine(static_cast<int>(loopBounds.getX()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
                g.drawVerticalLine(static_cast<int>(loopBounds.getRight()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
            }
        }
    }
    const auto dirty = g.getClipBounds().toFloat();
    // A clip wears the colour of the lane it is drawn on, so the two read as
    // one band; a clip that has been coloured by hand keeps its own. Darkened,
    // because a card is a solid block behind dark text and a clip is a
    // translucent fill behind light text, and the same value cannot do both.
    // Read per paint rather than per clip: trackColour walks the edit's track
    // list, and a busy arrangement asks this hundreds of times a frame.
    std::vector<juce::Colour> laneColours;
    laneColours.reserve(static_cast<size_t>(session.trackCount()));
    for (int track = 0; track < session.trackCount(); ++track)
        laneColours.push_back(session.trackColour(track));
    const auto laneTint = [&laneColours](int track)
    {
        const auto own = juce::isPositiveAndBelow(track, static_cast<int>(laneColours.size()))
                             ? laneColours[static_cast<size_t>(track)] : juce::Colour();
        return own.isTransparent() ? palette::control.brighter(track == 0 ? 0.12f : 0.06f) : own.darker(0.5f);
    };
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
        const auto label = clip.colour.isTransparent() ? laneTint(paintTrack) : clip.colour;
        g.setColour(label.withAlpha(isSelected(clip.id) ? 0.82f : 0.68f));
        g.fillRect(box);
        g.setColour(label.brighter(0.55f));
        g.fillRect(box.withHeight(4.0f));
        g.setColour(isSelected(clip.id) ? palette::selection : palette::border.brighter(0.25f));
        g.drawRect(box.reduced(0.5f), isSelected(clip.id) ? 2.0f : 1.0f);
        // The two rows the pointer reads, drawn so they can be seen: the strip
        // along the top is the clip itself and carries its name, and the row
        // under the line belongs to the timeline.
        const auto headerHeight = clipHeaderHeight(box.getHeight());
        g.setColour(label.darker(0.75f));
        g.fillRect(visible.getX(), box.getY() + headerHeight, visible.getWidth(), 1.0f);
        g.setColour(palette::text);
        if (visible.getWidth() >= 24.0f)
            drawSnappedText(g, clip.name, visible.reduced(6.0f, 0).withHeight(headerHeight).toNearestInt(),
                            juce::Justification::centredLeft, true);
        if (clip.clipPlugins > 0)
        {
            const auto badge = visible.withSizeKeepingCentre(28.0f, 16.0f).withRightX(visible.getRight() - 5.0f).withY(visible.getY() + 5.0f);
            g.setColour(palette::appBackground.withAlpha(0.8f));
            g.fillRect(badge);
            g.setColour(label.brighter(0.75f));
            g.drawRect(badge.reduced(0.5f), 1.0f);
            g.setColour(juce::Colours::white);
            drawSnappedText(g, "FX" + juce::String(clip.clipPlugins), badge.toNearestInt(),
                            juce::Justification::centred, true);
        }
        const auto position = displayedPosition(clip);
        if (clip.waveform)
        {
            auto waveArea = visible.withTop(box.getY() + 26.0f).reduced(0, 5).getSmallestIntegerContainer();
            if (clip.waveform->thumbnail.getTotalLength() > 0.0)
            {
                const auto start = (position.offset + std::max(0.0, timeAt(visible.getX()) - position.start)) * clip.speed;
                const auto end = start + visible.getWidth() / lane(0).getWidth() * viewSpan * clip.speed;
                // A modest display-only lift keeps low-amplitude and steady tones legible
                // at arrangement zoom without changing the source audio or clip gain.
                paintWaveformLanes(g, clip.waveform->thumbnail, waveArea, start, end, 1.45f,
                                   juce::Colour(0xff8cc5d2));
            }
            else
            {
                g.setColour(palette::textDim);
                drawSnappedText(g, clip.waveform->readable ? "Reading waveform..." : "Missing or unreadable audio",
                                waveArea.reduced(6, 0).toNearestInt(), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            juce::Graphics::ScopedSaveState clipContentScope(g);
            g.reduceClipRegion(visible.getSmallestIntegerContainer());
            const auto noteArea = box.withTop(box.getY() + 28.0f).reduced(6.0f, 5.0f);
            // No grid of its own. A clip used to rule itself into sixteen equal
            // steps, which agreed with the timeline only when the clip happened
            // to be one bar long at a 1/16 grid and crossed it everywhere else.
            // The clip is filled translucent, so the arrangement's own lines
            // carry through it and a MIDI clip reads in the same columns as the
            // audio beside it.
            auto lowPitch = Session::lowestNote;
            auto highPitch = Session::lowestNote + Session::pitches - 1;
            for (const auto& note : clip.midiNotes)
            {
                lowPitch = std::min(lowPitch, note.pitch);
                highPitch = std::max(highPitch, note.pitch);
            }
            // A note is fixed in the sequence the clip is a window onto, so what
            // carries it is the window's origin rather than its start. Moving a
            // clip slides both together and the notes travel with it; trimming
            // the start advances the start and the offset by the same amount
            // over a sequence that has not moved, so the notes that survive
            // stay exactly where they were. Carrying them by the start alone
            // slid them right through a left trim and pushed the last of them
            // off the clip's own end, so the preview appeared to crop from the
            // right until mouse-up drew it again.
            const auto noteShift = (position.start - position.offset)
                                 - (clip.position.start - clip.position.offset);
            for (const auto& note : clip.midiNotes)
            {
                const auto x1 = xFor(note.start + noteShift);
                const auto x2 = xFor(note.end + noteShift);
                const auto w = std::max(3.0f, x2 - x1);
                const auto pitchScale = static_cast<float>(note.pitch - lowPitch)
                    / static_cast<float>(std::max(1, highPitch - lowPitch));
                const auto h = std::max(4.0f, noteArea.getHeight() / Session::pitches - 1.0f);
                const auto y = noteArea.getBottom() - h - pitchScale * (noteArea.getHeight() - h);
                const juce::Rectangle<float> noteBox {x1, y, w, h};
                if (!noteBox.intersects(visible)) continue;
                g.setColour(palette::selection);
                g.fillRect(noteBox);
                g.setColour(palette::selection.brighter(0.3f));
                g.drawRect(noteBox.reduced(0.5f), 1.0f);
            }
            if (clip.midiNotes.empty())
            {
                g.setColour(palette::textDim);
                drawSnappedText(g, "Edit notes below", noteArea.toNearestInt(),
                                juce::Justification::centredLeft, true);
            }
        }
    }
    // The hint belongs on the first empty track, and says what that track is
    // for rather than assuming audio: a MIDI track wants an instrument.
    for (int track = 0; track < session.trackCount(); ++track)
        if (!tracksWithClips.contains(track) && !session.trackHasInstrument(track) && !isTrackHidden(track)
            && !session.isGroupBusTrack(track))
        {
            g.setColour(palette::textDim);
            drawSnappedText(g, session.trackType(track) == Session::TrackType::midi
                                ? "Double-click to add a clip, or drop an instrument here"
                                : "Drop audio here",
                            lane(track).reduced(16, 0).toNearestInt(), juce::Justification::centredLeft, true);
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
        g.setColour(palette::selection);
        g.fillRect(0.0f, y - 1.0f, static_cast<float>(getWidth()) - 14.0f, 2.0f);
    }
    // What is being recorded, while it is being recorded. The engine writes no
    // clip until the transport stops, so without this the armed lane would sit
    // empty through the take and the whole thing would appear at the end. The
    // band runs from where recording started to the playhead, on the tracks
    // that are actually capturing.
    if (session.isRecording() && session.recordingStartSeconds() >= 0.0)
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(headerWidth), static_cast<int>(lanesTop),
                                                std::max(1, getWidth() - static_cast<int>(headerWidth) - 14),
                                                std::max(1, static_cast<int>(laneContentHeight()))));
        const auto x1 = xFor(session.recordingStartSeconds());
        const auto x2 = std::max(x1, playhead);
        for (int track = 0; track < session.trackCount(); ++track)
        {
            if (!isTrackArmed(track) || isTrackHidden(track)) continue;
            const auto row = lane(track);
            if (row.getHeight() <= 0.0f) continue;
            const juce::Rectangle<float> band {x1, row.getY(), std::max(2.0f, x2 - x1), row.getHeight() - 1.0f};
            g.setColour(palette::recordAccent.withAlpha(0.22f));
            g.fillRect(band);
            g.setColour(palette::recordAccent);
            g.fillRect(band.withWidth(2.0f));
            // The notes as they are played. They are drawn in the colour and
            // the layout the clip will use, so the take does not change
            // appearance at the moment it becomes a clip.
            const auto& live = session.recordingNotes(track);
            if (live.empty() || band.getHeight() < 10.0f)
                continue;
            auto lowPitch = Session::lowestNote;
            auto highPitch = Session::lowestNote + Session::pitches - 1;
            for (const auto& note : live)
            {
                lowPitch = std::min(lowPitch, note.pitch);
                highPitch = std::max(highPitch, note.pitch);
            }
            const auto noteArea = band.withTrimmedTop(std::min(14.0f, band.getHeight() * 0.25f)).reduced(0.0f, 3.0f);
            const auto noteHeight = std::max(3.0f, noteArea.getHeight() / Session::pitches - 1.0f);
            for (const auto& note : live)
            {
                const auto noteLeft = xFor(note.startSeconds);
                const auto noteRight = note.isHeld() ? std::max(noteLeft + 2.0f, x2)
                                                     : std::max(noteLeft + 2.0f, xFor(note.endSeconds));
                const auto pitchScale = static_cast<float>(note.pitch - lowPitch)
                    / static_cast<float>(std::max(1, highPitch - lowPitch));
                const juce::Rectangle<float> noteBox {
                    noteLeft, noteArea.getBottom() - noteHeight - pitchScale * (noteArea.getHeight() - noteHeight),
                    noteRight - noteLeft, noteHeight};
                if (!noteBox.intersects(dirty))
                    continue;
                g.setColour(palette::selection);
                g.fillRect(noteBox);
            }
        }
    }
    // Over the clips: the region is the thing the commands act on, so it has
    // to read as covering what it contains.
    paintTimeSelection(g);
    if (playhead >= headerWidth)
    {
        g.setColour(playheadColour);
        // Only a running transport draws through the lanes. Stopped, the line
        // is a mark in the bar ruler saying where play would start from.
        const auto bottom = playheadSweepsLanes ? getHeight() - bottomInset - 18.0f : lanesTop;
        g.fillRect(playhead, rulerTop, 2.0f, bottom - rulerTop);
    }
    // The strip the clip and device panes float over, in the shell's own
    // background. The panel spans the window so that its lanes keep their
    // height, which leaves it painting the ground the toggle strip and the
    // gaps inside the pane stand on - and those belong to the shell.
    if (bottomInset > 0.0f)
    {
        g.setColour(palette::appBackground);
        g.fillRect(0.0f, getHeight() - bottomInset, static_cast<float>(getWidth()), bottomInset);
    }
}

}
