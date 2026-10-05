#include "ForgeProcessor.h"

// The message thread's half of MIDI learn: arming a control, completing a
// learn, applying what the audio thread handed over, and keeping the bindings
// on disk. The audio thread's half is ForgeMidiMap.h, and the one place the two
// meet is consumedByMidiMap, which runs inside processBlock.
namespace rhino::forge
{
namespace
{
const juce::String mapTag("MIDIMAP");
const juce::String bindingTag("BINDING");
const juce::String noteKind("note");
const juce::String controllerKind("cc");

// Empty means the ordinary place. Held here rather than as a class member so
// that redirecting it costs the Processor no state at all: it is one location
// per process, which is what it already was.
juce::File& midiMapOverride()
{
    static juce::File redirected;
    return redirected;
}
}

void Processor::setMidiMapFile(const juce::File& file) { midiMapOverride() = file; }

// Takes the bound messages out of a block's MIDI and leaves the rest in place.
// Runs on the audio thread and is called once per block rather than once per
// sample. The survivors are gathered in midiKept, which prepareToPlay sized, and
// copied back only when something was taken out. The host's buffer is cleared
// and refilled rather than swapped for another, so it keeps its own storage: a
// swap allocated a buffer every block and left the host's to be freed here.
void Processor::applyMidiMap(juce::MidiBuffer& midi)
{
    // Nothing bound means nothing to filter.
    if (midiMap.boundCount() == 0 && !midiMap.isLearning()) return;

    midiKept.clear();
    auto consumed = false;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (consumedByMidiMap(message)) consumed = true;
        else midiKept.addEvent(message, metadata.samplePosition);
    }
    if (!consumed) return;
    midi.clear();
    midi.addEvents(midiKept, 0, -1, 0);
}

bool Processor::consumedByMidiMap(const juce::MidiMessage& message)
{
    const auto isController = message.isController();
    if (!isController && !message.isNoteOnOrOff()) return false;

    const MidiSource source {
        isController ? MidiSourceKind::controller : MidiSourceKind::note,
        message.getChannel(),
        isController ? message.getControllerNumber() : message.getNoteNumber()
    };

    // Only a press arms a binding. A key lifting is not a choice of control,
    // and learning from it would bind whichever pad the hand happened to leave
    // last rather than the one it meant.
    if (midiMap.isLearning() && (isController || message.isNoteOn())) midiMap.notice(source);
    if (!midiMap.isBound(source)) return false;

    midiQueue.push({ source,
                     isController ? message.getControllerValue() : message.getVelocity(),
                     !isController && !message.isNoteOn() });

    // A bound controller or pad press is taken out of the stream: it drives its
    // control and does not also reach the wheels, the arp or the voices, because
    // a pad that flipped a switch and sounded a note at the same time is two
    // instruments at once.
    //
    // A release is the exception and is deliberately left in. A pad bound while
    // it was already held down has a note sounding that this never consumed, and
    // swallowing the note-off that belongs to it would hang that note forever.
    // Passing it through costs nothing in the ordinary case: the voice it would
    // stop was never started, and stopping a note that is not sounding does
    // nothing.
    return isController || message.isNoteOn();
}

void Processor::learnMidi(const juce::String& parameterId)
{
    const auto index = parameterIndexFor(parameterId);
    if (index >= 0) midiMap.arm(index);
}

void Processor::cancelMidiLearn() { midiMap.cancelLearn(); }

juce::String Processor::midiLearnTarget() const { return parameterIdAt(midiMap.learningParameter()); }

juce::String Processor::midiSourceLabel(const juce::String& parameterId) const
{
    const auto index = parameterIndexFor(parameterId);
    if (index < 0) return {};
    const auto source = midiMap.sourceFor(index);
    return source.valid() ? source.label() : juce::String();
}

void Processor::forgetMidi(const juce::String& parameterId)
{
    const auto index = parameterIndexFor(parameterId);
    if (index < 0) return;
    midiMap.forgetParameter(index);
    saveMidiMap();
}

void Processor::clearMidiMap()
{
    midiMap.clear();
    saveMidiMap();
}

void Processor::applyDefaultMidiMap()
{
    for (int macro = 0; macro < macroCount; ++macro)
    {
        const MidiSource source { MidiSourceKind::controller, 1, firstDefaultMacroCc + macro };
        // Never over something already there. This runs on a machine with no
        // bindings file and from the panel's menu, and in both cases a binding
        // that exists was made deliberately and outranks a default.
        if (midiMap.isBound(source)) continue;
        const auto index = parameterIndexFor("macro" + juce::String(macro + 1));
        if (index >= 0) midiMap.bind(source, index);
    }
}

void Processor::timerCallback() { drainMidiControl(); }

void Processor::drainMidiControl()
{
    MidiSource learned;
    if (midiMap.completeLearn(learned)) saveMidiMap();

    const auto& all = getParameters();
    MidiControlEvent event;
    while (midiQueue.pop(event))
    {
        const auto index = midiMap.target(event.source);
        if (index < 0 || index >= all.size()) continue;
        auto* parameter = all[index];
        if (parameter == nullptr) continue;

        // A controller is a position, so its value goes straight onto the
        // control's travel. There are no gesture brackets around it: those tell
        // a host that a hand is on the control, and a pair of them around every
        // one of the hundred-odd values a knob sweeps through is noise in an
        // automation lane rather than information.
        if (event.source.kind == MidiSourceKind::controller)
        {
            parameter->setValueNotifyingHost(static_cast<float>(event.value) / 127.0f);
            continue;
        }

        // A pad is a press, and what a press means depends on what it is
        // pointed at — the two useful answers are genuinely different controls.
        // A switch wants flipping and leaving flipped. A knob wants holding up
        // for as long as the pad is held, and putting back afterwards. Reading
        // that off the parameter rather than off a mode setting means a pad
        // moved from one to the other does the right thing without being told.
        if (parameter->isDiscrete())
        {
            if (event.release) continue;
            const auto steps = juce::jmax(2, parameter->getNumSteps());
            const auto current = juce::roundToInt(parameter->getValue() * static_cast<float>(steps - 1));
            parameter->setValueNotifyingHost(static_cast<float>((current + 1) % steps)
                                             / static_cast<float>(steps - 1));
            continue;
        }

        if (event.release)
        {
            const auto held = heldByPad.find(index);
            if (held == heldByPad.end()) continue;
            parameter->setValueNotifyingHost(held->second);
            heldByPad.erase(held);
            continue;
        }

        // Velocity is the only thing a pad says beyond "pressed", so it is what
        // decides how far the control goes: a soft hit is a nudge and a hard one
        // is the whole travel.
        heldByPad.insert_or_assign(index, parameter->getValue());
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f,
                                                      static_cast<float>(event.value) / 127.0f));
    }
}

int Processor::parameterIndexFor(const juce::String& parameterId) const
{
    if (parameterId.isEmpty()) return -1;
    const auto& all = getParameters();
    for (int index = 0; index < all.size(); ++index)
        if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(all[index]))
            if (withId->paramID == parameterId) return index;
    return -1;
}

juce::String Processor::parameterIdAt(int index) const
{
    const auto& all = getParameters();
    if (index < 0 || index >= all.size()) return {};
    if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(all[index]))
        return withId->paramID;
    return {};
}

juce::File Processor::midiMapFile()
{
    if (midiMapOverride() != juce::File()) return midiMapOverride();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Rhino Forge")
        .getChildFile("MidiMap.xml");
}

void Processor::loadMidiMap()
{
    midiMap.clear();
    const auto file = midiMapFile();
    const auto root = file.existsAsFile() ? juce::parseXML(file) : nullptr;
    // No file at all is a machine that has never bound anything, which is the
    // one moment the eight-knobs-to-eight-macros default is written. A file that
    // exists and binds nothing is a person who took the defaults off, and that
    // is left exactly as they left it.
    if (root == nullptr || !root->hasTagName(mapTag))
    {
        applyDefaultMidiMap();
        return;
    }

    for (const auto* binding : root->getChildWithTagNameIterator(bindingTag))
    {
        const MidiSource source {
            binding->getStringAttribute("kind") == noteKind ? MidiSourceKind::note
                                                            : MidiSourceKind::controller,
            binding->getIntAttribute("channel", 1),
            binding->getIntAttribute("number", -1)
        };
        // A binding naming a control this build no longer has is dropped rather
        // than kept as ballast, for the same reason a preset drops a retired
        // parameter: there is nothing for it to point at, and a file full of
        // names nothing resolves is a file nobody can read.
        const auto index = parameterIndexFor(binding->getStringAttribute("parameter"));
        if (source.valid() && index >= 0) midiMap.bind(source, index);
    }
}

void Processor::saveMidiMap() const
{
    juce::XmlElement root(mapTag);
    midiMap.forEachBinding([this, &root] (const MidiSource& source, int index)
    {
        const auto id = parameterIdAt(index);
        if (id.isEmpty()) return;
        auto* binding = root.createNewChildElement(bindingTag);
        binding->setAttribute("kind", source.kind == MidiSourceKind::note ? noteKind : controllerKind);
        binding->setAttribute("channel", source.channel);
        binding->setAttribute("number", source.number);
        binding->setAttribute("parameter", id);
    });

    const auto file = midiMapFile();
    file.getParentDirectory().createDirectory();
    root.writeTo(file);
}
}
