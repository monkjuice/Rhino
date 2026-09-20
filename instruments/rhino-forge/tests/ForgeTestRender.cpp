#include "ForgeTestTools.h"

#include "../src/ForgeProcessor.h"

#include <iostream>
#include <memory>

namespace rhino::forge::tests
{
namespace
{
// A patch that exercises as much of the voice at once as a single render can:
// both oscillators detuned and stacked, both warp stages working, the sub and
// the noise in, the filter half closed with resonance and drive, four
// modulation slots joined up, and a rack on the main output.
//
// Deliberately not a musical sound. What it is for is that almost any change to
// the engine's arithmetic shows up in it, so two builds writing the same bytes
// is a strong claim rather than a weak one.
void everythingPatch(Processor& processor)
{
    const auto set = [&processor] (const char* id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
        else
            std::cerr << "no such parameter: " << id << '\n';
    };

    for (const auto* id : {"oscAEnable", "oscBEnable", "subEnable", "noiseEnable", "filterEnable"})
        set(id, 1.0f);

    set("oscAPosition", 0.62f);
    set("oscAUnison", 5.0f);
    set("oscADetune", 0.42f);
    set("oscABlend", 0.35f);
    set("oscAPan", -0.3f);
    set("oscALevel", 0.7f);
    set("oscASemitone", 0.0f);
    set("oscAWarp1Mode", 3.0f);
    set("oscAWarp1", 0.55f);
    set("oscAWarp2Mode", 12.0f);
    set("oscAWarp2", 0.4f);

    set("oscBPosition", 0.18f);
    set("oscBUnison", 3.0f);
    set("oscBDetune", 0.7f);
    set("oscBOctave", -1.0f);
    set("oscBFine", 7.0f);
    set("oscBPan", 0.35f);
    set("oscBLevel", 0.6f);
    set("oscBWarp1Mode", 20.0f);
    set("oscBWarp1", 0.65f);

    set("subWave", 2.0f);
    set("subOctave", -1.0f);
    set("subLevel", 0.45f);
    set("noiseLevel", 0.12f);

    set("cutoff", 1450.0f);
    set("resonance", 0.55f);
    set("drive", 0.35f);
    set("filterType", 0.0f);

    set("env1Attack", 0.004f);
    set("env1Decay", 0.35f);
    set("env1Sustain", 0.6f);
    set("env1Release", 0.4f);
    set("env2Attack", 0.02f);
    set("env2Decay", 0.5f);

    set("lfo1Rate", 4.3f);
    set("lfo1Shape", 1.0f);
    set("lfo2Rate", 0.9f);
    set("lfo2Shape", 4.0f);

    const auto slot = [&set] (int index, float source, float destination, float depth)
    {
        const auto id = [index] (const char* suffix)
        {
            return ("mod" + juce::String(index) + suffix).toStdString();
        };
        set(id("Source").c_str(), source);
        set(id("Dest").c_str(), destination);
        set(id("Depth").c_str(), depth);
    };
    // ENV 2-4 and LFO 2-6 have no enumerators of their own: each run is named by
    // its first and counted from there, which is what keeps the two lists from
    // drifting apart. See ModSource.
    const auto env = [] (int which) { return static_cast<float>(ModSource::env1) + which - 1; };
    const auto lfo = [] (int which) { return static_cast<float>(ModSource::lfo1) + which - 1; };

    slot(1, lfo(1), 13.0f, 0.5f);                                  // the cutoff
    slot(2, env(2), 5.0f, 0.3f);                                   // oscillator A's pitch
    slot(3, static_cast<float>(ModSource::velocity), 11.0f, 0.4f); // the sub
    slot(4, lfo(2), 6.0f, 0.25f);

    set(fxParameterId(0, 0, "Type").toRawUTF8(), 1.0f);
    set(fxParameterId(0, 0, "Mix").toRawUTF8(), 0.5f);
    set(fxParameterId(0, 1, "Type").toRawUTF8(), 3.0f);
    set(fxParameterId(0, 1, "Mix").toRawUTF8(), 0.4f);

    set("polyphony", 6.0f);
    set("output", 0.8f);
}
}

// Render that patch and write the samples raw, for comparing one build against
// another byte for byte. Nothing about a refactor of the engine should change a
// single sample of it; if it does, the diff is the answer rather than the
// starting point.
//
// Raw interleaved floats rather than a .wav, because a .wav header carries no
// information this needs and the point is that the bytes are the audio.
int runRender(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: --render output.raw [blocks]\n";
        return 2;
    }
    constexpr int blockSize = 512;
    const auto blocks = argc > 3 ? juce::String(argv[3]).getIntValue() : 60;

    auto processor = std::make_unique<Processor>();
    everythingPatch(*processor);
    processor->prepareToPlay(48000.0, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    // A chord, then one note lifted part way through, so the release path and
    // the voice tail are in the render as well as the attack.
    for (const auto note : {45, 52, 57, 61, 64})
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.85f), 0);

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr) return 1;

    for (int block = 0; block < blocks; ++block)
    {
        if (block == blocks / 2) midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
        processor->processBlock(buffer, midi);
        midi.clear();
        for (int i = 0; i < blockSize; ++i)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto sample = buffer.getSample(channel, i);
                stream->write(&sample, sizeof(sample));
            }
    }
    return 0;
}
}
