#include "SessionInternal.h"
#include <algorithm>

namespace rhino
{

const juce::Identifier starterPlaceholderID {"rhinoStarterPlaceholder"};
const juce::Identifier clipColourID {"rhinoClipColour"};
const juce::Identifier editorStepsID {"rhinoEditorSteps"};
const juce::Identifier trackAutomationID {"rhinoTrackAutomation"};
const juce::Identifier automationPointID {"point"};
const juce::Identifier automationDeviceID {"device"};
const juce::Identifier automationParameterID {"parameter"};
const juce::Identifier deviceKeyID {"rhinoDeviceKey"};
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
// What the track was made for, so a MIDI track can hold a clip before it
// runs anything. Only "midi" is ever written: a track with no answer is an
// audio track, which is what every document written before this contains.
const juce::Identifier trackTypeID {"rhinoTrackType"};
// Whether you hear what the track is taking in. Absent is the default for the
// track's kind - auto on MIDI, off on audio - so the common answer writes
// nothing; see the note on Session::InputMonitoring for why those two differ.
const juce::Identifier trackMonitorID {"rhinoMonitor"};
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

std::unique_ptr<te::Edit> loadDocumentEdit(te::Engine& engine, const juce::ValueTree& state, const juce::File& document,
                                           te::Edit::EditRole role)
{
    auto id = te::ProjectItemID::fromProperty(state, te::IDs::projectID);
    if (!id.isValid())
        id = te::ProjectItemID::createNewID(te::ProjectID{});
    te::Edit::Options options {engine, state, id};
    options.role = role;
    options.editFileRetriever = [document] { return document; };
    return te::Edit::createEdit(std::move(options));
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

// The audio track holding a clip: its timeline track, or the track whose
// clip slot it sits in, which the clip's own lookup may not answer for.
te::AudioTrack* trackHoldingClip(te::Edit& edit, const te::Clip& clip)
{
    if (auto* track = dynamic_cast<te::AudioTrack*>(clip.getClipTrack()))
        return track;
    for (auto* track : te::getAudioTracks(edit))
        for (auto* slot : track->getClipSlotList().getClipSlots())
            if (slot->getClip() == &clip)
                return track;
    return nullptr;
}

int clipTrackIndex(te::Edit& edit, const te::Clip& clip)
{
    return te::getAudioTracks(edit).indexOf(trackHoldingClip(edit, clip));
}

juce::Colour nextClipColour(juce::Colour current)
{
    static constexpr juce::uint32 palette[] {
        0xff3d6f8b, 0xff738044, 0xff8d5f42, 0xff7a5b8f,
        0xff9b4f67, 0xff4b7f68, 0xff8a7a42, 0xff56636c
    };
    constexpr auto count = static_cast<int>(std::size(palette));
    for (int i = 0; i < count; ++i)
        if (current == juce::Colour(palette[i]))
            // Past the last entry the clip is handed back to no colour of its
            // own, which is what makes it follow its track again: without that
            // step a clip coloured once could never rejoin its lane.
            return i + 1 < count ? juce::Colour(palette[static_cast<size_t>(i + 1)]) : juce::Colour();
    return juce::Colour(palette[0]);
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

juce::String deviceKeyOf(const te::Plugin& plugin)
{
    return plugin.state.getProperty(deviceKeyID).toString();
}

juce::String ensureDeviceKey(te::Plugin& plugin)
{
    auto key = deviceKeyOf(plugin);
    if (key.isEmpty())
    {
        key = juce::Uuid().toString();
        plugin.state.setProperty(deviceKeyID, key, nullptr);
    }
    return key;
}

void removeDeviceLanes(juce::ValueTree owner, const te::Plugin& plugin, juce::UndoManager* undoManager)
{
    const auto key = deviceKeyOf(plugin);
    if (key.isEmpty())
        return;
    for (int i = owner.getNumChildren(); --i >= 0;)
        if (const auto lane = owner.getChild(i);
            lane.hasType(trackAutomationID) && lane.getProperty(automationDeviceID).toString() == key)
            owner.removeChild(i, undoManager);
}

int slotOfDevice(const te::PluginList& list, const juce::String& key)
{
    if (key.isEmpty())
        return -1;
    for (int slot = 0; slot < list.size(); ++slot)
        if (const auto* plugin = list[slot]; plugin != nullptr && deviceKeyOf(*plugin) == key)
            return slot;
    return -1;
}

// A lane only drives its parameter once it holds a curve. Revealing a lane on
// its own leaves the knob alone, which is what makes "show automation" safe.
// The lane is looked up where it lives - on its own track - because a lane
// keeps no copy of its track index for a track deletion to invalidate, and by
// the key of the device in the slot asked about, because it keeps no copy of
// that slot for a reorder to invalidate either.
bool hasActiveTrackAutomation(te::Edit& edit, Session::DeviceTarget target)
{
    if (target.track < 0 || target.slot < 0 || target.parameter < 0)
        return false;
    const auto tracks = te::getAudioTracks(edit);
    juce::ValueTree owner;
    te::PluginList* list = nullptr;
    if (target.track == tracks.size())
    {
        owner = edit.state;
        list = &edit.getMasterPluginList();
    }
    else if (juce::isPositiveAndBelow(target.track, tracks.size()))
    {
        owner = tracks[target.track]->state;
        list = &tracks[target.track]->pluginList;
    }
    else
        return false;
    if (!juce::isPositiveAndBelow(target.slot, list->size()) || (*list)[target.slot] == nullptr)
        return false;
    const auto key = deviceKeyOf(*(*list)[target.slot]);
    if (key.isEmpty())
        return false;

    for (int i = 0; i < owner.getNumChildren(); ++i)
    {
        const auto state = owner.getChild(i);
        if (!state.hasType(trackAutomationID)
            || state.getProperty(automationDeviceID).toString() != key
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

DrumRackDevice* findDrumRack(te::AudioTrack& track)
{
    for (auto* plugin : track.pluginList)
        if (auto* drums = dynamic_cast<DrumRackDevice*>(plugin))
            return drums;
    return nullptr;
}

const DeviceDescriptor* carriedInstrument(te::AudioTrack& track)
{
    auto* plugin = trackInstrument(track);
    if (plugin == nullptr)
        return nullptr;
    // Forge is an external plugin, so it has no type name to look up.
    if (isForgePlugin(*plugin))
        return DeviceCatalog::byId("RhinoForge");
    return DeviceCatalog::byTypeName(plugin->getPluginType());
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

int djInputEnd(const te::AudioTrack& track)
{
    const auto& list = track.pluginList;
    return list.size() > 0 && list[0] != nullptr && list[0]->getPluginType() == DjTrackInjectorDevice::xmlTypeName ? 1 : 0;
}

void ensureDjPortDevices(te::Edit& edit, te::AudioTrack& track)
{
    auto& list = track.pluginList;
    if (djInputEnd(track) == 0)
        if (auto input = edit.getPluginCache().createNewPlugin(DjTrackInjectorDevice::xmlTypeName, {}))
            list.insertPlugin(input, 0, nullptr);
    for (auto* plugin : list)
        if (plugin != nullptr && plugin->getPluginType() == DjTrackTapDevice::xmlTypeName)
            return;
    auto output = edit.getPluginCache().createNewPlugin(DjTrackTapDevice::xmlTypeName, {});
    if (output == nullptr)
        return;
    // Where the track's own devices end and its channel strip begins - the
    // first of the Utility, the Volume & Pan and the meter, in whatever order
    // the track was made with them - so a deck takes the track as its own
    // devices leave it, and the song's trim, fader, pan and mute stay the
    // song's. Every device added later lands ahead of it, as every add path
    // puts a device ahead of the channel strip.
    auto at = list.size();
    for (int i = djInputEnd(track); i < list.size(); ++i)
        if (auto* plugin = list[i]; plugin != nullptr)
        {
            const auto type = plugin->getPluginType();
            const auto* entry = DeviceCatalog::byTypeName(type);
            if (entry != nullptr ? entry->infrastructure : (type == "volume" || type == "level"))
            {
                at = i;
                break;
            }
        }
    list.insertPlugin(output, at, nullptr);
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

te::AudioTrack* firstMidiTrackOf(te::Edit& edit)
{
    for (auto* track : te::getAudioTracks(edit))
        if (track != nullptr && static_cast<int>(track->state.getProperty(trackGroupBusID, 0)) == 0
            && track->state.getProperty(trackTypeID).toString() == "midi")
            return track;
    return nullptr;
}

// Projects written while a track could stack several instruments keep the
// enabled one and lose the rest. Whichever was audible stays audible.
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
            {
                removeDeviceLanes(track->state, *plugin, nullptr);
                plugin->removeFromParent();
            }
        if (!keep->isEnabled())
            keep->setEnabled(true);
    }
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, const DeviceDescriptor& device,
                                   InstrumentSwitch& switched, const juce::PluginDescription* forgeDescription)
{
    // MIDI effects run ahead of the instrument, so a new one goes in after them.
    int instrumentInsertIndex = djInputEnd(track);
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
            switched.devices = true;
        }
        else
        {
            auto created = edit.getPluginCache().createNewPlugin(device.typeName, {});
            if (created == nullptr)
                return juce::Result::fail("The target track device could not be created.");
            selected = created.get();
            track.pluginList.insertPlugin(created, juce::jlimit(0, track.pluginList.size(), instrumentInsertIndex), nullptr);
            switched.devices = true;
        }
    }

    // One instrument per track: everything the switch replaced goes away rather
    // than lingering disabled. Its patch goes with it, which is what replacing
    // an instrument means in Live and Logic.
    for (auto* plugin : existingInstruments)
        if (plugin != selected)
        {
            removeDeviceLanes(track.state, *plugin, &edit.getUndoManager());
            plugin->removeFromParent();
            switched.devices = true;
        }

    if (!selected->isEnabled())
    {
        selected->setEnabled(true);
        switched.devices = true;
    }

    // A track is named after the instrument it runs, so the arrangement header
    // says what the track is rather than what it was called when created.
    if (const auto name = selected->getName(); name.isNotEmpty() && track.getName() != name)
    {
        track.setName(name);
        switched.renamed = true;
    }

    // What the track is was settled when it was created, so nothing is written
    // here: only a MIDI track is ever offered an instrument in the first place.
    return juce::Result::ok();
}

juce::Result switchTrackInstrument(te::Edit& edit, te::AudioTrack& track, Session::Instrument instrument,
                                   InstrumentSwitch& switched, const juce::PluginDescription* forgeDescription)
{
    const auto* device = descriptorFor(instrument);
    if (device == nullptr)
        return juce::Result::fail("That instrument is not in this build.");
    return switchTrackInstrument(edit, track, *device, switched, forgeDescription);
}

juce::Result checkClipFile(te::Clip& clip)
{
    if (dynamic_cast<te::WaveAudioClip*>(&clip) == nullptr)
        return juce::Result::ok();
    // An empty file is a clip that never named one: a render makes no node
    // for it, so it holds nothing up.
    const auto file = clip.getSourceFileReference().getFile();
    if (file == juce::File() || file.existsAsFile())
        return juce::Result::ok();
    return juce::Result::fail("The clip " + clip.getName().quoted() + " plays " + file.getFullPathName()
                              + ", which is missing.");
}

std::shared_ptr<bool> stopRenderWhenStalled(te::Renderer::RenderTask& task, const std::atomic<float>& progress,
                                            double stallSeconds)
{
    auto stalled = std::make_shared<bool>(false);
    // A render that keeps coming back without moving is waiting on something
    // that will not come: a clip's file that is there but cannot be read
    // (WaveNode::isReadyToProcess at 4536d8a). Given up through the engine's
    // own cancellation, which closes the file and deletes it. Measured from
    // the last move rather than from the start, so a long song is never cut
    // short; the render's half second of run-in counts as a stall, so a
    // budget is never set that short.
    task.setCancellationCheck([&progress, stallSeconds, stalled, lastProgress = -1.0f, movedAt = 0.0]() mutable
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (const auto reached = progress.load(); reached != lastProgress)
        {
            lastProgress = reached;
            movedAt = now;
        }
        *stalled = now - movedAt > stallSeconds * 1000.0;
        return *stalled;
    });
    return stalled;
}

juce::Result renderTaskOutcome(const te::Renderer::RenderTask& task, bool stalled, double stallSeconds)
{
    if (stalled)
        return juce::Result::fail("The render made no progress for " + juce::String(stallSeconds, 0)
                                  + " seconds and was stopped: a clip's audio file may not be readable.");
    return task.errorMessage.isEmpty() ? juce::Result::ok() : juce::Result::fail(task.errorMessage);
}

juce::Result runRenderTask(te::Renderer::RenderTask& task, const std::atomic<float>& progress, double stallSeconds)
{
    const auto stalled = stopRenderWhenStalled(task, progress, stallSeconds);
    while (task.runJob() != juce::ThreadPoolJob::jobHasFinished) {}
    return renderTaskOutcome(task, *stalled, stallSeconds);
}

juce::File nextRenderFile(const juce::File& folder, const juce::String& trackName, const juce::String& kind)
{
    auto name = juce::File::createLegalFileName(trackName.trim());
    if (name.isEmpty())
        name = "Track";
    for (int take = 1; take < 10000; ++take)
    {
        const auto file = folder.getChildFile(name + " " + kind + " " + juce::String(take) + ".wav");
        if (!file.exists())
            return file;
    }
    return {};
}

}
