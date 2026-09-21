#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <set>

namespace rhino
{
// Playing notes from the typing keyboard, as Live's Computer MIDI Keyboard
// does and for the same reason: most of the time there is no controller
// plugged in, and a MIDI track you cannot play is a MIDI track you cannot
// record.
//
// The notes go into the engine's MIDI *input*, not into the track, so
// everything downstream treats them exactly as it treats a controller's:
// arming decides which track hears them, monitoring decides whether they are
// audible, and recording captures them. There is no second path and no special
// case anywhere in Session for where a note came from.
//
// It is a juce::KeyListener rather than a Component's own keyPressed because
// the keys have to be caught wherever the focus happens to be - the timeline,
// the note editor, the device rack - and a listener is consulted before the
// component's own handler. A text editor that has focus consumes its own keys
// and never reaches the listener, which is what keeps typing a track name from
// playing a chord.
//
// While it is on it takes the letter keys, so the plain-letter shortcuts those
// keys carry are unavailable until it is switched off again. That is the trade
// every DAW with this feature makes, and it is why the feature has a toggle.
class ComputerKeyboard final : public juce::KeyListener
{
public:
    // Live's layout: the home row is the white keys and the row above it the
    // black ones, so the shape of a piano is visible in the shape of the keys.
    // A through P is sixteen semitones, an octave and a fourth.
    static constexpr int lowestOctave = 0, highestOctave = 7;
    static constexpr int minimumVelocity = 1, maximumVelocity = 127;

    // Note on and note off, in MIDI note numbers.
    std::function<void(int note, int velocity, bool isNoteOn)> note;
    // What to say about a change the user just made.
    std::function<void(const juce::String&)> status;

    bool isEnabled() const { return enabled; }
    void setEnabled(bool);
    void toggle() { setEnabled(!enabled); }
    int octave() const { return baseOctave; }
    int velocity() const { return noteVelocity; }
    // What the status line and the menu say about it.
    juce::String describe() const;

    // Adds this as a key listener to a component that can hold focus while
    // notes are being played.
    void listenTo(juce::Component&);

    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

private:
    // Nothing may be left sounding when the keyboard is switched off, the
    // window loses focus, or the octave moves under a held key.
    void releaseAll();
    // Reads the keys that are physically down and makes the sounding notes
    // match. Driven by key state changes rather than by keyPressed, so a held
    // key does not retrigger on the system's auto-repeat.
    void refreshHeldNotes();
    int noteFor(int semitone) const;

    bool enabled = false;
    // Octave 3 puts the lowest key on C3, which is where the note editor's own
    // lowest row sits.
    int baseOctave = 3;
    int noteVelocity = 100;
    std::set<int> sounding;
};
}
