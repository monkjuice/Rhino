// Polyphony, mono and glide, stealing a voice without a click, and letting a
// stolen voice ring out instead of truncating the filter.
#include "ForgeTestSupport.h"

namespace rhino::forge::tests
{
namespace
{
// The largest step from one sample to the next. A click is exactly that: a
// discontinuity the ear hears as a tick over the top of the note. Read on a
// signal made only of sines, so every legitimate step is bounded by the
// frequency and anything larger came from the engine cutting something off.
float largestStep(const juce::AudioBuffer<float>& buffer)
{
    auto worst = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int i = 1; i < buffer.getNumSamples(); ++i)
            worst = std::max(worst, std::abs(data[i] - data[i - 1]));
    }
    return worst;
}

void voicingSuite()
{
    // Mono collapses to a single voice, so polyphony stops meaning anything.
    // The panel greys the POLY knob out to say so; this checks the engine
    // agrees rather than the two drifting apart.
    constexpr int samples = 8192;
    constexpr int settled = 1024;
    juce::AudioBuffer<float> buffer(2, samples);

    const auto chordLevel = [&buffer] (bool mono, float polyphony)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "mono", mono ? 1.0f : 0.0f);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "glide", 0.0f);
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (const auto note : {52, 57, 61})
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        processor.processBlock(buffer, midi);
        return rms(buffer, 0, settled);
    };

    const auto polyThree = chordLevel(false, 8.0f);
    const auto monoThree = chordLevel(true, 8.0f);
    require(polyThree > 0.0f && monoThree > 0.0f, "both voicings make sound");
    require(monoThree < polyThree * 0.8f, "mono plays one note where poly plays three");

    // And polyphony genuinely does nothing while mono is on.
    requireClose(chordLevel(true, 1.0f), monoThree, monoThree * 0.001f,
                 "polyphony does not affect a mono patch");
    requireClose(chordLevel(true, 16.0f), monoThree, monoThree * 0.001f,
                 "raising polyphony does not affect a mono patch either");

    // While in poly it very much does.
    require(chordLevel(false, 1.0f) < polyThree * 0.8f, "polyphony limits a polyphonic patch");
}

// Taking a voice away from a note that is still sounding must not be audible as
// anything but the new note arriving. A run of notes longer than the polyphony
// is the ordinary way to reach that, and it used to leave a tick on every note
// past the fourth.
void voiceStealSuite()
{
    constexpr int samples = 32768;
    constexpr int spacing = 3000;
    const int notes[] = {45, 52, 57, 61, 64, 68, 71, 76, 78, 81};

    const auto worstStep = [&] (float polyphony)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        // Both oscillators, which is where the user hears it: two of them make
        // the step twice the size.
        setValue(processor, "oscBEnable", 1.0f);
        setValue(processor, "oscBPosition", 0.0f);
        setValue(processor, "oscBUnison", 1.0f);
        setValue(processor, "oscBDetune", 0.0f);
        setValue(processor, "oscBSemitone", 0.0f);
        setValue(processor, "oscBLevel", 1.0f);
        setValue(processor, "polyphony", polyphony);
        // Long tails, so every voice is still sounding when the next note wants
        // one and the run really does have to take them.
        setValue(processor, "env1Release", 4.0f);
        setValue(processor, "env1Sustain", 1.0f);
        setValue(processor, "env1Attack", 0.01f);

        juce::AudioBuffer<float> buffer(2, samples);
        buffer.clear();
        processor.prepareToPlay(48000.0, samples);
        juce::MidiBuffer midi;
        for (int i = 0; i < static_cast<int>(std::size(notes)); ++i)
            midi.addEvent(juce::MidiMessage::noteOn(1, notes[i], 1.0f), i * spacing);
        processor.processBlock(buffer, midi);
        return largestStep(buffer);
    };

    // Four voices for ten notes: six of them have to take a voice that is still
    // sounding. Sixteen voices for the same ten notes never steals at all, so
    // it is the same music with the steals taken out — which makes it the
    // reference for how large a step this material legitimately contains.
    const auto stealing = worstStep(4.0f);
    const auto roomy = worstStep(16.0f);
    require(roomy > 0.0f, "the reference run makes sound");
    require(stealing < roomy * 1.5f,
            "taking a voice from a sounding note is no louder a step than the notes themselves");
    if (stealing >= roomy * 1.5f)
        std::cerr << "       worst step " << stealing << " stealing, " << roomy << " with room" << std::endl;

    // And a voice is only ever taken when one genuinely has to be. A note that
    // has finished leaves a voice free, and the next note has to take that one
    // rather than whichever slot a rotation had reached — strict rotation would
    // silence a note still under the player's finger while a dead voice sat
    // beside it. The invariant that says so: while no more notes sound at once
    // than the patch has voices, raising the polyphony cannot change a sample.
    const auto sequence = [] (float polyphony, juce::AudioBuffer<float>& buffer)
    {
        rhino::forge::Processor processor;
        soloSineOnA(processor);
        setValue(processor, "polyphony", polyphony);
        setValue(processor, "env1Sustain", 1.0f);
        setValue(processor, "env1Release", 0.01f);
        buffer.clear();
        processor.prepareToPlay(48000.0, buffer.getNumSamples());
        juce::MidiBuffer midi;
        // One note held throughout, a second struck and let go, and a third
        // arriving long after the second has died away. Never more than two at
        // once, so two voices are enough and nothing need ever be taken.
        midi.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 1000);
        midi.addEvent(juce::MidiMessage::noteOff(1, 57), 2000);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 12000);
        processor.processBlock(buffer, midi);
    };

    juce::AudioBuffer<float> tight(2, samples), spare(2, samples);
    sequence(2.0f, tight);
    sequence(8.0f, spare);
    require(rms(tight, 0, samples / 2) > 0.0f, "the two-voice run makes sound");
    require(identical(tight, spare),
            "a free voice is taken before a sounding one, so more polyphony than the music needs changes nothing");
}

// A voice is let go of, not cut off. Everything a voice makes passes through
// its filter, and a filter holds energy: at a low cutoff it is still ringing
// after the envelope that fed it has reached zero. Dropping the voice at that
// point truncates the ring, and the truncation is a click at the end of a note.
void voiceTailSuite()
{
    rhino::forge::Processor processor;
    // The setting that leaves the most in the filter to be cut off: a cutoff
    // near the bottom of its range with the sub at full level, a sine an octave
    // down being exactly what a low cutoff passes.
    setValue(processor, "cutoff", 54.0f);
    setValue(processor, "subEnable", 1.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "env1Release", 0.35f);

    constexpr int total = 96000;
    juce::AudioBuffer<float> buffer(2, total);
    buffer.clear();
    processor.prepareToPlay(48000.0, total);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, 45), total / 4);
    processor.processBlock(buffer, midi);

    const auto* data = buffer.getReadPointer(0);
    auto peak = 0.0f;
    for (int i = 0; i < total; ++i) peak = std::max(peak, std::abs(data[i]));
    auto last = total - 1;
    while (last > 0 && data[last] == 0.0f) --last;

    require(peak > 0.01f, "the note sounds");
    require(last < total - 1, "the note has finished well inside the render");
    // Cutting the voice at the envelope's zero left this 39 dB below the peak
    // of the note, which against the silence after a note is plainly audible.
    require(std::abs(data[last]) < peak * 0.0005f,
            "a finished voice is faded out rather than cut off, so the filter's ring is not truncated");
    if (std::abs(data[last]) >= peak * 0.0005f)
        std::cerr << "       left " << std::abs(data[last]) << " against a peak of " << peak << std::endl;
}
}

void voicingTests()
{
    voicingSuite();
    voiceStealSuite();
    voiceTailSuite();
}
}
