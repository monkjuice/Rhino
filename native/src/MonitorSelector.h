#pragma once
#include "Session.h"
#include <array>
#include <functional>

namespace rhino
{
// The On / Auto / Off control a track card carries, under the chooser that
// says which input the track takes.
//
// It is one control rather than three buttons because it carries one answer:
// a click on any segment is a choice, never a toggle, so there is no state in
// which none of them is lit and none in which two are. Three TextButtons would
// have needed a group to enforce that and would still have drawn three frames
// where the card has room for one.
//
// The order is the one the control is read in - at all times, while armed,
// never - rather than the enum's, which runs the other way because `off` is
// what a track that has never been asked resolves to.
//
// It is a type of its own rather than a detail of the arrangement because a
// card is not the only place the three settings belong: the mixer strip and
// the session view's track head both want the same control, and because a
// named type is one the workflow test can take the measure of.
class MonitorSelector final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    static constexpr std::array<Session::InputMonitoring, 3> order {
        Session::InputMonitoring::on, Session::InputMonitoring::automatic, Session::InputMonitoring::off
    };

    std::function<void(Session::InputMonitoring)> onChoose;

    void setMode(Session::InputMonitoring);
    Session::InputMonitoring mode() const { return current; }

    // Laid out in floats and rounded at each edge, so three segments fill the
    // width exactly however it divides - rounding each segment to the same
    // width instead leaves a seam at the right-hand end on most card widths.
    juce::Rectangle<int> segmentBounds(int index) const;
    int segmentAt(juce::Point<int>) const;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void setHovered(int index);

    Session::InputMonitoring current = Session::InputMonitoring::off;
    int hovered = -1;
};
}
