#include "SessionInternal.h"
#include <algorithm>
#include <utility>

// The knobs an external plugin shows on the rack, and mapping more of them
// from its own window.
//
// Rhino's own devices declare a handful of controls and the rack shows them
// all. Rhino Forge declares several hundred, so its rack is a chosen list:
// the controls a synth is played by until someone says otherwise, and from
// then on whatever has been mapped from its window. The list is kept on the
// device's own state, by the plugin's parameter ids rather than by index, so
// it is saved with the document, follows the device along the chain and
// survives the plugin gaining parameters in the middle of its list. A lane
// names a knob by its place in this list, which is why a knob only ever joins
// at the end and taking one off moves the lanes above it down.

namespace rhino
{
namespace
{
// Tracktion names an external plugin's parameter after its index in the
// plugin's own list, so that is the key from a plugin parameter to the
// engine's automatable one.
juce::String automatableIdFor(int pluginParameter)
{
    return juce::String(pluginParameter);
}

juce::AudioProcessor* processorOf(const te::Plugin& plugin)
{
    return dynamic_cast<const te::ExternalPlugin*>(&plugin) != nullptr ? plugin.getWrappedAudioProcessor() : nullptr;
}

// The id a plugin parameter is kept under: the one its host wrapper reports,
// which for a VST3 is the plugin's own parameter id. Empty for a parameter
// with none, which cannot be kept.
juce::String idOf(const juce::AudioProcessorParameter& parameter)
{
    if (const auto* hosted = dynamic_cast<const juce::HostedAudioProcessorParameter*>(&parameter))
        return hosted->getParameterID();
    return {};
}

int pluginParameterNamed(const juce::AudioProcessor& processor, const juce::String& name)
{
    const auto& parameters = processor.getParameters();
    for (int i = 0; i < parameters.size(); ++i)
        if (parameters[i] != nullptr && parameters[i]->getName(1024) == name)
            return i;
    return -1;
}

// What a device shows before anyone has mapped a knob: the parameters its
// catalog entry names, in that order (Forge's are the controls a synth is
// played by, Duck's the ones a duck is shaped by). An external plugin whose
// entry names none shows its first twelve, which is what the generic face
// showed before the list existed.
juce::StringArray defaultMappedIds(const te::Plugin& plugin, const juce::AudioProcessor& processor)
{
    juce::StringArray ids;
    if (const auto* device = externalDevice(plugin); device != nullptr && !device->rackParameters.isEmpty())
    {
        for (const auto& name : device->rackParameters)
            if (const auto index = pluginParameterNamed(processor, name); index >= 0)
                if (const auto id = idOf(*processor.getParameters()[index]); id.isNotEmpty())
                    ids.add(id);
        return ids;
    }
    for (const auto* parameter : processor.getParameters())
    {
        if (ids.size() >= 12)
            break;
        if (parameter != nullptr && parameter->isAutomatable())
            if (const auto id = idOf(*parameter); id.isNotEmpty())
                ids.add(id);
    }
    return ids;
}

juce::StringArray mappedIds(const te::Plugin& plugin, const juce::AudioProcessor& processor)
{
    if (!plugin.state.hasProperty(mappedParametersID))
        return defaultMappedIds(plugin, processor);
    juce::StringArray ids;
    ids.addTokens(plugin.state.getProperty(mappedParametersID).toString(), ",", {});
    ids.removeEmptyStrings();
    return ids;
}

// Resolving a list means finding each id among several hundred plugin
// parameters, and then that parameter's engine twin, which a knob drag asks
// for on every pixel. So a list is resolved once and kept, by indices only,
// and checked against the plugin on every use: the plugin's parameter count
// and the engine parameter's id say whether the plugin and the list are
// still the ones the entries were made for. Message thread only, like
// everything else here.
struct ResolvedList
{
    te::EditItemID plugin;
    juce::String list;
    int pluginParameters = -1;
    struct Entry
    {
        int pluginParameter = -1;   // -1 for an id the plugin no longer has
        int automatable = -1;       // its place in the engine's list, or -1
        juce::String automatableId;
    };
    std::vector<Entry> entries;
};

ResolvedList& resolvedListFor(te::Plugin& plugin, juce::AudioProcessor& processor)
{
    static std::vector<ResolvedList> lists;
    const auto stored = plugin.state.getProperty(mappedParametersID).toString();
    const auto count = processor.getParameters().size();
    auto found = std::find_if(lists.begin(), lists.end(), [&plugin] (const auto& list) { return list.plugin == plugin.itemID; });
    if (found != lists.end() && found->list == stored && found->pluginParameters == count)
        return *found;
    if (found == lists.end())
    {
        // A document holds a few external devices at most; the oldest entry
        // makes room rather than the list growing with every document opened.
        if (lists.size() >= 8)
            lists.erase(lists.begin());
        lists.push_back({});
        found = std::prev(lists.end());
    }
    auto& resolved = *found;
    resolved.plugin = plugin.itemID;
    resolved.list = stored;
    resolved.pluginParameters = count;
    resolved.entries.clear();

    const auto& parameters = processor.getParameters();
    std::vector<juce::String> ids;
    ids.reserve(static_cast<size_t>(count));
    for (const auto* parameter : parameters)
        ids.push_back(parameter != nullptr ? idOf(*parameter) : juce::String());
    const auto& automatables = plugin.getAutomatableParameters();
    for (const auto& id : mappedIds(plugin, processor))
    {
        ResolvedList::Entry entry;
        for (int i = 0; i < count && entry.pluginParameter < 0; ++i)
            if (ids[static_cast<size_t>(i)] == id)
                entry.pluginParameter = i;
        if (entry.pluginParameter >= 0)
        {
            entry.automatableId = automatableIdFor(entry.pluginParameter);
            for (int i = 0; i < automatables.size() && entry.automatable < 0; ++i)
                if (automatables[i] != nullptr && automatables[i]->paramID == entry.automatableId)
                    entry.automatable = i;
        }
        resolved.entries.push_back(std::move(entry));
    }
    return resolved;
}
}

const juce::Identifier mappedParametersID {"rhinoMappedParameters"};

bool hasMappedParameters(const te::Plugin& plugin)
{
    return processorOf(plugin) != nullptr;
}

int mappedParameterCount(te::Plugin& plugin)
{
    auto* processor = processorOf(plugin);
    return processor != nullptr ? static_cast<int>(resolvedListFor(plugin, *processor).entries.size()) : 0;
}

te::AutomatableParameter* mappedParameterAt(te::Plugin& plugin, int index)
{
    auto* processor = processorOf(plugin);
    if (processor == nullptr)
        return nullptr;
    const auto& resolved = resolvedListFor(plugin, *processor);
    if (!juce::isPositiveAndBelow(index, resolved.entries.size()))
        return nullptr;
    const auto& entry = resolved.entries[static_cast<size_t>(index)];
    const auto& automatables = plugin.getAutomatableParameters();
    if (!juce::isPositiveAndBelow(entry.automatable, automatables.size()))
        return nullptr;
    auto* parameter = automatables[entry.automatable];
    // The engine's list can only have been built again by the plugin being
    // loaded again, which changed its parameter count and so the entries;
    // all the same, an entry that no longer names its parameter is not used.
    return parameter != nullptr && parameter->paramID == entry.automatableId ? parameter : nullptr;
}

bool Session::deviceHasMappedParameters(int track, int slot) const
{
    const auto* plugin = devicePlugin(track, slot);
    return plugin != nullptr && hasMappedParameters(*plugin);
}

// The whole of a map without the announcement, so the window's mapping can
// say what it did and announce once. `alreadyMapped` says the refusal was
// for a knob already on the rack, which a mapping passes over in silence.
juce::Result Session::mapDeviceParameterInEdit(int track, int slot, int pluginParameter, juce::String& name,
                                               bool* alreadyMapped)
{
    if (alreadyMapped != nullptr)
        *alreadyMapped = false;
    auto* plugin = devicePlugin(track, slot);
    auto* processor = plugin != nullptr ? processorOf(*plugin) : nullptr;
    if (processor == nullptr)
        return juce::Result::fail("Only a plugin with a window of its own maps knobs onto the rack.");
    const auto& parameters = processor->getParameters();
    if (!juce::isPositiveAndBelow(pluginParameter, parameters.size()) || parameters[pluginParameter] == nullptr)
        return juce::Result::fail("That control is not one of the plugin's parameters.");
    auto& parameter = *parameters[pluginParameter];
    name = parameter.getName(1024);
    const auto id = idOf(parameter);
    if (id.isEmpty() || id.containsChar(','))
        return juce::Result::fail(name + " has no id the rack can keep it by.");
    // A parameter the plugin says cannot be automated has no engine twin:
    // Tracktion made none, so there is nothing for a knob or a lane to drive.
    auto* automatable = [plugin, pluginParameter] () -> te::AutomatableParameter*
    {
        for (auto* candidate : plugin->getAutomatableParameters())
            if (candidate != nullptr && candidate->paramID == automatableIdFor(pluginParameter))
                return candidate;
        return nullptr;
    }();
    if (!parameter.isAutomatable() || automatable == nullptr)
        return juce::Result::fail(name + " cannot be automated, so it cannot go on the rack.");
    auto ids = mappedIds(*plugin, *processor);
    if (ids.contains(id))
    {
        if (alreadyMapped != nullptr)
            *alreadyMapped = true;
        return juce::Result::fail(name + " is already on the rack.");
    }
    if (ids.size() >= maximumMappedParameters)
        return juce::Result::fail("The rack is full: it holds " + juce::String(maximumMappedParameters)
                                  + " knobs. Take one off before mapping another.");
    ids.add(id);
    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Map " + name);
    plugin->state.setProperty(mappedParametersID, ids.joinIntoString(","), &undoManager);
    closeUndoStep();
    // A knob on the rack renders nothing: no lane moved, no deck's clip changed.
    markModified(track, ModelChange::none);
    return juce::Result::ok();
}

juce::Result Session::mapDeviceParameter(int track, int slot, int pluginParameter)
{
    juce::String name;
    const auto result = mapDeviceParameterInEdit(track, slot, pluginParameter, name);
    if (result.wasOk())
        sendSynchronousChangeMessage();
    return result;
}

juce::Result Session::unmapDeviceParameter(int track, int slot, int index)
{
    auto* plugin = devicePlugin(track, slot);
    auto* processor = plugin != nullptr ? processorOf(*plugin) : nullptr;
    if (processor == nullptr)
        return juce::Result::fail("Only a plugin with a window of its own maps knobs onto the rack.");
    auto ids = mappedIds(*plugin, *processor);
    if (!juce::isPositiveAndBelow(index, ids.size()))
        return juce::Result::fail("That knob is not on the rack.");
    const auto* parameter = mappedParameterAt(*plugin, index);
    const auto name = parameter != nullptr ? parameter->getParameterName() : juce::String("A missing control");

    auto& undoManager = edit->getUndoManager();
    undoManager.beginNewTransaction("Remove " + name + " from the rack");
    // The knob's lane goes with it, and the lanes of the knobs after it move
    // down one, as the knobs do. Lanes name a knob by its place in the list.
    const auto key = deviceKeyOf(*plugin);
    if (key.isNotEmpty())
    {
        auto owner = automationOwnerState(track);
        for (int i = owner.getNumChildren(); --i >= 0;)
        {
            auto lane = owner.getChild(i);
            if (!lane.hasType(trackAutomationID) || lane.getProperty(automationDeviceID).toString() != key)
                continue;
            const auto parameterIndex = static_cast<int>(lane.getProperty(automationParameterID, -1));
            if (parameterIndex == index)
                owner.removeChild(i, &undoManager);
            else if (parameterIndex > index)
                lane.setProperty(automationParameterID, parameterIndex - 1, &undoManager);
        }
    }
    // So does what was taken over by hand: an override belongs to a knob, not
    // to a place in the list.
    const auto ofThisDevice = [&key, track, slot] (const AutomationRuntime& runtime)
    {
        return key.isNotEmpty() ? runtime.deviceKey == key
            : runtime.deviceKey.isEmpty() && runtime.target.track == track && runtime.target.slot == slot;
    };
    std::erase_if(automationRuntime, [&] (const AutomationRuntime& runtime)
    {
        return ofThisDevice(runtime) && runtime.target.parameter == index;
    });
    for (auto& runtime : automationRuntime)
        if (ofThisDevice(runtime) && runtime.target.parameter > index)
            --runtime.target.parameter;
    if (lastTouchedParameter.track == track && lastTouchedParameter.slot == slot)
        lastTouchedParameter = {};
    ids.remove(index);
    plugin->state.setProperty(mappedParametersID, ids.joinIntoString(","), &undoManager);
    closeUndoStep();
    markModified(track, ModelChange::curves);
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

// ---- Mapping from the plugin's window -------------------------------------

std::optional<std::pair<int, int>> Session::locateDevice(te::EditItemID device) const
{
    for (int track = 0; track <= masterTrackIndex(); ++track)
        if (const auto* list = pluginListForTrack(track))
            for (int slot = 0; slot < list->size(); ++slot)
                if (const auto* plugin = (*list)[slot]; plugin != nullptr && plugin->itemID == device)
                    return std::pair {track, slot};
    return std::nullopt;
}

juce::Result Session::beginParameterMapping(int track, int slot)
{
    auto* plugin = devicePlugin(track, slot);
    auto* processor = plugin != nullptr ? processorOf(*plugin) : nullptr;
    if (processor == nullptr)
        return juce::Result::fail("Only a plugin with a window of its own maps knobs onto the rack.");
    if (processor == mappingProcessor)
        return juce::Result::ok();
    endParameterMapping();
    processor->addListener(&parameterMapper);
    mappingDevice = plugin->itemID;
    mappingProcessor = processor;
    sendSynchronousChangeMessage();
    return juce::Result::ok();
}

void Session::endParameterMapping()
{
    if (!mappingDevice.has_value())
        return;
    detachParameterMapping();
    sendSynchronousChangeMessage();
}

void Session::detachParameterMapping()
{
    if (!mappingDevice.has_value())
        return;
    // The listener comes off the processor it went on, and only that one: a
    // device brought back by undo is a new processor that never heard of it,
    // and the one that did went with the old device.
    if (const auto located = locateDevice(*mappingDevice))
        if (auto* plugin = devicePlugin(located->first, located->second))
            if (auto* processor = processorOf(*plugin); processor != nullptr && processor == mappingProcessor)
                processor->removeListener(&parameterMapper);
    dropParameterMapping();
}

void Session::dropParameterMapping()
{
    parameterMapper.cancelPendingUpdate();
    parameterMapper.gestured.clear();
    parameterMapper.changed.clear();
    mappingDevice.reset();
    mappingProcessor = nullptr;
}

// Called by markModified for a change to a chain: the device being mapped
// from may have left with it, taking the processor the listener was on.
void Session::dropParameterMappingIfDeviceGone()
{
    if (!mappingDevice.has_value())
        return;
    const auto located = locateDevice(*mappingDevice);
    const auto* plugin = located ? devicePlugin(located->first, located->second) : nullptr;
    if (plugin == nullptr || processorOf(*plugin) != mappingProcessor)
        dropParameterMapping();
}

bool Session::isMappingParameters(int track, int slot) const
{
    if (!mappingDevice.has_value())
        return false;
    const auto* plugin = devicePlugin(track, slot);
    return plugin != nullptr && plugin->itemID == *mappingDevice && processorOf(*plugin) == mappingProcessor;
}

void Session::ParameterMapper::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor* processor, int parameter)
{
    // The window's own thread is the message thread. The audio thread reports
    // a parameter the engine moved, which no hand touched.
    if (processor != session.mappingProcessor || !juce::MessageManager::existsAndIsCurrentThread())
        return;
    if (std::find(gestured.begin(), gestured.end(), parameter) == gestured.end())
        gestured.push_back(parameter);
    triggerAsyncUpdate();
}

void Session::ParameterMapper::audioProcessorParameterChanged(juce::AudioProcessor* processor, int parameter, float)
{
    if (processor != session.mappingProcessor || !juce::MessageManager::existsAndIsCurrentThread())
        return;
    if (std::find(changed.begin(), changed.end(), parameter) == changed.end())
        changed.push_back(parameter);
    triggerAsyncUpdate();
}

void Session::ParameterMapper::handleAsyncUpdate()
{
    session.flushParameterMapping();
}

void Session::flushParameterMapping()
{
    // A gesture is a hand on a control. A value with no gesture is a hand
    // too, when it comes alone: some of a window's controls send none, and a
    // preset loaded there changes dozens of values at once.
    auto touched = std::move(parameterMapper.gestured);
    if (parameterMapper.changed.size() == 1
        && std::find(touched.begin(), touched.end(), parameterMapper.changed.front()) == touched.end())
        touched.push_back(parameterMapper.changed.front());
    parameterMapper.gestured.clear();
    parameterMapper.changed.clear();
    parameterMapper.cancelPendingUpdate();
    if (touched.empty() || !mappingDevice.has_value())
        return;
    const auto located = locateDevice(*mappingDevice);
    if (!located.has_value())
    {
        dropParameterMapping();
        sendSynchronousChangeMessage();
        return;
    }
    auto announce = false;
    for (const auto parameter : touched)
    {
        juce::String name;
        auto alreadyMapped = false;
        const auto result = mapDeviceParameterInEdit(located->first, located->second, parameter, name, &alreadyMapped);
        // A knob already on the rack is touched all the time: the rack's own
        // knob of it moves the plugin's, which reports the move back here.
        if (alreadyMapped)
            continue;
        mappingReport.text = result.wasOk() ? "Mapped " + name + " onto the rack" : result.getErrorMessage();
        ++mappingReport.serial;
        announce = true;
    }
    if (announce)
        sendSynchronousChangeMessage();
}
}
