#pragma once
#include <juce_data_structures/juce_data_structures.h>

// Shared by DrumRackDevice's own translation units, DrumRackDevice.cpp and
// DrumRackDeviceEditing.cpp, and nothing else.
namespace rhino::drumrack
{
// The device's state: one PADS child holding a PAD for each note that holds a
// sound, and two properties of view state beside the controls.
inline const juce::Identifier padsId { "PADS" };
inline const juce::Identifier padId { "PAD" };
inline const juce::Identifier noteId { "note" };
inline const juce::Identifier sampleId { "sample" };
inline const juce::Identifier synthId { "synth" };
inline const juce::Identifier nameId { "name" };
inline const juce::Identifier chokeId { "choke" };
inline const juce::Identifier muteId { "mute" };
inline const juce::Identifier soloId { "solo" };
inline const juce::Identifier selectedId { "selPad" };
inline const juce::Identifier firstShownId { "firstNote" };
// A sample pad's playback, each written only where it differs from a plain
// one-shot of the whole file.
inline const juce::Identifier modeId { "mode" };
inline const juce::Identifier startId { "start" };
inline const juce::Identifier endId { "end" };
inline const juce::Identifier fadeInId { "fadeIn" };
inline const juce::Identifier fadeOutId { "fadeOut" };
inline const juce::Identifier attackId { "attack" };
inline const juce::Identifier sustainId { "sustain" };
inline const juce::Identifier releaseId { "release" };
inline const juce::Identifier loopId { "loop" };
inline const juce::Identifier sliceById { "sliceBy" };
inline const juce::Identifier divisionsId { "divisions" };
inline const juce::Identifier sensitivityId { "sensitivity" };

inline bool validPad(int note)
{
    return juce::isPositiveAndBelow(note, 128);
}
}
