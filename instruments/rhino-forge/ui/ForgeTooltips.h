#pragma once

#include "../core/ForgeWarp.h"
#include "../core/ForgeFilter.h"
#include "../core/ForgeNoise.h"

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

// What one filter type does, keyed by the type rather than by the parameter for
// the same reason the warp's is: the field it is shown on is thirty-four things
// depending on where it is set, so the explanation follows the setting.
// What one noise source sounds like, keyed by the source rather than by the
// parameter, for the same reason a warp mode's and a filter type's are: the
// field it is shown on is nineteen things depending on where it is set, so the
// explanation follows the setting.
//
// The analog five say what they are modelled on without claiming to be it.
// Every source here is generated; none is a recording, and the tooltips are
// written so nobody reads one and expects otherwise.
inline juce::String noiseSourceTooltipFor(int source)
{
    switch (noiseSourceOf(static_cast<float>(source)))
    {
        case NoiseSource::pink:
            return "Flat per octave rather than per hertz -- 3 dB an octave down. The one "
                   "that sits under a pad without hissing over it";
        case NoiseSource::brown:
            return "6 dB an octave down: rumble, wind and weather rather than air";
        case NoiseSource::blue:
            return "3 dB an octave UP, which is pink turned over. Air and sibilance with "
                   "nothing underneath it";
        case NoiseSource::violet:
            return "6 dB an octave up, the brightest thing here. Cymbal tops, tape hiss "
                   "caricatured, and dither";
        case NoiseSource::grey:
            return "Weighted so it sounds flat rather than measures flat: both ends of the "
                   "band lifted against the middle, where hearing is sharpest";
        case NoiseSource::vintagePoly:
            return "Full-bodied vintage polysynth hiss with a soft top. Pads, brass and "
                   "anything that wants weight under the noise";
        case NoiseSource::vintagePolyHp:
            return "The same soft top with the body taken out at the source, not turned "
                   "down. Leads, plucks and hats that must not thicken the low end";
        case NoiseSource::mono101:
            return "Raw monosynth noise: a focused low-mid band leaned on until it grits. "
                   "Bass attacks, percussion and old-school sweeps";
        case NoiseSource::tapeHiss:
            return "Pink with the top lifted and a slow breathing wobble over it, the way "
                   "a tape machine's floor moves";
        case NoiseSource::hardwareHum:
            return "Mains hum: fifty cycles and the partials above it, over a quiet floor. "
                   "Power-supply bleed as an instrument";
        case NoiseSource::bright:
            return "The bottom taken off and a resonant gloss over what is left. Air, "
                   "sheen and the top of a snare";
        case NoiseSource::bit:
            return "Held and quantised at once -- a converter out of both kinds of "
                   "resolution. Crunchy, aliased and deliberately cheap";
        case NoiseSource::alpha:
            return "A short shift register clocked inside the band: flat, but stepping "
                   "rather than random. Glassy and hollow";
        case NoiseSource::metallic:
            return "Five resonators at ratios that are not a harmonic series, so what "
                   "rings is a struck object rather than a note";
        case NoiseSource::vinyl:
            return "A record rather than a spectrum: the surface, the dirt on it and the "
                   "rumble of the table under both";
        case NoiseSource::wind:
            return "A band that moves. Two slow cycles beating against each other sweep "
                   "it, which is what tells wind from filtered hiss";
        case NoiseSource::geiger:
            return "Sparse clicks at random intervals rather than a spectrum. Glitch, "
                   "crackle and experimental percussion";
        case NoiseSource::crackle:
            return "The same clicks packed close enough to read as a surface. Fire, "
                   "static and grit";
        case NoiseSource::white:
            break;
    }
    return "Flat across the band: the reference the other eighteen are measured against, "
           "and what air, hats and snare tops are made of";
}

inline juce::String filterTooltipFor(int type)
{
    switch (filterTypeOf(static_cast<float>(type)))
    {
        case FilterType::highPass:
            return "Take the bottom off: everything under the cutoff goes";
        case FilterType::bandPass:
            return "Keep a band around the cutoff and drop what is either side of it";
        case FilterType::notch:
            return "Take out a narrow band at the cutoff and leave the rest. RES is how narrow";
        case FilterType::peak:
            return "Lift a band at the cutoff without taking anything out either side of it, "
                   "which is a bell rather than a filter. RES is how much lift";

        case FilterType::lowHigh:
            return "A low pass at the cutoff and a high pass at FREQ, so the band that gets "
                   "through has an edge you can place at each end";
        case FilterType::lowBand:
            return "A low pass at the cutoff, then a band pass at FREQ inside what it left";
        case FilterType::lowPeak:
            return "A low pass at the cutoff with a bell lifted at FREQ under it";
        case FilterType::lowNotch:
            return "A low pass at the cutoff with a notch cut out of it at FREQ";
        case FilterType::highBand:
            return "A high pass at the cutoff, then a band pass at FREQ inside what it left";
        case FilterType::highPeak:
            return "A high pass at the cutoff with a bell lifted at FREQ above it";
        case FilterType::highNotch:
            return "A high pass at the cutoff with a notch cut out of it at FREQ";
        case FilterType::bandPeak:
            return "A band pass at the cutoff with a bell lifted at FREQ inside it";
        case FilterType::bandNotch:
            return "A band pass at the cutoff with a notch taken out of it at FREQ";
        case FilterType::peakPeak:
            return "Two bells, one at the cutoff and one at FREQ, sharing the RES knob";
        case FilterType::peakNotch:
            return "A bell at the cutoff and a notch at FREQ: lift one band and lose another, "
                   "which is a lot of a vocal sound in two controls";
        case FilterType::notchNotch:
            return "Two notches, one at the cutoff and one at FREQ";

        case FilterType::morphLowBandHigh:
            return "One filter swept from low pass through band pass to high pass by MORPH. "
                   "Point an LFO at MORPH and the whole response moves rather than the corner";
        case FilterType::morphLowPeakHigh:
            return "Low pass to a bell to high pass, swept by MORPH";
        case FilterType::morphLowNotchHigh:
            return "Low pass to a notch to high pass, swept by MORPH";
        case FilterType::morphBandPeakNotch:
            return "Band pass to a bell to a notch, swept by MORPH: from one band only to "
                   "everything but that band";

        case FilterType::ladder:
            return "A four-pole transistor ladder: the poles in a chain and the last one fed "
                   "back round them through a saturator. Warm, and its resonance limits itself";
        case FilterType::acid:
            return "A three-pole diode ladder, leaned one way the way a diode conducts. "
                   "Squelchy, and the sound a 303 makes";
        case FilterType::dirtyLadder:
            return "The transistor ladder driven hard inside its own loop. PAIN is how much "
                   "more; DRIVE in front of it is what feeds the whole thing";
        case FilterType::ems:
            return "A three-pole ladder that keeps a little of the signal past the poles, so "
                   "the body is still there with the resonance up rather than swallowed by it";

        case FilterType::comb:
            return "A delay tuned to the cutoff and fed back, which is a row of peaks on its "
                   "harmonics rather than a corner. RES is how sharp; DAMP darkens the ring";
        case FilterType::flanger:
            return "The same delay against the dry signal, so its teeth are nulls instead of "
                   "peaks. Sweep the cutoff for the classic sound";
        case FilterType::phaser:
            return "All-pass stages against the dry signal: nulls that move with the cutoff "
                   "and none of a comb's fixed spacing. STAGES is how many, so how deep";

        case FilterType::formant:
            return "Three resonators on a vowel's own formants. The cutoff moves the mouth "
                   "from A to U and SHIFT moves the whole voice in pitch";
        case FilterType::ringMod:
            return "Multiply the signal by a sine at the cutoff, which replaces its harmonics "
                   "with sums and differences. SPREAD opens a second modulator above the first";
        case FilterType::sampleHold:
            return "Freeze the signal at the cutoff rate, which is aliasing used on purpose. "
                   "DIFF crosses to the difference between what is held and what came in";
        case FilterType::diffusor:
            return "All-pass stages in series: the magnitude is untouched and the phase is "
                   "smeared, so a transient spreads out. STAGES is how far";
        case FilterType::scream:
            return "A low pass with the damping taken out from under it until it oscillates, "
                   "held only by the saturation on its own resonant path. FEED is how far";
        case FilterType::reverb:
            return "The delay loop with all-pass stages inside it, so each pass scatters "
                   "instead of repeating. Resonant rather than roomy; DAMP darkens the tail";

        case FilterType::lowPass:
            break;
    }
    return "Take the top off: everything above the cutoff goes";
}

// What the one field beside TYPE does, which is a different question for every
// family. Read off the same table the label is, so the two cannot disagree.
inline juce::String filterSecondTooltipFor(int type)
{
    const auto chosen = filterTypeOf(static_cast<float>(type));
    switch (chosen)
    {
        case FilterType::dirtyLadder:
            return "How hard the ladder's own loop is driven, on top of the saturation it "
                   "always has";
        case FilterType::formant:
            return "Move the whole formant set in pitch, an octave either way: the same vowel "
                   "in a bigger or a smaller mouth";
        case FilterType::ringMod:
            return "Open a second modulator above the first, up to an octave apart. At nothing "
                   "there is one";
        case FilterType::sampleHold:
            return "Cross from the held steps to the difference between them and the signal";
        case FilterType::phaser:
        case FilterType::diffusor:
            return "How many all-pass stages are in the chain, which is how deep it goes";
        case FilterType::scream:
            return "How much damping is taken out from under the filter, and so how readily "
                   "it screams";
        case FilterType::comb:
        case FilterType::flanger:
        case FilterType::reverb:
            return "Where the one-pole in the feedback path sits: low is a thud, high is a "
                   "bright ring";
        default:
            break;
    }
    if (filterCategoryOf(chosen) == FilterCategory::dual)
        return "The second filter's own cutoff, on the same scale as CUTOFF. It stays put while "
               "CUTOFF sweeps past it; point a modulator at it to move the pair together";
    if (filterCategoryOf(chosen) == FilterCategory::morph)
        return "Sweep the response through the three the type names. The middle of the knob "
               "is the middle one on its own";
    return "Saturate the path the resonance comes back through, which rounds a sharp peak off "
           "and puts harmonics where it was";
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
        {"noiseSource", "Which generator the module reads: flat white, pink at 3 dB an "
                        "octave, brown at 6, or geiger's sparse clicks"},
        {"noiseTone", "Tilt the noise dark or bright about 1 kHz. Twelve o'clock is the "
                      "source exactly as it is generated"},
        {"noiseStereo", "Pull the two channels apart. At nothing both ears hear the same "
                        "samples; at the top they share no state at all"},
        {"noiseLevel", "Blend in broadband noise"},
        {"filterEnable", "Switch the filter out of the voice, drive and all"},
        {"cutoff", "Open or close the filter. On a comb or a ring modulator this sets the "
                   "frequency they are tuned to, and on a formant filter it moves the vowel"},
        {"resonance", "Emphasise the filter edge"},
        {"drive", "Saturate what is routed into the filter"},
        {"filterType", "Which filter this is. Click for the list, grouped by family, or step "
                       "through it with the arrows"},
        {"filterFreq", "The one control the filter type decides the meaning of: a second corner, "
                       "a morph position, a damping. Its label says which"},
        // One switch with two drawings: a lettered chip on the FILTER module,
        // and the TO field at the top of the channel's mixer strip.
        {"filterKeyTrack", "Make the cutoff follow the note, an octave for an octave, "
                           "measured from middle C. Off, the corner stays where CUTOFF puts it "
                           "whatever is played"},
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
