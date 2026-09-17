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
    std::vector<TrackAutomation> automations;
    const auto owner = automationOwnerState(track);
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
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::applyTrackAutomationAt(double timelineSeconds)
{
    if (!std::isfinite(timelineSeconds) || timelineSeconds < 0.0)
        return;

    bool changed = false;
    std::vector<DeviceTarget> activeTargets;
    const auto applyTo = [&] (te::Plugin* plugin, const TrackAutomation& automation)
    {
        if (plugin == nullptr)
            return;
        auto* parameter = exposedParameterAt(*plugin, automation.target.parameter);
        if (parameter == nullptr)
            return;
        auto& runtime = automationRuntimeFor(automation.target);
        activeTargets.push_back(automation.target);
        if (!runtime.hasBaseValue)
        {
            runtime.baseValue = parameter->getCurrentValue();
            runtime.hasBaseValue = true;
        }
        runtime.active = true;
        if (runtime.overridden)
            return;

        const auto range = parameter->getValueRange();
        const auto next = juce::jlimit(range.getStart(),
                                       exposedParameterMaximum(*plugin, automation.target.parameter, range.getEnd()),
                                       automation.valueAt(timelineSeconds));
        if (std::abs(parameter->getCurrentValue() - next) > 0.0001f)
        {
            parameter->setParameter(next, juce::sendNotification);
            changed = true;
        }
    };

    for (int track = 0; track <= masterTrackIndex(); ++track)
    {
        auto* list = pluginListForTrack(track);
        if (list == nullptr)
            continue;
        for (const auto& automation : readTrackAutomations(track, false))
        {
            if (!automation.active() || !juce::isPositiveAndBelow(automation.target.slot, list->size()))
                continue;
            applyTo((*list)[automation.target.slot], automation);
        }
    }

    // A lane that stopped driving hands the parameter back to the value the
    // user last set by hand, rather than freezing on its final point.
    for (auto& runtime : automationRuntime)
    {
        if (!runtime.active)
            continue;
        bool stillActive = false;
        for (const auto target : activeTargets)
            if (sameDeviceTarget(runtime.target, target))
            {
                stillActive = true;
                break;
            }
        if (stillActive)
            continue;

        runtime.active = false;
        if (runtime.overridden || !runtime.hasBaseValue)
            continue;
        auto* list = pluginListForTrack(runtime.target.track);
        if (list == nullptr || !juce::isPositiveAndBelow(runtime.target.slot, list->size()))
            continue;
        auto* plugin = (*list)[runtime.target.slot];
        if (plugin == nullptr)
            continue;
        if (auto* parameter = exposedParameterAt(*plugin, runtime.target.parameter))
        {
            const auto range = parameter->getValueRange();
            const auto next = juce::jlimit(range.getStart(),
                                           exposedParameterMaximum(*plugin, runtime.target.parameter, range.getEnd()), runtime.baseValue);
            if (std::abs(parameter->getCurrentValue() - next) > 0.0001f)
            {
                parameter->setParameter(next, juce::sendNotification);
                changed = true;
            }
        }
    }

    if (changed)
        sendSynchronousChangeMessage();
}


// Playback drives the lanes from the shell's 30 Hz timer, which only runs while
// the transport is playing. An offline render has neither: it walks the edit on
// a worker thread with the transport stopped, so a swept parameter would be
// written to the file frozen at whatever value the timer last left it holding.
// Mirroring each lane into the engine's own automation curve is what puts the
// movement somewhere the render can read, since the node graph pulls those per
// sub-block whether it is playing live or rendering.
void Session::beginOfflineAutomation()
{
    endOfflineAutomation();

    for (int track = 0; track <= masterTrackIndex(); ++track)
    {
        auto* list = pluginListForTrack(track);
        if (list == nullptr)
            continue;
        for (const auto& automation : readTrackAutomations(track, false))
        {
            if (!automation.active() || !juce::isPositiveAndBelow(automation.target.slot, list->size()))
                continue;
            // A lane the user has taken over by hand drives nothing during
            // playback, so it must not drive the render either.
            if (const auto* runtime = findAutomationRuntime(automation.target); runtime != nullptr && runtime->overridden)
                continue;
            auto* plugin = (*list)[automation.target.slot];
            if (plugin == nullptr)
                continue;
            auto* parameter = exposedParameterAt(*plugin, automation.target.parameter);
            if (parameter == nullptr)
                continue;
            auto& curve = parameter->getCurve();
            // An engine curve already on the parameter is somebody else's
            // automation. Leave it alone and let it render on its own terms.
            if (curve.getNumPoints() > 0)
                continue;

            offlineAutomation.push_back({parameter, parameter->getCurrentValue()});
            const auto range = parameter->getValueRange();
            const auto ceiling = exposedParameterMaximum(*plugin, automation.target.parameter, range.getEnd());
            for (const auto& point : automation.points)
                curve.addPoint(tracktion::core::TimePosition::fromSeconds(point.timeSeconds),
                               juce::jlimit(range.getStart(), ceiling, point.value), 0.0f, nullptr);
        }
    }
}

void Session::endOfflineAutomation()
{
    for (auto& mirrored : offlineAutomation)
    {
        if (mirrored.parameter == nullptr)
            continue;
        mirrored.parameter->getCurve().clear(nullptr);
        mirrored.parameter->setParameter(mirrored.restoreValue, juce::dontSendNotification);
    }
    offlineAutomation.clear();
}

}
