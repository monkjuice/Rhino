#pragma once
#include "DrumRackEngine.h"
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <optional>

namespace rhino
{
// One drum sound: everything a Drum Rack pad holds, kept apart from any rack.
// A drum preset is one of these in a file; a kit is sixteen, one per pad.
//
// A sample is named, never embedded. A sound Rhino ships is named as
// "library:" and its path under the content library, so a kit opens on any
// machine wherever the library is; anything else by its full path
// (ContentLibrary::storedPath).
struct DrumSound
{
    // A sound is a sample or a synth. An empty pad has no sound at all.
    DrumRackEngine::Source source = DrumRackEngine::Source::sample;
    // What the pad is called. Empty means it takes its sound's name: the
    // sample's file name, or the synth's model.
    juce::String name;
    juce::String sample;
    DrumModel model = DrumModel::Kick;
    DrumRackEngine::PadSettings settings;
    int choke = 0;

    // A file as dropping it on a pad makes it: untouched, at the defaults.
    static DrumSound forSample(const juce::File&);
    // A synth at its model's own Decay and Tone.
    static DrumSound forSynth(DrumModel);
    // The name a pad shows for this sound.
    juce::String displayName() const;
};

struct DrumKit
{
    std::array<std::optional<DrumSound>, DrumRackEngine::padCount> pads;
};

// The two files. A drum preset (.rdp) is one sound:
//
//   <RHINO_DRUM_SOUND format="1" name="Analog Kick" synth="Kick"
//                     tune="0" decay="0.6" tone="0.4" velocity="1" level="0" pan="0" choke="0"/>
//
// A kit (.rdk) is a rack's pads, each a sound or nothing:
//
//   <RHINO_DRUM_KIT format="1">
//     <PAD index="0" name="Kick" sample="library:Samples/TR808/TR808Kick.wav" .../>
//   </RHINO_DRUM_KIT>
//
// A setting a file leaves out is the control's default, so a sound plays the
// same whatever the pad held before it. Numbers are written to read back
// exactly. Format 1 is all that is read: no compatibility is owed before a
// production release, and the version is written so that it can be owed then.
struct DrumFiles
{
    static constexpr int format = 1;
    static constexpr const char* kitExtension = ".rdk";
    static constexpr const char* soundExtension = ".rdp";

    static std::unique_ptr<juce::XmlElement> toXml(const DrumSound&);
    static std::unique_ptr<juce::XmlElement> toXml(const DrumKit&);
    // Each fails with a message a person can act on, naming what is wrong.
    static juce::Result fromXml(const juce::XmlElement&, DrumSound& into);
    static juce::Result fromXml(const juce::XmlElement&, DrumKit& into);

    static juce::Result write(const DrumSound&, const juce::File&);
    static juce::Result write(const DrumKit&, const juce::File&);
    static juce::Result read(const juce::File&, DrumSound& into);
    static juce::Result read(const juce::File&, DrumKit& into);

    // A kit's or a preset's name is its file name.
    static juce::String nameOf(const juce::File& file) { return file.getFileNameWithoutExtension(); }
};
}
