#pragma once

#include <juce_core/juce_core.h>
#include <map>

// What every control says when the pointer rests on it. Kept beside the layout
// rather than inside the editor so the layout test can hold it to the same
// standard it holds the parameter ids to: a control declared with no tooltip is
// a control nobody explained, and that is as much a mistake as a knob wired to
// a parameter that does not exist.
//
// Keyed by parameter id, so a module can be renamed, reordered or moved to
// another tab without disturbing any of this.
namespace rhino::forge::ui
{
inline juce::String tooltipFor(const juce::String& id)
{
    // Both oscillators expose the same controls, so their tooltips are keyed by
    // the suffix and the oscillator's letter is filled in below.
    static const std::map<juce::String, juce::String> perOscillator {
        {"Enable", "Switch oscillator % out of the voice entirely"},
        {"Position", "Scan oscillator %'s harmonic shape: sine, triangle, saw, square"},
        {"Octave", "Transpose oscillator % in octaves"},
        {"Semitone", "Transpose oscillator % in semitones"},
        {"Fine", "Detune oscillator % in cents"},
        {"Unison", "Stack detuned copies of oscillator %"},
        {"Detune", "Spread oscillator %'s stack in pitch and across the stereo field"},
        {"Blend", "Balance the centre of oscillator %'s stack against its edges"},
        {"Pan", "Place oscillator % in the stereo field"},
        {"Level", "Set oscillator %'s level"},
    };
    for (const auto& letter : {"A", "B"})
        if (id.startsWith("osc" + juce::String(letter)))
        {
            const auto found = perOscillator.find(id.fromFirstOccurrenceOf("osc" + juce::String(letter), false, false));
            if (found != perOscillator.end()) return found->second.replace("%", letter);
        }

    static const std::map<juce::String, juce::String> tips {
        {"subEnable", "Switch the sub oscillator on or off"},
        {"subLevel", "Blend in a sine one octave below the note"},
        {"noiseEnable", "Switch the noise source on or off"},
        {"noiseLevel", "Blend in broadband noise"},
        {"filterEnable", "Switch the filter out of the voice, drive and all"},
        {"cutoff", "Open or close the filter"},
        {"resonance", "Emphasise the filter edge"},
        {"drive", "Saturate what is routed into the filter"},
        {"filterType", "Low pass, high pass or band pass"},
        {"routeA", "Send oscillator A through the filter"},
        {"routeB", "Send oscillator B through the filter"},
        {"routeSub", "Send the sub through the filter"},
        {"routeNoise", "Send the noise through the filter"},
        {"attack", "How the note begins"},
        {"decay", "The fall from the attack peak"},
        {"sustain", "The level a held note settles at"},
        {"release", "How the note fades once released"},

        {"polyphony", "Limit simultaneous notes"},
        {"mono", "Collapse to one voice for basses and leads"},
        {"legato", "Keep the envelope running across overlapping mono notes"},
        {"glide", "Slide between monophonic notes"},
        {"output", "Forge's final level"},
    };
    // Six LFOs expose the same controls, so their tooltips are keyed by the
    // suffix and the LFO's number is filled in, the same way the oscillators'
    // are keyed by their letter.
    static const std::map<juce::String, juce::String> perLfo {
        {"Shape", "The curve LFO % runs: sine, triangle, saw, square or sample and hold"},
        {"Mode", "TRIG restarts LFO % on every new note and loops for as long as one is held; "
                 "ENV restarts it and stops at the end of the shape, as a one-shot envelope; "
                 "OFF free-runs across notes and never resets"},
        {"RateUnit", "Set LFO %'s rate in Hertz, or in divisions of the host's tempo"},
        {"Rate", "LFO %'s speed in Hertz. Point it somewhere in the matrix"},
        {"Division", "LFO %'s speed as the length of one cycle in beats, against the host's tempo"},
    };
    if (id.startsWith("lfo") && id.length() > 4 && juce::CharacterFunctions::isDigit(id[3]))
    {
        const auto found = perLfo.find(id.substring(4));
        if (found != perLfo.end())
            return found->second.replace("%", id.substring(3, 4));
    }

    if (id.startsWith("macro"))
        return "A performance macro. Drag its number onto a knob, or right-click the knob";

    // Matrix slots: eight of each, all reading the same way.
    if (id.startsWith("mod"))
    {
        if (id.endsWith("Source")) return "What drives this slot";
        if (id.endsWith("Dest")) return "Which control this slot moves";
        if (id.endsWith("Depth"))
            return "How far, and in which direction, the source moves the target. "
                   "Draggable on the target's own ring too";
    }
    const auto found = tips.find(id);
    return found == tips.end() ? juce::String() : found->second;
}
}
