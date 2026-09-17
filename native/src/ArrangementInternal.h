#pragma once
#include "Arrangement.h"

// Shared internals of the Arrangement implementation, which spans
// Arrangement.cpp, ArrangementGeometry.cpp, ArrangementPainter.cpp,
// ArrangementSync.cpp, ArrangementGestures.cpp and ArrangementDrops.cpp.
// Waveform lives here because sync() creates them and Arrangement destroys
// them, so both translation units need the complete type.

namespace rhino
{

struct Arrangement::Waveform final : juce::ChangeListener
{
    Waveform(Arrangement& a, const juce::File& file)
        : owner(a), thumbnail(512, a.formats, a.thumbnailCache)
    {
        thumbnail.addChangeListener(this);
        readable = thumbnail.setSource(new juce::FileInputSource(file));
    }
    ~Waveform() override { thumbnail.removeChangeListener(this); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        for (const auto& clip : owner.clips)
            if (clip.waveform == this)
                owner.repaint(owner.bounds(clip).getIntersection(owner.lane(clip.track)).getSmallestIntegerContainer());
    }
    Arrangement& owner;
    juce::AudioThumbnail thumbnail;
    bool readable = false;
};

}
