#include "ForgeTestSupport.h"
#include "ForgeTestSpectrum.h"

#include "../core/ForgeSpectral.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

// The spectral oscillator, measured off what it renders.
//
// Everything here asks its question of the audio, the way tuningSuite() does
// and for the same reason: a check that read the vocoder's own phase
// accumulator would agree with it by construction. A sample of known content
// goes in, the rendered signal is transformed, and the peak is compared with
// what the note and the knobs say it should be.
//
// The two claims worth the most are the ones in SPECTRAL.md's architecture
// section, because neither is obvious and both would fail silently: that the
// pitch follows the note, and that SCAN moves through the sample without
// touching the pitch while doing it.
namespace rhino::forge::tests
{
namespace
{
constexpr double testRate = 48000.0;

// A sample of a steady sine, long enough to analyse into a few hundred frames.
// Built rather than loaded: a file would make these checks depend on content in
// the repository, and what is being measured is the engine.
std::unique_ptr<Sample> sineSample(double hz, double seconds = 2.0)
{
    const auto length = static_cast<int>(testRate * seconds);
    std::vector<float> audio(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * hz
                           * static_cast<double>(i) / testRate));
    return std::make_unique<Sample>(audio.data(), length, testRate, "SINE");
}

// A sample that is one tone for its first half and another for its second, so
// where the playhead has got to can be read off the pitch that comes out.
std::unique_ptr<Sample> twoToneSample(double first, double second, double seconds = 4.0)
{
    const auto length = static_cast<int>(testRate * seconds);
    const auto half = length / 2;
    std::vector<float> audio(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
    {
        const auto hz = i < half ? first : second;
        audio[static_cast<size_t>(i)] = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * hz
                           * static_cast<double>(i) / testRate));
    }
    return std::make_unique<Sample>(audio.data(), length, testRate, "TWO TONE");
}

Patch spectralOnly(const Sample& sample)
{
    Patch patch;
    patch.oscillators[0].enable = 1.0f;
    patch.oscillators[0].mode = static_cast<float>(OscMode::spectral);
    patch.oscillators[0].sample = &sample;
    patch.oscillators[0].unison = 1.0f;
    patch.oscillators[0].level = 1.0f;
    patch.oscillators[0].scan = 1.0f;
    patch.oscillators[0].cut = 1.0f;
    patch.oscillators[0].mix = 0.0f;
    patch.oscillators[1].enable = 0.0f;
    patch.oscillators[2].enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.envs[ampEnv].attack = 0.001f;
    patch.envs[ampEnv].sustain = 1.0f;
    return patch;
}

// The magnitude spectrum of a spectral patch, measured after `skip` samples so
// the window lands where the caller means it to. The vocoder needs a few hops
// before its overlap-add is full, so nothing is measured at zero.
std::vector<double> spectralSpectrum(const Patch& patch, int note, int skip)
{
    Core core;
    core.initialise(testRate);
    core.noteOn(note, 1.0f, patch);
    for (int i = 0; i < skip; ++i) { auto l = 0.0f, r = 0.0f; core.renderSample(patch, l, r); }

    std::vector<float> data(2 * spectrumSize, 0.0f);
    for (int i = 0; i < spectrumSize; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        const auto w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                            * static_cast<double>(i) / static_cast<double>(spectrumSize));
        data[static_cast<size_t>(i)] = static_cast<float>(0.5 * (l + r) * w);
    }

    juce::dsp::FFT fft(spectrumOrder);
    fft.performRealOnlyForwardTransform(data.data());
    std::vector<double> magnitude(static_cast<size_t>(spectrumSize / 2), 0.0);
    for (int bin = 0; bin < spectrumSize / 2; ++bin)
    {
        const auto re = static_cast<double>(data[static_cast<size_t>(2 * bin)]);
        const auto im = static_cast<double>(data[static_cast<size_t>(2 * bin + 1)]);
        magnitude[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
    }
    return magnitude;
}

double spectralPitch(const Patch& patch, int note, int skip = 8192)
{
    return loudestPeak(spectralSpectrum(patch, note, skip), testRate).frequency;
}

// How far two frequencies are apart, in cents, which is the unit a tuning error
// is actually heard in.
double centsBetween(double actual, double expected)
{
    if (actual <= 0.0 || expected <= 0.0) return 1.0e6;
    return 1200.0 * std::log2(actual / expected);
}

// --- The checks ---------------------------------------------------------------

// A sample resynthesised at the root note comes back at its own pitch. This is
// the one that fails if the window, the hop, the overlap-add scaling or the
// phase advance is wrong, and it fails loudly rather than subtly.
void rootPitchSuite()
{
    const auto sample = sineSample(440.0);
    const auto patch = spectralOnly(*sample);
    // MIDI 60 is spectralRootHz, so the shift is exactly one.
    const auto rendered = spectralPitch(patch, 60);
    requireClose(static_cast<float>(centsBetween(rendered, 440.0)), 0.0f, 12.0f,
                 "a sample resynthesised at the root note keeps its own pitch");
}

// And transposes with the keyboard. An octave is the test that catches a ratio
// applied to the wrong quantity: a shift applied to the hop rather than the
// spectrum would still move the pitch, just not by this.
void transposeSuite()
{
    const auto sample = sineSample(440.0);
    const auto patch = spectralOnly(*sample);

    const auto up = spectralPitch(patch, 72);
    requireClose(static_cast<float>(centsBetween(up, 880.0)), 0.0f, 18.0f,
                 "an octave up resynthesises an octave up");

    const auto down = spectralPitch(patch, 48);
    requireClose(static_cast<float>(centsBetween(down, 220.0)), 0.0f, 18.0f,
                 "an octave down resynthesises an octave down");
}

// The claim the whole architecture is arranged around: SCAN moves the playhead
// and does not move the pitch. A frozen playhead and one running at full speed
// render the same note.
//
// If pitch were done by reading the output faster — the obvious implementation,
// and the one SPECTRAL.md explains why this is not — these two would come back
// an octave or more apart.
void scanDoesNotChangePitchSuite()
{
    const auto sample = sineSample(440.0);

    auto still = spectralOnly(*sample);
    still.oscillators[0].scan = 0.0f;
    auto running = spectralOnly(*sample);
    running.oscillators[0].scan = 1.0f;
    auto backwards = spectralOnly(*sample);
    backwards.oscillators[0].scan = -1.0f;

    const auto frozen = spectralPitch(still, 60);
    const auto moving = spectralPitch(running, 60);
    const auto reversed = spectralPitch(backwards, 60);

    requireClose(static_cast<float>(centsBetween(moving, frozen)), 0.0f, 12.0f,
                 "scanning the sample does not change the pitch it sounds at");
    requireClose(static_cast<float>(centsBetween(reversed, frozen)), 0.0f, 12.0f,
                 "scanning backwards does not change the pitch either");
}

// And the other half of that claim: SCAN really does move the playhead, and
// twice the rate reaches the second half of the sample in half the time.
//
// Measured on a sample that changes pitch at its midpoint, so where the
// playhead is can be read off what comes out. The note is the root, so the
// sample's own tones arrive unshifted.
void scanMovesThePlayheadSuite()
{
    // Ten seconds, which is 934 frames and a midpoint at 467.
    //
    // The length is not arbitrary. A measurement window is 32768 samples, which
    // is 64 hops, so the playhead travels 64 frames through it at single speed
    // and 128 at double — and the whole window has to stay on one side of the
    // midpoint or it measures a mixture of the two tones and reports whichever
    // happened to win. A shorter sample also wraps its loop inside the window,
    // which puts the playhead back in the first half while it is being read.
    const auto sample = twoToneSample(440.0, 880.0, 10.0);

    auto slow = spectralOnly(*sample);
    slow.oscillators[0].scan = 1.0f;
    auto fast = spectralOnly(*sample);
    fast.oscillators[0].scan = 2.0f;

    // Two and a half seconds in: 234 hops. At one frame per hop that is frame
    // 234, comfortably inside the first half; at two it is frame 468, just past
    // the midpoint, and the window that follows stays in the second half.
    const auto skip = static_cast<int>(testRate * 2.5);
    const auto atSlow = loudestPeak(spectralSpectrum(slow, 60, skip), testRate).frequency;
    const auto atFast = loudestPeak(spectralSpectrum(fast, 60, skip), testRate).frequency;

    requireClose(static_cast<float>(centsBetween(atSlow, 440.0)), 0.0f, 40.0f,
                 "at one frame per hop the playhead is still in the sample's first half");
    requireClose(static_cast<float>(centsBetween(atFast, 880.0)), 0.0f, 40.0f,
                 "at two frames per hop it has reached the second half");
}

// CUT takes the top off, and MIX decides how much of that is heard. Measured as
// the level of a tone that sits above the corner, against the same tone with
// the filter mixed out.
void spectralFilterSuite()
{
    const auto sample = sineSample(4000.0);

    auto dry = spectralOnly(*sample);
    dry.oscillators[0].mix = 0.0f;
    auto wet = spectralOnly(*sample);
    // A corner well below where the tone sits, fully mixed in.
    wet.oscillators[0].cut = 0.05f;
    wet.oscillators[0].mix = 1.0f;

    const auto open = loudestPeak(spectralSpectrum(dry, 60, 8192), testRate).amplitude;
    const auto cut = loudestPeak(spectralSpectrum(wet, 60, 8192), testRate).amplitude;

    require(open > 1.0e-4, "a spectral oscillator with the filter mixed out is audible");
    require(cut < open * 0.2,
            "CUT below a tone takes it out when MIX is all the way wet");
}

// An oscillator in spectral mode with nothing loaded is silent, rather than
// falling back to a table or to whatever the buffers held. There is no built-in
// sample, so silence is the honest answer.
void emptySpectralIsSilentSuite()
{
    Patch patch;
    patch.oscillators[0].enable = 1.0f;
    patch.oscillators[0].mode = static_cast<float>(OscMode::spectral);
    patch.oscillators[0].sample = nullptr;
    patch.oscillators[0].level = 1.0f;
    patch.oscillators[1].enable = 0.0f;
    patch.oscillators[2].enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;

    Core core;
    core.initialise(testRate);
    core.noteOn(60, 1.0f, patch);
    auto peak = 0.0f;
    for (int i = 0; i < 8192; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        peak = juce::jmax(peak, std::abs(l), std::abs(r));
    }
    require(peak < 1.0e-6f, "a spectral oscillator with no sample loaded is silent");
}

// Switching an oscillator to spectral must not disturb the wavetable engine
// beside it. OSC B on a saw renders identically whether OSC A is a silent
// spectral oscillator or a switched-off wavetable one.
void modesDoNotLeakSuite()
{
    const auto render = [] (bool spectralNeighbour)
    {
        Patch patch;
        patch.oscillators[0].enable = spectralNeighbour ? 1.0f : 0.0f;
        patch.oscillators[0].mode = spectralNeighbour ? static_cast<float>(OscMode::spectral) : 0.0f;
        patch.oscillators[0].sample = nullptr;
        patch.oscillators[1].enable = 1.0f;
        patch.oscillators[1].position = 6.0f / 9.0f;
        patch.oscillators[1].unison = 1.0f;
        patch.oscillators[1].level = 0.75f;
        patch.oscillators[1].semitone = 0.0f;
        patch.oscillators[2].enable = 0.0f;
        patch.subEnable = 0.0f;
        patch.noiseEnable = 0.0f;
        patch.filterEnable = 0.0f;
        patch.envs[ampEnv].attack = 0.001f;
        patch.envs[ampEnv].sustain = 1.0f;

        Core core;
        core.initialise(testRate);
        core.noteOn(57, 1.0f, patch);
        std::vector<float> out(4096);
        for (int i = 0; i < 4096; ++i)
        {
            auto l = 0.0f, r = 0.0f;
            core.renderSample(patch, l, r);
            out[static_cast<size_t>(i)] = l + r;
        }
        return out;
    };

    const auto without = render(false);
    const auto with = render(true);
    auto worst = 0.0f;
    for (size_t i = 0; i < without.size(); ++i)
        worst = juce::jmax(worst, std::abs(without[i] - with[i]));
    require(worst < 1.0e-6f,
            "a spectral oscillator beside a wavetable one changes nothing about it");
}

// The loader, end to end: a file on disk becomes a published sample that a
// voice can render. This is the one check here that goes through Processor
// rather than Core, because decoding and analysis are the Processor's and
// because a path that only ever ran from the UI would be untested.
void loaderSuite()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("forge-spectral-test.wav");
    file.deleteFile();

    {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(testRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0
                               * static_cast<double>(i) / testRate)));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        require(stream != nullptr, "the test can write a sample to the temp directory");
        if (stream == nullptr) return;
        const std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), testRate, 1, 16, {}, 0));
        require(writer != nullptr, "the test can encode a wav");
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    Processor processor;
    const auto result = processor.importSample(0, file);
    require(result.wasOk(), "a wav file loads as a spectral sample");
    require(processor.sampleStore().sample(0) != nullptr,
            "the loaded sample is published to the audio thread");
    requireText(processor.sampleStore().sourceName(0), "FORGE-SPECTRAL-TEST",
                "the loaded sample is named after its file");

    // And it renders. The oscillator is switched to spectral through the same
    // parameter the panel writes, so this exercises the mode as well as the
    // loader.
    setValue(processor, "oscAMode", static_cast<float>(OscMode::spectral));
    setValue(processor, "oscBEnable", 0.0f);
    setValue(processor, "oscCEnable", 0.0f);
    setValue(processor, "subEnable", 0.0f);
    setValue(processor, "noiseEnable", 0.0f);
    require(peakForNote(processor, 16384) > 1.0e-3f,
            "a loaded spectral sample is audible");

    processor.clearSample(0);
    require(processor.sampleStore().sample(0) == nullptr, "clearing a sample unpublishes it");

    const auto missing = processor.importSample(0, file.getSiblingFile("not-there.wav"));
    require(missing.failed(), "loading a file that is not there fails rather than crashing");

    file.deleteFile();
}

// A sample survives a save. This is the check that would have caught the gap
// the engine shipped with: the mode persisted and the sample did not, so a
// patch reopened silent with every knob in the right place.
//
// Round-tripped through host state rather than a preset file because that is
// the path a DAW project takes, and because it needs no temp file of its own.
void persistenceSuite()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("forge-spectral-persist.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(testRate));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0
                               * static_cast<double>(i) / testRate)));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr) { require(false, "the test can write a sample"); return; }
        const std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), testRate, 1, 16, {}, 0));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    juce::MemoryBlock saved;
    {
        Processor processor;
        require(processor.importSample(0, file).wasOk(), "the sample loads");
        setValue(processor, "oscAMode", static_cast<float>(OscMode::spectral));
        processor.getStateInformation(saved);
    }
    require(saved.getSize() > 0, "state was written");
    // Stored as FLAC rather than as the float the analysis reads. That is the
    // difference between a preset somebody can send and one they cannot, and it
    // is worth a check because a regression to raw float would be invisible
    // until a patch carrying ten seconds of audio weighed twenty-five megabytes.
    const auto asRawFloat = static_cast<size_t>(testRate) * sizeof(float) * 4 / 3;
    require(saved.getSize() < asRawFloat,
            "a sample is stored compressed rather than as raw float");

    Processor reopened;
    require(reopened.sampleStore().sample(0) == nullptr, "a fresh processor has no sample");
    reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    const auto* sample = reopened.sampleStore().sample(0);
    require(sample != nullptr, "a spectral sample survives a save and reload");
    requireText(reopened.sampleStore().sourceName(0), "FORGE-SPECTRAL-PERSIST",
                "the reloaded sample keeps its name");
    requireClose(value(reopened, "oscAMode"), static_cast<float>(OscMode::spectral), 0.01f,
                 "the oscillator reopens in spectral mode");
    if (sample != nullptr)
        require(sample->frameCount() > 0 && !sample->waveform().empty(),
                "the reloaded sample carries its audio");

    // And it still sounds like what went in. FLAC is lossless and the 16-bit
    // quantisation is far below anything the pitch measurement can see, so the
    // reloaded sample renders at the same note as the original did.
    setValue(reopened, "oscBEnable", 0.0f);
    setValue(reopened, "oscCEnable", 0.0f);
    setValue(reopened, "subEnable", 0.0f);
    setValue(reopened, "noiseEnable", 0.0f);
    require(peakForNote(reopened, 16384) > 1.0e-3f, "the reloaded sample is audible");

    // A state with no sample node clears whatever was loaded, rather than
    // leaving the previous patch's sample under the new one's knobs.
    juce::MemoryBlock empty;
    { Processor bare; bare.getStateInformation(empty); }
    reopened.setStateInformation(empty.getData(), static_cast<int>(empty.getSize()));
    require(reopened.sampleStore().sample(0) == nullptr,
            "a patch with no sample clears the one that was loaded");

    file.deleteFile();
}

// The playhead reads where the sample actually is, and moves at the rate SCAN
// asks for. Measured through the Processor because that is what crosses the
// reading to the message thread, and the crossing is as much of this feature as
// the reading is.
void playheadSuite()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("forge-spectral-playhead.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(testRate * 4));
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, static_cast<float>(
                0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0
                               * static_cast<double>(i) / testRate)));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr) { require(false, "the test can write a sample"); return; }
        const std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), testRate, 1, 16, {}, 0));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    const auto scanAfter = [&file] (float scan, int samples)
    {
        Processor processor;
        if (processor.importSample(0, file).failed()) return -1.0f;
        setValue(processor, "oscAMode", static_cast<float>(OscMode::spectral));
        setValue(processor, "oscAScan", scan);
        setValue(processor, "oscBEnable", 0.0f);
        setValue(processor, "oscCEnable", 0.0f);
        processor.prepareToPlay(testRate, samples);
        juce::AudioBuffer<float> buffer(2, samples);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        processor.processBlock(buffer, midi);
        return processor.scanPosition(0);
    };

    // A wavetable oscillator has no sample to be anywhere in, so it reports
    // nothing rather than a stale reading from whatever was loaded before.
    {
        Processor processor;
        setValue(processor, "oscAMode", static_cast<float>(OscMode::wavetable));
        peakForNote(processor, 4096);
        requireClose(processor.scanPosition(0), 0.0f, 0.001f,
                     "a wavetable oscillator reports no scan position");
    }

    const auto quarter = static_cast<int>(testRate);
    const auto slow = scanAfter(1.0f, quarter);
    const auto fast = scanAfter(2.0f, quarter);
    require(slow > 0.0f, "a sounding spectral oscillator reports where it has got to");
    require(fast > slow * 1.5f,
            "twice the scan rate reaches twice as far through the sample");
    // A second into a four-second sample at single speed is a quarter of the
    // way through it, give or take the hop the vocoder is part way into.
    requireClose(slow, 0.25f, 0.02f, "the playhead reads the position it has actually reached");

    file.deleteFile();
}
}

void spectralTests()
{
    rootPitchSuite();
    transposeSuite();
    scanDoesNotChangePitchSuite();
    scanMovesThePlayheadSuite();
    spectralFilterSuite();
    emptySpectralIsSilentSuite();
    modesDoNotLeakSuite();
    loaderSuite();
    persistenceSuite();
    playheadSuite();
}
}
