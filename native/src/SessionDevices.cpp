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

Session::DeviceKind deviceKind(te::Plugin& plugin)
{
    if (const auto* device = DeviceCatalog::byTypeName(plugin.getPluginType()))
        return device->kind;
    // Forge is an external plugin and has no type name to look up.
    if (isForgePlugin(plugin))
        return Session::DeviceKind::Instrument;
    return Session::DeviceKind::AudioEffect;
}

// A chain runs MIDI effects, then the instrument, then audio effects. That is
// the order every add path already builds, and a drag may not break it: a MIDI
// effect behind the instrument has no notes left to rewrite, and an audio
// effect in front of it has no audio yet.
int chainRank(Session::DeviceKind kind)
{
    return kind == Session::DeviceKind::MidiEffect ? 0
         : kind == Session::DeviceKind::Instrument ? 1
         : 2;
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
    // An instrument is played by notes, and only a MIDI track holds any. A
    // facility is exempt: it joins the chain rather than becoming what the
    // track plays, so it belongs on an audio track as much as a MIDI one.
    if (!device.infrastructure && trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop instruments on a MIDI track instead.");

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
    // A MIDI effect rewrites notes on their way to an instrument. An audio
    // track carries no notes for it to act on.
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop MIDI FX on a MIDI track instead.");

    const auto& name = device.displayName;
    const auto& type = device.typeName;
    auto* track = tracks[trackIndex];
    edit->getUndoManager().beginNewTransaction("Add " + name);
    auto plugin = edit->getPluginCache().createNewPlugin(type, {});
    if (plugin == nullptr)
        return juce::Result::fail(name + " could not be created.");

    // In front of the instrument when there is one. With none yet, after the
    // MIDI effects already at the front of the chain - which is exactly where
    // switchTrackInstrument will put an instrument, so one added later lands
    // behind this rather than ahead of it. Appending to the end instead left
    // the effect after the channel strip, and then after the instrument.
    int insertIndex = -1;
    for (int i = 0; i < track->pluginList.size(); ++i)
        if (auto* existing = track->pluginList[i]; existing != nullptr && deviceKind(*existing) == DeviceKind::Instrument)
        {
            insertIndex = i;
            break;
        }
    if (insertIndex < 0)
    {
        insertIndex = 0;
        while (insertIndex < track->pluginList.size())
        {
            auto* existing = track->pluginList[insertIndex];
            if (existing == nullptr || deviceKind(*existing) != DeviceKind::MidiEffect)
                break;
            ++insertIndex;
        }
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
    // A kit is an instrument, so it goes where an instrument goes.
    if (trackType(trackIndex) != TrackType::midi)
        return juce::Result::fail("That is an audio track. Drop drum kits on a MIDI track instead.");
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
    edit->getUndoManager().beginNewTransaction();
    markModified();
    if (edit->getTransport().isPlaying())
        edit->restartPlayback();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// A device with a face of its own needs the device itself, not a copy of its
// numbers: a meter reads what the DSP is doing right now, and a scale is a
// dozen booleans that no parameter list has a shape for.  Callers cast to the
// device they know about and cope with a null, which is what they get for a
// slot holding something else or nothing.
te::Plugin* Session::devicePlugin(int track, int slot) const
{
    auto* list = pluginListForTrack(track);
    if (list == nullptr || !juce::isPositiveAndBelow(slot, list->size()))
        return nullptr;
    return (*list)[slot];
}

std::vector<Session::DeviceSlot> Session::deviceSlots(int track) const
{
    std::vector<DeviceSlot> slots;
    auto* list = pluginListForTrack(track);
    if (list == nullptr) return slots;
    // Every device a track runs is shown and can be taken off. The channel
    // strip is the one thing hidden, because it is the track's own rather than
    // something put on it. No track is special for being first.
    for (int pluginIndex = 0; pluginIndex < list->size(); ++pluginIndex)
    {
        auto* plugin = (*list)[pluginIndex];
        if (plugin == nullptr) continue;
        const auto type = plugin->getPluginType();
        if (isTrackInfrastructure(type))
            continue;
        slots.push_back({plugin->getDisplayName(), type, deviceKind(*plugin), pluginIndex,
                         plugin->isEnabled(), true});
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

// Reordering is the one device edit that is about the chain rather than about
// one device, so it is the one that speaks in visible positions: the rack
// hides the channel strip and any dormant instrument, and the person drags
// what they can see.
juce::Result Session::moveDevice(int track, int fromDevice, int toDevice)
{
    auto* list = pluginListForTrack(track);
    if (list == nullptr)
        return juce::Result::fail("That track has no device chain.");
    const auto slots = deviceSlots(track);
    const auto count = static_cast<int>(slots.size());
    if (!juce::isPositiveAndBelow(fromDevice, count))
        return juce::Result::fail("Select a device first.");
    toDevice = juce::jlimit(0, count - 1, toDevice);
    if (toDevice == fromDevice)
        return juce::Result::ok();

    auto order = slots;
    const auto moved = order[static_cast<size_t>(fromDevice)];
    order.erase(order.begin() + fromDevice);
    order.insert(order.begin() + toDevice, moved);
    for (int i = 1; i < count; ++i)
        if (chainRank(order[static_cast<size_t>(i)].kind) < chainRank(order[static_cast<size_t>(i - 1)].kind))
            return juce::Result::fail(
                moved.kind == DeviceKind::MidiEffect ? "MIDI FX run before the instrument."
                : moved.kind == DeviceKind::Instrument ? "The instrument runs after the MIDI FX and before the audio effects."
                : "Audio effects run after the instrument.");

    te::Plugin::Ptr plugin = (*list)[moved.pluginIndex];
    if (plugin == nullptr)
        return juce::Result::fail("Select a device first.");

    // The plugin leaves the list before it rejoins it, so every plugin behind
    // it has shifted down one by the time the insert index is read. The index
    // is taken from the neighbour the device is landing against, never
    // counted, because the list also holds plugins the chain does not show.
    const auto removed = moved.pluginIndex;
    const auto shifted = [removed](int pluginIndex) { return pluginIndex > removed ? pluginIndex - 1 : pluginIndex; };
    const auto insertIndex = toDevice + 1 < count
        ? shifted(order[static_cast<size_t>(toDevice + 1)].pluginIndex)
        : shifted(order[static_cast<size_t>(toDevice - 1)].pluginIndex) + 1;

    edit->getUndoManager().beginNewTransaction("Move " + moved.name);
    plugin->removeFromParent();
    list->insertPlugin(plugin, juce::jlimit(0, list->size(), insertIndex), nullptr);
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
    if (plugin == nullptr)
        return juce::Result::fail("Select a removable device first.");
    if (isTrackInfrastructure(plugin->getPluginType()))
        return juce::Result::fail("The track's channel strip stays on the track.");
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
