#include "DuckSuites.h"
#include "DuckTestSupport.h"

// The plugin as a host meets it: its parameters, its state, its buses, its
// latency, and a block driven by a host clock and a MIDI note.
namespace rhino::duck::tests
{
namespace
{
constexpr int cycle = 24000;

// Runs `input` through the processor on both channels in blocks of 480,
// the play head advancing, and returns the left output.
std::vector<float> run(Processor& processor, TestPlayHead& head, const std::vector<float>& input, juce::MidiBuffer notes = {})
{
    constexpr int block = 480;
    processor.setPlayHead(&head);
    processor.prepareToPlay(testRate, block);
    const auto frames = static_cast<int>(input.size());
    std::vector<float> out;
    out.reserve(input.size());
    juce::AudioBuffer<float> buffer(4, block);
    for (int start = 0; start < frames; start += block)
    {
        const auto count = std::min(block, frames - start);
        buffer.clear();
        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom(channel, 0, input.data() + start, count);
        juce::MidiBuffer midi;
        for (const auto metadata : notes)
            if (metadata.samplePosition >= start && metadata.samplePosition < start + count)
                midi.addEvent(metadata.getMessage(), metadata.samplePosition - start);
        head.ppq = start / testRate * head.bpm / 60.0;
        juce::AudioBuffer<float> slice(buffer.getArrayOfWritePointers(), 4, count);
        processor.processBlock(slice, midi);
        for (int i = 0; i < count; ++i) out.push_back(slice.getSample(0, i));
    }
    return out;
}

void parametersAreDeclared()
{
    Processor processor;
    for (const auto* id : { id::mix, id::curve, id::length, id::trigger, id::loop, id::end, id::band, id::cutoff,
                            id::bandSolo, id::antiClick, id::midiChannel, id::midiNote })
        require(processor.state.getParameter(id) != nullptr, id);
    requireClose(value(processor, id::mix), 1.0f, 1.0e-6f, "Mix starts at 100%");
    requireClose(value(processor, id::length), 1.0f, 1.0e-6f, "Length starts at 1/4");
    requireClose(value(processor, id::trigger), 0.0f, 1.0e-6f, "the trigger starts in Sync");
    requireClose(value(processor, id::loop), 1.0f, 1.0e-6f, "Loop starts on");
    requireClose(value(processor, id::end), 1.0f, 1.0e-6f, "End starts at the cycle's end");
    requireClose(value(processor, id::band), 0.0f, 1.0e-6f, "the band split starts off");
    requireClose(value(processor, id::cutoff), 200.0f, 1.0e-3f, "the cutoff starts at 200 Hz");
    requireClose(value(processor, id::antiClick), 1.0f, 1.0e-6f, "anti-click smoothing starts on");
    require(processor.state.getParameter(id::curve)->getCurrentValueAsText() == "1 Classic", "the curve reads as its number and name");
    require(processor.state.getParameter(id::cutoff)->getCurrentValueAsText() == "200 Hz", "the cutoff reads in hertz");
    require(processor.state.getParameter(id::midiNote)->getCurrentValueAsText() == "All", "the MIDI note starts at All");
    require(processor.getName() == "Rhino Duck" && processor.acceptsMidi() && !processor.producesMidi(), "the plugin names itself and takes MIDI");
}

void stateRoundTrips()
{
    Processor first;
    setValue(first, id::mix, 0.4f);
    setValue(first, id::curve, 5.0f);
    setValue(first, id::trigger, 2.0f);
    setValue(first, id::cutoff, 500.0f);
    first.setCurveHold(5, 0.2f);
    first.setCurveRelease(5, 0.5f);
    first.setCurveAttack(5, 12.0f);
    first.setCurveDepth(5, 0.7f);
    first.setCurveShift(5, -0.125f);
    first.setCurveShift(12, 0.25f);
    first.setUiScale(1.5f);
    juce::MemoryBlock saved;
    first.getStateInformation(saved);

    Processor second;
    second.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    requireClose(value(second, id::mix), 0.4f, 1.0e-5f, "Mix comes back");
    requireClose(value(second, id::curve), 5.0f, 1.0e-5f, "the curve comes back");
    requireClose(value(second, id::trigger), 2.0f, 1.0e-5f, "the trigger comes back");
    requireClose(value(second, id::cutoff), 500.0f, 0.05f, "the cutoff comes back");
    requireClose(slopedPart(5, second.curveAdjust(5)).start, 0.2f, 1.0e-5f, "a curve's Hold comes back");
    requireClose(slopedPart(5, second.curveAdjust(5)).length, 0.5f, 1.0e-5f, "its Release");
    requireClose(second.curveAttackMs(5), 12.0f, 1.0e-4f, "its Attack");
    requireClose(second.curveAdjust(5).depth, 0.7f, 1.0e-6f, "its Depth");
    requireClose(second.curveAdjust(5).shift, -0.125f, 1.0e-6f, "and its Shift");
    requireClose(second.curveAdjust(12).shift, 0.25f, 1.0e-6f, "for every curve that was adjusted");
    require(second.curveAdjust(0) == CurveAdjust {}, "and an untouched curve stays as designed");
    requireClose(second.uiScale(), 1.5f, 1.0e-6f, "the window size comes back");

    const auto revision = second.curveRevision();
    second.resetCurve(5);
    require(second.curveAdjust(5) == CurveAdjust {} && second.curveAttackMs(5) == 0.0f, "Reset takes a curve back to its design");
    require(second.curveRevision() > revision, "and moves the revision");
    second.resetAllCurves();
    require(second.curveAdjust(12) == CurveAdjust {}, "Reset all takes every curve back");
    require(first.curveAdjust(5).depth == 0.7f, "while the other instance keeps its own");
}

void holdAndReleaseKeepToTheirRoom()
{
    Processor processor;
    processor.setCurveRelease(0, 5.0f);
    const auto stretched = slopedPart(0, processor.curveAdjust(0));
    processor.setCurveHold(0, 0.5f);
    const auto held = slopedPart(0, processor.curveAdjust(0));
    requireClose(held.length, stretched.length, 1.0e-6f, "Hold never takes from the Release");
    require(held.start + held.length <= mobileRoom(curveDef(0)).highest + 1.0e-5f, "and the sloped part stays in its room");
    processor.setCurveDepth(0, 3.0f);
    require(processor.curveAdjust(0).depth == 1.0f, "Depth stops at the whole dip");
    processor.setCurveAttack(0, 500.0f);
    require(processor.curveAttackMs(0) == longestAttackMs, "Attack stops at its longest");
}

// Attack is kept in milliseconds and read in phase, at the Length chosen and
// the tempo the host last gave.
void attackIsKeptInMilliseconds()
{
    Processor processor;
    processor.setCurveAttack(0, 25.0f);
    requireClose(static_cast<float>(processor.cycleSeconds()), 0.5f, 1.0e-6f, "a quarter note at 120 bpm is half a second");
    requireClose(processor.curveAdjust(0).attack, 0.05f, 1.0e-6f, "so 25 ms is a twentieth of the cycle");
    setValue(processor, id::length, 3.0f);
    requireClose(processor.curveAdjust(0).attack, 0.0125f, 1.0e-6f, "a longer cycle keeps the same milliseconds");

    TestPlayHead head;
    head.bpm = 60.0;
    processor.setPlayHead(&head);
    processor.prepareToPlay(testRate, 512);
    juce::AudioBuffer<float> buffer(4, 512);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    requireClose(static_cast<float>(processor.cycleSeconds()), 4.0f, 1.0e-6f, "the host's tempo counts once a block has told it");
    requireClose(processor.curveAdjust(0).attack, 0.00625f, 1.0e-6f, "and the Attack follows");
}

void latencyFollowsTheTriggerMode()
{
    Processor processor;
    TestPlayHead head;
    processor.setPlayHead(&head);
    processor.prepareToPlay(testRate, 512);
    require(processor.getLatencySamples() == 0, "Sync reports no latency");
    juce::AudioBuffer<float> buffer(4, 512);
    juce::MidiBuffer midi;
    setValue(processor, id::trigger, 2.0f);
    processor.processBlock(buffer, midi);
    processor.applyPendingLatency();
    require(processor.getLatencySamples() == 768, "Audio with smoothing reports 16 ms");
    setValue(processor, id::antiClick, 0.0f);
    processor.processBlock(buffer, midi);
    processor.applyPendingLatency();
    require(processor.getLatencySamples() == 480, "and 10 ms without it");
    setValue(processor, id::trigger, 1.0f);
    setValue(processor, id::antiClick, 1.0f);
    processor.processBlock(buffer, midi);
    processor.applyPendingLatency();
    require(processor.getLatencySamples() == 288, "MIDI with smoothing reports 6 ms");
}

void busLayoutsAreTheExpectedOnes()
{
    Processor processor;
    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add(juce::AudioChannelSet::stereo());
    stereo.inputBuses.add(juce::AudioChannelSet::stereo());
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    require(processor.checkBusesLayoutSupported(stereo), "stereo in, stereo sidechain, stereo out");
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add(juce::AudioChannelSet::mono());
    mono.inputBuses.add(juce::AudioChannelSet::disabled());
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    require(processor.checkBusesLayoutSupported(mono), "mono in and out with no sidechain");
    juce::AudioProcessor::BusesLayout mismatched;
    mismatched.inputBuses.add(juce::AudioChannelSet::stereo());
    mismatched.inputBuses.add(juce::AudioChannelSet::stereo());
    mismatched.outputBuses.add(juce::AudioChannelSet::mono());
    require(!processor.checkBusesLayoutSupported(mismatched), "but not stereo in and mono out");
    require(processor.getBusCount(true) == 2 && processor.getBus(true, 1)->getName() == "Sidechain", "the second input is the sidechain");
}

void ducksThroughTheHostClock()
{
    Processor processor;
    TestPlayHead head;
    const auto out = run(processor, head, constant(cycle * 2));
    require(meanOf(out, cycle + 100, cycle + 600) < 0.05f, "the duck lands on the host's beat");
    require(out[20000] > 0.95f, "and the signal is open between beats");
    require(processor.engine().cycleCount() >= 2, "the engine counted the cycles");
}

void midiNotesTriggerWhenTheyMatch()
{
    const auto cut = [] (int channel, int note, int channelWanted, int noteWanted)
    {
        Processor processor;
        setValue(processor, id::trigger, 1.0f);
        setValue(processor, id::antiClick, 0.0f);
        setValue(processor, id::midiChannel, static_cast<float>(channelWanted));
        setValue(processor, id::midiNote, static_cast<float>(noteWanted));
        TestPlayHead head;
        head.playing = false;
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(channel, note, 0.8f), 1000);
        const auto out = run(processor, head, constant(6000), notes);
        return meanOf(out, 1100, 1500) < 0.05f;
    };
    require(cut(1, 60, 0, 0), "any note on any channel triggers by default");
    require(cut(2, 60, 2, 0), "a note on the chosen channel triggers");
    require(!cut(1, 60, 2, 0), "a note on another channel does not");
    require(cut(1, 60, 0, 61), "the chosen note triggers");
    require(!cut(1, 62, 0, 61), "another note does not");
}
}

void processorTests()
{
    parametersAreDeclared();
    stateRoundTrips();
    holdAndReleaseKeepToTheirRoom();
    attackIsKeptInMilliseconds();
    latencyFollowsTheTriggerMode();
    busLayoutsAreTheExpectedOnes();
    ducksThroughTheHostClock();
    midiNotesTriggerWhenTheyMatch();
}
}
