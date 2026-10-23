#pragma once

#include "ForgePresetBank.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Opening another synth's preset as a Forge patch.
//
// A converted preset is a Forge state tree, the same shape a .forgepreset
// carries: a PARAM per parameter it sets, a TABLE per oscillator whose
// wavetable it brings, a ForgeLfoTable per drawn LFO, the macro names as
// properties. Loading one is then exactly loading a Forge preset
// (Processor::applySavedState), and copying one into the bank is writing that
// tree to a file, so there is one way a patch arrives whatever it was.
//
// What a conversion cannot carry — the other synth's second filter, which the
// converters do not yet put in Forge's, a seventh LFO, a source Forge has no
// equivalent of, an effect it does not have — is left out and said
// in a warning, one line each, rather than approximated by something that
// would sound like a mistake. The plate and the browser show them.
//
// Message thread only: every converter reads files, and a Serum preset
// decompresses its whole patch.
namespace rhino::forge
{
struct ImportedPreset
{
    juce::ValueTree state {"RhinoForgeState"};
    PresetInfo info;
};

// Serum 2 or Vital, by extension. The parameters are what the tree's values
// are clamped against, so a value past one of Forge's ranges is noted rather
// than silently cut.
juce::Result importPreset(const juce::File&, const juce::AudioProcessorValueTreeState& parameters,
                          ImportedPreset& result);

juce::Result importSerumPreset(const juce::File&, const juce::AudioProcessorValueTreeState&, ImportedPreset&);
juce::Result importVitalPreset(const juce::File&, const juce::AudioProcessorValueTreeState&, ImportedPreset&);

// The metadata at the head of a .SerumPreset, readable without decompressing
// the patch behind it. What the browser lists.
struct SerumPresetHeader
{
    juce::String name, author, style, description;
};
bool readSerumPresetHeader(const juce::File&, SerumPresetHeader&);
}
