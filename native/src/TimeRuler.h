#pragma once
#include "Session.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace rhino
{
// Wall-clock readings, as mm:ss or mm:ss:ms. Minutes are padded to two digits
// rather than widened past an hour: an arrangement that long would push the
// load meters out of the transport readout, and the bar number is the reading
// that matters at that length anyway.
//
// It lives here rather than in the shell because the shell's readout and the
// ruler below the timeline have to spell a time the same way. Two of them
// drifting apart is the kind of difference nobody notices until they are read
// against each other.
juce::String formatClock(double seconds, bool withMilliseconds);

// The band under the arrangement: one wall-clock time per labelled bar.
//
// It is placed on the timeline's own lane rectangle and given the view the
// arrangement is showing, so a time maps to the same column the painter's xFor
// would put it in. That, plus stepping through bars by the shared rule in
// ArrangementGrid.h, is what keeps every time directly under a bar number
// rather than merely near one.
//
// The times come out round whenever the tempo is: at 120 in four four a bar is
// two seconds, so a two-bar step reads 00:00:000, 00:04:000, 00:08:000. At a
// tempo that does not divide evenly they are whatever the bars land on - the
// alternative is labels that do not line up with anything above them, which is
// the worse of the two.
class TimeRuler final : public juce::Component
{
public:
    explicit TimeRuler(Session&);

    // The span of time across this component's width. Repaints only when it
    // has actually moved: it is pushed from the arrangement on every scroll.
    void setView(double start, double span);

    void paint(juce::Graphics&) override;

    // How tall the band wants to be. The shell reserves this much between the
    // arrangement and the Clip and Devices strip.
    static constexpr int standardHeight = 18;

private:
    Session& session;
    double viewStart = 0.0, viewSpan = 8.0;
};
}
