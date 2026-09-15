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
    // The chevron paints its glyph only. A plain TextButton would carry the
    // look-and-feel's rounded background and outline inside the readout.
    struct GlyphButton final : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton(juce::Graphics&, bool highlighted, bool pressed) override;
    };

    juce::String text;
    GlyphButton configuration {"v"};
};
}
