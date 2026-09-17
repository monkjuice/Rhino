#include "SessionInternal.h"
#include <algorithm>
#include <set>

// Device creation, inspection and parameter gestures. Serves Device View.

namespace rhino
{
namespace
{
bool isTrackInfrastructure(const juce::String& type)
{
    // Tracktion represents permanent channel-strip facilities as plugins in
    // the processing graph. They are deliberately not user devices.
    return type == UtilityDevice::xmlTypeName || type == "volume" || type == "level";
}

bool isBuiltInInstrument(const juce::String& type)
{
    return type == te::FourOscPlugin::xmlTypeName || type == DrumDevice::xmlTypeName
        || type == RhinoWaveDevice::xmlTypeName;
}

Session::DeviceKind deviceKind(te::Plugin& plugin)
{
    const auto type = plugin.getPluginType();
    if (type == RhinoArpDevice::xmlTypeName)
        return Session::DeviceKind::MidiEffect;
    if (isBuiltInInstrument(type) || isForgePlugin(plugin))
        return Session::DeviceKind::Instrument;
    return Session::DeviceKind::AudioEffect;
}

bool isSelectedPatternInstrument(te::Plugin& plugin, const juce::String& selected)
{
    const auto type = plugin.getPluginType();
    if (type == te::FourOscPlugin::xmlTypeName) return selected.isEmpty() || selected == "synth";
    if (type == DrumDevice::xmlTypeName) return selected == "drums";
    if (type == RhinoWaveDevice::xmlTypeName) return selected == "wave";
    if (isForgePlugin(plugin)) return selected == "forge";
    return true;
}

int channelStripInsertIndex(te::AudioTrack& track)
{
    for (int i = 0; i < track.pluginList.size(); ++i)
        if (auto* plugin = track.pluginList[i]; plugin != nullptr && isTrackInfrastructure(plugin->getPluginType()))
            return i;
    return track.pluginList.size();
}
}

// The master track is one past the last audio track. It wraps the edit's master
// plugin list rather than a track's, and the engine already refuses anything
// that cannot be added to the master, which is what keeps instruments off it.
te::PluginList* Session::pluginListForTrack(int trackIndex) const
{
    if (isMasterTrack(trackIndex))
        return &edit->getMasterPluginList();
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return nullptr;
    return &tracks[trackIndex]->pluginList;
}

juce::Result Session::addAudioEffect(AudioEffect effect, int trackIndex)
{
    const char* type = nullptr;
    juce::String name;
    if (!effectTypeAndName(effect, type, name))
        return juce::Result::fail("That audio effect could not be created.");

    auto* list = pluginListForTrack(trackIndex);
    if (list == nullptr)
        return juce::Result::fail("Drop audio effects on a track.");

    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");
    if (isMasterTrack(trackIndex))
    {
        if (!plugin->canBeAddedToMaster())
            return juce::Result::fail(name + " cannot go on the main track.");
        list->insertPlugin(plugin, list->size(), nullptr);
    }
    else
    {
        auto* track = te::getAudioTracks(*edit)[trackIndex];
        list->insertPlugin(plugin, channelStripInsertIndex(*track), nullptr);
    }
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
    if (instrument == Instrument::RhinoForge && !forgeDescription)
        initialiseExternalPlugins(true);

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track.");

    auto* track = tracks[trackIndex];
    const char* type = nullptr;
    juce::String name;
    switch (instrument)
    {
        case Instrument::FourOsc: type = te::FourOscPlugin::xmlTypeName; name = "4OSC"; break;
        case Instrument::RhinoWave: type = RhinoWaveDevice::xmlTypeName; name = "Rhino Wave"; break;
        case Instrument::RhinoForge: name = "Rhino Forge"; break;
        case Instrument::Drums:   type = DrumDevice::xmlTypeName;        name = "Rhino Drums"; break;
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
        edit->state.setProperty("rhinoPatternInstrument",
                                instrument == Instrument::Drums ? "drums"
                                    : instrument == Instrument::RhinoWave ? "wave"
                                    : instrument == Instrument::RhinoForge ? "forge" : "synth",
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
        case MidiEffect::RhinoArp: type = RhinoArpDevice::xmlTypeName; name = "Rhino Arp"; break;
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
        if (existing != nullptr && deviceKind(*existing) == DeviceKind::Instrument)
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

juce::Result Session::addDrumKit(DrumDevice::Kit kit, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop drum kits on a track.");
    const auto name = DrumDevice::kitName(kit);
    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    if (const auto result = switchTrackInstrument(*edit, *tracks[trackIndex], Instrument::Drums, changed);
        result.failed())
        return result;
    auto* drums = findDrumDevice(*tracks[trackIndex]);
    if (drums == nullptr)
        return juce::Result::fail("The drum instrument could not be created.");
    drums->setKit(kit);
    // The track is named after its instrument, and the kit is what the
    // instrument now is.
    tracks[trackIndex]->setName(name);
    if (trackIndex == 0)
        edit->state.setProperty("rhinoPatternInstrument", "drums", &edit->getUndoManager());
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
    auto* list = pluginListForTrack(track);
    if (list == nullptr) return slots;
    const auto selectedPatternInstrument = edit->state.getProperty("rhinoPatternInstrument").toString();
    for (int pluginIndex = 0; pluginIndex < list->size(); ++pluginIndex)
    {
        auto* plugin = (*list)[pluginIndex];
        if (plugin == nullptr) continue;
        const auto type = plugin->getPluginType();
        if (isTrackInfrastructure(type))
            continue;
        if (track == 0 && (isBuiltInInstrument(type) || isForgePlugin(*plugin))
            && !isSelectedPatternInstrument(*plugin, selectedPatternInstrument))
            continue;
        const auto corePatternInstrument = track == 0 && deviceKind(*plugin) == DeviceKind::Instrument;
        slots.push_back({plugin->getDisplayName(), type, deviceKind(*plugin), pluginIndex,
                         plugin->isEnabled(), !corePatternInstrument});
    }
    return slots;
}

std::vector<Session::DeviceParameter> Session::deviceParameters(int track, int slot) const
{
    std::vector<DeviceParameter> parameters;
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return parameters;
    auto* plugin = (*list)[slot];
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
                                      hasActiveTrackAutomation(*edit, target),
                                      runtime != nullptr && runtime->overridden});
            }
        return parameters;
    }

    if (auto* wavePlugin = dynamic_cast<RhinoWaveDevice*>(plugin))
    {
        for (int i = 0; i < 23; ++i)
            if (auto* parameter = rhinoWaveMacroParameterAt(*wavePlugin, i))
            {
                const auto range = parameter->getValueRange();
                if (!std::isfinite(range.getStart()) || !std::isfinite(range.getEnd()) || range.getLength() <= 0.0f)
                    continue;
                const DeviceTarget target {track, slot, i};
                const auto* runtime = findAutomationRuntime(target);
                parameters.push_back({rhinoWaveMacroName(i),
                                      formatRhinoWaveMacroValue(i, parameter->getCurrentValue(), *parameter),
                                      parameter->getCurrentValue(),
                                      range.getStart(),
                                      range.getEnd(),
                                      parameter->isDiscrete(),
                                      hasActiveTrackAutomation(*edit, target),
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
                              hasActiveTrackAutomation(*edit, target),
                              runtime != nullptr && runtime->overridden});
    }
    return parameters;
}

juce::Result Session::beginDeviceParameterGesture(int track, int slot, int parameterIndex)
{
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = (*list)[slot];
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
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = (*list)[slot];
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
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = (*list)[slot];
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
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a device first.");
    auto* plugin = (*list)[slot];
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
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return juce::Result::fail("Select a removable device first.");
    auto* plugin = (*list)[slot];
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
