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
        {"Send1", "How much of oscillator % is sent to BUS 1, on top of where it already goes"},
        {"Send2", "How much of oscillator % is sent to BUS 2, on top of where it already goes"},
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
        // One switch with two drawings: a lettered chip on the FILTER module,
        // and the TO field at the top of the channel's mixer strip.
        {"routeA", "Send oscillator A through the filter, or straight to the main output"},
        {"routeB", "Send oscillator B through the filter, or straight to the main output"},
        {"routeSub", "Send the sub through the filter, or straight to the main output"},
        {"routeNoise", "Send the noise through the filter, or straight to the main output"},

        // The mixer. A channel's own controls read the same way whichever
        // channel they belong to, so each says what it does to that channel.
        {"subPan", "Place the sub in the stereo field"},
        {"noisePan", "Place the noise in the stereo field"},
        {"subSend1", "How much of the sub is sent to BUS 1, on top of where it already goes"},
        {"subSend2", "How much of the sub is sent to BUS 2, on top of where it already goes"},
        {"noiseSend1", "How much of the noise is sent to BUS 1, on top of where it already goes"},
        {"noiseSend2", "How much of the noise is sent to BUS 2, on top of where it already goes"},
        {"filterPan", "Place the filter's output in the stereo field"},
        {"filterMix", "Blend the filter's output against what was sent into it"},
        {"filterLevel", "Set the level of everything coming out of the filter"},
        {"filterSend1", "How much of the filter's output is sent to BUS 1, as well as to the output"},
        {"filterSend2", "How much of the filter's output is sent to BUS 2, as well as to the output"},
        {"bus1Enable", "Switch BUS 1 out of the mix; what is sent to it is then heard nowhere"},
        {"bus2Enable", "Switch BUS 2 out of the mix; what is sent to it is then heard nowhere"},
        {"bus1Dest", "Send BUS 1 to the main output, or across into BUS 2"},
        {"bus2Dest", "Send BUS 2 to the main output, or across into BUS 1"},
        {"bus1Pan", "Place everything arriving at BUS 1 in the stereo field"},
        {"bus2Pan", "Place everything arriving at BUS 2 in the stereo field"},
        {"bus1Level", "Set the level of everything arriving at BUS 1"},
        {"bus2Level", "Set the level of everything arriving at BUS 2"},

        {"polyphony", "Limit simultaneous notes"},
        {"mono", "Collapse to one voice for basses and leads"},
        {"legato", "Keep the envelope running across overlapping mono notes"},
        {"glide", "Slide between monophonic notes"},
        {"output", "Forge's final level"},
    };
    // Four envelopes expose the same controls, keyed by the suffix with the
    // envelope's number filled in, the same way the oscillators' are keyed by
    // their letter. ENV 1 is the amplitude and ENV 2-4 reach a control only
    // through the matrix, so what each one does is said once, in the number.
    static const std::map<juce::String, juce::String> perEnv {
        {"Attack", "How ENV % begins"},
        {"Decay", "ENV %'s fall from the attack peak"},
        {"Sustain", "The level ENV % settles at while a note is held"},
        {"Release", "How ENV % falls once the note is released"},
    };
    if (id.startsWith("env") && id.length() > 4 && juce::CharacterFunctions::isDigit(id[3]))
    {
        const auto found = perEnv.find(id.substring(4));
        if (found != perEnv.end())
        {
            const auto number = id.substring(3, 4);
            // ENV 1 is wired to the amplitude and needs no telling where it
            // goes; the others go nowhere until a slot sends them.
            return found->second.replace("%", number)
                 + (number == "1" ? ", the voice's amplitude"
                                  : ". Point it somewhere in the matrix");
        }
    }

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

    // The rack. A slot's controls are the same twelve whatever type it holds,
    // so what they say here is what they are *for* — the panel replaces each
    // one at runtime with the type's own words, which is the only place that
    // can be known. See Editor::refreshFxSlots.
    if (id.startsWith("fx"))
    {
        if (id.endsWith("Type")) return "What this slot of the rack is: a reverb, a delay, "
                                        "a chorus, a distortion, an equaliser, a filter, or nothing";
        if (id.endsWith("ModeA") || id.endsWith("ModeB"))
            return "A choice belonging to whichever effect is in this slot";
        if (id.contains("Knob")) return "A control of whichever effect is in this slot. "
                                        "Its name and its units come from that effect";
        if (id.endsWith("Mix")) return "The wet and dry balance for this slot, from all dry to all wet";
        if (id.endsWith("Level")) return "The output level of this slot";
        // The rack's own bypass carries no slot in its id; a slot's does.
        if (id.endsWith("Bypass"))
            return id.contains("s") ? "Take this slot out of the rack without removing what is set on it"
                                    : "Take this whole rack out of the signal";
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
