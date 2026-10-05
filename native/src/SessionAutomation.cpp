#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Track automation. Serves Arrangement and the device rack.
//
// A lane is stored on the track it is displayed under, keyed by the device
// parameter it drives, and holds an ordered list of points spanning the whole
// timeline rather than one clip. Fewer than two points means the lane has been
// revealed but never drawn: it shows the knob's resting value and drives
// nothing.

namespace rhino
{
namespace
{
double pointTime(const juce::ValueTree& state)
{
    return static_cast<double>(state.getProperty(automationTimeID, 0.0));
}

// A lane's track is where it is stored, never a copy of the index: deleting a
// track renumbers every track below it, and a stored index would go stale.
bool addressesDevice(const juce::ValueTree& state, Session::DeviceTarget target)
{
    return static_cast<int>(state.getProperty(automationSlotID, -1)) == target.slot
        && static_cast<int>(state.getProperty(automationParameterID, -1)) == target.parameter;
}
}

float Session::TrackAutomation::valueAt(double seconds) const
{
    if (points.empty())
        return restingValue;
    if (points.size() == 1 || seconds <= points.front().timeSeconds)
        return points.front().value;
    if (seconds >= points.back().timeSeconds)
        return points.back().value;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& previous = points[i - 1];
        const auto& next = points[i];
        if (seconds > next.timeSeconds)
            continue;
        const auto span = next.timeSeconds - previous.timeSeconds;
        if (span <= 0.0)
            return next.value;
        const auto amount = static_cast<float>((seconds - previous.timeSeconds) / span);
        return previous.value + (next.value - previous.value) * amount;
    }
    return points.back().value;
}

// The master track keeps its lanes on the edit, every other track on itself.
// Both are part of the saved document, so persistence needs no extra step.
juce::ValueTree Session::automationOwnerState(int track) const
{
    if (isMasterTrack(track))
        return edit->state;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return {};
    return tracks[track]->state;
}

juce::ValueTree Session::findTrackAutomationState(DeviceTarget target) const
{
    const auto owner = automationOwnerState(target.track);
    if (!owner.isValid())
        return {};
    for (int i = 0; i < owner.getNumChildren(); ++i)
    {
        const auto state = owner.getChild(i);
        if (state.hasType(trackAutomationID) && addressesDevice(state, target))
            return state;
    }
    return {};
}

std::vector<Session::TrackAutomation> Session::readTrackAutomations(int track, bool resolveParameterInfo) const
{
    return readTrackAutomations(automationOwnerState(track), track, resolveParameterInfo);
}

std::vector<Session::TrackAutomation> Session::readTrackAutomations(const juce::ValueTree& owner, int track,
                                                                    bool resolveParameterInfo) const
{
    std::vector<TrackAutomation> automations;
    if (!owner.isValid())
        return automations;

    std::vector<DeviceSlot> slots;
    std::vector<DeviceParameter> parameters;
    int parametersForSlot = -1;
    for (int i = 0; i < owner.getNumChildren(); ++i)
    {
        const auto state = owner.getChild(i);
        if (!state.hasType(trackAutomationID))
            continue;
        TrackAutomation automation;
        automation.target = {track,
                             static_cast<int>(state.getProperty(automationSlotID, -1)),
                             static_cast<int>(state.getProperty(automationParameterID, -1))};
        if (!automation.target.isValid())
            continue;
        automation.ownLane = static_cast<bool>(state.getProperty(automationOwnLaneID, false));
        for (int child = 0; child < state.getNumChildren(); ++child)
        {
            const auto point = state.getChild(child);
            if (!point.hasType(automationPointID))
                continue;
            const auto time = pointTime(point);
            const auto value = static_cast<float>(point.getProperty(automationValueID, 0.0));
            if (std::isfinite(time) && std::isfinite(value))
                automation.points.push_back({std::max(0.0, time), value});
        }
        std::stable_sort(automation.points.begin(), automation.points.end(),
                         [] (const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });

        if (resolveParameterInfo)
        {
            if (parametersForSlot != automation.target.slot)
            {
                parameters = deviceParameters(track, automation.target.slot);
                parametersForSlot = automation.target.slot;
            }
            if (juce::isPositiveAndBelow(automation.target.parameter, parameters.size()))
            {
                const auto& parameter = parameters[static_cast<size_t>(automation.target.parameter)];
                automation.parameterName = parameter.name;
                automation.minimum = parameter.minimum;
                automation.maximum = parameter.maximum;
                automation.restingValue = juce::jlimit(parameter.minimum, parameter.maximum, parameter.value);
            }
            if (slots.empty())
                slots = deviceSlots(track);
            for (const auto& slot : slots)
                if (slot.pluginIndex == automation.target.slot)
                {
                    automation.deviceName = slot.name;
                    break;
                }
            // A lane whose device or parameter has gone is not displayable.
            if (automation.parameterName.isEmpty())
                continue;
        }
        automations.push_back(std::move(automation));
    }
    return automations;
}

std::vector<Session::TrackAutomation> Session::trackAutomations(int track) const
{
    return readTrackAutomations(track, true);
}

Session::AutomationLaneState Session::trackAutomationState(DeviceTarget target) const
{
    AutomationLaneState lane;
    const auto state = findTrackAutomationState(target);
    if (!state.isValid())
        return lane;
    lane.visible = true;
    lane.ownLane = static_cast<bool>(state.getProperty(automationOwnLaneID, false));
    int points = 0;
    for (int i = 0; i < state.getNumChildren(); ++i)
        if (state.getChild(i).hasType(automationPointID))
            ++points;
    lane.active = points >= 2;
    return lane;
}

// The engine formats only the value a parameter is currently on, so a reading
// for a point further along the curve is converted here instead. An empty
// answer means the device or the parameter has gone; the caller falls back to
// the raw number rather than printing nothing.
juce::String Session::automationValueText(DeviceTarget target, float value) const
{
    auto* list = pluginListForTrack(target.track);
    if (list == nullptr || !juce::isPositiveAndBelow(target.slot, list->size()))
        return {};
    auto* plugin = (*list)[target.slot];
    if (plugin == nullptr)
        return {};
    auto* parameter = exposedParameterAt(*plugin, target.parameter);
    if (parameter == nullptr)
        return {};
    return formatExposedParameterValue(*plugin, target.parameter, value, *parameter);
}

// Creates the lane if it is missing, inside whatever transaction the caller has
// already opened, so revealing and drawing a lane is one undo step either way.
juce::ValueTree Session::ensureTrackAutomationState(DeviceTarget target, bool ownLane, bool keepExistingLane)
{
    auto state = findTrackAutomationState(target);
    if (state.isValid())
    {
        if (!keepExistingLane)
            state.setProperty(automationOwnLaneID, ownLane, &edit->getUndoManager());
        return state;
    }
    auto owner = automationOwnerState(target.track);
    if (!owner.isValid())
        return {};
    juce::ValueTree lane(trackAutomationID);
    lane.setProperty(automationSlotID, target.slot, nullptr);
    lane.setProperty(automationParameterID, target.parameter, nullptr);
    lane.setProperty(automationOwnLaneID, ownLane, nullptr);
    owner.addChild(lane, -1, &edit->getUndoManager());
    return lane;
}

juce::Result Session::showTrackAutomation(DeviceTarget target, bool ownLane)
{
    if (!target.isValid())
        return juce::Result::fail("Pick a device parameter first.");
    // The main row is pinned under the arrangement at a fixed height and has
    // nowhere to stack a lane, so promising one would be a lie.
    if (isMasterTrack(target.track))
        return juce::Result::fail("Main track automation lanes are not shown yet.");
    const auto parameters = deviceParameters(target.track, target.slot);
    if (!juce::isPositiveAndBelow(target.parameter, parameters.size()))
        return juce::Result::fail("That parameter is no longer available.");

    edit->getUndoManager().beginNewTransaction("Show automation");
    if (!ensureTrackAutomationState(target, ownLane, false).isValid())
        return juce::Result::fail("That track is no longer available.");
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::hideTrackAutomation(DeviceTarget target)
{
    auto state = findTrackAutomationState(target);
    if (!state.isValid())
        return juce::Result::fail("That parameter has no automation lane.");
    auto owner = state.getParent();

    edit->getUndoManager().beginNewTransaction("Hide automation");
    owner.removeChild(state, &edit->getUndoManager());
    if (auto* runtime = findAutomationRuntime(target))
    {
        runtime->active = false;
        runtime->overridden = false;
    }
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::setTrackAutomationPoints(DeviceTarget target, std::vector<AutomationPoint> points)
{
    if (!target.isValid())
        return juce::Result::fail("Pick a device parameter first.");
    const auto parameters = deviceParameters(target.track, target.slot);
    if (!juce::isPositiveAndBelow(target.parameter, parameters.size()))
        return juce::Result::fail("That parameter is no longer available.");
    const auto& parameter = parameters[static_cast<size_t>(target.parameter)];

    std::stable_sort(points.begin(), points.end(),
                     [] (const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    for (auto& point : points)
    {
        if (!std::isfinite(point.timeSeconds) || !std::isfinite(point.value))
            return juce::Result::fail("That automation point is out of range.");
        point.timeSeconds = std::max(0.0, point.timeSeconds);
        point.value = juce::jlimit(parameter.minimum, parameter.maximum, point.value);
    }

    edit->getUndoManager().beginNewTransaction("Edit automation");
    // Drawing a curve on a parameter nobody revealed yet reveals it, so the
    // result on screen always matches what was just written.
    auto state = ensureTrackAutomationState(target, false, true);
    if (!state.isValid())
        return juce::Result::fail("The automation lane could not be created.");
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild(i).hasType(automationPointID))
            state.removeChild(i, &edit->getUndoManager());
    for (const auto& point : points)
    {
        juce::ValueTree node(automationPointID);
        node.setProperty(automationTimeID, point.timeSeconds, nullptr);
        node.setProperty(automationValueID, point.value, nullptr);
        state.addChild(node, -1, &edit->getUndoManager());
    }
    if (auto* runtime = findAutomationRuntime(target))
        runtime->overridden = false;
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// Clearing keeps the lane on screen, back to the dotted resting line, which is
// the state "show automation" leaves a fresh parameter in.
juce::Result Session::clearTrackAutomationPoints(DeviceTarget target)
{
    auto state = findTrackAutomationState(target);
    if (!state.isValid())
        return juce::Result::fail("That parameter has no automation lane.");

    edit->getUndoManager().beginNewTransaction("Delete automation");
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild(i).hasType(automationPointID))
            state.removeChild(i, &edit->getUndoManager());
    if (auto* runtime = findAutomationRuntime(target))
    {
        runtime->active = false;
        runtime->overridden = false;
    }
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

Session::AutomationRuntime& Session::automationRuntimeFor(DeviceTarget target)
{
    if (auto* runtime = findAutomationRuntime(target))
        return *runtime;
    automationRuntime.push_back({target});
    return automationRuntime.back();
}

Session::AutomationRuntime* Session::findAutomationRuntime(DeviceTarget target)
{
    for (auto& runtime : automationRuntime)
        if (sameDeviceTarget(runtime.target, target))
            return &runtime;
    return nullptr;
}

const Session::AutomationRuntime* Session::findAutomationRuntime(DeviceTarget target) const
{
    for (const auto& runtime : automationRuntime)
        if (sameDeviceTarget(runtime.target, target))
            return &runtime;
    return nullptr;
}

juce::Result Session::toggleParameterAutomationOverride(int track, int slot, int parameter)
{
    const DeviceTarget target {track, slot, parameter};
    if (!target.isValid())
        return juce::Result::fail("Select an automated parameter first.");
    if (!hasActiveTrackAutomation(*edit, target))
        return juce::Result::fail("This parameter has no automation.");

    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = (*list)[slot];
    if (plugin == nullptr)
        return juce::Result::fail("Select a device first.");
    auto* pluginParameter = exposedParameterAt(*plugin, parameter);
    if (pluginParameter == nullptr)
        return juce::Result::fail("Select a parameter first.");

    auto& runtime = automationRuntimeFor(target);
    runtime.baseValue = pluginParameter->getCurrentValue();
    runtime.hasBaseValue = true;
    runtime.overridden = !runtime.overridden;
    // An override is not a change to the document, so it is mirrored here
    // rather than waiting for one.
    mirrorAutomationToEngine();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::AutomationMirror::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (session.automationMirrorStale)
        session.mirrorAutomationToEngine();
}

// The lanes used to be swept from the shell's 30 Hz timer: parameters moved in
// steps a frame apart, stopped moving whenever the message thread was busy, an
// offline render had to mirror the lanes for itself, and every step announced
// a change that rebuilt the whole interface. The engine's own curves have none
// of that, so the lanes now live there for as long as they drive anything.
void Session::mirrorAutomationToEngine()
{
    automationMirrorStale = false;
    const auto samePoints = [](const std::vector<AutomationPoint>& a, const std::vector<AutomationPoint>& b)
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const auto& x, const auto& y)
        {
            return x.timeSeconds == y.timeSeconds && x.value == y.value;
        });
    };

    // Every track's devices and the lanes stored on it, the main track's last,
    // found in one walk of the track list: this runs after every change to the
    // document, and asking track by track walked the list again each time.
    struct Owner
    {
        te::PluginList& devices;
        juce::ValueTree lanes;
    };
    std::vector<Owner> owners;
    for (auto* track : te::getAudioTracks(*edit))
        owners.push_back({track->pluginList, track->state});
    owners.push_back({edit->getMasterPluginList(), edit->state});

    // What each lane asks of the engine now: a curve for every lane that is
    // drawn, whose device and parameter still exist, and that nobody has taken
    // over by hand. A lane taken over still counts as driving its knob, which
    // is what makes the next touch of that knob an override.
    std::vector<MirroredCurve> wanted;
    std::vector<DeviceTarget> driving;
    for (int track = 0; track < static_cast<int>(owners.size()); ++track)
    {
        auto* list = &owners[static_cast<size_t>(track)].devices;
        for (const auto& automation : readTrackAutomations(owners[static_cast<size_t>(track)].lanes, track, false))
        {
            if (!automation.active() || !juce::isPositiveAndBelow(automation.target.slot, list->size()))
                continue;
            te::Plugin::Ptr plugin = (*list)[automation.target.slot];
            if (plugin == nullptr)
                continue;
            te::AutomatableParameter::Ptr parameter = exposedParameterAt(*plugin, automation.target.parameter);
            if (parameter == nullptr)
                continue;
            driving.push_back(automation.target);
            auto& runtime = automationRuntimeFor(automation.target);
            runtime.active = true;
            // Read before any curve of ours is on it: this is the value the
            // parameter is handed back when the lane stops driving it.
            if (!runtime.hasBaseValue)
            {
                runtime.baseValue = parameter->getCurrentValue();
                runtime.hasBaseValue = true;
            }
            if (runtime.overridden)
                continue;
            MirroredCurve curve {automation.target, plugin, parameter, {}};
            const auto range = parameter->getValueRange();
            const auto ceiling = exposedParameterMaximum(*plugin, automation.target.parameter, range.getEnd());
            for (const auto& point : automation.points)
                curve.points.push_back({point.timeSeconds, juce::jlimit(range.getStart(), ceiling, point.value)});
            wanted.push_back(std::move(curve));
        }
    }
    for (auto& runtime : automationRuntime)
        if (runtime.active && std::none_of(driving.begin(), driving.end(), [&runtime](const auto& target)
                                           { return sameDeviceTarget(runtime.target, target); }))
            runtime.active = false;

    const auto findWanted = [&wanted](const te::AutomatableParameter* parameter) -> const MirroredCurve*
    {
        for (const auto& curve : wanted)
            if (curve.parameter.get() == parameter)
                return &curve;
        return nullptr;
    };

    // Every engine curve in the edit is Rhino's to keep. One on a parameter no
    // lane drives is left over, and it would play a movement nobody can see.
    // The curves this mirror wrote are cleared from its own record. Any other
    // can only have arrived with a plugin - in a document saved with its
    // curves, a device brought back by undo, one moved to another track -
    // because curves are never written through the undo manager, so the whole
    // edit is searched only when the plugins in it have changed. By their ids
    // rather than their addresses, which a freed plugin can hand on.
    std::vector<te::EditItemID> plugins;
    for (const auto& owner : owners)
        for (auto* plugin : owner.devices)
            if (plugin != nullptr)
                plugins.push_back(plugin->itemID);
    if (plugins != mirroredPlugins)
    {
        for (const auto& owner : owners)
            for (auto* plugin : owner.devices)
                if (plugin != nullptr)
                    for (auto* parameter : plugin->getAutomatableParameters())
                        if (parameter != nullptr && parameter->getCurve().getNumPoints() > 0
                            && findWanted(parameter) == nullptr)
                            parameter->getCurve().clear(nullptr);
        mirroredPlugins = std::move(plugins);
    }
    for (const auto& before : mirroredCurves)
        if (findWanted(before.parameter.get()) == nullptr && before.plugin->state.isAChildOf(edit->state)
            && before.parameter->getCurve().getNumPoints() > 0)
            before.parameter->getCurve().clear(nullptr);

    // A lane that stopped driving a parameter hands it back to the value the
    // user last set by hand, rather than leaving it on wherever the curve last
    // put it. Not one the user has taken over, whose knob is already theirs,
    // and not one whose device has left the edit.
    bool handedBack = false;
    for (const auto& before : mirroredCurves)
    {
        if (findWanted(before.parameter.get()) != nullptr || before.plugin == nullptr
            || !before.plugin->state.isAChildOf(edit->state))
            continue;
        const auto* runtime = findAutomationRuntime(before.target);
        if (runtime == nullptr || runtime->overridden || !runtime->hasBaseValue)
            continue;
        const auto range = before.parameter->getValueRange();
        const auto next = juce::jlimit(range.getStart(),
                                       exposedParameterMaximum(*before.plugin, before.target.parameter, range.getEnd()),
                                       runtime->baseValue);
        if (std::abs(before.parameter->getCurrentValue() - next) > 0.0001f)
        {
            before.parameter->setParameter(next, juce::sendNotification);
            handedBack = true;
        }
    }

    for (const auto& curve : wanted)
    {
        auto& engineCurve = curve.parameter->getCurve();
        const auto previous = std::find_if(mirroredCurves.begin(), mirroredCurves.end(), [&curve](const auto& before)
        {
            return before.parameter == curve.parameter;
        });
        if (previous != mirroredCurves.end() && samePoints(previous->points, curve.points)
            && engineCurve.getNumPoints() == static_cast<int>(curve.points.size()))
            continue;
        engineCurve.clear(nullptr);
        for (const auto& point : curve.points)
            engineCurve.addPoint(tracktion::core::TimePosition::fromSeconds(point.timeSeconds), point.value, 0.0f, nullptr);
    }
    mirroredCurves = std::move(wanted);

    // Whoever has already heard this announcement read the value from before
    // the hand-back, so say it again.
    if (handedBack)
        sendChangeMessage();
}

}
