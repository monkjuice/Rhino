// The four envelopes. ENV 1 is the voice amplitude and says when a voice is
// finished; ENV 2-4 reach a control through a modulation slot or not at all.
#include "ForgeTestSupport.h"

namespace rhino::forge::tests
{
namespace
{
void envelopeSuite()
{
    // ENV 1 is hardwired to amplitude and is the one envelope Forge has, so its
    // timing is the timing of every note.
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    enum Stage { idle, attack, decay, sustain, release };

    rhino::forge::Processor processor;
    soloSineOnA(processor);
    setValue(processor, "env1Attack", 0.1f);
    setValue(processor, "env1Decay", 0.1f);
    setValue(processor, "env1Sustain", 0.5f);
    setValue(processor, "env1Release", 0.2f);
    processor.prepareToPlay(rate, block);

    juce::AudioBuffer<float> buffer(2, block);
    const auto advance = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            processor.processBlock(buffer, events);
            events.clear();
        }
    };

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);

    // A tenth of the way into a 100 ms attack, the envelope is a tenth up.
    advance(static_cast<int>(rate * 0.01), noteOn);
    require(processor.envelopeStage() == attack, "a new note starts in attack");
    requireClose(processor.envelopeLevel(), 0.1f, 0.03f, "the attack climbs linearly");

    // Past the attack, into decay, and on to the sustain level.
    // 110 ms in: the 100 ms attack has finished and 10 ms of a 100 ms decay has
    // run, so the level has fallen a tenth of the way from 1.0 towards 0.5.
    advance(static_cast<int>(rate * 0.1));
    require(processor.envelopeStage() == decay, "the envelope reaches decay after the attack time");
    requireClose(processor.envelopeLevel(), 0.95f, 0.02f, "decay falls from the peak towards sustain");

    advance(static_cast<int>(rate * 0.12));
    require(processor.envelopeStage() == sustain, "the envelope settles into sustain after the decay time");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain holds at the sustain level");

    advance(static_cast<int>(rate * 0.2));
    require(processor.envelopeStage() == sustain, "sustain holds for as long as the note is held");
    requireClose(processor.envelopeLevel(), 0.5f, 0.02f, "sustain does not drift");

    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    advance(static_cast<int>(rate * 0.1), noteOff);
    require(processor.envelopeStage() == release, "releasing the note enters release");
    requireClose(processor.envelopeLevel(), 0.25f, 0.03f, "release falls from the held level");

    advance(static_cast<int>(rate * 0.15));
    require(processor.envelopeStage() == idle, "the envelope reaches idle after the release time");
    requireClose(processor.envelopeLevel(), 0.0f, 0.001f, "an idle envelope is silent");

    // Released mid-attack, the fall starts from the level actually reached,
    // not from the sustain level the note never got to.
    rhino::forge::Processor early;
    soloSineOnA(early);
    setValue(early, "env1Attack", 1.0f);
    setValue(early, "env1Decay", 0.1f);
    setValue(early, "env1Sustain", 0.9f);
    setValue(early, "env1Release", 0.2f);
    early.prepareToPlay(rate, block);

    const auto advanceEarly = [&] (int samples, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < samples; rendered += block)
        {
            early.processBlock(buffer, events);
            events.clear();
        }
    };

    advanceEarly(static_cast<int>(rate * 0.2), noteOn);
    const auto reached = early.envelopeLevel();
    require(early.envelopeStage() == attack, "a long attack is still climbing after 200 ms");
    requireClose(reached, 0.2f, 0.03f, "a fifth of the way up a one-second attack");

    advanceEarly(static_cast<int>(rate * 0.1), noteOff);
    require(early.envelopeStage() == release, "a note released mid-attack enters release");
    require(early.envelopeLevel() < reached,
            "a note released mid-attack falls rather than continuing to climb");
    requireClose(early.envelopeLevel(), reached * 0.5f, 0.03f,
                 "the fall starts from the level actually reached, not from sustain");

    advanceEarly(static_cast<int>(rate * 0.15));
    require(early.envelopeStage() == idle,
            "a release from a partial attack still takes the full release time");
}

// ENV 2-4: the same shape as ENV 1, wired to nothing. What has to be true of an
// auxiliary envelope is that it is silent until something points at it, that it
// then reaches whatever that is, and that it keeps times of its own rather than
// following the amplitude's.
void auxEnvelopeSuite()
{
    constexpr double rate = 48000.0;
    constexpr int block = 64;
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    enum Stage { idle, attack, decay, sustain, release };

    // Nothing routed: an auxiliary envelope may not colour the sound at all,
    // whatever its knobs are set to. Bit-identical rather than nearly so — a
    // second envelope leaking into the signal path is exactly the hardwired
    // filter envelope this design set out to remove.
    juce::AudioBuffer<float> plain(2, samples), moved(2, samples);
    rhino::forge::Processor quiet;
    closedFilterOnA(quiet);
    renderNote(quiet, plain);
    for (int env = 1; env < rhino::forge::envCount; ++env)
    {
        setValue(quiet, rhino::forge::envParameterId(env, "Attack").toRawUTF8(), 2.0f);
        setValue(quiet, rhino::forge::envParameterId(env, "Decay").toRawUTF8(), 0.01f);
        setValue(quiet, rhino::forge::envParameterId(env, "Sustain").toRawUTF8(), 0.0f);
        setValue(quiet, rhino::forge::envParameterId(env, "Release").toRawUTF8(), 4.0f);
    }
    renderNote(quiet, moved);
    require(identical(plain, moved),
            "an envelope nothing points at changes nothing, however it is set");

    // Each one reaches the matrix as a source of its own, checked the way the
    // six LFOs were: point it at a closed filter and hear the filter open.
    const auto closed = rms(plain, 0, settled);
    for (int env = 0; env < rhino::forge::envCount; ++env)
    {
        rhino::forge::Processor swept;
        closedFilterOnA(swept);
        setSlot(swept, 1, static_cast<float>(srcEnv1 + env), destCutoff, 1.0f);
        renderNote(swept, moved);
        require(rms(moved, 0, settled) > closed * 1.2f,
                "every envelope opens a filter it is pointed at");
        if (!(rms(moved, 0, settled) > closed * 1.2f))
            std::cerr << "       envelope " << env + 1 << " reaches nothing\n";
    }

    // Four envelopes under one note, each running its own shape. ENV 1 is long
    // since settled while ENV 2 is still climbing and ENV 3 has already fallen
    // to nothing, which no single shared shape could do.
    rhino::forge::Processor apart;
    soloSineOnA(apart);
    setValue(apart, "env1Attack", 0.01f);
    setValue(apart, "env1Decay", 0.01f);
    setValue(apart, "env1Sustain", 0.75f);
    setValue(apart, "env2Attack", 1.0f);
    setValue(apart, "env3Attack", 0.001f);
    setValue(apart, "env3Decay", 0.02f);
    setValue(apart, "env3Sustain", 0.0f);
    apart.prepareToPlay(rate, block);

    juce::AudioBuffer<float> buffer(2, block);
    const auto advance = [&] (int count, const juce::MidiBuffer& midi = {})
    {
        auto events = midi;
        for (auto rendered = 0; rendered < count; rendered += block)
        {
            apart.processBlock(buffer, events);
            events.clear();
        }
    };

    juce::MidiBuffer noteOn;
    noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
    advance(static_cast<int>(rate * 0.2), noteOn);

    require(apart.envelopeStage(0) == sustain, "ENV 1 has settled 200 ms into the note");
    requireClose(apart.envelopeLevel(0), 0.75f, 0.02f, "ENV 1 holds its own sustain");
    require(apart.envelopeStage(1) == attack, "ENV 2 is still climbing an attack of its own");
    requireClose(apart.envelopeLevel(1), 0.2f, 0.03f,
                 "a fifth of the way up ENV 2's one-second attack");
    require(apart.envelopeStage(2) == sustain, "ENV 3 has reached its own sustain");
    requireClose(apart.envelopeLevel(2), 0.0f, 0.001f, "ENV 3 sustains at nothing");
    // ENV 4 is untouched, so it is still running the defaults every envelope
    // opens on: a 10 ms attack long past, and 190 ms of a 240 ms decay from the
    // peak towards a sustain of 0.75.
    require(apart.envelopeStage(3) == decay, "an envelope left alone runs the default shape");
    requireClose(apart.envelopeLevel(3), 0.80f, 0.02f,
                 "and is most of the way down that decay 200 ms in");

    // The key that started them releases them all. An auxiliary envelope that
    // only fell when the amplitude did would be a shape with no release of its
    // own, which is half a control.
    const auto reached = apart.envelopeLevel(1);
    juce::MidiBuffer noteOff;
    noteOff.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
    advance(static_cast<int>(rate * 0.05), noteOff);
    require(apart.envelopeStage(1) == release, "lifting the key releases ENV 2 as well as ENV 1");
    require(apart.envelopeLevel(1) < reached,
            "ENV 2 falls from the level it had reached rather than climbing on");

    // And once the voice has gone there is no reading at all, for the same
    // reason a knob's ring stops moving: a source reaches anything only through
    // a voice, so with no voice there is nothing to report.
    advance(static_cast<int>(rate * 1.0));
    for (int env = 0; env < rhino::forge::envCount; ++env)
    {
        require(apart.envelopeStage(env) == idle, "every envelope is idle once the voice has gone");
        requireClose(apart.envelopeLevel(env), 0.0f, 0.001f, "an idle envelope reads nothing");
    }

    // The ring on a knob and the curve on the display are one reading, for an
    // auxiliary envelope exactly as for ENV 1.
    rhino::forge::Processor watched;
    closedFilterOnA(watched);
    setSlot(watched, 1, static_cast<float>(srcEnv1 + 1), destCutoff, 1.0f);
    renderNote(watched, moved);
    require(watched.modulationOffset(destCutoff) == watched.envelopeLevel(1),
            "the offset drawn on a knob is the same reading ENV 2's own display draws");
}
}

void envelopeTests()
{
    envelopeSuite();
    auxEnvelopeSuite();
}
}
