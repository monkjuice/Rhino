#pragma once
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace rhino
{
// One device's settings, saved as a .rnd file: Rhino Arp's
// "Super Creative Arpeggiation.rnd" is a preset of Rhino Arp. A device itself
// is code and has no file; its presets are what is kept on disk.
//
//   <RHINO_PRESET format="1" device="RhinoArp">
//     <PARAM id="style" value="3"/>
//   </RHINO_PRESET>
//
// A value is stored under its control's engine parameter id, which a device
// never renames because documents store it too, and in the control's own
// units. So any built-in device has presets, whether or not it is written on
// the SDK. The preset's name is its file name, so renaming the file renames
// the preset.
//
// Format 1 is the only format, and nothing older is read: no compatibility is
// owed before a production release. The version is written so that it can be
// owed then.
struct DevicePreset
{
    static constexpr int format = 1;
    static constexpr const char* extension = ".rnd";

    juce::String deviceId;  // the device's catalog id
    std::vector<std::pair<juce::String, float>> values;

    // The value saved for a control, if there is one.
    std::optional<float> valueOf(const juce::String& parameterId) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    // Fails with a message a person can act on, naming what is wrong.
    static juce::Result fromXml(const juce::XmlElement&, DevicePreset& into);

    juce::Result write(const juce::File&) const;
    static juce::Result read(const juce::File&, DevicePreset& into);

    static juce::String nameOf(const juce::File& file) { return file.getFileNameWithoutExtension(); }
};
}
