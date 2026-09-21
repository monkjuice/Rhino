#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>

namespace rhino
{
// One lane per channel, the way Live draws a clip: left above right, each with
// its own zero line, and a hairline of space between them so a stereo file
// reads as two lanes rather than as one taller waveform.
//
// JUCE's own AudioThumbnail::drawChannels already divides the height per
// channel, so the split is not what this adds. What it adds is the separation:
// drawn edge to edge at the heights an arrangement lane offers, two channels
// merge into a single block and a stereo clip is indistinguishable from a mono
// one. The zero line does the same work in the other direction - a passage
// that is nearly silent still shows the lane it belongs to.
//
// Shared by the arrangement and the audio clip editor, which is why it is a
// plain header rather than a member of either.
inline void paintWaveformLanes(juce::Graphics& g, juce::AudioThumbnail& thumbnail,
                               juce::Rectangle<int> area, double startSeconds, double endSeconds,
                               float verticalZoom, juce::Colour waveColour)
{
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;
    const auto channels = std::max(1, thumbnail.getNumChannels());
    // A gap only where there is height to spare. Below that the lanes are
    // better off touching than trimmed to nothing, which is what a collapsed
    // track row would otherwise do to them.
    const auto gap = channels > 1 && area.getHeight() >= channels * 8 ? 2 : 0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto top = area.getY() + (channel * area.getHeight()) / channels;
        const auto bottom = area.getY() + ((channel + 1) * area.getHeight()) / channels;
        auto lane = juce::Rectangle<int>(area.getX(), top, area.getWidth(), bottom - top);
        if (channel > 0)
            lane = lane.withTrimmedTop(gap);
        if (lane.getHeight() <= 0)
            continue;
        // The zero line goes down first so the waveform is drawn over it
        // rather than through it.
        g.setColour(waveColour.withMultipliedAlpha(0.30f));
        g.fillRect(lane.getX(), lane.getCentreY(), lane.getWidth(), 1);
        g.setColour(waveColour);
        thumbnail.drawChannel(g, lane, startSeconds, endSeconds, channel, verticalZoom);
    }
}
}
