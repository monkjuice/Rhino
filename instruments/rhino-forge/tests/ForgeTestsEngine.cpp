// The engine as a whole: every source is independently audible, switching one
// off really does silence it, and an extreme patch stays finite and inside full
// scale.
#include "ForgeTestSupport.h"
#include "ForgeTestTools.h"

namespace rhino::forge::tests
{
namespace
{
// The audio thread's first rule: a block allocates nothing. Checked rather than
// read off the code, because what allocates is usually a few calls down — a
// parameter id spelled as a juce::String — and looks like nothing at the call.
void realtimeSuite()
{
    // A bindings file that is not there is the first-run default, knobs 1-8 on
    // the macros, which is what most machines have and the case that filters
    // every block. Never written: only learning or the menu saves a map.
    const auto mapFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("RhinoForgeRealtimeMidiMap.xml");
    mapFile.deleteFile();
    Processor::setMidiMapFile(mapFile);

    auto processor = std::make_unique<Processor>();
    everythingPatch(*processor);
    require(processor->midiBindingCount() == macroCount, "a first run binds knobs 1-8 to the macros");

    constexpr int blockSize = 256;
    processor->prepareToPlay(48000.0, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize(1024);
    // Notes starting and stopping, and a bound knob turning, so the voices, the
    // matrix, the racks and the controller filter all run.
    const auto playBlock = [&] (int index)
    {
        midi.clear();
        if (index % 4 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48 + index % 12, 0.8f), 3);
        if (index % 4 == 2) midi.addEvent(juce::MidiMessage::noteOff(1, 48 + (index - 2) % 12), 17);
        if (index % 3 == 0)
            midi.addEvent(juce::MidiMessage::controllerEvent(1, Processor::firstDefaultMacroCc, index % 128), 40);
        processor->processBlock(buffer, midi);
    };
    for (int i = 0; i < 8; ++i) playBlock(i);

    long long allocated = 0;
    {
        AllocationCounter counter;
        for (int i = 8; i < 72; ++i) playBlock(i);
        allocated = counter.count();
    }
    if (allocated != 0) std::cerr << "  " << allocated << " allocations in 64 blocks\n";
    require(allocated == 0, "a block allocates nothing, with notes playing and a bound knob turning");

    // Taking a bound controller out of the host's buffer leaves the host its own
    // storage. Swapping a fresh buffer in handed the host's memory to the audio
    // thread to free.
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, Processor::firstDefaultMacroCc, 64), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    const auto* storage = midi.data.begin();
    processor->processBlock(buffer, midi);
    require(midi.data.begin() == storage, "filtering a bound controller keeps the host's MIDI storage");
    require(midi.getNumEvents() == 1, "the bound controller is taken out and the note is kept");

    Processor::setMidiMapFile(juce::File());
}

void engineSuite()
{
    // Hosts own plugin instances dynamically. Mirror that here: the engine
    // suite deliberately nests many render helpers, and eight preallocated FX
    // slots should not turn that test call tree into a Windows stack test.
    auto processorStorage = std::make_unique<rhino::forge::Processor>();
    auto& processor = *processorStorage;

    // A silent patch really is silent: with every source switched off, nothing
    // reaches the output however the level knobs are set.
    for (const auto* id : {"oscAEnable", "oscBEnable", "oscCEnable", "subEnable", "noiseEnable"})
        setValue(processor, id, 0.0f);
    setValue(processor, "subLevel", 1.0f);
    setValue(processor, "noiseLevel", 1.0f);
    require(peakForNote(processor) == 0.0f, "every source disabled renders silence");

    // Each source is independently audible.
    setValue(processor, "subEnable", 1.0f);
    const auto subOnly = peakForNote(processor);
    require(subOnly > 0.0f, "the sub module alone makes sound");
    setValue(processor, "subEnable", 0.0f);

    setValue(processor, "noiseEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "the noise module alone makes sound");
    setValue(processor, "noiseEnable", 0.0f);

    setValue(processor, "oscAEnable", 1.0f);
    const auto oscAOnly = peakForNote(processor);
    require(oscAOnly > 0.0f, "oscillator A alone makes sound");

    // Each oscillator owns its level outright. Nothing about B may reach A.
    setValue(processor, "oscBLevel", 0.9f);
    setValue(processor, "oscBDetune", 1.0f);
    setValue(processor, "oscBUnison", 8.0f);
    requireClose(peakForNote(processor), oscAOnly, 0.0001f,
                 "oscillator B's controls do not affect A while B is switched off");

    setValue(processor, "oscCLevel", 0.8f);
    setValue(processor, "oscCDetune", 0.6f);
    setValue(processor, "oscCUnison", 6.0f);
    requireClose(peakForNote(processor), oscAOnly, 0.0001f,
                 "oscillator C's controls do not affect A while C is switched off");

    setValue(processor, "oscBEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "oscillators A and B together make sound");

    setValue(processor, "oscAEnable", 0.0f);
    setValue(processor, "oscBEnable", 0.0f);
    setValue(processor, "oscCEnable", 1.0f);
    require(peakForNote(processor) > 0.0f, "oscillator C alone makes sound");
    setValue(processor, "oscAEnable", 1.0f);
    setValue(processor, "oscBEnable", 1.0f);

    // Bypassing the filter must actually bypass it: a cutoff low enough to
    // remove nearly everything should stop mattering.
    setValue(processor, "cutoff", 60.0f);
    setValue(processor, "filterEnable", 1.0f);
    const auto filtered = peakForNote(processor);
    setValue(processor, "filterEnable", 0.0f);
    const auto bypassed = peakForNote(processor);
    require(bypassed > filtered * 2.0f, "switching the filter off bypasses it");

    // Output stays finite and bounded across an extreme patch.
    auto extremeStorage = std::make_unique<rhino::forge::Processor>();
    auto& extreme = *extremeStorage;
    for (const auto* id : {"oscAEnable", "oscBEnable", "oscCEnable", "subEnable", "noiseEnable", "filterEnable"})
        setValue(extreme, id, 1.0f);
    setValue(extreme, "oscAUnison", 8.0f);
    setValue(extreme, "oscBUnison", 8.0f);
    setValue(extreme, "oscCUnison", 8.0f);
    setValue(extreme, "oscADetune", 1.0f);
    setValue(extreme, "oscBDetune", 1.0f);
    setValue(extreme, "oscCDetune", 1.0f);
    setValue(extreme, "resonance", 1.0f);
    setValue(extreme, "drive", 1.0f);
    setValue(extreme, "output", 1.25f);
    setValue(extreme, "subLevel", 1.0f);
    setValue(extreme, "noiseLevel", 1.0f);
    setValue(extreme, "lfo1Rate", 20.0f);
    // Drive the matrix hard too: LFO 1 into the cutoff and into oscillator A's
    // pitch, both at full depth.
    setValue(extreme, "mod1Source", 2.0f);
    setValue(extreme, "mod1Dest", static_cast<float>(destCutoff));
    setValue(extreme, "mod1Depth", 1.0f);
    setValue(extreme, "mod2Source", 2.0f);
    setValue(extreme, "mod2Dest", static_cast<float>(destAPitch));
    setValue(extreme, "mod2Depth", 1.0f);

    constexpr int blockSize = 512;
    extreme.prepareToPlay(48000.0, blockSize);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (int note = 48; note < 58; ++note)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
    auto peak = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        extreme.processBlock(buffer, midi);
        midi.clear();
        require(allSamplesFinite(buffer), "every rendered sample is finite");
        peak = juce::jmax(peak, buffer.getMagnitude(0, blockSize));
    }
    require(peak > 0.0f, "an extreme patch still produces signal");
    require(peak <= 1.0f, "an extreme patch stays within full scale");
}
}

void engineTests()
{
    engineSuite();
    realtimeSuite();
}
}
