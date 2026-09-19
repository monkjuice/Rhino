#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rhino
{
namespace te = tracktion::engine;

// What a device is, which decides where the browser files it and how the
// device rack and the instrument rules treat it. Session aliases this rather
// than declaring its own, so there is one spelling of the idea.
enum class DeviceKind
{
    MidiEffect,
    Instrument,
    AudioEffect
};

// The drum rack's kits. They live here rather than on DrumDevice because the
// browser and Session name a kit, and neither should have to include a device
// header to do it. DrumDevice aliases this as DrumDevice::Kit.
enum class DrumKit
{
    Rhino808,
    House,
    Break,
    Minimal,
    Clap
};
inline constexpr int drumKitCount = 5;

// Everything the rest of Rhino needs to know about one device.
//
// This replaces metadata that used to be spread across five files: an enum in
// Session.h, a type-name switch in SessionDevices.cpp, a colour switch and a
// pattern-key switch in SessionInternal.cpp, a row in BrowserPanel.cpp and a
// string mapping in BrowserIds.h. Adding a device meant finding all six.
struct DeviceDescriptor
{
    // Stable identity. Browser drag-and-drop descriptions carry it, so it is
    // written down in places outside this process and must not be renamed.
    juce::String id;

    // The engine's plugin type. Empty for an external plugin, which is found
    // by scanning rather than created by name.
    juce::String typeName;

    // The name the device is given when it is created.
    juce::String displayName;

    // What the browser row says, when that differs from the device's own
    // name: the row reads "4OSC synth", the device on the track reads "4OSC".
    // Empty means the two are the same.
    juce::String browserLabel;

    DeviceKind kind = DeviceKind::AudioEffect;

    // Second-level browser grouping under Instruments / Audio FX / MIDI FX.
    // May be empty, which the browser renders as an ungrouped row.
    juce::String category;
    juce::String description;

    // Shown by the device rack. ARGB; 0 means the device has no colour of
    // its own. Stored as a number so this library needs no juce_graphics.
    juce::uint32 colour = 0;

    // Which pattern instrument selects this device, "" if none does.
    juce::String patternKey;

    // Listed in the browser. A device can be real and still not be offered
    // directly -- the drum rack is reached through its kits instead.
    bool browsable = false;

    // Discovered as a VST3 rather than created from typeName.
    bool external = false;

    // A permanent channel-strip facility the engine keeps in the graph, not
    // something the user added.
    bool infrastructure = false;
};

// The one list of Rhino's devices. Adding a device is a new file under
// instruments/, audio/ or midi/, a line in this target's CMakeLists, and an
// entry here -- nothing in Session or the UI needs to change.
class DeviceCatalog
{
public:
    static const std::vector<DeviceDescriptor>& all();
    static std::vector<const DeviceDescriptor*> ofKind(DeviceKind);

    // Both return nullptr when nothing matches; a project may name a device
    // this build does not have.
    static const DeviceDescriptor* byId(const juce::String& id);
    static const DeviceDescriptor* byTypeName(const juce::String& typeName);

    // browserLabel if it has one, displayName otherwise.
    static juce::String labelFor(const DeviceDescriptor&);

    // Teaches the engine to create every Rhino device. Called once, from
    // Session's constructor, in place of a createBuiltInType line per device.
    static void registerBuiltInTypes(te::Engine&);
};
}
