#include "ForgeTestSupport.h"
#include "ForgeTestSpectrum.h"

#include "../core/ForgeSpectral.h"
#include "../ui/ForgePlacement.h"
#include "../ui/ForgeDisplays.h"

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

// A sample of a bright tone: thirty harmonics of 110 Hz falling as a saw's do,
// which is the content a level loss in the shifting shows up on first — a
// single sine has only one partial to keep whole.
std::unique_ptr<Sample> harmonicSample(double seconds = 2.0)
{
    const auto length = static_cast<int>(testRate * seconds);
    std::vector<float> audio(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
    {
        auto value = 0.0;
        for (int harmonic = 1; harmonic <= 30; ++harmonic)
            value += std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * harmonic
                              * static_cast<double>(i) / testRate) / harmonic;
        audio[static_cast<size_t>(i)] = static_cast<float>(0.3 * value);
    }
    return std::make_unique<Sample>(audio.data(), length, testRate, "HARMONIC");
}

// How loud the vocoder plays a sample at a given shift, in decibels against the
// sample itself placed at the centre of the image. Measured on the vocoder
// alone, so the voice's own output gain — which a wavetable oscillator goes
// through just the same — is not part of the answer.
double vocoderGainDb(const Sample& sample, float ratio)
{
    auto voice = std::make_unique<SpectralVoice>();
    auto scratch = std::make_unique<SpectralScratch>();
    scratch->prepare();
    juce::dsp::FFT fft(spectralFftOrder);
    SpectralSettings settings;
    settings.pitchRatio = ratio;
    settings.transients = true;

    auto sum = 0.0;
    auto count = 0;
    for (int i = 0; i < static_cast<int>(testRate); ++i)
    {
        auto l = 0.0f, r = 0.0f;
        spectralRead(*voice, sample, settings, *scratch, fft, l, r);
        if (i < 8192) continue;
        sum += static_cast<double>(l) * l;
        ++count;
    }
    auto source = 0.0;
    for (const auto value : sample.waveform()) source += static_cast<double>(value) * value;
    // The centre of the image is the pan law's half power on each side.
    const auto expected = std::sqrt(0.5 * source / static_cast<double>(sample.waveform().size()));
    return 20.0 * std::log10(std::sqrt(sum / juce::jmax(1, count)) / expected);
}

// A sample comes out as loud as it went in, at its own pitch and an octave
// either way of it. This is the check the user's ear found first: the shift
// used to read each output bin from wherever bin / ratio landed, which crushed
// a partial's window an octave down and stretched it an octave up, and the
// overlap-add stopped summing to one — 11 dB lost an octave down on a real
// loop, 7 dB an octave up. Moving each partial's region whole keeps its shape.
void levelSuite()
{
    const auto sine = sineSample(440.0);
    const auto bright = harmonicSample();
    for (const auto* sample : {sine.get(), bright.get()})
        for (const auto ratio : {0.5f, 1.0f, 2.0f})
            requireClose(static_cast<float>(vocoderGainDb(*sample, ratio)), 0.0f, 1.5f,
                         "a spectral oscillator keeps a sample's level at the root and an octave either way");
}

// Every sample peaks at full scale once it is loaded, wherever it was recorded,
// so a quiet file and a loud one sit at the level a wavetable does.
void normalisedSuite()
{
    constexpr auto length = 8192;
    std::vector<float> quiet(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        quiet[static_cast<size_t>(i)] = 0.1f * std::sin(0.05f * static_cast<float>(i));
    const Sample sample(quiet.data(), length, testRate, "QUIET");
    auto peak = 0.0f;
    for (const auto value : sample.waveform()) peak = juce::jmax(peak, std::abs(value));
    requireClose(peak, 1.0f, 0.001f, "a sample is normalised to full scale when it is analysed");
}

// A file recorded at another rate plays at its own pitch and its own speed.
// Without the rate in the ratio a 44.1 kHz file at 48 kHz played 147 cents
// sharp and ran 9% fast — and nearly every sample anybody downloads is 44.1.
void sampleRateSuite()
{
    constexpr auto fileRate = 44100.0;
    const auto length = static_cast<int>(fileRate * 4.0);
    std::vector<float> audio(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(
            0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * static_cast<double>(i) / fileRate));
    const Sample sample(audio.data(), length, fileRate, "CD RATE");
    const auto patch = spectralOnly(sample);

    requireClose(static_cast<float>(centsBetween(spectralPitch(patch, 60), 440.0)), 0.0f, 12.0f,
                 "a sample recorded at another rate keeps its own pitch at the root note");

    Core core;
    core.initialise(testRate);
    core.noteOn(60, 1.0f, patch);
    for (int i = 0; i < static_cast<int>(testRate); ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
    }
    requireClose(core.scanPosition(0), 0.25f, 0.02f,
                 "a second of playing reaches a second into a sample recorded at another rate");
}

// --- The loop modes ------------------------------------------------------------
//
// Where the playhead goes, hop by hop. Read straight off the voice rather than
// off the audio: a loop is a statement about position, and the pitch checks
// above have already shown that position and pitch do not touch.

struct Trail
{
    std::vector<double> frames;
    bool ended = false;
};

Trail playheadTrail(const Sample& sample, const SpectralSettings& settings, int hops)
{
    auto voice = std::make_unique<SpectralVoice>();
    auto scratch = std::make_unique<SpectralScratch>();
    scratch->prepare();
    juce::dsp::FFT fft(spectralFftOrder);
    Trail trail;
    for (int hop = 0; hop < hops; ++hop)
    {
        // Where this hop reads, then the hop itself, which moves it on.
        spectralSynthesise(*voice, sample, settings, *scratch, fft);
        trail.frames.push_back(voice->frame);
    }
    trail.ended = voice->ended;
    return trail;
}

SpectralSettings loopSettings(SpectralLoop mode, float scan = 1.0f)
{
    SpectralSettings settings;
    settings.loopMode = mode;
    settings.scan = scan;
    settings.start = 0.1f;
    settings.end = 0.9f;
    settings.loopStart = 0.5f;
    settings.loopEnd = 0.7f;
    return settings;
}

void loopModesSuite()
{
    // Four seconds: 372 frames, so the loop is seventy-four frames long. Every
    // mode but MANUAL runs from START, so a loop mode reaches the loop's start
    // at hop 149 and its end at hop 223 — which is where REV LOOP and FWD/REV
    // arrive, since both turn at the far end. Each check reads from past its
    // own mode's arrival.
    const auto sample = sineSample(440.0, 4.0);
    const auto last = static_cast<double>(sample->frameCount() - 1);
    const auto loopStart = 0.5 * last, loopEnd = 0.7 * last;
    constexpr auto slack = 1.0e-6;

    // ONE-SHOT and every loop mode start on START going forwards and on END
    // going backwards: the run into the first time round is where START puts
    // it.
    for (const auto mode : {SpectralLoop::oneShot, SpectralLoop::forward, SpectralLoop::reverse,
                            SpectralLoop::pingPong})
    {
        const auto forwards = playheadTrail(*sample, loopSettings(mode), 1);
        requireClose(static_cast<float>(forwards.frames.front()), static_cast<float>(0.1 * last + 1.0), 0.01f,
                     "a voice begins playing at the start marker");
        const auto backwards = playheadTrail(*sample, loopSettings(mode, -1.0f), 1);
        requireClose(static_cast<float>(backwards.frames.front()), static_cast<float>(0.9 * last - 1.0), 0.01f,
                     "a voice with SCAN running backwards begins at the end marker");
    }

    // START moved up onto the loop's start makes the first time round the
    // loop itself: the playhead begins inside it and never leaves it.
    {
        auto settings = loopSettings(SpectralLoop::forward);
        settings.start = settings.loopStart;
        const auto trail = playheadTrail(*sample, settings, 300);
        auto inside = true;
        for (const auto frame : trail.frames)
            inside = inside && frame >= loopStart - slack && frame <= loopEnd + slack;
        require(inside, "with START on the loop's start, the first time round is the loop");
    }

    // START moved past the loop's start carries the loop's edge with it: the
    // loop is held between START and END, so it goes round from START.
    {
        auto settings = loopSettings(SpectralLoop::forward);
        settings.start = 0.6f;
        const auto trail = playheadTrail(*sample, settings, 300);
        auto inside = true;
        for (const auto frame : trail.frames)
            inside = inside && frame >= 0.6 * last - slack && frame <= loopEnd + slack;
        require(inside, "a loop is held between START and END");
    }

    // ONE-SHOT runs to END and stops, whatever the loop is set to: the
    // playhead parks, the voice reports it has ended, and what comes out after
    // that is silence.
    {
        const auto trail = playheadTrail(*sample, loopSettings(SpectralLoop::oneShot), 400);
        require(trail.ended, "a one-shot ends once it has played to the end marker");
        requireClose(static_cast<float>(trail.frames.back()), static_cast<float>(0.9 * last), 0.01f,
                     "a one-shot plays past the loop and stops at the end marker");

        auto voice = std::make_unique<SpectralVoice>();
        auto scratch = std::make_unique<SpectralScratch>();
        scratch->prepare();
        juce::dsp::FFT fft(spectralFftOrder);
        const auto settings = loopSettings(SpectralLoop::oneShot);
        auto tail = 0.0f;
        for (int i = 0; i < 400 * spectralHop; ++i)
        {
            auto l = 0.0f, r = 0.0f;
            spectralRead(*voice, *sample, settings, *scratch, fft, l, r);
            if (i >= 380 * spectralHop) tail = juce::jmax(tail, std::abs(l), std::abs(r));
        }
        require(tail < 1.0e-6f, "a one-shot that has ended is silent");
    }

    // FWD LOOP reaches the loop and stays in it, going round: once inside,
    // every position is between the loop's ends and the playhead jumps back
    // to the loop's start at least once.
    {
        const auto trail = playheadTrail(*sample, loopSettings(SpectralLoop::forward), 400);
        auto inside = true, wrapped = false;
        for (size_t hop = 200; hop < trail.frames.size(); ++hop)
        {
            inside = inside && trail.frames[hop] >= loopStart - slack && trail.frames[hop] <= loopEnd + slack;
            wrapped = wrapped || trail.frames[hop] < trail.frames[hop - 1];
        }
        require(inside, "a forward loop stays between the loop markers once it has reached them");
        require(wrapped, "a forward loop goes back round to the loop start");
    }

    // REV LOOP turns at the loop's end and goes round backwards: once looping,
    // the playhead only ever falls, except where it wraps back up to the end.
    {
        const auto trail = playheadTrail(*sample, loopSettings(SpectralLoop::reverse), 540);
        auto falling = 0, rising = 0;
        auto inside = true;
        for (size_t hop = 280; hop < trail.frames.size(); ++hop)
        {
            const auto step = trail.frames[hop] - trail.frames[hop - 1];
            if (step < -slack) ++falling;
            if (step > slack) ++rising;
            inside = inside && trail.frames[hop] >= loopStart - slack && trail.frames[hop] <= loopEnd + slack;
        }
        require(inside, "a reverse loop stays between the loop markers");
        require(falling > rising * 20, "a reverse loop runs backwards round the loop");
        require(rising > 0, "a reverse loop wraps from the loop start back to the loop end");
    }

    // FWD/REV bounces: once looping it spends as long going up as coming down,
    // and never leaves the loop or jumps across it.
    {
        const auto trail = playheadTrail(*sample, loopSettings(SpectralLoop::pingPong), 730);
        auto falling = 0, rising = 0;
        auto inside = true, jumped = false;
        for (size_t hop = 280; hop < trail.frames.size(); ++hop)
        {
            const auto step = trail.frames[hop] - trail.frames[hop - 1];
            if (step < -slack) ++falling;
            if (step > slack) ++rising;
            jumped = jumped || std::abs(step) > 1.5;
            inside = inside && trail.frames[hop] >= loopStart - slack && trail.frames[hop] <= loopEnd + slack;
        }
        require(inside, "a forward/reverse loop stays between the loop markers");
        require(!jumped, "a forward/reverse loop turns rather than jumping");
        require(rising > 50 && falling > 50 && std::abs(rising - falling) < rising / 4,
                "a forward/reverse loop goes both ways about equally");
    }

    // MANUAL does not run: the playhead is wherever SCAN puts it across the
    // whole sample, every hop, however long the note — START and END, which
    // it does not show, do not narrow it.
    {
        auto settings = loopSettings(SpectralLoop::manual);
        settings.position = 0.25f;
        const auto trail = playheadTrail(*sample, settings, 100);
        const auto expected = 0.25 * last;
        auto still = true;
        // To a thousandth of a frame: the position arrives as a float.
        for (const auto frame : trail.frames) still = still && std::abs(frame - expected) < 1.0e-3;
        require(still, "a manual playhead stays where SCAN puts it across the whole sample");
    }
}

// The same, through the parameters a host and the panel write: a one-shot set
// from the Processor plays its sample once and then goes quiet.
void loopParametersSuite()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("forge-spectral-oneshot.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> buffer(1, static_cast<int>(testRate / 2));
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

    const auto lateLevel = [&file] (SpectralLoop mode)
    {
        Processor processor;
        if (processor.importSample(0, file).failed()) return -1.0f;
        setValue(processor, "oscAMode", static_cast<float>(OscMode::spectral));
        setValue(processor, "oscALoopMode", static_cast<float>(mode));
        setValue(processor, "oscBEnable", 0.0f);
        setValue(processor, "oscCEnable", 0.0f);
        setValue(processor, "subEnable", 0.0f);
        setValue(processor, "noiseEnable", 0.0f);
        // Two seconds of a half-second sample, read for the last half second.
        juce::AudioBuffer<float> buffer(2, static_cast<int>(testRate * 2));
        renderNote(processor, buffer, 60);
        return buffer.getMagnitude(0, static_cast<int>(testRate * 1.5), static_cast<int>(testRate / 2));
    };

    const auto looped = lateLevel(SpectralLoop::forward);
    const auto once = lateLevel(SpectralLoop::oneShot);
    require(looped > 1.0e-3f, "a looping sample is still sounding long after it would have ended");
    require(once >= 0.0f && once < 1.0e-5f, "a one-shot set from its parameter is silent once it has played");
    file.deleteFile();
}

// The markers on the real panel: START and END in ONE-SHOT, the loop's bracket
// in the modes that go round, nothing but the playhead in MANUAL, and never the
// pair a mode does not read, either drawn or picked up. Driven through the
// editor's own mouse handlers, so the hit test, the cursor, the drag and the
// paint are the ones a hand meets.
void loopMarkersSuite()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("forge-spectral-markers.wav");
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

    Processor processor;
    require(processor.importSample(0, file).wasOk(), "the marker test's sample loads");
    file.deleteFile();
    setValue(processor, "oscAMode", static_cast<float>(OscMode::spectral));
    setValue(processor, "oscAStart", 0.1f);
    setValue(processor, "oscAEnd", 0.9f);
    setValue(processor, "oscALoopStart", 0.3f);
    setValue(processor, "oscALoopEnd", 0.7f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);

    const ui::Module* oscA = nullptr;
    for (const auto& module : ui::modules())
        if (juce::String(module.id) == "oscA") oscA = &module;
    require(oscA != nullptr, "OSC A is declared");
    if (oscA == nullptr) return;
    // Where the editor draws the spectrogram: the display less its loop strip,
    // inside the well's wall.
    const auto plot = ui::displayPlotBounds(ui::moduleBounds(editor->getLocalBounds(), *oscA), *oscA,
                                            static_cast<int>(OscMode::spectral)).reduced(ui::spectralWellWall);
    const auto xAt = [&plot] (float proportion)
    {
        return plot.getX() + juce::roundToInt(proportion * static_cast<float>(plot.getWidth()));
    };

    const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    const auto when = juce::Time::getCurrentTime();
    const auto eventAt = [&] (juce::Point<int> at, bool dragged)
    {
        return juce::MouseEvent(mouse, at.toFloat(), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                editor.get(), editor.get(), when, at.toFloat(), when, 1, dragged);
    };
    const auto cursorOver = [&] (juce::Point<int> at)
    {
        editor->mouseMove(eventAt(at, false));
        return editor->getMouseCursor();
    };
    const auto drag = [&] (juce::Point<int> from, juce::Point<int> to)
    {
        editor->mouseDown(eventAt(from, false));
        editor->mouseDrag(eventAt(to, true));
        editor->mouseUp(eventAt(to, true));
    };
    const auto plotPixels = [&]
    {
        juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g(image);
        editor->paintEntireComponent(g, false);
        return image.getClippedImage(plot);
    };
    const auto samePixels = [] (const juce::Image& a, const juce::Image& b)
    {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };

    const auto middle = plot.getCentreY();
    const juce::MouseCursor resize(juce::MouseCursor::LeftRightResizeCursor);
    const juce::MouseCursor plain(juce::MouseCursor::NormalCursor);

    // ONE-SHOT offers START and END down their whole height, and nothing where
    // the loop is.
    setValue(processor, "oscALoopMode", static_cast<float>(SpectralLoop::oneShot));
    require(cursorOver({xAt(0.1f), middle}) == resize && cursorOver({xAt(0.9f), plot.getBottom() - 3}) == resize,
            "a one-shot offers its start and end markers down their whole height");
    require(cursorOver({xAt(0.3f), middle}) == plain && cursorOver({xAt(0.7f), middle}) == plain,
            "a one-shot offers no loop marker to drag");
    const auto oneShot = plotPixels();

    // MANUAL offers no marker at all, and shows the playhead where SCAN puts
    // it — with no note sounding, and following the knob. Read along the top
    // of the picture, which a 440 Hz sine leaves dark.
    const auto brightAt = [&plot] (const juce::Image& image, float proportion)
    {
        const auto x = juce::roundToInt(proportion * static_cast<float>(plot.getWidth()));
        auto brightest = 0.0f;
        for (int dx = -1; dx <= 2; ++dx)
            brightest = juce::jmax(brightest, image.getPixelAt(juce::jlimit(0, image.getWidth() - 1, x + dx), 3)
                                                  .getBrightness());
        return brightest;
    };
    setValue(processor, "oscALoopMode", static_cast<float>(SpectralLoop::manual));
    require(cursorOver({xAt(0.1f), middle}) == plain && cursorOver({xAt(0.9f), middle}) == plain
                && cursorOver({xAt(0.3f), middle}) == plain && cursorOver({xAt(0.7f), middle}) == plain,
            "manual offers no marker to drag");
    setValue(processor, "oscAScan", 1.0f);
    const auto right = plotPixels();
    setValue(processor, "oscAScan", -1.0f);
    const auto left = plotPixels();
    require(brightAt(right, spectralManualPosition(1.0f)) > 0.5f && brightAt(left, spectralManualPosition(1.0f)) < 0.3f,
            "manual draws its playhead where SCAN puts it, with no note sounding");
    require(brightAt(left, spectralManualPosition(-1.0f)) > 0.5f && brightAt(right, spectralManualPosition(-1.0f)) < 0.3f,
            "and the playhead follows SCAN when it moves");
    setValue(processor, "oscAScan", 1.0f);

    // A loop mode offers the loop's ends down their whole height, and START
    // and END as well, since it runs in from START; it draws the loop on top
    // of what a one-shot draws.
    for (const auto mode : {SpectralLoop::forward, SpectralLoop::reverse, SpectralLoop::pingPong})
    {
        setValue(processor, "oscALoopMode", static_cast<float>(mode));
        require(cursorOver({xAt(0.3f), middle}) == resize && cursorOver({xAt(0.7f), plot.getY() + 2}) == resize,
                "every loop mode offers both loop markers down their whole height");
        require(cursorOver({xAt(0.1f), middle}) == resize && cursorOver({xAt(0.9f), middle}) == resize,
                "every loop mode offers START and END as well");
        require(!samePixels(oneShot, plotPixels()), "a loop mode draws the loop as well as START and END");
    }

    // Each pair moves its own parameters and leaves the other pair alone.
    const auto step = 1.5f / static_cast<float>(plot.getWidth());
    setValue(processor, "oscALoopMode", static_cast<float>(SpectralLoop::oneShot));
    drag({xAt(0.1f), middle}, {xAt(0.15f), middle});
    drag({xAt(0.9f), middle}, {xAt(0.85f), middle});
    requireClose(value(processor, "oscAStart"), 0.15f, step, "dragging the start marker moves START");
    requireClose(value(processor, "oscAEnd"), 0.85f, step, "dragging the end marker moves END");
    requireClose(value(processor, "oscALoopStart"), 0.3f, 1.0e-6f, "and neither touches the loop");
    const auto startBefore = value(processor, "oscAStart");

    setValue(processor, "oscALoopMode", static_cast<float>(SpectralLoop::forward));
    drag({xAt(0.3f), middle}, {xAt(0.2f), middle});
    requireClose(value(processor, "oscALoopStart"), 0.2f, step, "dragging the left loop marker moves the loop's start");
    requireClose(value(processor, "oscALoopEnd"), 0.7f, step, "and leaves its end where it was");
    drag({xAt(0.45f), plot.getY() + 3}, {xAt(0.55f), plot.getY() + 3});
    requireClose(value(processor, "oscALoopStart"), 0.3f, step, "dragging the bar moves the loop's start");
    requireClose(value(processor, "oscALoopEnd"), 0.8f, step, "and its end by the same distance");
    requireClose(value(processor, "oscAStart"), startBefore, 1.0e-6f, "and none of it touches START");

    // A loop end standing right on START — where both open — is told apart by
    // height: on the bar it is the loop's, at the foot it is START.
    setValue(processor, "oscAStart", 0.2f);
    setValue(processor, "oscALoopStart", 0.2f);
    drag({xAt(0.2f), plot.getY() + 3}, {xAt(0.25f), plot.getY() + 3});
    requireClose(value(processor, "oscALoopStart"), 0.25f, step, "on the bar, a loop end on START is the loop's");
    requireClose(value(processor, "oscAStart"), 0.2f, 1.0e-6f, "and START stays where it was");
    setValue(processor, "oscALoopStart", 0.2f);
    drag({xAt(0.2f), plot.getBottom() - 2}, {xAt(0.15f), plot.getBottom() - 2});
    requireClose(value(processor, "oscAStart"), 0.15f, step, "at the foot, it is START");
    requireClose(value(processor, "oscALoopStart"), 0.2f, 1.0e-6f, "and the loop stays where it was");

    // And the loop stays between START and END: its start stops at START.
    drag({xAt(0.2f), plot.getY() + 3}, {xAt(0.05f), plot.getY() + 3});
    requireClose(value(processor, "oscALoopStart"), value(processor, "oscAStart"), step,
                 "the loop's start cannot be dragged past START");
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
    levelSuite();
    normalisedSuite();
    sampleRateSuite();
    loopModesSuite();
    loopParametersSuite();
    loopMarkersSuite();
}
}
