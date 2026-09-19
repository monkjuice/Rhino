#pragma once
#include "Session.h"

// Session's three device enums are a typed shorthand for the devices that
// existed before the catalog did. This header is the only place they turn
// into catalog ids: below it there is exactly one way to name a device.
//
// A device added from here on needs no enum. It is a file under src/devices,
// a line in that target's CMakeLists, and an entry in DeviceCatalog.cpp; the
// browser, the drop targets and the device rack pick it up from the catalog.

namespace rhino
{

inline juce::String deviceIdFor(Session::AudioEffect effect)
{
    switch (effect)
    {
        case Session::AudioEffect::Equaliser:  return "Equaliser";
        case Session::AudioEffect::Reverb:     return "Reverb";
        case Session::AudioEffect::Delay:      return "Delay";
        case Session::AudioEffect::Compressor: return "Compressor";
        case Session::AudioEffect::RhinoSpace: return "RhinoSpace";
        case Session::AudioEffect::RhinoBloom: return "RhinoBloom";
    }
    return {};
}

inline juce::String deviceIdFor(Session::Instrument instrument)
{
    switch (instrument)
    {
        case Session::Instrument::FourOsc:    return "FourOsc";
        case Session::Instrument::RhinoWave:  return "RhinoWave";
        case Session::Instrument::RhinoForge: return "RhinoForge";
        case Session::Instrument::Drums:      return "Drums";
        case Session::Instrument::Utility:    return "Utility";
    }
    return {};
}

inline juce::String deviceIdFor(Session::MidiEffect effect)
{
    switch (effect)
    {
        case Session::MidiEffect::RhinoArp: return "RhinoArp";
    }
    return {};
}

// Every enum value above names a catalog entry, so these do not return null
// in practice; callers still check, because a lookup that can fail should
// read like one.
template <typename Enum>
const DeviceDescriptor* descriptorFor(Enum value)
{
    return DeviceCatalog::byId(deviceIdFor(value));
}

}
