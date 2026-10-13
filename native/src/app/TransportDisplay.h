#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
// The control bar's one readout: a single recessed panel carrying everything
// the transport has to say, arranged in three columns rather than stacked into
// two lines of run-together text.
//
//   +-----------------------------------------------------------------+
//   |              |  120   4/4          |  LOOP 001.1.1 - 001.4.0     |
//   |  001.1.1     |                     |                             |
//   |              |  00:00.000 / 02:00  |  CPU 4%  RAM 85 MB          |
//   +-----------------------------------------------------------------+
//
// The position is the thing the eye lands on, so it is set large and given the
// left column to itself; everything beside it is a reading that is watched
// rather than read, and is set at a size to match. The regions are separated by
// space, not by boxes: three displays in a row would each need a bezel, and
// three bezels in a control bar is what this replaced.
//
// Nothing here is interactive but the chevron, which opens the menu that says
// which of these readings to show. That is deliberate: a display that took a
// click could seek the transport or take editor focus by accident.
class TransportDisplay final : public juce::Component
{
public:
    TransportDisplay();
    void paint(juce::Graphics&) override;
    void resized() override;

    // The left column. Bars, beats and subdivision, or the count-in while one
    // is running - whatever the shell decides the position reads as.
    void setPosition(const juce::String&);
    const juce::String& getPosition() const { return position; }

    // The middle column's two rows: tempo and signature over the clock. They
    // are set together because they share a column and are measured against
    // each other to size it.
    void setTempoAndSignature(const juce::String& tempo, const juce::String& signature);
    void setClock(const juce::String&);

    // The right column's two rows. The loop is where the transport will turn;
    // the statistics are the machine's load. Both describe the session rather
    // than follow it, so they sit against the right edge and leave the left of
    // the panel to the readings that move.
    //
    // The loop is given as its range alone - "001.1.1 - 002.1.0". The word
    // LOOP in front of it is the display's, because the word is the first
    // thing it gives up when the box is tight: the range is still a range
    // without it, and five characters is most of a column's worth of space.
    void setLoop(const juce::String& range);
    void setStatistics(const juce::String&);
    // Sample rate, buffer and latency. The lowest-priority reading in the box:
    // once the spacing and the LOOP label have been given up, this is the
    // first reading to go.
    void setDeviceInfo(const juce::String&);

    const juce::String& getLoop() const { return loop; }
    const juce::String& getStatistics() const { return statistics; }

    std::function<void()> configurationRequested;

private:
    // The chevron paints its glyph only. A plain TextButton would carry the
    // look-and-feel's background and outline inside the readout.
    struct GlyphButton final : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton(juce::Graphics&, bool highlighted, bool pressed) override;
    };

    void refresh(juce::String& field, const juce::String& next);

    juce::String position, tempo, signature, clock, loop, statistics, deviceInfo;
    GlyphButton configuration {"v"};
};
}
