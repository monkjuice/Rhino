#include "ArrangementInternal.h"

// The automation chooser on every card, beside the input chooser: which of the
// lanes drawn over the track its own row shows and edits, or None for the
// clips alone. The track menu opens the same list for a row too short to show
// the chooser. The choice is the session's (Session::setTrackShownAutomation),
// a view setting kept on the track.

namespace rhino
{

const Session::TrackAutomation* Arrangement::shownAutomation(int track) const
{
    if (!juce::isPositiveAndBelow(track, static_cast<int>(trackLanes.size())))
        return nullptr;
    for (const auto& automation : trackLanes[static_cast<size_t>(track)])
        if (automation.shown)
            return &automation;
    return nullptr;
}

void Arrangement::syncAutomationChoosers()
{
    for (int track = 0; track < static_cast<int>(automationChooser.size()); ++track)
    {
        const auto* shown = shownAutomation(track);
        const auto name = factsFor(track).name;
        auto& chooser = *automationChooser[static_cast<size_t>(track)];
        chooser.setButtonText(shown != nullptr ? shown->parameterName : juce::String("None"));
        chooser.setTooltip(shown != nullptr
            ? "Automation on " + name + ": " + shown->deviceName + " " + shown->parameterName
                  + ". Click under the clip names to add a node, drag one to move it, Shift for fine steps. "
                    "None shows the clips alone."
            : "Automation on " + name + ": none, so the row shows its clips alone. Pick a lane to draw on it here; "
              "right-click a device knob, the fader or the pan to show one.");
    }
}

void Arrangement::showAutomationChooser(int track)
{
    if (!juce::isPositiveAndBelow(track, static_cast<int>(trackLanes.size())))
        return;
    // Captured now: the announcement a choice makes rebuilds the lanes. Only
    // the lanes drawn over the track: one on a row of its own is edited there.
    std::vector<Session::DeviceTarget> lanes;
    juce::PopupMenu menu;
    menu.addSectionHeader("AUTOMATION");
    menu.addItem(1, "None", true, shownAutomation(track) == nullptr);
    menu.addSeparator();
    for (const auto& automation : trackLanes[static_cast<size_t>(track)])
    {
        if (automation.ownLane)
            continue;
        lanes.push_back(automation.target);
        menu.addItem(static_cast<int>(lanes.size()) + 1, automation.deviceName + ": " + automation.parameterName,
                     true, automation.shown);
    }
    if (lanes.empty())
        menu.addItem(1000, "Right-click a knob, the fader or the pan to show its automation", false, false);
    auto options = juce::PopupMenu::Options().withTargetComponent(this).withMousePosition();
    if (juce::isPositiveAndBelow(track, static_cast<int>(automationChooser.size()))
        && automationChooser[static_cast<size_t>(track)]->isShowing())
        options = juce::PopupMenu::Options().withTargetComponent(automationChooser[static_cast<size_t>(track)].get());
    menu.showMenuAsync(options,
        [safe = juce::Component::SafePointer<Arrangement>(this), track, lanes] (int result)
        {
            if (safe == nullptr || result < 1 || result > static_cast<int>(lanes.size()) + 1)
                return;
            safe->showAutomationOnTrack(track, result == 1 ? Session::DeviceTarget {}
                                                           : lanes[static_cast<size_t>(result - 2)]);
        });
}

void Arrangement::showAutomationOnTrack(int track, Session::DeviceTarget lane)
{
    if (!juce::isPositiveAndBelow(track, static_cast<int>(trackLanes.size())))
        return;
    // Read before the session announces: the announcement rebuilds the lanes.
    const auto name = factsFor(track).name;
    juce::String parameter;
    for (const auto& automation : trackLanes[static_cast<size_t>(track)])
        if (automation.target.slot == lane.slot && automation.target.parameter == lane.parameter)
            parameter = automation.parameterName;
    const auto done = session.setTrackShownAutomation(track, lane);
    if (status)
        status(done.failed() ? done.getErrorMessage()
               : lane.isValid() ? name + " shows " + parameter + ": click under the clip names to add a node, "
                                  "drag one to move it, Shift for fine steps"
                                : name + " shows its clips alone");
    repaint();
}

}
