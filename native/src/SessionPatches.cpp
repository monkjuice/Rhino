#include "SessionInternal.h"

// Preset pattern data and the synth patches each preset applies.

namespace rhino
{

PresetPattern presetPattern(Session::PatternPreset preset)
{
    static constexpr PresetNote warmPulse[] {{0, 48, 2}, {4, 55, 2}, {8, 60, 2}, {12, 55, 2}};
    static constexpr PresetNote acidSteps[] {{0, 48, 1}, {3, 51, 1}, {6, 55, 1}, {7, 58, 1}, {10, 55, 1}, {13, 63, 1}, {15, 58, 1}};
    static constexpr PresetNote arpRun[] {{0, 48, 16}, {0, 52, 16}, {0, 55, 16}, {0, 60, 16}};
    static constexpr PresetNote chordPad[] {{0, 48, 7}, {0, 55, 7}, {0, 60, 7}, {0, 64, 7},
                                            {8, 50, 7}, {8, 57, 7}, {8, 62, 7}, {8, 65, 7}};
    static constexpr PresetNote subBass[] {{0, 36, 4}, {4, 36, 2}, {6, 43, 2}, {8, 39, 4}, {12, 34, 4}};
    static constexpr PresetNote reeseBass[] {{0, 36, 8}, {8, 39, 4}, {12, 41, 4}};
    static constexpr PresetNote sirenLead[] {{0, 48, 1}, {1, 55, 1}, {2, 60, 1}, {3, 67, 1}, {4, 72, 2}, {7, 67, 1},
                                             {8, 60, 1}, {9, 55, 1}, {10, 48, 1}, {12, 60, 1}, {14, 67, 1}, {15, 72, 1}};
    static constexpr PresetNote wavePad[] {{0, 48, 8}, {0, 55, 8}, {0, 60, 8}, {8, 50, 8}, {8, 57, 8}, {8, 62, 8}};
    static constexpr PresetNote waveBass[] {{0, 36, 3}, {3, 36, 1}, {4, 43, 2}, {8, 34, 4}, {12, 39, 3}, {15, 41, 1}};
    static constexpr PresetNote wavePluck[] {{0, 60, 1}, {2, 67, 1}, {4, 72, 2}, {7, 67, 1}, {8, 62, 1}, {10, 69, 1}, {12, 74, 2}, {15, 69, 1}};
    static constexpr PresetNote houseKit[] {{0, 48, 1}, {4, 48, 1}, {8, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1},
                                            {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote breakKit[] {{0, 48, 1}, {3, 48, 1}, {8, 48, 1}, {11, 48, 1}, {4, 53, 1}, {10, 53, 1},
                                            {1, 58, 1}, {3, 58, 1}, {6, 58, 1}, {9, 58, 1}, {12, 58, 1}, {15, 58, 1}};
    static constexpr PresetNote minimalKit[] {{0, 48, 1}, {7, 48, 1}, {12, 48, 1}, {4, 53, 1}, {12, 53, 1}, {2, 58, 1}, {10, 58, 1}, {14, 58, 1}};
    static constexpr PresetNote clapKit[] {{0, 48, 1}, {4, 56, 1}, {8, 48, 1}, {12, 56, 1}, {2, 58, 1}, {6, 58, 1}, {10, 58, 1}, {14, 58, 1}};

    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return {warmPulse,  static_cast<int>(std::size(warmPulse)),  "Warm pulse", false, false, SynthPatch::Default};
        case Session::PatternPreset::AcidSteps:  return {acidSteps,  static_cast<int>(std::size(acidSteps)),  "Acid steps", false, false, SynthPatch::Default};
        case Session::PatternPreset::ArpRun:     return {arpRun,     static_cast<int>(std::size(arpRun)),     "Arp run", false, false, SynthPatch::Default};
        case Session::PatternPreset::ChordPad:   return {chordPad,   static_cast<int>(std::size(chordPad)),   "Chord pad", false, false, SynthPatch::ChordPad};
        case Session::PatternPreset::SubBass:    return {subBass,    static_cast<int>(std::size(subBass)),    "Sub bass", false, false, SynthPatch::SubBass};
        case Session::PatternPreset::ReeseBass:  return {reeseBass,  static_cast<int>(std::size(reeseBass)),  "Reese bass", false, false, SynthPatch::ReeseBass};
        case Session::PatternPreset::SirenLead:  return {sirenLead,  static_cast<int>(std::size(sirenLead)),  "Siren lead", false, false, SynthPatch::Default};
        case Session::PatternPreset::WavePad:    return {wavePad,    static_cast<int>(std::size(wavePad)),    "Wave pad", false, true,  SynthPatch::Default};
        case Session::PatternPreset::WaveBass:   return {waveBass,   static_cast<int>(std::size(waveBass)),   "Wave bass", false, true,  SynthPatch::Default};
        case Session::PatternPreset::WavePluck:  return {wavePluck,  static_cast<int>(std::size(wavePluck)),  "Wave pluck", false, true,  SynthPatch::Default};
        case Session::PatternPreset::HouseKit:   return {houseKit,   static_cast<int>(std::size(houseKit)),   "House kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::BreakKit:   return {breakKit,   static_cast<int>(std::size(breakKit)),   "Break kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::MinimalKit: return {minimalKit, static_cast<int>(std::size(minimalKit)), "Minimal kit", true,  false, SynthPatch::Default};
        case Session::PatternPreset::ClapKit:    return {clapKit,    static_cast<int>(std::size(clapKit)),    "Clap kit", true,  false, SynthPatch::Default};
    }
    return {};
}

void fillMidiClip(te::MidiClip& clip, const PresetPattern& preset, juce::UndoManager& undoManager)
{
    clip.state.removeProperty(starterPlaceholderID, &undoManager);
    auto& sequence = clip.getSequence();
    sequence.removeAllNotes(&undoManager);
    for (int i = 0; i < preset.count; ++i)
        sequence.addNote(preset.notes[i].pitch, tracktion::core::BeatPosition::fromBeats(preset.notes[i].step * 0.25),
                         tracktion::core::BeatDuration::fromBeats(std::max(1, preset.notes[i].length) * 0.225), 100, 0,
                         &undoManager);
    clip.setName(preset.name);
}

void setPluginParameter(te::AutomatableParameter::Ptr parameter, float value)
{
    if (parameter == nullptr)
        return;
    const auto range = parameter->getValueRange();
    parameter->setParameter(juce::jlimit(range.getStart(), range.getEnd(), value), juce::sendNotification);
}

void applyChordPadPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.18f);
    setPluginParameter(synth.ampDecay, 0.55f);
    setPluginParameter(synth.ampSustain, 78.0f);
    setPluginParameter(synth.ampRelease, 0.85f);
    setPluginParameter(synth.ampVelocity, 55.0f);
    setPluginParameter(synth.filterAttack, 0.12f);
    setPluginParameter(synth.filterDecay, 0.45f);
    setPluginParameter(synth.filterSustain, 62.0f);
    setPluginParameter(synth.filterRelease, 0.75f);
    setPluginParameter(synth.filterFreq, 78.0f);
    setPluginParameter(synth.filterResonance, 12.0f);
    setPluginParameter(synth.filterAmount, 0.08f);
    setPluginParameter(synth.chorusSpeed, 0.65f);
    setPluginParameter(synth.chorusDepth, 7.5f);
    setPluginParameter(synth.chorusWidth, 0.9f);
    setPluginParameter(synth.chorusMix, 0.24f);
    setPluginParameter(synth.reverbSize, 0.48f);
    setPluginParameter(synth.reverbDamping, 0.62f);
    setPluginParameter(synth.reverbWidth, 0.95f);
    setPluginParameter(synth.reverbMix, 0.11f);
    setPluginParameter(synth.masterLevel, -9.0f);

    synth.state.setProperty("voiceMode", 2, &undoManager);
    synth.state.setProperty("voices", 32, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 12, &undoManager);
    synth.state.setProperty("chorusOn", true, &undoManager);
    synth.state.setProperty("reverbOn", true, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -9.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.02f);
        setPluginParameter(synth.oscParams[0]->spread, 45.0f);
        setPluginParameter(synth.oscParams[1]->level, -12.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, -8.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.04f);
        setPluginParameter(synth.oscParams[1]->spread, -55.0f);
        setPluginParameter(synth.oscParams[2]->level, -16.0f);
        setPluginParameter(synth.oscParams[2]->tune, 12.0f);
        setPluginParameter(synth.oscParams[2]->fineTune, 5.0f);
        setPluginParameter(synth.oscParams[2]->spread, 70.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applySubBassPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.006f);
    setPluginParameter(synth.ampDecay, 0.18f);
    setPluginParameter(synth.ampSustain, 88.0f);
    setPluginParameter(synth.ampRelease, 0.22f);
    setPluginParameter(synth.ampVelocity, 42.0f);
    setPluginParameter(synth.filterAttack, 0.0f);
    setPluginParameter(synth.filterDecay, 0.12f);
    setPluginParameter(synth.filterSustain, 55.0f);
    setPluginParameter(synth.filterRelease, 0.18f);
    setPluginParameter(synth.filterFreq, 48.0f);
    setPluginParameter(synth.filterResonance, 3.0f);
    setPluginParameter(synth.filterAmount, -0.03f);
    setPluginParameter(synth.chorusMix, 0.0f);
    setPluginParameter(synth.reverbMix, 0.0f);
    setPluginParameter(synth.legato, 55.0f);
    setPluginParameter(synth.masterLevel, -8.0f);

    synth.state.setProperty("voiceMode", 1, &undoManager);
    synth.state.setProperty("voices", 1, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 24, &undoManager);
    synth.state.setProperty("chorusOn", false, &undoManager);
    synth.state.setProperty("reverbOn", false, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -5.5f);
        setPluginParameter(synth.oscParams[0]->tune, 0.0f);
        setPluginParameter(synth.oscParams[0]->fineTune, 0.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.0f);
        setPluginParameter(synth.oscParams[0]->spread, 0.0f);
        setPluginParameter(synth.oscParams[1]->level, -19.0f);
        setPluginParameter(synth.oscParams[1]->tune, 12.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, 0.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.0f);
        setPluginParameter(synth.oscParams[1]->spread, 0.0f);
        setPluginParameter(synth.oscParams[2]->level, -100.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applyReeseBassPatch(te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    setPluginParameter(synth.ampAttack, 0.012f);
    setPluginParameter(synth.ampDecay, 0.45f);
    setPluginParameter(synth.ampSustain, 82.0f);
    setPluginParameter(synth.ampRelease, 0.38f);
    setPluginParameter(synth.ampVelocity, 48.0f);
    setPluginParameter(synth.filterAttack, 0.03f);
    setPluginParameter(synth.filterDecay, 0.55f);
    setPluginParameter(synth.filterSustain, 48.0f);
    setPluginParameter(synth.filterRelease, 0.28f);
    setPluginParameter(synth.filterFreq, 63.0f);
    setPluginParameter(synth.filterResonance, 18.0f);
    setPluginParameter(synth.filterAmount, 0.12f);
    setPluginParameter(synth.chorusSpeed, 0.35f);
    setPluginParameter(synth.chorusDepth, 4.5f);
    setPluginParameter(synth.chorusWidth, 0.72f);
    setPluginParameter(synth.chorusMix, 0.16f);
    setPluginParameter(synth.reverbMix, 0.0f);
    setPluginParameter(synth.legato, 85.0f);
    setPluginParameter(synth.masterLevel, -10.0f);

    synth.state.setProperty("voiceMode", 1, &undoManager);
    synth.state.setProperty("voices", 1, &undoManager);
    synth.state.setProperty("filterType", 1, &undoManager);
    synth.state.setProperty("filterSlope", 12, &undoManager);
    synth.state.setProperty("chorusOn", true, &undoManager);
    synth.state.setProperty("reverbOn", false, &undoManager);
    synth.state.setProperty("distortionOn", false, &undoManager);

    if (synth.oscParams.size() >= 4)
    {
        setPluginParameter(synth.oscParams[0]->level, -9.0f);
        setPluginParameter(synth.oscParams[0]->fineTune, -9.0f);
        setPluginParameter(synth.oscParams[0]->detune, 0.08f);
        setPluginParameter(synth.oscParams[0]->spread, -70.0f);
        setPluginParameter(synth.oscParams[1]->level, -9.0f);
        setPluginParameter(synth.oscParams[1]->fineTune, 9.0f);
        setPluginParameter(synth.oscParams[1]->detune, 0.08f);
        setPluginParameter(synth.oscParams[1]->spread, 70.0f);
        setPluginParameter(synth.oscParams[2]->level, -18.0f);
        setPluginParameter(synth.oscParams[2]->tune, -12.0f);
        setPluginParameter(synth.oscParams[2]->spread, 0.0f);
        setPluginParameter(synth.oscParams[3]->level, -100.0f);
    }
}

void applySynthPatch(SynthPatch patch, te::FourOscPlugin& synth, juce::UndoManager& undoManager)
{
    switch (patch)
    {
        case SynthPatch::Default:   break;
        case SynthPatch::ChordPad:  applyChordPadPatch(synth, undoManager); break;
        case SynthPatch::SubBass:   applySubBassPatch(synth, undoManager); break;
        case SynthPatch::ReeseBass: applyReeseBassPatch(synth, undoManager); break;
    }
}

void applyRhinoWavePatch(Session::PatternPreset preset, RhinoWaveDevice& wave)
{
    const auto set = [&wave] (int parameterIndex, float value)
    {
        int active = 0;
        for (auto* parameter : wave.getAutomatableParameters())
        {
            if (parameter == nullptr || !parameter->isParameterActive())
                continue;
            if (active++ != parameterIndex)
                continue;
            const auto range = parameter->getValueRange();
            parameter->setParameter(juce::jlimit(range.getStart(), range.getEnd(), value), juce::sendNotification);
            return;
        }
    };

    if (preset == Session::PatternPreset::WavePad)
    {
        set(0, 0.38f);    // Position
        set(1, 0.72f);    // Shape
        set(2, 0.35f);    // Motion
        set(3, 5200.0f);  // Cutoff
        set(4, 0.22f);    // Env
        set(5, 4.0f);     // Drive
        set(6, 0.08f);    // Sub
        set(7, 0.18f);    // Resonance
        set(8, 0.22f);    // Attack
        set(9, 0.85f);    // Decay
        set(10, 0.78f);   // Sustain
        set(11, 1.15f);   // Release
        set(12, 3.0f);    // Unison
        set(13, 0.13f);   // Detune
        set(14, 0.78f);   // Width
        set(15, -12.0f);  // Output
        set(16, 0.34f);   // Osc 2
        set(17, 12.0f);   // Tune 2
    }
    else if (preset == Session::PatternPreset::WaveBass)
    {
        set(0, 0.58f);
        set(1, 0.34f);
        set(2, 0.12f);
        set(3, 2600.0f);
        set(4, -0.08f);
        set(5, 7.5f);
        set(6, 0.42f);
        set(7, 0.12f);
        set(8, 0.006f);
        set(9, 0.16f);
        set(10, 0.7f);
        set(11, 0.18f);
        set(12, 1.0f);
        set(13, 0.02f);
        set(14, 0.18f);
        set(15, -10.0f);
        set(16, 0.28f);
        set(17, -12.0f);
    }
    else if (preset == Session::PatternPreset::WavePluck)
    {
        set(0, 0.64f);
        set(1, 0.82f);
        set(2, 0.55f);
        set(3, 4200.0f);
        set(4, 0.65f);
        set(5, 8.0f);
        set(6, 0.1f);
        set(7, 0.28f);
        set(8, 0.004f);
        set(9, 0.24f);
        set(10, 0.18f);
        set(11, 0.22f);
        set(12, 2.0f);
        set(13, 0.06f);
        set(14, 0.52f);
        set(15, -11.0f);
        set(16, 0.22f);
        set(17, 7.0f);
    }
}

}
