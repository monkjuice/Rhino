// The six LFOs: their shapes, their three modes, and a rate set in beats
// following the host rather than the knob.
#include "ForgeTestSupport.h"

namespace rhino::forge::tests
{
namespace
{
// A host that reports one fixed tempo and nothing else, so a synced LFO can be
// checked against a BPM the test controls.
class FixedTempo final : public juce::AudioPlayHead
{
public:
    explicit FixedTempo(double beatsPerMinute) : bpm(beatsPerMinute) {}

    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm);
        return info;
    }

    double bpm = 120.0;
};

void lfoSuite()
{
    using rhino::forge::LfoShape;
    constexpr int samples = 8192;

    // Every shape has to stay inside the bounds a source promises, and has to
    // come back to where it started after exactly one cycle. A source that ran
    // past one would push a destination further than its depth allows.
    for (int shape = 0; shape < rhino::forge::lfoShapeCount; ++shape)
    {
        const auto which = static_cast<LfoShape>(shape);
        auto extreme = 0.0f;
        for (int i = 0; i <= 512; ++i)
        {
            const auto phase = static_cast<float>(i) / 512.0f;
            const auto at = rhino::forge::lfoWave(which, phase, 1.0f);
            require(std::isfinite(at), "an LFO shape is finite everywhere");
            extreme = std::max(extreme, std::abs(at));
        }
        require(extreme <= 1.0f, "an LFO shape stays inside plus or minus one");
        require(extreme > 0.9f, "an LFO shape uses the range it is given");
    }

    // Sine and triangle join up at the cycle boundary. A saw and a square jump
    // a full swing there instead, and that jump is the shape rather than a
    // fault — checked so neither can be quietly smoothed away.
    for (const auto continuous : {LfoShape::sine, LfoShape::triangle})
        requireClose(rhino::forge::lfoWave(continuous, 0.0f, 0.0f),
                     rhino::forge::lfoWave(continuous, 1.0f, 0.0f), 0.0001f,
                     "a continuous LFO shape joins up across the cycle");
    for (const auto stepped : {LfoShape::saw, LfoShape::square})
        require(std::abs(rhino::forge::lfoWave(stepped, 0.0f, 0.0f)
                         - rhino::forge::lfoWave(stepped, 0.999f, 0.0f)) > 1.5f,
                "a saw and a square jump a full swing at the cycle boundary");

    // Sample and hold is its held step and nothing else: it does not move
    // within a cycle, which is what makes it a step rather than a ramp.
    for (const auto phase : {0.0f, 0.2f, 0.75f, 0.99f})
        requireClose(rhino::forge::lfoWave(LfoShape::sampleHold, phase, -0.4f), -0.4f, 0.0001f,
                     "sample and hold holds its step for the whole cycle");

    // Shape is a real choice, not a relabelled sine: the four continuous shapes
    // have to differ from each other somewhere.
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
        {
            auto differs = false;
            for (int i = 0; i < 64; ++i)
            {
                const auto phase = static_cast<float>(i) / 64.0f;
                if (std::abs(rhino::forge::lfoWave(static_cast<LfoShape>(a), phase, 0.0f)
                             - rhino::forge::lfoWave(static_cast<LfoShape>(b), phase, 0.0f)) > 0.01f)
                    differs = true;
            }
            require(differs, "no two LFO shapes are the same curve");
        }

    // --- The three modes ------------------------------------------------------
    //
    // Driven through the Core rather than the Processor: the question is what
    // the phase does across a note, which is exactly what the Core owns and
    // what a block of audio would only let us infer.
    const auto runCore = [] (rhino::forge::Core& core, const rhino::forge::Patch& patch, int count)
    {
        float left = 0.0f, right = 0.0f;
        for (int i = 0; i < count; ++i) core.renderSample(patch, left, right);
    };
    // One cycle a second at 48 kHz, so a count of samples is a share of a cycle
    // and every check below reads as a fraction of one.
    constexpr int cycle = 48000;
    const auto patchFor = [] (rhino::forge::LfoMode mode)
    {
        rhino::forge::Patch patch;
        auto& first = patch.lfos.front();
        first.rate = 1.0f;
        first.shape = static_cast<float>(rhino::forge::LfoShape::saw);
        first.mode = static_cast<float>(mode);
        // One voice, so the phase the Core publishes is that voice's own. An
        // LFO that answers the keyboard lives inside the voice now, and what
        // reaches the panel is the loudest voice's copy of it — which is the
        // point of the per-voice check further down, but here it would only get
        // in the way of asking what one note does.
        patch.polyphony = 1.0f;
        return patch;
    };
    // The phase is published while rendering, so a note has to be given a
    // sample before what it did to its LFO can be read back.
    const auto phaseAfterNote = [&] (rhino::forge::Core& core, const rhino::forge::Patch& patch,
                                     int note)
    {
        core.noteOn(note, 1.0f, patch);
        runCore(core, patch, 1);
        return core.lfoPosition(0);
    };

    for (const auto keyed : {rhino::forge::LfoMode::trigger, rhino::forge::LfoMode::envelope})
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(keyed);
        // Nothing is sounding, so the shape waits at the start rather than
        // running on where nobody can hear it. The indicator is then sitting
        // exactly where the next key press will start it from.
        runCore(core, patch, cycle / 4);
        requireClose(core.lfoPosition(0), 0.0f, 0.0001f,
                     "a key-synced LFO waits at the start until a note arrives");
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "a key-synced LFO runs once a note is playing");
        requireClose(phaseAfterNote(core, patch, 60), 0.0f, 0.0001f,
                     "a new note restarts a key-synced LFO");
    }

    // And it comes back to the start when the last voice has gone, having run
    // on through the release tail rather than being cut dead at note-off.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.envs[rhino::forge::ampEnv].attack = 0.001f;
        patch.envs[rhino::forge::ampEnv].decay = 0.001f;
        patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
        patch.envs[rhino::forge::ampEnv].release = 0.005f;
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "TRIG runs while the note is held");

        core.noteOff(57);
        const auto atRelease = core.lfoPosition(0);
        runCore(core, patch, 100);
        require(core.lfoPosition(0) > atRelease, "TRIG keeps running through the release tail");
        runCore(core, patch, cycle / 2);
        requireClose(core.lfoPosition(0), 0.0f, 0.0001f,
                     "TRIG returns to the start once nothing is sounding");
    }

    // OFF is the mode that keeps its place, which is the whole reason to have
    // it: a free-running LFO does not jump every time a key goes down.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::free);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        require(before > 0.2f, "a free-running LFO runs before a note arrives");
        requireClose(phaseAfterNote(core, patch, 57), before, 0.001f,
                     "a note does not restart a free-running LFO");
    }

    // TRIG loops for as long as the note is held: past the end of the cycle it
    // comes round again rather than stopping.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::trigger);
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle + cycle / 4);
        const auto wrapped = core.lfoPosition(0);
        require(wrapped > 0.1f && wrapped < 0.5f, "TRIG comes round again at the end of the cycle");
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > wrapped, "TRIG keeps running after it has wrapped");
    }

    // ENV is the same restart followed by a full stop on the last point of the
    // shape. A saw ends at the top, so the held value is the one thing a
    // one-shot envelope is for: it stays where the shape left it.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        const auto patch = patchFor(rhino::forge::LfoMode::envelope);
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle + cycle / 4);
        requireClose(core.lfoPosition(0), 1.0f, 0.0001f, "ENV stops at the end of its shape");
        requireClose(core.lfoOutput(0), 1.0f, 0.001f, "ENV holds the value the shape ended on");
        runCore(core, patch, 4 * cycle);
        requireClose(core.lfoPosition(0), 1.0f, 0.0001f, "ENV stays stopped however long it is left");

        // And the next note starts it over, or it would be a one-shot that only
        // ever fired once.
        requireClose(phaseAfterNote(core, patch, 60), 0.0f, 0.0001f,
                     "a new note restarts a stopped ENV");
        runCore(core, patch, cycle / 4);
        require(core.lfoPosition(0) > 0.2f, "a restarted ENV runs again");
    }

    // A legato note in mono did not lift a key, so it does not restart the
    // shape — the same rule the amp envelope already follows.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.mono = 1.0f;
        patch.legato = 1.0f;
        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        requireClose(phaseAfterNote(core, patch, 60), before, 0.001f,
                     "a legato note does not restart the LFO");

        // Without legato it is a new note again, and it does.
        patch.legato = 0.0f;
        requireClose(phaseAfterNote(core, patch, 62), 0.0f, 0.0001f,
                     "a mono note without legato restarts the LFO");
    }

    // --- Six of them, and each one per voice -----------------------------------
    //
    // An LFO that answers the keyboard lives inside the voice, so a new note
    // restarts its own copy and leaves a note already sounding alone. One LFO
    // shared by every voice meant a second key jerked whatever the first was
    // driving, part-way through a note.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        auto patch = patchFor(rhino::forge::LfoMode::trigger);
        patch.polyphony = 8.0f;
        core.noteOn(45, 1.0f, patch);
        runCore(core, patch, cycle / 4);
        const auto before = core.lfoPosition(0);
        require(before > 0.2f, "the first note's LFO is running");

        core.noteOn(57, 1.0f, patch);
        runCore(core, patch, 64);
        // What reaches the panel is the loudest voice's copy, and that is still
        // the note that has been sounding — so its own cycle carried straight on
        // across the new one.
        const auto after = core.lfoPosition(0);
        require(after > before, "a second note leaves the first note's LFO running");
        requireClose(after, before, 0.01f, "a second note does not jump the first note's LFO");
    }

    // The six are independent: each runs at its own rate rather than six views
    // of one cycle.
    {
        rhino::forge::Core core;
        core.initialise(48000.0);
        rhino::forge::Patch patch;
        for (int i = 0; i < rhino::forge::lfoCount; ++i)
        {
            patch.lfos[static_cast<size_t>(i)].rate = 1.0f + static_cast<float>(i);
            patch.lfos[static_cast<size_t>(i)].mode = static_cast<float>(rhino::forge::LfoMode::free);
        }
        runCore(core, patch, cycle / 8);
        for (int i = 0; i < rhino::forge::lfoCount; ++i)
            for (int j = i + 1; j < rhino::forge::lfoCount; ++j)
                require(std::abs(core.lfoPosition(i) - core.lfoPosition(j)) > 0.01f,
                        "no two LFOs are the same cycle");
    }

    // And each is a source in its own right, reachable from the matrix. Square
    // and free-running, so the source holds a steady +1 across the render
    // rather than sweeping through it.
    for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
    {
        const auto level = [lfo] (float depth)
        {
            rhino::forge::Processor processor;
            soloSineOnA(processor);
            setValue(processor, "subEnable", 1.0f);
            setValue(processor, "subLevel", 0.0f);
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Shape").toRawUTF8(),
                     static_cast<float>(rhino::forge::LfoShape::square));
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Mode").toRawUTF8(),
                     static_cast<float>(rhino::forge::LfoMode::free));
            setValue(processor, rhino::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(processor, rhino::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 0.05f);
            setSlot(processor, 1, static_cast<float>(srcLfo1 + lfo), destSub, depth);
            juce::AudioBuffer<float> rendered(2, samples);
            rendered.clear();
            renderNote(processor, rendered);
            return rms(rendered, 0, 1024);
        };
        require(level(1.0f) > level(0.0f) * 1.2f,
                "every LFO reaches the matrix as a source of its own");
    }

    // Every mode has a name of its own, or the stepper would show two the same.
    for (int a = 0; a < rhino::forge::lfoModeCount; ++a)
    {
        require(juce::String(rhino::forge::lfoModeName(a)).isNotEmpty(), "every LFO mode is named");
        for (int b = a + 1; b < rhino::forge::lfoModeCount; ++b)
            require(juce::String(rhino::forge::lfoModeName(a)) != rhino::forge::lfoModeName(b),
                    "no two LFO modes share a name");
    }

    // Each LFO's rate is resolved from its own parameters, not LFO 1's.
    {
        rhino::forge::Processor six;
        for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
        {
            setValue(six, rhino::forge::lfoParameterId(lfo, "RateUnit").toRawUTF8(), 0.0f);
            setValue(six, rhino::forge::lfoParameterId(lfo, "Rate").toRawUTF8(), 1.0f + static_cast<float>(lfo));
        }
        for (int lfo = 0; lfo < rhino::forge::lfoCount; ++lfo)
            requireClose(six.lfoRateHz(lfo), 1.0f + static_cast<float>(lfo), 0.001f,
                         "each LFO reports its own rate");
    }

    // Free-running, the rate is the knob.
    rhino::forge::Processor free;
    setValue(free, "lfo1RateUnit", 0.0f);
    setValue(free, "lfo1Rate", 3.0f);
    requireClose(free.lfoRateHz(0), 3.0f, 0.001f, "an unsynced LFO runs at its rate knob");

    // Synced, the rate is a division of the host's tempo and the knob stops
    // mattering. 1/4 at 120 BPM is two beats a second, so two cycles a second.
    rhino::forge::Processor synced;
    setValue(synced, "lfo1RateUnit", 1.0f);
    setValue(synced, "lfo1Rate", 3.0f);
    setValue(synced, "lfo1Division", 2.0f);
    FixedTempo tempo(120.0);
    synced.setPlayHead(&tempo);
    juce::AudioBuffer<float> buffer(2, samples);
    juce::MidiBuffer midi;
    synced.prepareToPlay(48000.0, samples);
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(0), 2.0f, 0.001f, "a synced LFO divides the host tempo");

    // And it follows the tempo rather than latching the first one it saw.
    tempo.bpm = 60.0;
    synced.processBlock(buffer, midi);
    requireClose(synced.lfoRateHz(0), 1.0f, 0.001f, "a synced LFO tracks a tempo change");

    // A longer division is a slower cycle, in proportion.
    setValue(synced, "lfo1Division", 0.0f);   // 1/1, a bar of four beats
    requireClose(synced.lfoRateHz(0), 0.25f, 0.001f, "a whole-bar division is four beats long");

    // The phase the display draws has to be the one the voice is reading, and
    // it has to move.
    // In OFF, because that is the mode that runs with nothing playing — which
    // is the case this check is about: the phase reaching the panel is the one
    // the voice is reading, and it moves.
    rhino::forge::Processor running;
    setValue(running, "lfo1RateUnit", 0.0f);
    setValue(running, "lfo1Rate", 1.0f);
    setValue(running, "lfo1Mode", static_cast<float>(rhino::forge::LfoMode::free));
    running.prepareToPlay(48000.0, samples);
    juce::MidiBuffer none;
    buffer.clear();
    running.processBlock(buffer, none);
    const auto first = running.lfoPhase(0);
    running.processBlock(buffer, none);
    const auto second = running.lfoPhase(0);
    require(first >= 0.0f && first < 1.0f, "the published LFO phase stays inside one cycle");
    require(second != first, "the published LFO phase advances with the blocks");

    // The same panel reading, in TRIG with nothing playing: parked at the start
    // rather than sweeping a display for a shape that is not running.
    rhino::forge::Processor idle;
    setValue(idle, "lfo1RateUnit", 0.0f);
    setValue(idle, "lfo1Rate", 1.0f);
    setValue(idle, "lfo1Mode", static_cast<float>(rhino::forge::LfoMode::trigger));
    idle.prepareToPlay(48000.0, samples);
    buffer.clear();
    idle.processBlock(buffer, none);
    idle.processBlock(buffer, none);
    requireClose(idle.lfoPhase(0), 0.0f, 0.0001f,
                 "the panel shows a key-synced LFO parked at the start with nothing playing");

    synced.setPlayHead(nullptr);
}
}

void lfoTests()
{
    lfoSuite();
}
}
