#pragma once
#include "DrumRackEngine.h"
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <optional>

namespace rhino
{
// One drum sound: everything a Drum Rack pad holds, kept apart from any rack.
// A drum preset is one of these in a file; a kit is a rack's worth, one per
// note that holds a sound.
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
    // How a sample plays: the sample editor's mode, part, fades and
    // envelope. A synth ignores it.
    DrumRackEngine::Playback playback;
    int choke = 0;

    // A file as dropping it on a pad makes it: untouched, at the defaults.
    static DrumSound forSample(const juce::File&);
    // A synth at its model's own Decay and Tone.
    static DrumSound forSynth(DrumModel);
    // The name a pad shows for this sound.
    juce::String displayName() const;
};

// A rack's pads, by MIDI note.
struct DrumKit
{
    std::array<std::optional<DrumSound>, DrumRackEngine::padCount> pads;
};

// The two files. A drum preset (.rdp) is one sound:
//
//   <RHINO_DRUM_SOUND format="1" name="Analog Kick" synth="Kick"
//                     tune="0" decay="0.6" tone="0.4" velocity="1" level="0" pan="0" choke="0"/>
//
// A kit (.rdk) is a rack's pads, each on its note:
//
//   <RHINO_DRUM_KIT format="2">
//     <PAD note="48" name="Kick" sample="library:Samples/TR808/TR808Kick.wav" .../>
//   </RHINO_DRUM_KIT>
//
// A sample's playback is written only where it differs from a plain one-shot
// of the whole file: mode="classic" or "slice", start and end as fractions of
// the file, fadeIn, fadeOut, attack, sustain and release, loop="1", and how
// Slice cuts (sliceBy="divisions", divisions, sensitivity).
//
// A setting a file leaves out is the control's default, so a sound plays the
// same whatever the pad held before it. Numbers are written to read back
// exactly. One format of each is read: no compatibility is owed before a
// production release, and the version is written so that it can be owed then.
// Kits are format 2 since a rack has a pad on every note; format 1 numbered
// sixteen pads from C2 and is refused.
struct DrumFiles
{
    static constexpr int soundFormat = 1;
    static constexpr int kitFormat = 2;
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

// What a play mode is called in a file.
juce::String playModeId(DrumRackEngine::PlayMode);
std::optional<DrumRackEngine::PlayMode> playModeFromId(const juce::String&);

// A pad's note as the face and every message name it. Middle C is C3, as the
// note editor names its rows, so a rack's first pads by default are C2 up.
juce::String padNoteName(int note);
}
