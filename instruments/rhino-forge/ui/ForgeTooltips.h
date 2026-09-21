#pragma once

#include "../core/ForgeWarp.h"

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
// What one warp mode does, in the words the manual uses for it. Keyed by the
// mode rather than by the parameter, because the field it is shown on is
// twenty-six things depending on where it is set -- so the explanation follows
// the setting, the way a rack slot's does.
inline juce::String warpTooltipFor(int mode)
{
    switch (warpModeOf(static_cast<float>(mode)))
    {
        case WarpMode::sync:
            return "Restart a second read of the table inside every cycle of the note. "
                   "The knob sets how much faster that read runs, which lifts the harmonics "
                   "without moving the pitch";
        case WarpMode::bendUp:
            return "Pinch both halves of the cycle inwards, towards the middle of each";
        case WarpMode::bendDown:
            return "Pull both halves of the cycle outwards, towards their edges";
        case WarpMode::bendBoth:
            return "Pinch or pull both halves of the cycle. Twelve o'clock is no change";
        case WarpMode::pwm:
            return "Give the first half of the wave more or less of the cycle than the second: "
                   "pulse width, and the classic sound of it on a square";
        case WarpMode::asym:
            return "Lean the whole cycle one way rather than bending each half of it. "
                   "Twelve o'clock is no change";
        case WarpMode::flip:
            return "Invert the wave part-way through the cycle. The knob says where the flip happens";
        case WarpMode::mirror:
            return "Play the second half of the cycle as the first half backwards, which doubles "
                   "the wave into the cycle and always has an audible effect";
        case WarpMode::quantize:
            return "Hold the read at steps rather than sweeping it, like a sample and hold on the "
                   "waveform. The grit follows the pitch rather than sitting at one frequency";
        case WarpMode::oddEven:
            return "Scale the odd and even harmonics against each other. Nothing is odd only, "
                   "the top is even only, and twelve o'clock is the wave as it was";
        case WarpMode::lowPass:
            return "Take the top off the waveform itself, at a corner that follows the note";
        case WarpMode::highPass:
            return "Take the bottom off the waveform itself, at a corner that follows the note";
        case WarpMode::bandPass:
            return "Keep a band of the waveform and drop what is either side of it, "
                   "at corners that follow the note";
        case WarpMode::tube:
            return "Valve saturation: a soft knee that is not the same either side of zero, "
                   "which is where its warmth comes from";
        case WarpMode::softClip:
            return "Round the peaks off rather than cutting them: quiet parts pass untouched";
        case WarpMode::hardClip:
            return "Cut the peaks off flat once they pass the threshold. The harsh one";
        case WarpMode::diode:
            return "Diode clipping, asymmetric the way the circuit it comes from is";
        case WarpMode::linearFold:
            return "Fold the wave back on itself every time it leaves full scale: metallic, "
                   "and busier the harder it is driven";
        case WarpMode::sineFold:
            return "The same folding through a sine, which rounds every corner the linear "
                   "fold leaves square";
        case WarpMode::rectify:
            return "Flip one half of the wave into the other. Harmonically rich, and often harsh";
        case WarpMode::tapeSat:
            return "The gentlest of the saturations: warmth and a little compression rather "
                   "than an edge";
        // PD moves where in the cycle the table is read. The note does not
        // move with it, which is the whole difference from FM below.
        case WarpMode::pdOsc:
            return "Push the read along the cycle with the other oscillator. It has to be on, "
                   "though its level can be down if it is only wanted as a modulator. "
                   "Unlike FM, this leaves the note exactly where it was";
        case WarpMode::pdSub:
            return "Push the read along the cycle with the sub, an octave below the note. "
                   "The SUB module has to be on, though its level can be down";
        case WarpMode::pdNoise:
            return "Push the read along the cycle with noise, which reads as grain rather "
                   "than as pitch";
        case WarpMode::pdSelf:
            return "Push the read along the cycle with this stage's own last output. "
                   "A little is a saw, a lot is chaos";

        // FM moves the rate the cycle runs at, which is why it can bend the
        // note where PD cannot.
        case WarpMode::fmOsc:
            return "Modulate this oscillator's frequency with the other one, in proportion. "
                   "It has to be on, though its level can be down. Clamped at zero rather than "
                   "running backwards, which is the traditional FM sound";
        case WarpMode::fmSub:
            return "Modulate this oscillator's frequency with the sub. The SUB module has to "
                   "be on, though its level can be down";
        case WarpMode::fmNoise:
            return "Modulate this oscillator's frequency with noise: grain and grit rather "
                   "than a second pitch";
        case WarpMode::fmExpOsc:
            return "The same, on an exponential curve: a small move in the other oscillator "
                   "sweeps the frequency a long way. Brighter and harsher than linear FM, "
                   "and it does not hold the note where linear does";
        case WarpMode::fmExpSub:
            return "Exponential frequency modulation from the sub. Sweeps further for the "
                   "same depth than linear does, and takes the pitch with it";
        case WarpMode::fmExpNoise:
            return "Exponential frequency modulation from noise. The most violent of the six";

        case WarpMode::amOsc:
            return "Ride this oscillator's level with the other one. The carrier is still in "
                   "there, so this is a tremolo taken up to audio rate";
        case WarpMode::amSub:
            return "Ride this oscillator's level with the sub, an octave below the note";
        case WarpMode::amNoise:
            return "Ride this oscillator's level with noise, which reads as grit on the sound";

        case WarpMode::rmOsc:
            return "Multiply this oscillator by the other one outright. The carrier's own "
                   "pitch leaves the sound and what is left is the two sidebands: the "
                   "clangorous, bell-like one";
        case WarpMode::rmSub:
            return "Multiply this oscillator by the sub, which puts the note an octave down "
                   "and takes the original out";
        case WarpMode::rmNoise:
            return "Multiply this oscillator by noise, which leaves neither pitch intact";

        case WarpMode::off:
            break;
    }
    return "No warp. Click to choose one; the arrows step through the list without opening it";
}

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
        {"Warp1Mode", "How oscillator % plays its table back, before WARP 2. "
                      "Click for the list, or step through it with the arrows"},
        {"Warp1", "How far oscillator %'s first warp goes"},
        {"Warp2Mode", "A second warp on oscillator %, applied to what the first one left"},
        {"Warp2", "How far oscillator %'s second warp goes"},
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
        {"subWave", "The shape the sub reads: a sine for weight and nothing else, "
                    "through to a saw or a pulse when the low end should have harmonics of its own"},
        {"subOctave", "How far below the note the sub plays. Zero is one octave down, "
                      "which is where a sub belongs and where it has always been"},
        {"subLevel", "Blend in the sub oscillator, an octave or more below the note"},
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

        // The arpeggiator. Every one of these says what it does to the notes
        // being held, because that is the only thing the arp acts on.
        {"arpEnable", "Play the keys being held as a sequence rather than as a chord"},
        {"arpLaunchQuant", "Wait for the next division of the host's bar before starting, "
                           "so an arp switched on mid-phrase still lands on the grid"},
        {"arpShape", "The order the held notes are played in: up, down, and the "
                     "thumb, pinky, converging and random patterns beside them"},
        {"arpRate", "How fast the pattern steps, in cycles per second"},
        {"arpDivision", "How fast the pattern steps, as a division of the host's beat"},
        {"arpRateUnit", "Count the rate in Hertz, free-running, or in divisions of the host's beat"},
        {"arpTriplet", "Make each step two thirds of the division, for triplets"},
        {"arpDotted", "Make each step half again as long as the division, for dotted notes"},
        {"arpShift", "How far each repetition of the pattern is transposed"},
        {"arpRange", "How many times the pattern is transposed by SHIFT before it starts over"},
        {"arpRangeShape", "The order the transpositions are visited in, "
                          "from the same list the pattern itself uses"},
        {"arpLatch", "Keep playing once the keys are let go. While the arp is on, "
                     "a sustain pedal works this rather than sustaining notes"},
        {"arpThru", "Pass the keys straight to the voices as well, the way a MIDI THRU port does. "
                    "Off, the arp consumes what it is given"},
        {"arpOffset", "Start the pattern somewhere other than its first note"},
        {"arpRepeats", "How many complete passes to play before falling silent. Zero is forever"},
        {"arpGate", "How long each note lasts against one step. Above 100% they overlap"},
        {"arpChance", "How likely each step is to sound at all"},
        {"arpChancePre", "PRE rolls for a note before the pattern moves on, so a note that was "
                         "skipped is the one played next time rather than being missed"},
        {"arpRetrigLaunch", "Start the pattern from its beginning when the arp is switched on"},
        {"arpRetrigRateOn", "Start the pattern from its beginning at a fixed interval"},
        {"arpRetrigRate", "How often the pattern is restarted"},
        {"arpRetrigNote", "Start the pattern from its beginning whenever a key goes down"},
        {"arpRetrigFirst", "Restart only on the first key of a chord, not on every "
                           "note of one played together"},
        {"arpVelEnable", "Let the played velocity drift as the arp runs, rather than "
                         "every note sounding at the velocity its key was struck at"},
        {"arpVelRetrig", "Return the velocity to the incoming one whenever the pattern restarts"},
        {"arpVelDecay", "How far towards the target one step moves the velocity"},
        {"arpVelTarget", "The velocity the decay moves towards"},

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
        return "A performance macro. Drag the number on the plate beside it onto a knob, "
               "or right-click that knob and choose this macro as a source";

    // Matrix slots: eight of each, all reading the same way.
    if (id.startsWith("mod"))
    {
        if (id.endsWith("Source")) return "What drives this slot";
        if (id.endsWith("Dest")) return "Which control this slot moves";
        if (id.endsWith("Depth"))
            return "How far, and in which direction, the source moves the target. "
                   "Draggable on the target's own ring too";
        if (id.endsWith("Bipolar"))
            return "BI centres the source, so the target's own setting is the middle "
                   "of the reach rather than the bottom of it: the source at rest "
                   "then pulls fully negative instead of doing nothing. LFOs already "
                   "swing both ways and are unaffected";
    }
    const auto found = tips.find(id);
    return found == tips.end() ? juce::String() : found->second;
}
}
