#include "../Session.h"
#include "ContentLibrary.h"
#include "DevicePreset.h"
#include <cmath>
#include <set>
#include <stdexcept>

// Device presets, run by --device-test after the devices themselves. The
// format is checked on its own; every factory preset against the device it is
// for; and saving, loading and adding through the session, undo included.
namespace rhino
{
namespace
{
void require(bool valid, const juce::String& what)
{
    if (!valid)
        throw std::runtime_error(("Presets: " + what).toStdString());
}

// The file format alone.
void checkFormat()
{
    // Values that have no short decimal form still come back exactly.
    DevicePreset preset;
    preset.deviceId = "RhinoFM";
    preset.values = { { "a", 0.1f }, { "b", 1.0f / 3.0f }, { "c", 0.002f }, { "d", -36.0f }, { "e", 1.0e-7f } };
    DevicePreset back;
    require(DevicePreset::fromXml(*preset.toXml(), back).wasOk() && back.deviceId == preset.deviceId
                && back.values == preset.values,
            "a preset does not read back exactly what was written");

    const auto rejects = [] (const char* xml, const char* what)
    {
        DevicePreset ignored;
        const auto parsed = juce::parseXML(juce::String(xml));
        require(parsed != nullptr && DevicePreset::fromXml(*parsed, ignored).failed(),
                juce::String("a preset ") + what + " is accepted");
    };
    rejects(R"(<SOMETHING format="1" device="RhinoFM"/>)", "that is not one");
    rejects(R"(<RHINO_PRESET format="2" device="RhinoFM"/>)", "in a format this build does not read");
    rejects(R"(<RHINO_PRESET format="1"/>)", "for no device");
    rejects(R"(<RHINO_PRESET format="1" device="RhinoFM"><PARAM id="mix" value="loud"/></RHINO_PRESET>)",
            "with a value that is not a number");
    rejects(R"(<RHINO_PRESET format="1" device="RhinoFM"><PARAM id="mix" value="nan"/></RHINO_PRESET>)",
            "with a value that is not finite");
    rejects(R"(<RHINO_PRESET format="1" device="RhinoFM"><PARAM id="mix" value="1"/><PARAM id="mix" value="2"/></RHINO_PRESET>)",
            "setting one control twice");
}

// Every factory preset is filed under the device it is for, names only
// controls that device has, and sets each to a value it can hold.
void checkFactoryPresets(Session& session)
{
    std::set<juce::String> devices;
    auto count = 0;
    for (const auto& entry : ContentLibrary::presets())
    {
        if (entry.user)
            continue;
        ++count;
        DevicePreset preset;
        const auto read = DevicePreset::read(entry.file, preset);
        require(read.wasOk(), entry.name + ": " + read.getErrorMessage());
        require(preset.deviceId == entry.deviceId,
                entry.name + " is filed under " + entry.deviceId + " but is for " + preset.deviceId);
        const auto* device = DeviceCatalog::byId(preset.deviceId);
        require(device != nullptr && device->browsable && !device->external && !device->infrastructure,
                entry.name + " is for " + preset.deviceId + ", which has no presets in this build");
        auto plugin = session.edit->getPluginCache().createNewPlugin(device->typeName, {});
        require(plugin != nullptr, device->displayName + " can be made for " + entry.name);
        for (const auto& [id, value] : preset.values)
        {
            const auto parameter = plugin->getAutomatableParameterByID(id);
            require(parameter != nullptr, entry.name + " sets " + id + ", which " + device->displayName + " does not have");
            require(parameter->valueRange.snapToLegalValue(value) == value,
                    entry.name + " sets " + id + " to " + juce::String(value) + ", which it cannot hold");
        }
        devices.insert(preset.deviceId);
    }
    require(devices == std::set<juce::String> { "RhinoArp", "RhinoFM", "RhinoSpace" },
            "the library has presets for Rhino Arp, Rhino FM and Rhino Space, found " + juce::String(count));
}

te::Plugin* deviceOn(Session& session, int track, const juce::String& deviceId, int* slot = nullptr)
{
    for (const auto& device : session.deviceSlots(track))
        if (device.deviceId == deviceId)
        {
            if (slot != nullptr)
                *slot = device.pluginIndex;
            return session.devicePlugin(track, device.pluginIndex);
        }
    return nullptr;
}

float valueOf(te::Plugin& plugin, const juce::String& id)
{
    const auto parameter = plugin.getAutomatableParameterByID(id);
    require(parameter != nullptr, plugin.getName() + " has " + id);
    return parameter->getCurrentValue();
}

bool sets(te::Plugin& plugin, const DevicePreset& preset)
{
    for (const auto& [id, value] : preset.values)
        if (valueOf(plugin, id) != value)
            return false;
    return true;
}

DevicePreset presetAt(const juce::File& file)
{
    DevicePreset preset;
    const auto read = DevicePreset::read(file, preset);
    require(read.wasOk(), file.getFileName() + ": " + read.getErrorMessage());
    return preset;
}

void checkThroughSession(Session& session)
{
    constexpr int midiTrack = 2, audioTrack = 3;
    require(session.trackType(midiTrack) == Session::TrackType::midi
                && session.trackType(audioTrack) == Session::TrackType::audio,
            "the starter stack has a MIDI third track and an audio fourth");
    const auto bellsFile = ContentLibrary::file("Presets/RhinoFM/Glass Bells.rnd");
    const auto bells = presetAt(bellsFile);

    // Added from a preset, a device is set the way it says, in one undo step.
    require(session.addDeviceFromPreset(bellsFile, midiTrack).wasOk(), "Glass Bells adds Rhino FM");
    int slot = -1;
    auto* fm = deviceOn(session, midiTrack, "RhinoFM", &slot);
    require(fm != nullptr && sets(*fm, bells), "Rhino FM added from Glass Bells is set as Glass Bells says");
    session.undo();
    require(deviceOn(session, midiTrack, "RhinoFM") == nullptr, "one undo takes away the device and its preset together");
    session.redo();
    fm = deviceOn(session, midiTrack, "RhinoFM", &slot);
    require(fm != nullptr && sets(*fm, bells), "and redo brings them back together");

    // Saved, the file holds what the device does.
    const auto saved = juce::File::createTempFile(DevicePreset::extension);
    require(session.saveDevicePreset(midiTrack, slot, saved).wasOk(), "Rhino FM saves as a preset");
    const auto resaved = presetAt(saved);
    require(resaved.deviceId == "RhinoFM" && resaved.values.size() == static_cast<size_t>(fm->getAutomatableParameters().size()),
            "a saved preset holds every control of its device");
    for (const auto& [id, value] : bells.values)
        require(resaved.valueOf(id) == std::optional<float>(value), "saving Glass Bells again keeps " + id);
    saved.deleteFile();

    // A control a preset leaves out goes back to its default, so a preset
    // sounds the same whatever was there before it.
    const auto partial = juce::File::createTempFile(DevicePreset::extension);
    DevicePreset onlyAlgorithm;
    onlyAlgorithm.deviceId = "RhinoFM";
    onlyAlgorithm.values = { { "algorithm", 0.0f } };
    require(onlyAlgorithm.write(partial).wasOk(), "a partial preset can be written");
    require(session.loadDevicePreset(midiTrack, slot, partial).wasOk(), "a preset loads into a device already there");
    require(valueOf(*fm, "algorithm") == 0.0f && valueOf(*fm, "op2Ratio") == 1.0f,
            "a control the preset leaves out goes back to its default");
    session.undo();
    require(valueOf(*fm, "algorithm") == 4.0f && valueOf(*fm, "op2Ratio") == 3.5f, "and loading it is one undo step");
    partial.deleteFile();

    // A preset for one device does not go on another.
    const auto roomFile = ContentLibrary::file("Presets/RhinoSpace/Small Room.rnd");
    const auto refused = session.loadDevicePreset(midiTrack, slot, roomFile);
    require(refused.failed() && refused.getErrorMessage().contains("Rhino Space"),
            "a Rhino Space preset is refused by Rhino FM, saying what it is for");

    // A track has one instrument, so an instrument's preset dropped where it
    // already plays goes into it rather than adding another.
    const auto devicesBefore = session.deviceSlots(midiTrack).size();
    require(session.addDeviceFromPreset(ContentLibrary::file("Presets/RhinoFM/Hard Bass.rnd"), midiTrack).wasOk(),
            "Hard Bass loads on a track already running Rhino FM");
    auto* bass = deviceOn(session, midiTrack, "RhinoFM");
    require(session.deviceSlots(midiTrack).size() == devicesBefore && bass != nullptr && valueOf(*bass, "mono") == 1.0f,
            "and goes into that Rhino FM rather than adding another");

    // Effects and MIDI effects, the Arp being a device written before the SDK.
    require(session.addDeviceFromPreset(roomFile, audioTrack).wasOk(), "Small Room goes on an audio track");
    auto* space = deviceOn(session, audioTrack, "RhinoSpace");
    require(space != nullptr && sets(*space, presetAt(roomFile)), "Rhino Space added from Small Room is set by it");
    const auto arpFile = ContentLibrary::file("Presets/RhinoArp/Super Creative Arpeggiation.rnd");
    require(session.addDeviceFromPreset(arpFile, midiTrack).wasOk(), "Super Creative Arpeggiation goes on a MIDI track");
    auto* arp = deviceOn(session, midiTrack, "RhinoArp");
    require(arp != nullptr && sets(*arp, presetAt(arpFile)), "Rhino Arp added from a preset is set by it");
    require(session.addDeviceFromPreset(arpFile, audioTrack).failed(), "an Arp preset is refused by an audio track");

    // The person's own presets are found beside the factory set, after it.
    const auto own = juce::File::createTempFile("").getSiblingFile("RhinoPresetTest" + juce::String(juce::Random().nextInt(1 << 30)));
    require(session.saveDevicePreset(midiTrack, slot, own.getChildFile("RhinoFM").getChildFile("Mine.rnd")).wasOk(),
            "a preset saves into a folder that does not exist yet");
    const auto found = ContentLibrary::presetsIn(own, true);
    require(found.size() == 1 && found[0].deviceId == "RhinoFM" && found[0].name == "Mine" && found[0].user,
            "a saved preset is found under its device, by its file name");
    own.deleteRecursively();
}
}

void checkDevicePresets(Session& session)
{
    checkFormat();
    checkFactoryPresets(session);
    checkThroughSession(session);
}
}
