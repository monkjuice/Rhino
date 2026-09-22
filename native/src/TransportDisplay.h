#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
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
    // The second line carries the readings that are watched rather than read:
    // wall-clock position, song length, and the two load meters. It is drawn
    // smaller and dimmer so the musical position stays the thing the eye lands
    // on, and an empty string gives the whole box back to that first line.
    void setSecondaryText(const juce::String&);
    const juce::String& getSecondaryText() const { return secondary; }
    // The far end of each line. These readings describe the session rather
    // than follow it - the loop the transport will turn at, the device it is
    // running on - so they sit against the right edge and leave the left of
    // the box to the position and the clock, where the eye already looks for
    // them. Either is dropped whole when its line is too narrow to hold both:
    // a reading that shoves the clock sideways is worse than no reading.
    void setTrailingText(const juce::String&);
    const juce::String& getTrailingText() const { return trailing; }
    void setSecondaryTrailingText(const juce::String&);
    const juce::String& getSecondaryTrailingText() const { return secondaryTrailing; }
    std::function<void()> configurationRequested;

private:
    // The chevron paints its glyph only. A plain TextButton would carry the
    // look-and-feel's rounded background and outline inside the readout.
    struct GlyphButton final : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton(juce::Graphics&, bool highlighted, bool pressed) override;
    };

    juce::String text, secondary, trailing, secondaryTrailing;
    GlyphButton configuration {"v"};
};
}
