#include "ArrangementInternal.h"
#include "ui/shared/WaveformLanes.h"
#include <algorithm>
#include <cmath>

// How the clips are drawn. Split from ArrangementPainter.cpp, which keeps the
// rows, the grid, the rulers and the selection. Same class, two translation
// units - no header, signature or call site changes.
//
// A clip is drawn piece by piece (displayedPieces): one piece, itself, until
// the selection is carried, when a clip the selection runs across is drawn as
// the part that travels, at its previewed place, and the parts that stay
// behind either side of it.

namespace rhino
{

void Arrangement::paintClips(juce::Graphics& g)
{
    const auto dirty = repaintArea(g);
    // The face every clip's name is set in, and measured in below.
    g.setFont(uiFont(10.0f));
    for (const auto& clip : clips)
    {
        const auto pieces = displayedPieces(clip);
        for (int piece = 0; piece < pieces.count; ++piece)
            paintClipPiece(g, clip, pieces.items[static_cast<size_t>(piece)], dirty);
    }
}

void Arrangement::paintClipPiece(juce::Graphics& g, const ClipView& clip, const DisplayedPiece& piece,
                                 juce::Rectangle<float> dirty)
{
    const auto paintTrack = piece.track;
    const auto position = piece.position;
    const auto box = boundsOf(position, paintTrack);
    const auto visible = box.getIntersection(laneOrSlot(paintTrack))
        .getIntersection({headerWidth, lanesTop, static_cast<float>(getWidth()) - 14.0f - headerWidth, laneContentHeight()});
    if (visible.isEmpty() || !visible.intersects(dirty)) return;
    juce::Graphics::ScopedSaveState scope(g);
    // Clipped to the lanes, not the panel: a clip that starts left of the view
    // is filled from its true start, which would otherwise run across the cards.
    g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(headerWidth), static_cast<int>(lanesTop),
                                            std::max(1, getWidth() - static_cast<int>(headerWidth) - 14),
                                            std::max(1, static_cast<int>(laneContentHeight()))));
    // A clip wears the colour of the lane it is drawn on, so the two read as
    // one band; a clip that has been coloured by hand keeps its own. Darkened,
    // because a card is a solid block behind dark text and a clip is a
    // translucent fill behind light text, and the same value cannot do both.
    // Read from the facts sync gathered: trackColour walks the edit's track
    // list, and a busy arrangement asks this hundreds of times a frame.
    const auto laneColour = factsFor(paintTrack).colour;
    const auto laneTint = laneColour.isTransparent() ? palette::control.brighter(paintTrack == 0 ? 0.12f : 0.06f)
                                                     : laneColour.darker(0.5f);
    // A muted clip keeps its place and plays nothing, so it is drawn grey,
    // as Live draws a clip it has switched off.
    const auto own = clip.colour.isTransparent() ? laneTint : clip.colour;
    const auto label = clip.muted ? own.withSaturation(0.0f) : own;
    // Outlined as selected when the selection holds the whole clip, and while
    // the selection is carried the piece that travels with it, which will be
    // a whole clip inside it once it lands. What stays behind is outside it.
    const auto highlighted = piece.carried || isWhollySelected(clip);
    g.setColour(label.withAlpha(highlighted ? 0.82f : 0.68f));
    g.fillRect(box);
    g.setColour(label.brighter(0.55f));
    g.fillRect(box.withHeight(4.0f));
    g.setColour(highlighted ? palette::selection : palette::border.brighter(0.25f));
    g.drawRect(box.reduced(0.5f), highlighted ? 2.0f : 1.0f);
    // The two rows the pointer reads, drawn so they can be seen: the strip
    // along the top is the clip itself and carries its name, and the row
    // under the line belongs to the timeline.
    const auto headerHeight = clipHeaderHeight(box.getHeight());
    g.setColour(label.darker(0.75f));
    g.fillRect(visible.getX(), box.getY() + headerHeight, visible.getWidth(), 1.0f);
    // A looped clip's passes, Live's notches: a short tick down from its top
    // where each pass of the loop begins, so a clip repeating its pattern reads
    // apart from one only dragged long. The passes run from the window's
    // origin, the clip's start less its offset, which counts into the loop.
    // Left out where they would stand closer than a few pixels.
    if (clip.loopSeconds > 0.0)
    {
        const auto origin = position.start - position.offset;
        if (xFor(origin + clip.loopSeconds) - xFor(origin) >= 4.0f)
        {
            const auto left = std::max(visible.getX(), dirty.getX()) - 2.0f;
            const auto right = std::min(visible.getRight(), dirty.getRight()) + 2.0f;
            const auto from = std::max(position.start, timeAt(left));
            // The first pass to begin after the clip's own start edge.
            auto pass = std::floor((from - origin) / clip.loopSeconds - 1.0e-9) + 1.0;
            for (auto at = origin + pass * clip.loopSeconds; at < position.end - 1.0e-6; at = origin + ++pass * clip.loopSeconds)
            {
                const auto x = xFor(at);
                if (x > right)
                    break;
                if (at > position.start + 1.0e-6)
                    g.fillRect(juce::Rectangle<float>(std::round(x) - 1.0f, box.getY(), 2.0f, 6.0f));
            }
        }
    }
    g.setColour(palette::text);
    if (visible.getWidth() >= 24.0f)
    {
        // Set left, so the name's ink stops near its own width: a repaint
        // right of that has nothing of it to draw. Shaping a name is the
        // dearest thing a clip draws, and a playhead strip crosses a clip
        // on every lane, so each name is measured once and kept. The
        // margins cover the rounding to whole pixels and a glyph's overhang.
        const auto nameArea = visible.reduced(6.0f, 0).withHeight(headerHeight);
        auto& width = clipNameWidths[clip.name];
        if (width <= 0.0f)
            width = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), clip.name) + 1.0f;
        const auto inkLeft = nameArea.getX() - 3.0f;
        const auto inkRight = nameArea.getX() + std::min(nameArea.getWidth(), width) + 3.0f;
        if (dirty.getRight() >= inkLeft && dirty.getX() <= inkRight)
            drawSnappedText(g, clip.name, nameArea.toNearestInt(), juce::Justification::centredLeft, true);
    }
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
    if (clip.waveform)
    {
        auto waveArea = visible.withTop(box.getY() + 26.0f).reduced(0, 5).getSmallestIntegerContainer();
        if (clip.waveform->thumbnail.getTotalLength() > 0.0)
        {
            const auto start = (position.offset + std::max(0.0, timeAt(visible.getX()) - position.start)) * clip.speed;
            const auto end = start + visible.getWidth() / lane(0).getWidth() * viewSpan * clip.speed;
            // A modest display-only lift keeps low-amplitude and steady tones legible
            // at arrangement zoom without changing the source audio or clip gain.
            const auto wave = juce::Colour(0xff8cc5d2);
            paintWaveformLanes(g, clip.waveform->thumbnail, waveArea, start, end, 1.45f,
                               clip.muted ? wave.withSaturation(0.0f).withMultipliedAlpha(0.5f) : wave);
        }
        else
        {
            g.setColour(palette::textDim);
            drawSnappedText(g, clip.waveform->readable ? "Reading waveform..." : "Missing or unreadable audio",
                            waveArea.reduced(6, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
        return;
    }
    juce::Graphics::ScopedSaveState clipContentScope(g);
    g.reduceClipRegion(visible.getSmallestIntegerContainer());
    const auto noteArea = box.withTop(box.getY() + 28.0f).reduced(6.0f, 5.0f);
    // No grid of its own. A clip used to rule itself into sixteen equal
    // steps, which agreed with the timeline only when the clip happened
    // to be one bar long at a 1/16 grid and crossed it everywhere else.
    // The clip is filled translucent, so the arrangement's own lines
    // carry through it and a MIDI clip reads in the same columns as the
    // audio beside it.
    static const std::vector<MidiNoteView> noNotes;
    const auto& notes = clip.midiNotes != nullptr ? *clip.midiNotes : noNotes;
    auto lowPitch = Session::lowestNote;
    auto highPitch = Session::lowestNote + Session::pitches - 1;
    for (const auto& note : notes)
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
    // right until mouse-up drew it again. A piece cut from a clip is a
    // window of its own onto the same sequence, drawn the same way.
    const auto origin = position.start - position.offset;
    for (const auto& note : notes)
    {
        const auto x1 = xFor(note.start + origin);
        const auto x2 = xFor(note.end + origin);
        const auto w = std::max(3.0f, x2 - x1);
        const auto pitchScale = static_cast<float>(note.pitch - lowPitch)
            / static_cast<float>(std::max(1, highPitch - lowPitch));
        const auto h = std::max(4.0f, noteArea.getHeight() / Session::pitches - 1.0f);
        const auto y = noteArea.getBottom() - h - pitchScale * (noteArea.getHeight() - h);
        const juce::Rectangle<float> noteBox {x1, y, w, h};
        // Against the repaint too: a playhead strip crosses a clip on
        // every lane, and each would otherwise issue all its notes.
        if (!noteBox.intersects(visible) || !noteBox.intersects(dirty)) continue;
        const auto ink = clip.muted ? palette::textDim : palette::selection;
        g.setColour(ink);
        g.fillRect(noteBox);
        g.setColour(ink.brighter(0.3f));
        g.drawRect(noteBox.reduced(0.5f), 1.0f);
    }
}

}
