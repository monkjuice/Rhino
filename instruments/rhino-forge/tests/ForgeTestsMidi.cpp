// Binding a controller's knobs and pads to the panel.
//
// Most of this needs no Processor. `MidiMap` is a table and a learn latch, and
// both can be exercised by calling them — which is the point of it being a type
// of its own rather than a handful of members on the Processor. The suites at
// the end go through processBlock instead, because the things lambdas cannot
// check are the ones that matter most here: that a bound message is taken out
// of the stream before the voices see it, and that a value written on the audio
// thread reaches its parameter on the other side.
#include "ForgeTestSupport.h"

#include <iostream>

namespace rhino::forge::tests
{
namespace
{
constexpr int knobOne = Processor::firstDefaultMacroCc;

MidiSource cc(int number, int channel = 1)
{
    return { MidiSourceKind::controller, channel, number };
}

MidiSource pad(int note, int channel = 1)
{
    return { MidiSourceKind::note, channel, note };
}

// A scratch bindings file, so nothing here writes over the bindings belonging
// to whoever is running the tests. Redirected once, for the whole file.
juce::File scratchMapFile()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("RhinoForgeTests")
        .getChildFile("MidiMap.xml");
}

// One block of audio with a MIDI buffer in front of it, and then the crossing
// drained by hand. Sixty-fourth of a second of silence is plenty: nothing here
// listens to the audio, only to what the parameters did.
void deliver(Processor& processor, const juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> buffer(2, 64);
    auto copy = midi;
    processor.processBlock(buffer, copy);
    processor.drainMidiControl();
}

juce::MidiBuffer controllerMessage(int number, int value, int channel = 1)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(channel, number, value), 0);
    return midi;
}

juce::MidiBuffer padPress(int note, int velocity = 127, int channel = 1)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity)), 0);
    return midi;
}

juce::MidiBuffer padRelease(int note, int channel = 1)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
    return midi;
}

// --- The table -----------------------------------------------------------------

void bindingSuite()
{
    MidiMap map;
    require(map.target(cc(21)) == MidiMap::unbound, "an empty map binds nothing");
    require(map.boundCount() == 0, "an empty map counts nothing");

    map.bind(cc(21), 7);
    require(map.target(cc(21)) == 7, "a bound controller answers its parameter");
    require(map.isBound(cc(21)), "a bound controller reads as bound");

    // The three axes a source is distinguished on. Each of these is a different
    // physical control on the desk, and confusing any two of them would make a
    // pad on channel 10 answer to a key on channel 1.
    require(map.target(cc(22)) == MidiMap::unbound, "a different number is a different source");
    require(map.target(cc(21, 2)) == MidiMap::unbound, "a different channel is a different source");
    require(map.target(pad(21)) == MidiMap::unbound, "a note is not the controller of that number");

    map.bind(pad(21), 9);
    require(map.target(cc(21)) == 7 && map.target(pad(21)) == 9,
            "a note and a controller of the same number are held apart");

    map.forget(cc(21));
    require(map.target(cc(21)) == MidiMap::unbound, "forgetting a source unbinds it");
    require(map.target(pad(21)) == 9, "forgetting one source leaves the other");
}

void bindingsPerParameterSuite()
{
    MidiMap map;
    map.bind(cc(21), 4);
    map.bind(cc(30), 4);
    map.bind(cc(40), 5);
    require(map.boundCount() == 3, "every binding is counted");

    // A control can be reached from two knobs, and taking it off the panel has
    // to take both away — otherwise the second one goes on driving a control
    // that now says it is unbound.
    map.forgetParameter(4);
    require(map.target(cc(21)) == MidiMap::unbound && map.target(cc(30)) == MidiMap::unbound,
            "forgetting a parameter drops every source pointed at it");
    require(map.target(cc(40)) == 5, "forgetting a parameter leaves the others alone");

    const auto found = map.sourceFor(5);
    require(found.valid() && found == cc(40), "a parameter reports the source driving it");
    require(!map.sourceFor(99).valid(), "a parameter nothing drives reports no source");

    map.clear();
    require(map.boundCount() == 0, "clearing drops everything");
}

void learnSuite()
{
    MidiMap map;
    MidiSource learned;
    require(!map.isLearning(), "a map is not learning until it is armed");
    require(!map.completeLearn(learned), "an unarmed map learns nothing");

    map.arm(3);
    require(map.isLearning() && map.learningParameter() == 3, "arming reports what is armed");
    require(!map.completeLearn(learned), "an armed map waits until something moves");

    map.notice(cc(74));
    require(map.completeLearn(learned), "the first thing that moves completes the learn");
    require(learned == cc(74), "the learn reports what it bound");
    require(map.target(cc(74)) == 3, "the learned source drives the armed parameter");
    require(!map.isLearning(), "a completed learn disarms");

    // A knob nudged back to a value it has already sent is still a knob that
    // moved. The count is what makes that a move; comparing the sources alone
    // would have this learn nothing at all.
    map.arm(8);
    map.notice(cc(74));
    require(map.completeLearn(learned) && learned == cc(74),
            "a source already seen can still complete a later learn");
    require(map.target(cc(74)) == 8, "learning the same source again moves it");

    map.arm(1);
    map.cancelLearn();
    map.notice(cc(50));
    require(!map.completeLearn(learned), "a cancelled learn binds nothing");
    require(map.target(cc(50)) == MidiMap::unbound, "a cancelled learn leaves the source free");
}

void learnMovesRatherThanSharesSuite()
{
    // A source already pointed somewhere is moved, not shared.
    MidiMap map;
    map.bind(cc(21), 2);
    MidiSource learned;
    map.arm(6);
    map.notice(cc(21));
    require(map.completeLearn(learned), "a source already bound can be learned again");
    require(map.target(cc(21)) == 6, "learning again repoints the source");
    require(!map.sourceFor(2).valid(), "the control it used to drive is left with nothing");

    // And a control already driven by something lets go of it. This is the half
    // the panel promises when its menu offers to replace what is there: a
    // control with two masters jumps when either moves, and nothing on the panel
    // would say which one did.
    MidiMap second;
    second.bind(cc(24), 4);
    second.arm(4);
    second.notice(cc(74));
    require(second.completeLearn(learned) && learned == cc(74), "the new source is learned");
    require(second.target(cc(74)) == 4, "and drives the control");
    require(second.target(cc(24)) == MidiMap::unbound, "the source that used to drive it is dropped");
    require(second.boundCount() == 1, "which leaves exactly one binding");
}

void queueSuite()
{
    MidiControlQueue queue;
    MidiControlEvent event;
    require(!queue.pop(event), "an empty queue hands back nothing");

    require(queue.push({cc(21), 64, false}), "a push into an empty queue is taken");
    require(queue.pop(event), "what was pushed comes back");
    require(event.source == cc(21) && event.value == 64 && !event.release,
            "what comes back is what went in");
    require(!queue.pop(event), "a drained queue hands back nothing");

    // A pad tapped and released between two ticks has to arrive as two events.
    // Publishing a last-value instead would lose the press entirely, which on a
    // switch is the difference between flipping it and doing nothing.
    queue.push({pad(40), 100, false});
    queue.push({pad(40), 0, true});
    require(queue.pop(event) && !event.release, "the press arrives first");
    require(queue.pop(event) && event.release, "the release arrives after it");

    // Full is a dropped event and not a stall. The audio thread may not wait
    // for anything, so the only other option would be blocking.
    MidiControlQueue full;
    auto taken = 0;
    for (unsigned int i = 0; i < MidiControlQueue::capacity + 4; ++i)
        if (full.push({cc(21), 1, false})) ++taken;
    require(taken == static_cast<int>(MidiControlQueue::capacity) - 1,
            "the queue fills and then refuses rather than overwriting");
}

// --- Through the Processor ------------------------------------------------------
//
// Every Processor here shares one bindings file, and several of these checks
// change it. So each suite says what it wants rather than inheriting whatever
// the one before it left behind: `startClean` for the defaults a fresh machine
// gets, and `clearMidiMap` on top of it for a blank map. A suite that read the
// leftovers would pass or fail on the order the runner happened to use.
void startClean() { scratchMapFile().deleteFile(); }

// Arms a control and lets the controller move once, which is what binds it. The
// moving message is deliberately not the one under test: it arrives before the
// binding exists, so it falls through to the voices exactly as an unbound
// message does, and the check that follows it is the first bound one.
void learnFrom(Processor& processor, const char* parameterId, const juce::MidiBuffer& from)
{
    processor.learnMidi(parameterId);
    deliver(processor, from);
}

void defaultMapSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    // Eight knobs onto eight macros is the fit that prompted the feature, and it
    // is what a machine with no bindings file starts with.
    for (int macro = 0; macro < macroCount; ++macro)
    {
        const auto id = "macro" + juce::String(macro + 1);
        requireText(processor.midiSourceLabel(id), "CC " + juce::String(knobOne + macro),
                    "each macro starts on its own knob");
    }
}

void controllerDrivesControlSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    setValue(processor, "macro1", 0.0f);

    deliver(processor, controllerMessage(knobOne, 127));
    requireClose(value(processor, "macro1"), 1.0f, 0.001f, "a knob at full drives its macro to full");

    deliver(processor, controllerMessage(knobOne, 0));
    requireClose(value(processor, "macro1"), 0.0f, 0.001f, "a knob at zero drives it back");

    // 64 of 127 is not quite half, and saying so is the point: the value is the
    // controller's own, not a rounded idea of where the knob looks.
    deliver(processor, controllerMessage(knobOne, 64));
    requireClose(value(processor, "macro1"), 64.0f / 127.0f, 0.001f,
                 "a knob part way drives it part way");
}

void boundControllerIsConsumedSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    processor.clearMidiMap();

    // CC 1 is the mod wheel everywhere until somebody binds it. Once bound it
    // belongs to what it was bound to and the wheel stops following it — a
    // controller doing two things at once is the bug this guards.
    deliver(processor, controllerMessage(1, 127));
    require(processor.modWheelValue() == 127, "an unbound CC 1 still moves the mod wheel");

    learnFrom(processor, "macro2", controllerMessage(1, 100));
    requireText(processor.midiSourceLabel("macro2"), "CC 1", "CC 1 can be learned like any other");
    // The message that did the learning reached the wheel on its way past,
    // because nothing was bound yet when it arrived.
    require(processor.modWheelValue() == 100, "the learning message itself was not consumed");

    processor.setModWheel(0);
    deliver(processor, controllerMessage(1, 127));
    require(processor.modWheelValue() == 0, "a bound CC 1 no longer reaches the mod wheel");
    requireClose(value(processor, "macro2"), 1.0f, 0.001f, "it reaches what it was bound to instead");
}

void padFlipsASwitchSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    processor.clearMidiMap();

    // A pad pointed at a switch flips it and leaves it flipped. Reading that off
    // the parameter rather than off a mode setting is what lets the same pad be
    // moved onto a knob and behave differently without being told to.
    const auto before = value(processor, "arpEnable");
    learnFrom(processor, "arpEnable", padPress(40));
    deliver(processor, padRelease(40));
    requireText(processor.midiSourceLabel("arpEnable"), "Note E1", "a pad can be learned");
    require(value(processor, "arpEnable") == before, "learning it did not already flip it");

    deliver(processor, padPress(40));
    require(value(processor, "arpEnable") != before, "a pad press flips the switch");
    deliver(processor, padRelease(40));
    require(value(processor, "arpEnable") != before, "releasing it leaves the switch flipped");
    deliver(processor, padPress(40));
    require(value(processor, "arpEnable") == before, "a second press flips it back");

    // The panel's own keyboard sends the same message a pad does — channel 1,
    // the same note number — so it has to be merged in after the map has taken
    // the bound messages out, or clicking E1 on screen would flip this switch.
    processor.keyboardState.noteOn(1, 40, 1.0f);
    deliver(processor, {});
    processor.keyboardState.noteOff(1, 40, 0.0f);
    deliver(processor, {});
    require(value(processor, "arpEnable") == before,
            "the panel's own keyboard is not read as the pad bound to that note");
}

void padHoldsAKnobSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    processor.clearMidiMap();
    setValue(processor, "macro3", 0.25f);

    learnFrom(processor, "macro3", padPress(44, 100));
    deliver(processor, padRelease(44));
    requireClose(value(processor, "macro3"), 0.25f, 0.01f, "learning it did not already move it");

    deliver(processor, padPress(44, 127));
    requireClose(value(processor, "macro3"), 1.0f, 0.01f,
                 "a pad pointed at a knob holds it up while it is held");
    deliver(processor, padRelease(44));
    requireClose(value(processor, "macro3"), 0.25f, 0.01f,
                 "releasing the pad puts the knob back where it was");

    // Velocity is the only thing a pad says beyond "pressed", so it is what
    // decides how far the control goes.
    deliver(processor, padPress(44, 64));
    requireClose(value(processor, "macro3"), 64.0f / 127.0f, 0.01f, "a softer hit moves it less far");
    deliver(processor, padRelease(44));
    requireClose(value(processor, "macro3"), 0.25f, 0.01f, "and it still comes back");
}

void forgettingSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    setValue(processor, "macro1", 0.0f);

    processor.forgetMidi("macro1");
    requireText(processor.midiSourceLabel("macro1"), "", "a forgotten control reports no source");
    deliver(processor, controllerMessage(knobOne, 127));
    requireClose(value(processor, "macro1"), 0.0f, 0.001f, "and its knob no longer reaches it");
}

void persistenceSuite()
{
    startClean();
    const auto file = scratchMapFile();

    {
        Processor processor;
        processor.prepareToPlay(44100.0, 64);
        processor.forgetMidi("macro1");
        learnFrom(processor, "macro4", controllerMessage(74, 10));
        requireText(processor.midiSourceLabel("macro4"), "CC 74", "the learn landed before saving");
    }

    require(file.existsAsFile(), "changing a binding writes the file");

    Processor reopened;
    reopened.prepareToPlay(44100.0, 64);
    requireText(reopened.midiSourceLabel("macro4"), "CC 74", "a binding survives the instance");
    // The defaults go on a machine with no file at all. A file that exists and
    // does not mention macro 1 is somebody who took that binding off, and
    // putting it back for them would make it impossible to remove.
    requireText(reopened.midiSourceLabel("macro1"), "",
                "a removed default stays removed once there is a file");
}

void channelsAreSeparateSuite()
{
    startClean();
    Processor processor;
    processor.prepareToPlay(44100.0, 64);
    processor.clearMidiMap();
    setValue(processor, "macro5", 0.0f);
    setValue(processor, "macro6", 0.0f);

    // The same controller number on two channels is two controls, which is what
    // a keyboard sending its pads on channel 10 and its knobs on channel 1
    // depends on.
    learnFrom(processor, "macro5", controllerMessage(80, 1, 1));
    learnFrom(processor, "macro6", controllerMessage(80, 1, 10));
    requireText(processor.midiSourceLabel("macro5"), "CC 80",
                "channel 1 is printed without its channel");
    requireText(processor.midiSourceLabel("macro6"), "CC 80 ch 10", "another channel says which");

    deliver(processor, controllerMessage(80, 127, 10));
    requireClose(value(processor, "macro6"), 1.0f, 0.001f, "the tenth channel drives its own control");
    requireClose(value(processor, "macro5"), 0.0f, 0.001f, "and leaves the first channel's alone");
}
}

void midiTests()
{
    std::cout << "MIDI learn\n";
    // Every Processor below reads and writes bindings, so the file they share
    // is moved out of the way first — for the whole area, since the redirect is
    // one location per process.
    Processor::setMidiMapFile(scratchMapFile());
    scratchMapFile().deleteFile();

    bindingSuite();
    bindingsPerParameterSuite();
    learnSuite();
    learnMovesRatherThanSharesSuite();
    queueSuite();
    defaultMapSuite();
    controllerDrivesControlSuite();
    boundControllerIsConsumedSuite();
    padFlipsASwitchSuite();
    padHoldsAKnobSuite();
    forgettingSuite();
    persistenceSuite();
    channelsAreSeparateSuite();

    scratchMapFile().deleteFile();
    scratchMapFile().getParentDirectory().deleteRecursively();
}
}
