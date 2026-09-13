#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Device creation, inspection and parameter gestures. Serves DeviceRack.

namespace theta
{

juce::Result Session::addAudioEffect(AudioEffect effect, int trackIndex)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop audio effects on a track.");

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    track->pluginList.insertPlugin(plugin, track->pluginList.size(), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addClipAudioEffect(AudioEffect effect, te::EditItemID clipID)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    auto* clip = dynamic_cast<te::AudioClipBase*>(findClip(clipID));
    if (clip == nullptr)
        return juce::Result::fail("Clip effects can be dropped on audio clips.");
    if (!clip->canHaveEffects())
        return juce::Result::fail("This audio clip cannot host effects while warped or reversed.");

    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    if (!plugin->canBeAddedToClip())
        return juce::Result::fail(name + " cannot be added to a clip.");

    edit->getUndoManager().beginNewTransaction("Add " + name + " to clip");
    clip->getPluginList()->insertPlugin(plugin, clip->getPluginList()->size(), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addInstrument(Instrument instrument, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track.");

    auto* track = tracks[trackIndex];
    const char* type = nullptr;
    juce::String name;
    switch (instrument)
    {
        case Instrument::FourOsc: type = te::FourOscPlugin::xmlTypeName; name = "4OSC"; break;
        case Instrument::ThetaWave: type = ThetaWaveDevice::xmlTypeName; name = "Theta Wave"; break;
        case Instrument::ThetaForge: name = "Theta Forge"; break;
        case Instrument::Drums:   type = DrumDevice::xmlTypeName;        name = "Theta Drums"; break;
        case Instrument::Utility: type = UtilityDevice::xmlTypeName;     name = "Utility"; break;
    }

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    if (instrument == Instrument::Utility)
    {
        te::Plugin* plugin = nullptr;
        const auto result = ensurePlugin(*edit, *track, type, track->pluginList.size(), plugin, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    else
    {
        const auto result = switchTrackInstrument(*edit, *track, instrument, changed,
                                                  forgeDescription ? &*forgeDescription : nullptr);
        if (result.failed())
            return result;
    }
    if (trackIndex == 0 && instrument != Instrument::Utility)
        edit->state.setProperty("thetaPatternInstrument",
                                instrument == Instrument::Drums ? "drums"
                                    : instrument == Instrument::ThetaWave ? "wave"
                                    : instrument == Instrument::ThetaForge ? "forge" : "synth",
                                &edit->getUndoManager());
    edit->getUndoManager().beginNewTransaction();
    if (changed)
        markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addMidiEffect(MidiEffect effect, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop MIDI FX on an instrument track.");

    const char* type = nullptr;
    juce::String name;
    switch (effect)
    {
        case MidiEffect::ThetaArp: type = ThetaArpDevice::xmlTypeName; name = "Theta Arp"; break;
    }

    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");

    int insertIndex = 0;
    for (int i = 0; i < track->pluginList.size(); ++i)
    {
        auto* existing = track->pluginList[i];
        if (existing != nullptr && (existing->getPluginType() == te::FourOscPlugin::xmlTypeName
                                    || existing->getPluginType() == DrumDevice::xmlTypeName))
        {
            insertIndex = i;
            break;
        }
        insertIndex = i + 1;
    }

    track->pluginList.insertPlugin(plugin, juce::jlimit(0, track->pluginList.size(), insertIndex), nullptr);
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

std::vector<Session::DeviceSlot> Session::deviceSlots(int track) const
{
    std::vector<DeviceSlot> slots;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size())) return slots;
    for (auto* plugin : tracks[track]->pluginList)
    {
        if (plugin == nullptr) continue;
        const auto type = plugin->getPluginType();
        const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
            || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
        slots.push_back({plugin->getDisplayName(), type, plugin->isEnabled(),
                         !coreStarterDevice && type != UtilityDevice::xmlTypeName});
    }
    return slots;
}

std::vector<Session::DeviceParameter> Session::deviceParameters(int track, int slot) const
{
    std::vector<DeviceParameter> parameters;
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return parameters;
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return parameters;

    if (auto* synthPlugin = dynamic_cast<te::FourOscPlugin*>(plugin))
    {
        for (int i = 0; i < 6; ++i)
            if (auto* parameter = fourOscMacroParameterAt(*synthPlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                const DeviceTarget target {track, slot, i};
                const auto* runtime = findAutomationRuntime(target);
                parameters.push_back({fourOscMacroName(i),
                                      formatFourOscMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      exposedParameterMaximum(*plugin, i, range.getEnd()),
                                      parameter->isDiscrete(),
                                      hasClipAutomationTarget(*edit, target),
                                      runtime != nullptr && runtime->overridden});
            }
        return parameters;
    }

    if (auto* wavePlugin = dynamic_cast<ThetaWaveDevice*>(plugin))
    {
        for (int i = 0; i < 23; ++i)
            if (auto* parameter = thetaWaveMacroParameterAt(*wavePlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                const DeviceTarget target {track, slot, i};
                const auto* runtime = findAutomationRuntime(target);
                parameters.push_back({thetaWaveMacroName(i),
                                      formatThetaWaveMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      range.getEnd(),
                                      parameter->isDiscrete(),
                                      hasClipAutomationTarget(*edit, target),
                                      runtime != nullptr && runtime->overridden});
            }
        return parameters;
    }

    int parameterIndex = 0;
    for (auto* parameter : plugin->getAutomatableParameters())
    {
        if (parameter == nullptr || !parameter->isParameterActive())
            continue;
        const auto currentIndex = parameterIndex++;
        const auto range = parameter->getValueRange();
        if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
            continue;
        const DeviceTarget target {track, slot, currentIndex};
        const auto* runtime = findAutomationRuntime(target);
        parameters.push_back({parameter->getParameterShortName(18),
                              parameter->getCurrentValueAsStringWithLabel(),
                              parameter->getCurrentValue(),
                              range.getStart(),
                              range.getEnd(),
                              parameter->isDiscrete(),
                              hasClipAutomationTarget(*edit, target),
                              runtime != nullptr && runtime->overridden});
    }
    return parameters;
}

juce::Result Session::beginDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    lastTouchedParameter = {track, slot, parameterIndex};
    if (auto* runtime = findAutomationRuntime(lastTouchedParameter); runtime != nullptr && runtime->active)
        runtime->overridden = true;
    parameter->parameterChangeGestureBegin();
    return juce::Result::ok();
}

juce::Result Session::setDeviceParameter(int track, int slot, int parameterIndex, float value)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    lastTouchedParameter = {track, slot, parameterIndex};
    const auto range = parameter->getValueRange();
    const auto next = juce::jlimit(range.getStart(), exposedParameterMaximum(*plugin, parameterIndex, range.getEnd()), value);
    auto& runtime = automationRuntimeFor(lastTouchedParameter);
    runtime.baseValue = next;
    runtime.hasBaseValue = true;
    if (runtime.active)
        runtime.overridden = true;
    parameter->setParameter(next, juce::sendNotification);
    markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::endDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    auto* parameter = exposedParameterAt(*plugin, parameterIndex);
    if (parameter == nullptr) return juce::Result::fail("Select a parameter first.");
    parameter->parameterChangeGestureEnd();
    edit->getUndoManager().beginNewTransaction();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::toggleDeviceEnabled(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    if (plugin == nullptr) return juce::Result::fail("Select a device first.");
    edit->getUndoManager().beginNewTransaction(plugin->isEnabled() ? "Bypass device" : "Enable device");
    plugin->setEnabled(!plugin->isEnabled());
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::deleteDevice(int track, int slot)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()) || !juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
        return juce::Result::fail("Select a removable device first.");
    auto* plugin = tracks[track]->pluginList[slot];
    const auto type = plugin->getPluginType();
    const auto coreStarterDevice = track == 0 && (type == UtilityDevice::xmlTypeName
        || type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName);
    if (plugin == nullptr || coreStarterDevice || type == UtilityDevice::xmlTypeName)
        return juce::Result::fail("Core devices stay in the starter track chain.");
    edit->getUndoManager().beginNewTransaction("Delete device");
    plugin->removeFromParent();
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

}
