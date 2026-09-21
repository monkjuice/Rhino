#include "SessionInternal.h"
#include <algorithm>

namespace rhino
{

const juce::Identifier starterPlaceholderID {"rhinoStarterPlaceholder"};
const juce::Identifier editorStepsID {"rhinoEditorSteps"};
const juce::Identifier trackAutomationID {"rhinoTrackAutomation"};
const juce::Identifier automationPointID {"point"};
const juce::Identifier automationSlotID {"slot"};
const juce::Identifier automationParameterID {"parameter"};
const juce::Identifier automationOwnLaneID {"ownLane"};
const juce::Identifier automationTimeID {"time"};
const juce::Identifier automationValueID {"value"};
// A group is a track: its bus carries the id, and each member carries the same
// id. Both are track properties, so the structure travels with the document and
// with the tracks when they are reordered, without a save path of its own.
const juce::Identifier trackGroupBusID {"rhinoGroupBus"};
const juce::Identifier trackGroupMemberID {"rhinoGroup"};
const juce::Identifier trackGroupCollapsedID {"rhinoGroupCollapsed"};
const juce::Identifier legacyTrackGroupID {"rhinoTrackGroup"};
const juce::Identifier legacyTrackGroupIdID {"id"};
const juce::Identifier legacyTrackGroupNameID {"name"};
const juce::Identifier legacyTrackGroupColourID {"colour"};
// Arming is a property of the track, so it travels with the track when it is
// reordered and is saved with the document, the way mute and solo are. The
// engine's own input destinations are rebuilt from it rather than being a
// second copy of the same answer.
const juce::Identifier trackArmedID {"rhinoArmed"};
// The count-in lives beside the other metronome settings, on the edit.
const juce::Identifier countInBarsID {"rhinoCountInBars"};

juce::PropertiesFile::Options rhinoSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Rhino";
    options.filenameSuffix = "settings";
    options.folderName = "Rhino";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

void panicMidiOnTrack(te::ClipTrack* clipTrack)
{
    auto* track = dynamic_cast<te::AudioTrack*>(clipTrack);
    if (track == nullptr)
        return;
    for (auto* plugin : track->pluginList)
        if (plugin != nullptr)
            plugin->midiPanic();
}

juce::Colour presetColour(Session::PatternPreset preset)
{
    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return juce::Colour(0xff4f7d8f);
        case Session::PatternPreset::AcidSteps:  return juce::Colour(0xff2f6e78);
        case Session::PatternPreset::ArpRun:     return juce::Colour(0xff77659a);
        case Session::PatternPreset::ChordPad:   return juce::Colour(0xff5f718f);
        case Session::PatternPreset::SubBass:    return juce::Colour(0xff34535f);
        case Session::PatternPreset::ReeseBass:  return juce::Colour(0xff4a5f38);
        case Session::PatternPreset::SirenLead:  return juce::Colour(0xff8f4f67);
        case Session::PatternPreset::WavePad:    return juce::Colour(0xff5e55b8);
        case Session::PatternPreset::WaveBass:   return juce::Colour(0xff355a86);
        case Session::PatternPreset::WavePluck:  return juce::Colour(0xff4c7a95);
        case Session::PatternPreset::HouseKit:   return juce::Colour(0xff657844);
        case Session::PatternPreset::BreakKit:   return juce::Colour(0xff6f7f43);
        case Session::PatternPreset::MinimalKit: return juce::Colour(0xff506d45);
        case Session::PatternPreset::ClapKit:    return juce::Colour(0xff8a7a42);
    }
    return juce::Colour(0xff4b6671);
}

juce::Colour instrumentColour(const DeviceDescriptor& device)
{
    return device.colour != 0 ? juce::Colour(device.colour) : juce::Colour(0xff4b6671);
}

juce::Colour instrumentColour(Session::Instrument instrument)
{
    if (const auto* device = descriptorFor(instrument))
        return instrumentColour(*device);
    return juce::Colour(0xff4b6671);
}

juce::Colour nextClipColour(juce::Colour current)
{
    static constexpr juce::uint32 palette[] {
        0xff3d6f8b, 0xff738044, 0xff8d5f42, 0xff7a5b8f,
        0xff9b4f67, 0xff4b7f68, 0xff8a7a42, 0xff56636c
    };
    int closest = -1;
    for (int i = 0; i < static_cast<int>(std::size(palette)); ++i)
        if (current == juce::Colour(palette[i]))
        {
            closest = i;
            break;
        }
    return juce::Colour(palette[static_cast<size_t>((closest + 1) % static_cast<int>(std::size(palette)))]);
}

const DeviceDescriptor* audioEffectDescriptor(Session::AudioEffect effect)
{
    return descriptorFor(effect);
}

void resetPluginList(te::PluginList* list)
{
    if (list == nullptr)
        return;
    for (auto* plugin : *list)
        if (plugin != nullptr)
        {
            plugin->midiPanic();
            plugin->reset();
        }
}

double stepDurationBeats(int steps)
{
    return 4.0 / static_cast<double>(juce::jlimit(Session::defaultSteps, Session::steps, steps));
}

bool sameDeviceTarget(Session::DeviceTarget a, Session::DeviceTarget b)
{
    return a.track == b.track && a.slot == b.slot && a.parameter == b.parameter;
}

// A lane only drives its parameter once it holds a curve. Revealing a lane on
// its own leaves the knob alone, which is what makes "show automation" safe.
// The lane is looked up where it lives - on its own track - because a lane
// keeps no copy of its track index for a track deletion to invalidate.
bool hasActiveTrackAutomation(const te::Edit& edit, Session::DeviceTarget target)
{
    if (target.track < 0 || target.slot < 0 || target.parameter < 0)
        return false;
    const auto tracks = te::getAudioTracks(edit);
    juce::ValueTree owner;
    if (target.track == tracks.size())
        owner = edit.state;
    else if (juce::isPositiveAndBelow(target.track, tracks.size()))
        owner = tracks[target.track]->state;
    else
        return false;

    for (int i = 0; i < owner.getNumChildren(); ++i)
    {
        const auto state = owner.getChild(i);
        if (!state.hasType(trackAutomationID)
            || static_cast<int>(state.getProperty(automationSlotID, -1)) != target.slot
            || static_cast<int>(state.getProperty(automationParameterID, -1)) != target.parameter)
            continue;
        int points = 0;
        for (int child = 0; child < state.getNumChildren(); ++child)
            if (state.getChild(child).hasType(automationPointID))
                ++points;
        return points >= 2;
    }
    return false;
}

te::Plugin* findPlugin(te::AudioTrack& track, const juce::String& type)
{
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->getPluginType() == type)
            return plugin;
    return nullptr;
}

te::FourOscPlugin* findFourOsc(te::AudioTrack& track)
{
    return dynamic_cast<te::FourOscPlugin*>(findPlugin(track, te::FourOscPlugin::xmlTypeName));
}

RhinoWaveDevice* findRhinoWave(te::AudioTrack& track)
{
    return dynamic_cast<RhinoWaveDevice*>(findPlugin(track, RhinoWaveDevice::xmlTypeName));
}

DrumDevice* findDrumDevice(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* drums = dynamic_cast<DrumDevice*>(plugin))
            return drums;
    return nullptr;
}

Session::Instrument activeTrackInstrument(te::AudioTrack& track)
{
    if (auto* drums = findDrumDevice(track))
        if (drums->isEnabled())
            return Session::Instrument::Drums;
    if (auto* wave = findRhinoWave(track))
        if (wave->isEnabled())
            return Session::Instrument::RhinoWave;
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && plugin->isEnabled() && isForgePlugin(*plugin))
            return Session::Instrument::RhinoForge;
    return Session::Instrument::FourOsc;
}

bool isForgePlugin(const te::Plugin& plugin)
{
    if (auto* processor = plugin.getWrappedAudioProcessor())
        return processor->getName().containsIgnoreCase("Rhino Forge")
            || processor->getName().equalsIgnoreCase("Forge");
    return false;
}

// A track has one instrument, as it does in Live and Logic. These two answer
// "which plugin is it", and switchTrackInstrument below is the only thing that
// changes the answer.
bool isInstrumentPlugin(te::Plugin& plugin)
{
    if (const auto* device = DeviceCatalog::byTypeName(plugin.getPluginType()))
        return device->kind == DeviceKind::Instrument;
    // Forge is an external plugin, so it has no type name to look up.
    return isForgePlugin(plugin);
}

te::Plugin* trackInstrument(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && isInstrumentPlugin(*plugin))
            return plugin;
    return nullptr;
}

juce::Result ensurePlugin(te::Edit& edit, te::AudioTrack& track, const juce::String& type,
                          int insertIndex, te::Plugin*& plugin, bool& changed)
{
    if ((plugin = findPlugin(track, type)) != nullptr)
        return juce::Result::ok();

    auto created = edit.getPluginCache().createNewPlugin(type, {});
    if (created == nullptr)
        return juce::Result::fail("The target track device could not be created.");

    plugin = created.get();
    track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), insertIndex), nullptr);
    changed = true;
    return juce::Result::ok();
}

// Projects written while a track could stack several instruments keep the
// enabled one and lose the rest. Whichever was audible stays audible.
te::AudioTrack* patternTrackOf(te::Edit& edit)
{
    for (auto* track : te::getAudioTracks(edit))
        if (track != nullptr && static_cast<int>(track->state.getProperty(trackGroupBusID, 0)) == 0)
            return track;
    return nullptr;
}

void collapseStackedInstruments(te::Edit& edit)
{
    for (auto* track : te::getAudioTracks(edit))
    {
        juce::Array<te::Plugin*> instruments;
        for (auto* plugin : track->pluginList)
            if (plugin != nullptr && isInstrumentPlugin(*plugin))
                instruments.add(plugin);
        if (instruments.size() < 2)
            continue;
        te::Plugin* keep = nullptr;
        for (auto* plugin : instruments)
            if (plugin->isEnabled())
            {
                keep = plugin;
                break;
            }
        if (keep == nullptr)
            keep = instruments.getFirst();
        for (auto* plugin : instruments)
            if (plugin != keep)
                plugin->removeFromParent();
        if (!keep->isEnabled())
            keep->setEnabled(true);
    }
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, const DeviceDescriptor& device,
                                   bool& changed, const juce::PluginDescription* forgeDescription)
{
    // MIDI effects run ahead of the instrument, so a new one goes in after them.
    int instrumentInsertIndex = 0;
    while (instrumentInsertIndex < track.pluginList.size())
    {
        auto* plugin = track.pluginList[instrumentInsertIndex];
        // Any MIDI effect, not just the one that existed when this was
        // written: the catalog knows which plugins run ahead of the instrument.
        const auto* leading = plugin != nullptr ? DeviceCatalog::byTypeName(plugin->getPluginType()) : nullptr;
        if (leading == nullptr || leading->kind != DeviceKind::MidiEffect)
            break;
        ++instrumentInsertIndex;
    }

    juce::Array<te::Plugin*> existingInstruments;
    for (auto* plugin : track.pluginList)
        if (plugin != nullptr && isInstrumentPlugin(*plugin))
            existingInstruments.add(plugin);

    const auto wants = [&device](te::Plugin& plugin)
    {
        if (device.external)
            return isForgePlugin(plugin);
        return device.typeName.isNotEmpty() && plugin.getPluginType() == device.typeName;
    };

    te::Plugin* selected = nullptr;
    for (auto* plugin : existingInstruments)
        if (wants(*plugin))
        {
            selected = plugin;
            break;
        }

    if (selected == nullptr)
    {
        // Put the replacement where the old instrument was, so MIDI effects
        // before it and audio effects after it keep their order.
        if (auto* current = existingInstruments.getFirst())
            instrumentInsertIndex = track.pluginList.indexOf(current);
        if (device.external)
        {
            if (forgeDescription == nullptr)
                return juce::Result::fail("Rhino Forge.vst3 was not found. Build or install the Forge VST3 first.");
            auto created = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *forgeDescription);
            if (created == nullptr)
                return juce::Result::fail("Rhino Forge.vst3 could not be loaded.");
            selected = created.get();
            track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), instrumentInsertIndex), nullptr);
            changed = true;
        }
        else
        {
            auto created = edit.getPluginCache().createNewPlugin(device.typeName, {});
            if (created == nullptr)
                return juce::Result::fail("The target track device could not be created.");
            selected = created.get();
            track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), instrumentInsertIndex), nullptr);
            changed = true;
        }
    }

    // One instrument per track: everything the switch replaced goes away rather
    // than lingering disabled. Its patch goes with it, which is what replacing
    // an instrument means in Live and Logic.
    for (auto* plugin : existingInstruments)
        if (plugin != selected)
        {
            plugin->removeFromParent();
            changed = true;
        }

    if (!selected->isEnabled())
    {
        selected->setEnabled(true);
        changed = true;
    }

    // A track is named after the instrument it runs, so the arrangement header
    // says what the track is rather than what it was called when created.
    if (const auto name = selected->getName(); name.isNotEmpty() && track.getName() != name)
    {
        track.setName(name);
        changed = true;
    }

    return juce::Result::ok();
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument, bool& changed,
                                   const juce::PluginDescription* forgeDescription)
{
    const auto* device = descriptorFor(instrument);
    if (device == nullptr)
        return juce::Result::fail("That instrument is not in this build.");
    return switchTrackInstrument(edit, track, *device, changed, forgeDescription);
}

}
