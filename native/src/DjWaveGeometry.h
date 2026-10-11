#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// The arithmetic of a deck's screen (DjDeckDisplay), kept apart from the
// component so it can be checked without a Session, as ClipGeometry.h is.

namespace rhino::djwave
{
// How far the strip has scrolled: the deck's position in device pixels of
// the strip's timeline, whole. Everything the strip draws is placed from
// this one number, so the waveform, its grid and the marks on it move
// together by whole device pixels, and a frame that moves the deck less than
// one draws nothing new.
inline long long scrollFor(double seconds, double pixelsPerSecond, double scale)
{
    return static_cast<long long>(std::llround(seconds * pixelsPerSecond * scale));
}

// The tiles, `tileWidth` device pixels each and the first at the top of the
// material, that the timeline from `left` up to `right` touches.
struct TileRange
{
    long long first = 0, last = -1;
    bool contains(long long index) const { return index >= first && index <= last; }
};

inline TileRange tilesTouching(double left, double right, int tileWidth)
{
    if (right <= left || tileWidth <= 0) return {};
    return {static_cast<long long>(std::floor(left / tileWidth)),
            static_cast<long long>(std::ceil(right / tileWidth)) - 1};
}

// A note of the material as the strip draws it.
struct NoteSpan
{
    double startBeat = 0.0, endBeat = 0.0;
    int note = 0, velocity = 0;
};

// Every note-on paired with the next note-off of its pitch after it, or
// running a quarter beat when none comes, sorted by where the notes start.
// Worked out once for a material rather than for every tile painted from it.
// Events are anything with beat, note, velocity and on, in beat order.
template <typename Event>
std::vector<NoteSpan> noteSpans(const std::vector<Event>& events)
{
    std::vector<NoteSpan> spans;
    std::vector<std::vector<size_t>> waiting(128);
    for (const auto& event : events)
    {
        if (event.note < 0 || event.note > 127) continue;
        auto& open = waiting[static_cast<size_t>(event.note)];
        if (event.on)
        {
            open.push_back(spans.size());
            spans.push_back({event.beat, event.beat + 0.25, event.note, event.velocity});
            continue;
        }
        for (const auto index : open)
            spans[index].endBeat = event.beat;
        open.clear();
    }
    std::stable_sort(spans.begin(), spans.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.startBeat < b.startBeat; });
    return spans;
}
}
