#pragma once
#include "Arrangement.h"
#include "SelectionInput.h"

// Shared internals of the Arrangement implementation, which spans
// Arrangement.cpp, ArrangementGeometry.cpp, ArrangementPainter.cpp,
// ArrangementSync.cpp, ArrangementGestures.cpp and ArrangementDrops.cpp.
// Waveform lives here because sync() creates them and Arrangement destroys
// them, so both translation units need the complete type.
//
// The pointer rules come from SelectionInput.h, which the note editor reads
// too. Only what is peculiar to a timeline lives here.

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

// The colour grid the track menu and the group menu both show. The palette is
// one grid rather than a list, so a colour is picked by where it sits, the way
// it is in the DAWs this borrows from. A menu item outlives the call that
// opened it, so it carries what to do with the colour rather than a reference
// to the thing being coloured.
struct TrackSwatches final : public juce::PopupMenu::CustomComponent
{
    static constexpr int columns = 8, cell = 18;

    TrackSwatches(juce::Colour current, std::function<void(juce::Colour)> onPick)
        : selected(current), apply(std::move(onPick))
    {
        setSize(columns * cell + 12, rowCount() * cell + 12);
    }

    static int rowCount()
    {
        return (static_cast<int>(Session::trackColourPalette().size()) + columns - 1) / columns;
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = columns * cell + 12;
        idealHeight = rowCount() * cell + 12;
    }

    juce::Rectangle<int> swatchBounds(int index) const
    {
        return {6 + index % columns * cell, 6 + index / columns * cell, cell - 2, cell - 2};
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = Session::trackColourPalette();
        for (int i = 0; i < static_cast<int>(palette.size()); ++i)
        {
            const auto box = swatchBounds(i);
            g.setColour(palette[static_cast<size_t>(i)]);
            g.fillRect(box);
            const auto isSelected = palette[static_cast<size_t>(i)] == selected;
            g.setColour(juce::Colour(isSelected ? 0xffe8eef2 : 0xff161b20));
            g.drawRect(box, isSelected ? 2 : 1);
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        const auto& palette = Session::trackColourPalette();
        for (int i = 0; i < static_cast<int>(palette.size()); ++i)
            if (swatchBounds(i).contains(event.getPosition()))
            {
                if (apply) apply(palette[static_cast<size_t>(i)]);
                break;
            }
        triggerMenuItem();
    }

    juce::Colour selected;
    std::function<void(juce::Colour)> apply;
};

}
