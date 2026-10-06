#include "SessionInternal.h"
#include "DevicePreset.h"

// Device presets: saving a device's controls to a .rnd file, and putting them
// back on a device or on a new one. The file format is core/DevicePreset.h;
// where presets are found is ContentLibrary::presets().

namespace rhino
{
namespace
{
// Only Rhino's own devices have presets. An external plugin keeps its sound in
// its own state, which a list of parameter values does not capture, and a
// channel-strip facility is part of the track rather than a sound.
const DeviceDescriptor* presetDeviceFor(te::Plugin& plugin)
{
    const auto* entry = DeviceCatalog::byTypeName(plugin.getPluginType());
    return entry != nullptr && !entry->external && !entry->infrastructure ? entry : nullptr;
}
}

bool applyDevicePreset(te::Plugin& plugin, const DevicePreset& preset)
{
    auto changed = false;
    for (auto* parameter : plugin.getAutomatableParameters())
    {
        if (parameter == nullptr || !parameter->isParameterActive())
            continue;
        auto wanted = preset.valueOf(parameter->paramID);
        if (!wanted.has_value())
            wanted = parameter->getDefaultValue();
        if (!wanted.has_value())
            continue;
        const auto value = parameter->valueRange.snapToLegalValue(*wanted);
        // Writing an unchanged value would still be an undoable action.
        if (parameter->getCurrentBaseValue() == value)
            continue;
        parameter->setParameter(value, juce::sendNotificationSync);
        changed = true;
    }
    return changed;
}

juce::Result Session::saveDevicePreset(int track, int slot, const juce::File& file)
{
    auto* plugin = devicePlugin(track, slot);
    if (plugin == nullptr)
        return juce::Result::fail("There is no device there to save.");
    const auto* device = presetDeviceFor(*plugin);
    if (device == nullptr)
        return juce::Result::fail(plugin->getName() + " has no presets. Only Rhino's own devices do.");
    DevicePreset preset;
    preset.deviceId = device->id;
    // The value as set, not where a lane happens to have moved it.
    for (auto* parameter : plugin->getAutomatableParameters())
        if (parameter != nullptr && parameter->isParameterActive())
            preset.values.emplace_back(parameter->paramID, parameter->getCurrentBaseValue());
    return preset.write(file);
}

juce::Result Session::loadDevicePreset(int track, int slot, const juce::File& file)
{
    DevicePreset preset;
    if (const auto read = DevicePreset::read(file, preset); read.failed())
        return read;
    auto* plugin = devicePlugin(track, slot);
    if (plugin == nullptr)
        return juce::Result::fail("There is no device there to load a preset into.");
    const auto* device = presetDeviceFor(*plugin);
    if (device == nullptr || device->id != preset.deviceId)
    {
        const auto* wanted = DeviceCatalog::byId(preset.deviceId);
        return juce::Result::fail(DevicePreset::nameOf(file) + " is a preset for "
                                  + (wanted != nullptr ? wanted->displayName : preset.deviceId) + ", not "
                                  + plugin->getName() + ".");
    }
    edit->getUndoManager().beginNewTransaction("Load " + DevicePreset::nameOf(file));
    const auto changed = applyDevicePreset(*plugin, preset);
    edit->getUndoManager().beginNewTransaction();
    if (changed)
        markModified();
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

juce::Result Session::addDeviceFromPreset(const juce::File& file, int track)
{
    DevicePreset preset;
    if (const auto read = DevicePreset::read(file, preset); read.failed())
        return read;
    const auto* device = DeviceCatalog::byId(preset.deviceId);
    if (device == nullptr || device->external || device->infrastructure)
        return juce::Result::fail(DevicePreset::nameOf(file) + " is for a device this build does not have.");
    return addDevice(device->id, track, &preset);
}
}
