#include "ArrangementInternal.h"
#include <algorithm>

// A card's fader and pan are automated as Live's are, from a right-click on
// either: the same menu a device knob's right-click opens, for the lane on the
// track's own fader (Session::trackMixerTarget). The lane is then an ordinary
// one - drawn over the track or on a row of its own, picked by the card's
// automation chooser, played by the engine - and while it plays the card's bar
// follows it. Moving the bar by hand takes it from the lane until the menu
// re-enables it, as touching an automated knob does.

namespace rhino
{
namespace
{
juce::String controlName(Session::MixerControl control)
{
    return control == Session::MixerControl::volume ? "Volume" : "Pan";
}
}

void Arrangement::showMixerMenu(int track, Session::MixerControl control)
{
    const auto target = session.trackMixerTarget(track, control);
    if (!target.isValid() || !juce::isPositiveAndBelow(track, static_cast<int>(volume.size())))
        return;
    const auto lane = session.trackAutomationState(target);
    const auto parameter = session.deviceParameter(target.track, target.slot, target.parameter);
    const auto item = [](MixerAutomationChoice choice) { return static_cast<int>(choice); };

    juce::PopupMenu menu;
    menu.addSectionHeader(controlName(control).toUpperCase());
    menu.addItem(item(MixerAutomationChoice::showOnTrack), "Show automation", true, lane.visible && !lane.ownLane);
    menu.addItem(item(MixerAutomationChoice::showOnLane), "Show automation on new lane", true,
                 lane.visible && lane.ownLane);
    menu.addSeparator();
    menu.addItem(item(MixerAutomationChoice::hide), "Hide automation", lane.visible);
    menu.addItem(item(MixerAutomationChoice::remove), "Delete automation", lane.active);
    // Only once the bar has been moved by hand is there anything to give back.
    if (parameter.has_value() && parameter->automationOverridden)
    {
        menu.addSeparator();
        menu.addItem(item(MixerAutomationChoice::reenable), "Re-enable automation");
    }
    auto* anchor = control == Session::MixerControl::volume ? volume[static_cast<size_t>(track)].get()
                                                            : pan[static_cast<size_t>(track)].get();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor),
        [safe = juce::Component::SafePointer<Arrangement>(this), track, control] (int result)
        {
            if (safe != nullptr && result > 0)
                safe->chooseMixerAutomation(track, control, static_cast<MixerAutomationChoice>(result));
        });
}

void Arrangement::chooseMixerAutomation(int track, Session::MixerControl control, MixerAutomationChoice choice)
{
    const auto target = session.trackMixerTarget(track, control);
    // Named before the session announces: the announcement rebuilds the cards.
    const auto name = controlName(control) + " of " + factsFor(track).name;
    auto outcome = juce::Result::fail("That track has no fader to automate.");
    juce::String done;
    if (target.isValid())
        switch (choice)
        {
            case MixerAutomationChoice::showOnTrack:
            case MixerAutomationChoice::showOnLane:
                outcome = session.showTrackAutomation(target, choice == MixerAutomationChoice::showOnLane);
                done = name + ": automation shown in the arrangement";
                break;
            case MixerAutomationChoice::hide:
                outcome = session.hideTrackAutomation(target);
                done = name + ": automation hidden";
                break;
            case MixerAutomationChoice::remove:
                outcome = session.clearTrackAutomationPoints(target);
                done = name + ": automation deleted";
                break;
            case MixerAutomationChoice::reenable:
            {
                // The override is a toggle, so it is asked first: a second
                // choice must not take the bar off its lane again.
                const auto parameter = session.deviceParameter(target.track, target.slot, target.parameter);
                outcome = parameter.has_value() && parameter->automationOverridden
                    ? session.toggleParameterAutomationOverride(target.track, target.slot, target.parameter)
                    : juce::Result::ok();
                done = name + " follows its automation again";
                break;
            }
        }
    if (status)
        status(outcome.wasOk() ? done : outcome.getErrorMessage());
    repaint();
}

void Arrangement::beginMixerGesture(int track, Session::MixerControl control)
{
    const auto target = session.trackMixerTarget(track, control);
    const auto before = target.isValid() ? session.deviceParameter(target.track, target.slot, target.parameter)
                                         : std::nullopt;
    if (control == Session::MixerControl::volume)
        session.beginTrackVolumeGesture(track);
    else
        session.beginTrackPanGesture(track);
    if (status && before.has_value() && before->automated && !before->automationOverridden)
        status(controlName(control) + " of " + factsFor(track).name
               + " no longer follows its automation: right-click it to re-enable it");
}

void Arrangement::findAutomatedMixers()
{
    automatedMixers.clear();
    for (int track = 0; track < static_cast<int>(trackLanes.size()); ++track)
    {
        const auto& lanes = trackLanes[static_cast<size_t>(track)];
        // Most tracks drive nothing, and are not asked where their fader is.
        if (std::none_of(lanes.begin(), lanes.end(), [](const auto& lane) { return lane.active(); }))
            continue;
        const auto volumeLane = session.trackMixerTarget(track, Session::MixerControl::volume);
        const auto panLane = session.trackMixerTarget(track, Session::MixerControl::pan);
        const auto drives = [](const Session::TrackAutomation& lane, Session::DeviceTarget control)
        {
            return lane.active() && lane.target.slot == control.slot && lane.target.parameter == control.parameter;
        };
        if (std::any_of(lanes.begin(), lanes.end(), [&](const auto& lane)
                        { return drives(lane, volumeLane) || drives(lane, panLane); }))
            automatedMixers.push_back(track);
    }
}

// A bar repaints itself only when its value moves, so a frame of a song whose
// lanes are flat reads a few values and paints nothing.
void Arrangement::followMixerAutomation()
{
    for (const auto track : automatedMixers)
    {
        if (!juce::isPositiveAndBelow(track, static_cast<int>(volume.size())))
            continue;
        const auto mixer = session.trackMixer(track);
        auto& fader = *volume[static_cast<size_t>(track)];
        auto& position = *pan[static_cast<size_t>(track)];
        if (!fader.isMouseButtonDown())
            fader.setValue(mixer.volumeDb, juce::dontSendNotification);
        if (!position.isMouseButtonDown())
            position.setValue(mixer.pan, juce::dontSendNotification);
    }
}

}
