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
    // the processing graph. They are deliberately not user devices. "volume"
    // and "level" are the engine's own and have no catalog entry.
    if (const auto* device = DeviceCatalog::byTypeName(type))
        return device->infrastructure;
    return type == "volume" || type == "level";
}

bool isBuiltInInstrument(const juce::String& type)
{
    const auto* device = DeviceCatalog::byTypeName(type);
    return device != nullptr && device->kind == DeviceKind::Instrument;
}

Session::DeviceKind deviceKind(te::Plugin& plugin)
{
    if (const auto* device = DeviceCatalog::byTypeName(plugin.getPluginType()))
        return device->kind;
    // Forge is an external plugin and has no type name to look up.
    if (isForgePlugin(plugin))
        return Session::DeviceKind::Instrument;
    return Session::DeviceKind::AudioEffect;
}

bool isSelectedPatternInstrument(te::Plugin& plugin, const juce::String& selected)
{
    const auto* device = DeviceCatalog::byTypeName(plugin.getPluginType());
    if (device == nullptr && isForgePlugin(plugin))
        device = DeviceCatalog::byId("RhinoForge");
    if (device == nullptr || device->patternKey.isEmpty())
        return true;
    // An unset property means the original starter synth.
    if (selected.isEmpty())
        return device->patternKey == "synth";
    return device->patternKey == selected;
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
    const auto* device = audioEffectDescriptor(effect);
    if (device == nullptr)
        return juce::Result::fail("That audio effect could not be created.");
    return addDevice(device->id, trackIndex);
}

// The one entry point for adding a device: everything else, including the
// enum overloads above, comes through here. A device with no enum of its own
// is added exactly like one that has.
juce::Result Session::addDevice(const juce::String& deviceId, int trackIndex)
{
    const auto* device = DeviceCatalog::byId(deviceId);
    if (device == nullptr)
        return juce::Result::fail("That device is not in this build.");
    // A group carries audio that has already been played: there is nothing for
    // an instrument to play, and no sequence for a MIDI effect to act on.
    if (isGroupBusTrack(trackIndex) && device->kind != DeviceKind::AudioEffect)
        return juce::Result::fail("A group track takes audio effects only.");
    if (device->kind == DeviceKind::Instrument)
        return addInstrumentDevice(*device, trackIndex);
    if (device->kind == DeviceKind::MidiEffect)
        return addMidiEffectDevice(*device, trackIndex);

    const auto& name = device->displayName;
    const auto& type = device->typeName;
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
    const auto* device = audioEffectDescriptor(effect);
    if (device == nullptr)
        return juce::Result::fail("That audio effect could not be created.");
    return addClipDevice(device->id, clipID);
}

juce::Result Session::addClipDevice(const juce::String& deviceId, te::EditItemID clipID)
{
    const auto* device = DeviceCatalog::byId(deviceId);
    if (device == nullptr || device->kind != DeviceKind::AudioEffect)
        return juce::Result::fail("Clip effects can only be audio effects.");
    const auto& name = device->displayName;
    const auto& type = device->typeName;

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
    const auto* device = descriptorFor(instrument);
    if (device == nullptr)
        return juce::Result::fail("That instrument is not in this build.");
    return addDevice(device->id, trackIndex);
}

juce::Result Session::addInstrumentDevice(const DeviceDescriptor& device, int trackIndex)
{
    // An external instrument may not have been scanned for yet.
    if (device.external && !forgeDescription)
        initialiseExternalPlugins(true);

    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop instruments on a track.");

    auto* track = tracks[trackIndex];
    const auto& name = device.displayName;

    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    // A channel-strip facility is added to the chain rather than becoming the
    // track's instrument, so it does not displace one.
    if (device.infrastructure)
    {
        te::Plugin* plugin = nullptr;
        const auto result = ensurePlugin(*edit, *track, device.typeName, track->pluginList.size(), plugin, changed);
        if (result.failed())
            return juce::Result::fail(name + " could not be created.");
    }
    else
    {
        const auto result = switchTrackInstrument(*edit, *track, device, changed,
                                                  forgeDescription ? &*forgeDescription : nullptr);
        if (result.failed())
            return result;
    }
    if (trackIndex == 0 && !device.infrastructure && device.patternKey.isNotEmpty())
        edit->state.setProperty("rhinoPatternInstrument", device.patternKey, &edit->getUndoManager());
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
    const auto* device = descriptorFor(effect);
    if (device == nullptr)
        return juce::Result::fail("That MIDI effect is not in this build.");
    return addDevice(device->id, trackIndex);
}

juce::Result Session::addMidiEffectDevice(const DeviceDescriptor& device, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop MIDI FX on an instrument track.");

    const auto& name = device.displayName;
    const auto& type = device.typeName;
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

juce::Result Session::addDrumKit(DrumKit kit, int trackIndex)
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(trackIndex, tracks.size()))
        return juce::Result::fail("Drop drum kits on a track.");
    const auto name = DrumDevice::kitName(kit);
    edit->getUndoManager().beginNewTransaction("Add " + name);
    bool changed = false;
    const auto* drumDevice = DeviceCatalog::byId("Drums");
    if (drumDevice == nullptr)
        return juce::Result::fail("The drum instrument is not in this build.");
    if (const auto result = switchTrackInstrument(*edit, *tracks[trackIndex], *drumDevice, changed);
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
    // The starter chain is a property of the first track, not of the devices
    // themselves, so this is a list of ids rather than a catalog flag.
    const auto* device = DeviceCatalog::byTypeName(type);
    const auto id = device != nullptr ? device->id : juce::String();
    const auto coreStarterDevice = track == 0 && (id == "Utility" || id == "FourOsc" || id == "Drums");
    if (plugin == nullptr || coreStarterDevice || isTrackInfrastructure(type))
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
