#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Clip automation. Serves Arrangement.

namespace theta
{

Session::ClipAutomation Session::clipAutomation(te::EditItemID id) const
{
    const auto automations = clipAutomations(id);
    return automations.empty() ? ClipAutomation{} : automations.front();
}

std::vector<Session::ClipAutomation> Session::clipAutomations(te::EditItemID id) const
{
    std::vector<ClipAutomation> automations;
    ClipAutomation automation;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return automations;

    for (int i = 0; i < clip->state.getNumChildren(); ++i)
    {
        const auto state = clip->state.getChild(i);
        if (!state.hasType(clipAutomationID))
            continue;

        automation = {};
        automation.target.track = static_cast<int>(state.getProperty(automationTrackID, -1));
        automation.target.slot = static_cast<int>(state.getProperty(automationSlotID, -1));
        automation.target.parameter = static_cast<int>(state.getProperty(automationParameterID, -1));
        automation.startSeconds = static_cast<double>(state.getProperty(automationStartID, 0.0));
        automation.endSeconds = static_cast<double>(state.getProperty(automationEndID, 0.0));
        automation.startValue = static_cast<float>(state.getProperty(automationStartValueID, 0.0));
        automation.endValue = static_cast<float>(state.getProperty(automationEndValueID, 0.0));
        automation.active = automation.target.isValid()
            && std::isfinite(automation.startSeconds)
            && std::isfinite(automation.endSeconds)
            && automation.endSeconds > automation.startSeconds;

        const auto parameters = deviceParameters(automation.target.track, automation.target.slot);
        if (juce::isPositiveAndBelow(automation.target.parameter, parameters.size()))
        {
            const auto& parameter = parameters[static_cast<size_t>(automation.target.parameter)];
            automation.parameterName = parameter.name;
            automation.minimum = parameter.minimum;
            automation.maximum = parameter.maximum;
        }
        if (automation.active)
            automations.push_back(automation);
    }
    return automations;
}

juce::Result Session::setClipAutomationRamp(te::EditItemID id, DeviceTarget target, double startSeconds, double endSeconds,
                                            float startValue, float endValue)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    if (!target.isValid())
        return juce::Result::fail("Move a device knob first, then draw automation.");

    auto parameters = deviceParameters(target.track, target.slot);
    if (!juce::isPositiveAndBelow(target.parameter, parameters.size()))
        return juce::Result::fail("The last touched knob is no longer available.");
    const auto& parameter = parameters[static_cast<size_t>(target.parameter)];

    if (endSeconds < startSeconds)
    {
        std::swap(startSeconds, endSeconds);
        std::swap(startValue, endValue);
    }
    const auto clipStart = clip->getPosition().time.getStart().inSeconds();
    const auto clipEnd = clip->getPosition().time.getEnd().inSeconds();
    startSeconds = juce::jlimit(clipStart, clipEnd, startSeconds);
    endSeconds = juce::jlimit(clipStart, clipEnd, endSeconds);
    if (endSeconds - startSeconds < 0.02)
        return juce::Result::fail("Draw a longer automation span.");

    startValue = juce::jlimit(parameter.minimum, parameter.maximum, startValue);
    endValue = juce::jlimit(parameter.minimum, parameter.maximum, endValue);

    edit->getUndoManager().beginNewTransaction("Draw clip automation");
    for (int i = clip->state.getNumChildren(); --i >= 0;)
    {
        const auto existing = clip->state.getChild(i);
        if (!existing.hasType(clipAutomationID))
            continue;
        const DeviceTarget existingTarget {
            static_cast<int>(existing.getProperty(automationTrackID, -1)),
            static_cast<int>(existing.getProperty(automationSlotID, -1)),
            static_cast<int>(existing.getProperty(automationParameterID, -1))
        };
        if (sameDeviceTarget(existingTarget, target))
            clip->state.removeChild(existing, &edit->getUndoManager());
    }
    juce::ValueTree automation(clipAutomationID);
    automation.setProperty(automationTrackID, target.track, &edit->getUndoManager());
    automation.setProperty(automationSlotID, target.slot, &edit->getUndoManager());
    automation.setProperty(automationParameterID, target.parameter, &edit->getUndoManager());
    automation.setProperty(automationStartID, startSeconds - clipStart, &edit->getUndoManager());
    automation.setProperty(automationEndID, endSeconds - clipStart, &edit->getUndoManager());
    automation.setProperty(automationStartValueID, startValue, &edit->getUndoManager());
    automation.setProperty(automationEndValueID, endValue, &edit->getUndoManager());
    clip->state.addChild(automation, -1, &edit->getUndoManager());
    if (auto* runtime = findAutomationRuntime(target))
        runtime->overridden = false;
    markModified();
    edit->getUndoManager().beginNewTransaction();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteClipAutomation(te::EditItemID id, DeviceTarget target)
{
    auto* clip = findClip(id);
    if (clip == nullptr)
        return juce::Result::fail("Select a clip first.");
    if (!target.isValid())
        return juce::Result::fail("Select an automation lane first.");

    for (int i = clip->state.getNumChildren(); --i >= 0;)
    {
        const auto existing = clip->state.getChild(i);
        if (!existing.hasType(clipAutomationID))
            continue;
        const DeviceTarget existingTarget {
            static_cast<int>(existing.getProperty(automationTrackID, -1)),
            static_cast<int>(existing.getProperty(automationSlotID, -1)),
            static_cast<int>(existing.getProperty(automationParameterID, -1))
        };
        if (sameDeviceTarget(existingTarget, target))
        {
            edit->getUndoManager().beginNewTransaction("Delete clip automation");
            clip->state.removeChild(existing, &edit->getUndoManager());
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
    }
    return juce::Result::fail("Automation lane was not found.");
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
    if (!hasClipAutomationTarget(*edit, target))
        return juce::Result::fail("This parameter has no clip automation.");

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

void Session::applyClipAutomationAt(double timelineSeconds)
{
    if (!std::isfinite(timelineSeconds) || timelineSeconds < 0.0)
        return;

    bool changed = false;
    std::vector<DeviceTarget> activeTargets;
    const auto targetTracks = te::getAudioTracks(*edit);
    for (auto* track : te::getAudioTracks(*edit))
        for (auto* clip : track->getClips())
        {
            const auto clipStart = clip->getPosition().time.getStart().inSeconds();
            const auto local = timelineSeconds - clipStart;
            for (const auto& automation : clipAutomations(clip->itemID))
            {
                if (local < automation.startSeconds || local > automation.endSeconds)
                    continue;

                const auto amount = (local - automation.startSeconds) / (automation.endSeconds - automation.startSeconds);
                const auto value = static_cast<float>(automation.startValue + (automation.endValue - automation.startValue) * amount);
                if (!juce::isPositiveAndBelow(automation.target.track, targetTracks.size())
                    || !juce::isPositiveAndBelow(automation.target.slot, targetTracks[automation.target.track]->pluginList.size()))
                    continue;
                auto* plugin = targetTracks[automation.target.track]->pluginList[automation.target.slot];
                if (plugin == nullptr)
                    continue;
                if (auto* parameter = exposedParameterAt(*plugin, automation.target.parameter))
                {
                    const auto range = parameter->getValueRange();
                    auto& runtime = automationRuntimeFor(automation.target);
                    activeTargets.push_back(automation.target);
                    if (!runtime.hasBaseValue)
                    {
                        runtime.baseValue = parameter->getCurrentValue();
                        runtime.hasBaseValue = true;
                    }
                    runtime.active = true;
                    if (runtime.overridden)
                        continue;

                    const auto next = juce::jlimit(range.getStart(),
                                                   exposedParameterMaximum(*plugin, automation.target.parameter, range.getEnd()), value);
                    if (std::abs(parameter->getCurrentValue() - next) > 0.0001f)
                    {
                        parameter->setParameter(next, juce::sendNotification);
                        changed = true;
                    }
                }
            }
        }

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
        if (!juce::isPositiveAndBelow(runtime.target.track, targetTracks.size())
            || !juce::isPositiveAndBelow(runtime.target.slot, targetTracks[runtime.target.track]->pluginList.size()))
            continue;
        auto* plugin = targetTracks[runtime.target.track]->pluginList[runtime.target.slot];
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

}
