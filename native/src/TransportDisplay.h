#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theta
{
// Visualization-only transport readout. Its arrow is the sole interactive
// surface so the display cannot accidentally seek or take editor focus.
class TransportDisplay final : public juce::Component
{
public:
    TransportDisplay();
    void paint(juce::Graphics&) override;
    void resized() override;
    void setDisplayText(const juce::String&);
    const juce::String& getDisplayText() const { return text; }
    std::function<void()> configurationRequested;

private:
    juce::String text;
    juce::TextButton configuration {"v"};
};
}
