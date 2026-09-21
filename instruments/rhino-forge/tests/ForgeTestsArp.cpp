// The arpeggiator: the orders the shapes visit a chord in, and the notes the
// engine actually emits when one is held down.
//
// Most of this needs no Processor at all. `Arp` is fed notes and advanced a
// sample at a time, and it hands its results to two callbacks — so the whole of
// it can be driven by a pair of lambdas that write into a vector, and what the
// arp did is a list of note numbers rather than a waveform to be measured. The
// suite at the end goes through the Processor instead, because the one thing
// lambdas cannot check is that the notes reach the voices.
#include "ForgeTestSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rhino::forge::tests
{
namespace
{
using rhino::forge::Arp;
using rhino::forge::ArpSettings;
using rhino::forge::ArpShape;

// The order a shape visits `count` things in, as a vector, for comparing
// against what the manual's own figures show.
std::vector<int> orderOf(ArpShape shape, int count)
{
    std::array<int, rhino::forge::arpMaxSteps> out {};
    const auto length = rhino::forge::arpOrder(shape, count, out);
    return {out.begin(), out.begin() + length};
}

juce::String printed(const std::vector<int>& values)
{
    juce::String text;
    for (const auto value : values) text << value << ' ';
    return text.trim();
}

void requireOrder(ArpShape shape, int count, const std::vector<int>& expected, const char* message)
{
    const auto actual = orderOf(shape, count);
    if (actual != expected)
        std::cerr << "  expected [" << printed(expected) << "] got [" << printed(actual) << "]\n";
    require(actual == expected, message);
}

// The quietest stretch of a render, as RMS. An arpeggio with a short gate has
// near-silence between its notes and a held chord does not, which is the
// difference the checks below are actually looking for.
//
// The window is deliberately shorter than one arp step. Measured over anything
// longer, every window catches a note and the gaps disappear into the average —
// which is exactly how this check first passed a build where the arp was doing
// nothing at all.
float quietestWindow(const juce::AudioBuffer<float>& buffer, int channel, int from, int window)
{
    auto quietest = std::numeric_limits<float>::max();
    for (int start = from; start + window <= buffer.getNumSamples(); start += window / 2)
    {
        const auto* data = buffer.getReadPointer(channel);
        auto sum = 0.0;
        for (int i = start; i < start + window; ++i) sum += data[i] * data[i];
        quietest = std::min(quietest, static_cast<float>(std::sqrt(sum / window)));
    }
    return quietest == std::numeric_limits<float>::max() ? 0.0f : quietest;
}

// Everything one run of the arp emitted, in order. A note-on is the note
// number; a note-off is recorded separately, because what is being checked is
// nearly always the sequence of pitches rather than when each stopped.
struct Run
{
    std::vector<int> started;
    std::vector<float> velocities;
    std::vector<int> stopped;
    // The sample each note started on, for checking a gate or a rate.
    std::vector<int> startedAt;
};

// Hold a chord and run the arp for a while. The sample rate is deliberately
// small and round: a step of 0.1s at 1000 Hz is 100 samples, so a step boundary
// is a number that can be written down rather than one that has to be measured.
Run play(ArpSettings settings, const std::vector<int>& chord, int samples,
         double sampleRate = 1000.0)
{
    Arp arp;
    arp.reset();
    for (const auto note : chord) arp.noteOn(note, 1.0f, settings);

    Run run;
    for (int i = 0; i < samples; ++i)
        arp.advance(settings, sampleRate,
                    [&run, i] (int note, float velocity)
                    {
                        run.started.push_back(note);
                        run.velocities.push_back(velocity);
                        run.startedAt.push_back(i);
                    },
                    [&run] (int note) { run.stopped.push_back(note); });
    return run;
}

// A plain settings block: on, a step every tenth of a second, nothing else
// doing anything. Every check below changes one thing about this.
ArpSettings plainSettings()
{
    ArpSettings settings;
    settings.enabled = true;
    settings.stepSeconds = 0.1f;
    settings.gate = 0.5f;
    return settings;
}

// --- The shapes ---------------------------------------------------------------
//
// Checked as orders over indices rather than as notes, because that is what the
// function actually produces and because the same orders are used a second time
// for the transposition stages. The expectations are read off the manual's own
// figures for a four-note chord.
void shapeSuite()
{
    requireOrder(ArpShape::up, 4, {0, 1, 2, 3}, "UP climbs the chord");
    requireOrder(ArpShape::down, 4, {3, 2, 1, 0}, "DOWN descends the chord");
    // The turning points sound once, which is what makes Up/Down an odd-length
    // pattern and Up+Down an even one. Getting this wrong is the classic
    // arpeggiator bug: the top note struck twice on every pass.
    requireOrder(ArpShape::upDown, 4, {0, 1, 2, 3, 2, 1}, "UP/DOWN turns without repeating the ends");
    requireOrder(ArpShape::downUp, 4, {3, 2, 1, 0, 1, 2}, "DOWN/UP turns without repeating the ends");
    requireOrder(ArpShape::upAndDown, 4, {0, 1, 2, 3, 3, 2, 1, 0}, "UP+DOWN holds both ends");
    requireOrder(ArpShape::downAndUp, 4, {3, 2, 1, 0, 0, 1, 2, 3}, "DOWN+UP holds both ends");
    // The thumb is the lowest note and sounds between every other one; the
    // pinky is the highest and does the same from the other end.
    requireOrder(ArpShape::thumbUp, 4, {0, 1, 0, 2, 0, 3}, "THUMB UP alternates with the lowest note");
    requireOrder(ArpShape::pinkyUp, 4, {0, 3, 1, 3, 2, 3}, "PINKY UP alternates with the highest note");
    requireOrder(ArpShape::converge, 4, {0, 3, 1, 2}, "CONVERGE closes in from both ends");
    requireOrder(ArpShape::diverge, 4, {2, 1, 3, 0}, "DIVERGE is CONVERGE read backwards");
    requireOrder(ArpShape::conDiverge, 4, {0, 3, 1, 2, 1, 3, 0},
                 "CON+DIVERGE is the two joined without striking the meeting note twice");
    // An odd chord has a middle note, which converge must sound exactly once
    // rather than pairing with itself.
    requireOrder(ArpShape::converge, 5, {0, 4, 1, 3, 2}, "CONVERGE sounds an odd chord's middle note once");

    // One key makes every shape the same shape. Several of the rules divide by
    // count - 1, so this is where they would go wrong.
    for (int shape = 0; shape < rhino::forge::arpShapeCount; ++shape)
        require(orderOf(static_cast<ArpShape>(shape), 1) == std::vector<int> {0},
                "one key is one step, whatever the shape");
    for (int shape = 0; shape < rhino::forge::arpShapeCount; ++shape)
        require(orderOf(static_cast<ArpShape>(shape), 0).empty(),
                "no keys is no steps, whatever the shape");

    // Nothing may run off the end of the fixed arrays the audio thread uses.
    for (int shape = 0; shape < rhino::forge::arpShapeCount; ++shape)
        require(static_cast<int>(orderOf(static_cast<ArpShape>(shape), rhino::forge::arpMaxHeld).size())
                    <= rhino::forge::arpMaxSteps,
                "no shape produces more steps than the array holds");

    // Every shape but CHORD visits only the keys that are held.
    for (int shape = 0; shape < rhino::forge::arpShapeCount; ++shape)
        for (const auto index : orderOf(static_cast<ArpShape>(shape), 4))
            require(index >= 0 && index < 4, "a shape never indexes past the chord");
}

// --- The clock and the notes ---------------------------------------------------
void sequenceSuite()
{
    const std::vector<int> chord {60, 64, 67};

    // The keys come out in the shape's order, at the shape's rate, whatever
    // order they were pressed in: the held set is sorted by pitch, so a chord
    // played top-down still arpeggiates upwards.
    {
        auto settings = plainSettings();
        const auto run = play(settings, {67, 60, 64}, 1000);
        require(run.started.size() == 10, "ten steps in a second at a tenth of a second each");
        require(std::vector<int>(run.started.begin(), run.started.begin() + 6)
                    == std::vector<int> {60, 64, 67, 60, 64, 67},
                "UP plays the chord low to high however it was pressed");
        // The first step lands on the first sample rather than a step later:
        // an arpeggio that waits a step before its first note is late on every
        // launch.
        require(run.startedAt.front() == 0, "the first step sounds immediately");
        require(run.startedAt[1] == 100, "a step of a tenth of a second at 1 kHz is 100 samples");
    }

    // GATE is the note's length against the step. At half, a note stops before
    // the next starts; above one they overlap, and the arp is what has to keep
    // track of several notes in flight.
    {
        auto settings = plainSettings();
        settings.gate = 0.5f;
        const auto run = play(settings, chord, 300);
        require(run.stopped.size() >= 2, "a short gate stops its notes");
        require(run.started.size() == run.stopped.size() + 1
                || run.started.size() == run.stopped.size(),
                "every note started is stopped, bar the one still sounding");
    }
    {
        auto settings = plainSettings();
        settings.gate = 1.9f;
        const auto run = play(settings, chord, 250);
        // Three steps have fired by 250 samples and the first is still
        // sounding, so fewer notes have stopped than started.
        require(run.started.size() > run.stopped.size(), "a long gate leaves notes overlapping");
    }

    // A key added to the chord joins the pattern; one taken away leaves it, and
    // the pattern goes on with what is left rather than stopping.
    {
        auto settings = plainSettings();
        Arp arp;
        arp.reset();
        arp.noteOn(60, 1.0f, settings);
        std::vector<int> started;
        const auto start = [&started] (int note, float) { started.push_back(note); };
        const auto stop = [] (int) {};
        for (int i = 0; i < 250; ++i) arp.advance(settings, 1000.0, start, stop);
        require(started.size() == 3 && started[0] == 60 && started[2] == 60,
                "one key repeats on every step");
        arp.noteOn(67, 1.0f, settings);
        for (int i = 0; i < 200; ++i) arp.advance(settings, 1000.0, start, stop);
        require(std::find(started.begin() + 3, started.end(), 67) != started.end(),
                "a key added to the chord joins the pattern");
        arp.noteOff(67, settings);
        const auto before = started.size();
        for (int i = 0; i < 300; ++i) arp.advance(settings, 1000.0, start, stop);
        for (auto i = before; i < started.size(); ++i)
            require(started[i] == 60, "a key lifted leaves the pattern");
    }

    // Everything let go of is silence, not a stuck pattern.
    {
        auto settings = plainSettings();
        Arp arp;
        arp.reset();
        arp.noteOn(60, 1.0f, settings);
        for (const auto note : {60}) arp.noteOff(note, settings);
        auto count = 0;
        for (int i = 0; i < 500; ++i)
            arp.advance(settings, 1000.0, [&count] (int, float) { ++count; }, [] (int) {});
        require(count == 0, "nothing held is nothing played");
    }
}

// --- Transposition, playback and velocity ---------------------------------------
void patternSuite()
{
    const std::vector<int> chord {60, 64, 67};

    // SHIFT moves each repetition and RANGE says how many there are. With a
    // three-note chord, a shift of 12 and a range of 2, the pattern is the
    // chord and then the chord an octave up, over and over.
    {
        auto settings = plainSettings();
        settings.shift = 12;
        settings.range = 2;
        const auto run = play(settings, chord, 700);
        require(std::vector<int>(run.started.begin(), run.started.begin() + 6)
                    == std::vector<int> {60, 64, 67, 72, 76, 79},
                "RANGE 2 repeats the pattern a SHIFT higher");
        require(run.started[6] == 60, "and then starts over");
    }

    // OFFSET rotates where the pattern begins without changing what it plays.
    {
        auto settings = plainSettings();
        settings.offset = 1;
        const auto run = play(settings, chord, 350);
        require(std::vector<int>(run.started.begin(), run.started.begin() + 3)
                    == std::vector<int> {64, 67, 60},
                "OFFSET starts the pattern further along");
    }

    // REPEATS counts complete passes and then stops. Zero is forever, which is
    // what the knob opens on.
    {
        auto settings = plainSettings();
        settings.repeats = 2;
        const auto run = play(settings, chord, 2000);
        require(run.started.size() == 6, "REPEATS 2 plays the pattern twice and stops");

        settings.repeats = 0;
        const auto forever = play(settings, chord, 2000);
        require(forever.started.size() == 20, "zero repeats runs on");
    }

    // CHANCE at nothing plays nothing; at everything it plays everything. The
    // values between are a roll of the dice and are not checked for a count.
    {
        auto settings = plainSettings();
        settings.chance = 0.0f;
        require(play(settings, chord, 1000).started.empty(), "CHANCE at zero sounds nothing");
        settings.chance = 1.0f;
        require(play(settings, chord, 1000).started.size() == 10, "CHANCE at one sounds everything");
    }

    // CHORD sounds every held key on every step, which is the one shape whose
    // step is not a single note.
    {
        auto settings = plainSettings();
        settings.shape = ArpShape::chord;
        const auto run = play(settings, chord, 250);
        require(run.started.size() == 9, "CHORD sounds all three keys on each of three steps");
        require(std::vector<int>(run.started.begin(), run.started.begin() + 3)
                    == std::vector<int> {60, 64, 67},
                "and sounds them all together");
    }

    // VELOCITY off is every note at the velocity its key was struck at. On, the
    // level walks towards the target instead.
    {
        auto settings = plainSettings();
        const auto flat = play(settings, {60}, 500);
        for (const auto level : flat.velocities)
            requireClose(level, 1.0f, 0.0001f, "with VELOCITY off every note keeps the played velocity");

        settings.velocityOn = true;
        settings.velocityDecay = 0.5f;
        settings.velocityTarget = 0.0f;
        const auto ramp = play(settings, {60}, 500);
        require(ramp.velocities.size() >= 4, "enough steps to see a ramp");
        for (size_t i = 1; i < ramp.velocities.size(); ++i)
            require(ramp.velocities[i] < ramp.velocities[i - 1],
                    "with VELOCITY on the level falls towards the target");
        require(ramp.velocities.back() < 0.2f, "and gets most of the way there");
    }

    // LATCH keeps the chord after the keys are let go, and the next key pressed
    // with nothing else down replaces it rather than joining it.
    {
        auto settings = plainSettings();
        settings.latch = true;
        Arp arp;
        arp.reset();
        arp.noteOn(60, 1.0f, settings);
        arp.noteOff(60, settings);
        std::vector<int> started;
        const auto start = [&started] (int note, float) { started.push_back(note); };
        const auto stop = [] (int) {};
        for (int i = 0; i < 300; ++i) arp.advance(settings, 1000.0, start, stop);
        require(started.size() == 3, "LATCH keeps playing after the key is released");

        arp.noteOn(67, 1.0f, settings);
        started.clear();
        for (int i = 0; i < 300; ++i) arp.advance(settings, 1000.0, start, stop);
        for (const auto note : started)
            require(note == 67, "a key pressed with nothing held replaces the latched chord");

        // And the latch let go of drops what no finger is on — which means the
        // key has to be off the keyboard first. A latch released while the
        // chord is still under your hands is meant to change nothing.
        arp.releaseLatch();
        started.clear();
        for (int i = 0; i < 300; ++i) arp.advance(settings, 1000.0, start, stop);
        require(!started.empty(), "releasing the latch on a key still held changes nothing");

        arp.noteOff(67, settings);
        arp.releaseLatch();
        started.clear();
        for (int i = 0; i < 300; ++i) arp.advance(settings, 1000.0, start, stop);
        require(started.empty(), "releasing the latch stops the chord");
    }

    // LAUNCH QUANT is a delay on the first step and nothing else: the steps
    // after it are spaced by the rate, so a quantised arp is late once rather
    // than slow. The Processor works the delay out against the host's bar and
    // hands it here in samples, so this is where that delay is checked.
    {
        auto settings = plainSettings();
        Arp arp;
        arp.reset();
        arp.noteOn(60, 1.0f, settings);
        arp.delayFirstStep(250.0);
        std::vector<int> at;
        for (int i = 0; i < 600; ++i)
            arp.advance(settings, 1000.0,
                        [&at, i] (int, float) { at.push_back(i); }, [] (int) {});
        require(!at.empty() && at.front() == 249, "the first step waits out the delay");
        // Within a sample of the rate, not exactly it. `stepSeconds` is a
        // float, so a tenth of a second at 1 kHz is 100.0000015 samples rather
        // than 100, and the timer carries that remainder rather than discarding
        // it — which is the whole point of carrying it, because discarding it
        // is what walks an arp off the beat over a bar.
        require(at.size() >= 3, "several steps after the delay");
        for (size_t i = 2; i < at.size(); ++i)
            require(std::abs(at[i] - at[i - 1] - 100) <= 1,
                    "the steps after the delay are spaced by the rate, not by the delay");
    }

    // LAUNCH: the pattern restarted from its beginning, wherever it had got to.
    {
        auto settings = plainSettings();
        Arp arp;
        arp.reset();
        for (const auto note : {60, 64, 67}) arp.noteOn(note, 1.0f, settings);
        std::vector<int> started;
        const auto start = [&started] (int note, float) { started.push_back(note); };
        const auto stop = [] (int) {};
        for (int i = 0; i < 150; ++i) arp.advance(settings, 1000.0, start, stop);
        require(started.size() == 2, "two steps in, the pattern is part-way through");
        arp.restart(settings);
        started.clear();
        for (int i = 0; i < 150; ++i) arp.advance(settings, 1000.0, start, stop);
        require(!started.empty() && started.front() == 60,
                "LAUNCH puts the pattern back at its first note");
    }

    // A chord larger than the arp holds must not reach past its arrays.
    {
        auto settings = plainSettings();
        std::vector<int> big;
        for (int i = 0; i < rhino::forge::arpMaxHeld + 8; ++i) big.push_back(40 + i);
        const auto run = play(settings, big, 2000);
        require(!run.started.empty(), "an oversized chord still plays");
        for (const auto note : run.started)
            require(note >= 40 && note < 40 + static_cast<int>(big.size()),
                    "an oversized chord plays only notes that were pressed");
    }
}

// --- Through the Processor -----------------------------------------------------
//
// The one thing the lambdas above cannot check: that the notes the arp produces
// actually reach the voices, and that switching it off hands the keyboard back.
void processorSuite()
{
    constexpr int samples = 24000;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto renderChord = [&buffer] (bool arpOn, float gate)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "arpEnable", arpOn ? 1.0f : 0.0f);
        setValue(processor, "arpRateUnit", 0.0f); // Hertz, so the test needs no tempo
        setValue(processor, "arpRate", 20.0f);
        setValue(processor, "arpGate", gate);
        setValue(processor, "env1Attack", 0.001f);
        setValue(processor, "env1Release", 0.005f);
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (const auto note : {60, 64, 67})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        processor.processBlock(buffer, midi);
    };

    // A held chord with the arp off is a held chord: the level is steady.
    // With the arp on and a short gate it is a sequence, so the level goes up
    // and down as notes start and stop. The swing between the two is what
    // proves the arp is in the signal path at all.
    // A step of 1/20 s at 48 kHz is 2400 samples, so 256 is comfortably inside
    // both a note and the gap after it.
    constexpr int window = 256;
    renderChord(false, 0.5f);
    const auto steady = quietestWindow(buffer, 0, 4096, window);
    require(allSamplesFinite(buffer), "a held chord renders finite samples");
    require(steady > 0.01f, "a held chord never goes quiet");

    renderChord(true, 0.25f);
    const auto arpeggiated = quietestWindow(buffer, 0, 4096, window);
    require(allSamplesFinite(buffer), "an arpeggio renders finite samples");
    require(arpeggiated < steady * 0.1f,
            "the arp turns a held chord into notes with silence between them");

    // THRU passes the keys to the voices as well as to the pattern. Measured
    // with CHANCE at zero, so the arp plays nothing at all and what is left is
    // exactly what THRU let through — the only way to read this control on its
    // own, because a note the arp is also playing is indistinguishable from one
    // it is not.
    {
        const auto renderThru = [&buffer] (bool thru)
        {
            rhino::forge::Processor processor;
            soloSineOnA(processor);
            setValue(processor, "arpEnable", 1.0f);
            setValue(processor, "arpRateUnit", 0.0f);
            setValue(processor, "arpRate", 20.0f);
            setValue(processor, "arpChance", 0.0f);
            setValue(processor, "arpThru", thru ? 1.0f : 0.0f);
            processor.prepareToPlay(48000.0, 24000);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            processor.processBlock(buffer, midi);
            return rms(buffer, 0, 4096);
        };
        require(renderThru(false) < 0.0001f, "without THRU the arp consumes the keys");
        require(renderThru(true) > 0.01f, "with THRU the keys reach the voices as well");
    }

    // The arp switched off mid-phrase lets go of the notes its own gate clock
    // was holding. Nothing else would ever come back to stop them, so this is
    // the check that catches a stuck note.
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "arpEnable", 1.0f);
        setValue(processor, "arpRateUnit", 0.0f);
        setValue(processor, "arpRate", 4.0f);
        setValue(processor, "arpGate", 1.9f);
        setValue(processor, "env1Release", 0.01f);
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (const auto note : {60, 64, 67})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        processor.processBlock(buffer, midi);

        setValue(processor, "arpEnable", 0.0f);
        juce::MidiBuffer none;
        processor.processBlock(buffer, none);
        processor.processBlock(buffer, none);
        require(rms(buffer, 0, 0) < 0.001f, "switching the arp off leaves no note sounding");
    }

    // A tempo-synced step is a division of the host's beat. With no host tempo
    // the standalone stands in 120 BPM, so 1/16 is 125 ms and the panel and the
    // engine must both say so.
    {
        rhino::forge::Processor processor;
        setValue(processor, "arpRateUnit", 1.0f);
        setValue(processor, "arpDivision", static_cast<float>(rhino::forge::arpDefaultDivision));
        setValue(processor, "arpTriplet", 0.0f);
        setValue(processor, "arpDotted", 0.0f);
        processor.prepareToPlay(48000.0, 512);
        requireClose(processor.arpStepSeconds(), 0.125f, 0.0005f, "1/16 at 120 BPM is 125 ms");
        setValue(processor, "arpTriplet", 1.0f);
        requireClose(processor.arpStepSeconds(), 0.125f * 2.0f / 3.0f, 0.0005f,
                     "TRIP makes a step two thirds as long");
        setValue(processor, "arpTriplet", 0.0f);
        setValue(processor, "arpDotted", 1.0f);
        requireClose(processor.arpStepSeconds(), 0.1875f, 0.0005f,
                     "DOT makes a step half again as long");
    }
}
}

void arpTests()
{
    shapeSuite();
    sequenceSuite();
    patternSuite();
    processorSuite();
}
}
