#include "ComputerKeyboard.h"

namespace rhino
{
namespace
{
struct KeyNote { int keyCode; int semitone; };

// A W S E D F T G Y H U J K O L P. The home row is the white keys and the row
// above it the black ones, so the layout reads as a keyboard. The semicolon
// Live also uses is left out deliberately: it is an OEM key whose code is not
// the character on every layout, and isKeyCurrentlyDown would miss it.
constexpr KeyNote keyNotes[] {
    {'A', 0}, {'W', 1}, {'S', 2}, {'E', 3}, {'D', 4}, {'F', 5}, {'T', 6}, {'G', 7},
    {'Y', 8}, {'H', 9}, {'U', 10}, {'J', 11}, {'K', 12}, {'O', 13}, {'L', 14}, {'P', 15}
};

constexpr int velocityStep = 10;

int semitoneFor(int keyCode)
{
    for (const auto& mapped : keyNotes)
        if (mapped.keyCode == keyCode)
            return mapped.semitone;
    return -1;
}
}

int ComputerKeyboard::noteFor(int semitone) const
{
    return juce::jlimit(0, 127, 12 * (baseOctave + 1) + semitone);
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
    return "A-P play, octave " + juce::String(baseOctave) + " (Z/X), velocity "
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
        for (const auto held_note : held)
            note(held_note, 0, false);
}

void ComputerKeyboard::refreshHeldNotes()
{
    std::set<int> wanted;
    if (enabled && !juce::ModifierKeys::getCurrentModifiers().isCommandDown()
        && !juce::ModifierKeys::getCurrentModifiers().isAltDown())
        for (const auto& mapped : keyNotes)
            if (juce::KeyPress::isKeyCurrentlyDown(mapped.keyCode))
                wanted.insert(noteFor(mapped.semitone));

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
    const auto mods = key.getModifiers();
    // A shortcut is a shortcut. Only unmodified keys ever become notes, so
    // Ctrl+C still copies while the keyboard is on.
    if (mods.isCommandDown() || mods.isAltDown())
        return false;
    const auto code = juce::CharacterFunctions::toUpperCase(
        static_cast<juce::juce_wchar>(key.getKeyCode() > 0 ? key.getKeyCode() : key.getTextCharacter()));
    // The toggle answers whether or not the keyboard is on; it is the only way
    // back out of it.
    if (code == 'M')
    {
        toggle();
        return true;
    }
    if (!enabled)
        return false;
    if (code == 'Z' || code == 'X')
    {
        const auto moved = juce::jlimit(lowestOctave, highestOctave, baseOctave + (code == 'X' ? 1 : -1));
        if (moved != baseOctave)
        {
            // Whatever is sounding was started in the octave being left, so it
            // is released rather than stranded a note that never ends.
            releaseAll();
            baseOctave = moved;
        }
        if (status) status(describe());
        return true;
    }
    if (code == 'C' || code == 'V')
    {
        noteVelocity = juce::jlimit(minimumVelocity, maximumVelocity,
                                    noteVelocity + (code == 'V' ? velocityStep : -velocityStep));
        if (status) status(describe());
        return true;
    }
    // Swallowed so the letter does not also reach the editor's own shortcut.
    // The note itself is started by keyStateChanged, which does not repeat.
    return semitoneFor(static_cast<int>(code)) >= 0;
}

bool ComputerKeyboard::keyStateChanged(bool, juce::Component*)
{
    if (!enabled && sounding.empty())
        return false;
    refreshHeldNotes();
    // Never claimed: a key state change is a notification, and other things -
    // the region drag that watches for a held S, for one - are entitled to it.
    return false;
}
}
