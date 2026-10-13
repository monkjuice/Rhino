#include "ComputerKeyboard.h"

namespace rhino
{
namespace
{
// juce::MidiKeyboardComponent's own default mapping, which is what Forge's
// on-screen keyboard plays from. Copying it rather than inventing one is the
// whole point: the same seventeen keys make the same notes in the plugin and
// in the host, so a part played into Forge's own window and a part recorded
// through Rhino cannot come out in different keys.
constexpr const char* noteKeys = "awsedftgyhujkolp;";
constexpr int noteKeyCount = 17;

constexpr int velocityStep = 10;

juce::KeyPress plain(char character)
{
    return {character, {}, 0};
}

int semitoneFor(const juce::KeyPress& key)
{
    for (int i = 0; i < noteKeyCount; ++i)
        if (key == plain(noteKeys[i]))
            return i;
    return -1;
}
}

int ComputerKeyboard::noteFor(int semitone) const
{
    // Forge's own arithmetic: MidiKeyboardComponent puts the first key at
    // 12 * baseOctave, so octave 5 starts on middle C.
    return juce::jlimit(0, 127, 12 * baseOctave + semitone);
}

void ComputerKeyboard::setEnabled(bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;
    enabled = shouldBeEnabled;
    if (!enabled)
        releaseAll();
    if (status)
        status(enabled ? "Computer keyboard on: " + describe()
                       : "Computer keyboard off. The letter keys are shortcuts again.");
}

juce::String ComputerKeyboard::describe() const
{
    return "A-; play, octave " + juce::String(baseOctave) + " (Z/X), velocity "
         + juce::String(noteVelocity) + " (C/V)";
}

void ComputerKeyboard::listenTo(juce::Component& component)
{
    component.addKeyListener(this);
}

void ComputerKeyboard::releaseAll()
{
    const auto held = sounding;
    sounding.clear();
    if (note)
        for (const auto heldNote : held)
            note(heldNote, 0, false);
}

void ComputerKeyboard::refreshHeldNotes()
{
    std::set<int> wanted;
    if (enabled)
        for (int i = 0; i < noteKeyCount; ++i)
            if (plain(noteKeys[i]).isCurrentlyDown())
                wanted.insert(noteFor(i));

    if (note)
    {
        for (const auto held : sounding)
            if (!wanted.contains(held))
                note(held, 0, false);
        for (const auto next : wanted)
            if (!sounding.contains(next))
                note(next, noteVelocity, true);
    }
    sounding = std::move(wanted);
}

bool ComputerKeyboard::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Every comparison below is juce::KeyPress's own, which is what Forge uses
    // and what keeps this honest about what was actually pressed. It requires
    // the modifiers to match exactly, so Ctrl+C still copies, and it refuses to
    // fold a key code above the ASCII range onto a letter - without that, F9
    // (0x10078) squeezed into a character is 'x', and the record key would
    // quietly shift the octave instead.
    if (key == plain('m'))
    {
        toggle();
        return true;
    }
    if (!enabled)
        return false;
    if (key == plain('z') || key == plain('x'))
    {
        const auto moved = juce::jlimit(lowestOctave, highestOctave,
                                        baseOctave + (key == plain('x') ? 1 : -1));
        if (moved != baseOctave)
        {
            // Whatever is sounding was started in the octave being left, so it
            // is released rather than stranded a note that never ends. Forge
            // does the same thing for the same reason.
            releaseAll();
            baseOctave = moved;
        }
        if (status) status(describe());
        return true;
    }
    if (key == plain('c') || key == plain('v'))
    {
        noteVelocity = juce::jlimit(minimumVelocity, maximumVelocity,
                                    noteVelocity + (key == plain('v') ? velocityStep : -velocityStep));
        if (status) status(describe());
        return true;
    }
    // Swallowed so the letter does not also reach the editor's own shortcut.
    // The note itself is started by keyStateChanged, which does not repeat.
    return semitoneFor(key) >= 0;
}

bool ComputerKeyboard::keyStateChanged(bool, juce::Component*)
{
    if (!enabled && sounding.empty())
        return false;
    refreshHeldNotes();
    // Never claimed: a key state change is a notification, and anything else
    // watching for a held key is entitled to see it too.
    return false;
}
}
