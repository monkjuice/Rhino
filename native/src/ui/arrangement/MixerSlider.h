#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace rhino
{
// A track card's fader or pan bar. A right-click opens what the card offers
// for it, its automation, rather than reaching the slider: a bar jumps to
// wherever it is pressed, so the right button moved the level as the left one
// does. Every event of that press is swallowed, the release included.
class MixerSlider final : public juce::Slider
{
public:
    std::function<void()> onRightClick;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            juce::Slider::mouseDown(event);
        else if (onRightClick)
            onRightClick();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            juce::Slider::mouseDrag(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            juce::Slider::mouseUp(event);
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            juce::Slider::mouseDoubleClick(event);
    }
};
}
