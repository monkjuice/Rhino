#pragma once

#include "ForgeStyle.h"

// The note guide is deliberately UI-only: it classifies keys for the player,
// but it never changes MIDI or the voice engine.
namespace rhino::forge::ui
{
enum class GuideChord { off, triad, sixth, seventh, maj7, augmented, diminished, aug7, dim7, halfDiminished, sus2, sus4 };
enum class GuideScale { off, major, naturalMinor, majorPentatonic, minorPentatonic, blues, harmonicMinor, melodicMinor, dorian, phrygian, lydian, mixolydian, locrian };

struct NoteGuideState
{
    int root = -1;
    GuideChord chord = GuideChord::off;
    bool minor = false;
    GuideScale scale = GuideScale::off;
};

enum class GuideTone { normal, scale, chord, root };

inline constexpr std::array<const char*, 12> guideRootNames {
    "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"
};
inline constexpr std::array<const char*, 12> guideSharpNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
inline constexpr std::array<const char*, 12> guideChordMenuNames {
    "Off", "Triad", "6th", "7th", "Maj7", "Augmented", "Diminished", "Aug7 (7#5)", "Dim7", "Half-Diminished (m7b5)", "Sus2", "Sus4"
};
inline constexpr std::array<const char*, 13> guideScaleMenuNames {
    "Off", "Major", "Natural Minor", "Major Pentatonic", "Minor Pentatonic", "Blues (Minor Blues)", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian"
};

inline constexpr bool guideChordHasQuality(GuideChord chord)
{
    return chord == GuideChord::triad || chord == GuideChord::sixth
        || chord == GuideChord::seventh || chord == GuideChord::maj7;
}

inline unsigned guideMask(std::initializer_list<int> intervals)
{
    unsigned mask = 0;
    for (const auto interval : intervals) mask |= 1u << (interval % 12);
    return mask;
}

inline unsigned guideChordMask(GuideChord chord, bool minor)
{
    switch (chord)
    {
        case GuideChord::triad:          return guideMask(minor ? std::initializer_list<int>{0, 3, 7} : std::initializer_list<int>{0, 4, 7});
        case GuideChord::sixth:          return guideMask(minor ? std::initializer_list<int>{0, 3, 7, 9} : std::initializer_list<int>{0, 4, 7, 9});
        case GuideChord::seventh:        return guideMask(minor ? std::initializer_list<int>{0, 3, 7, 10} : std::initializer_list<int>{0, 4, 7, 10});
        case GuideChord::maj7:           return guideMask(minor ? std::initializer_list<int>{0, 3, 7, 11} : std::initializer_list<int>{0, 4, 7, 11});
        case GuideChord::augmented:      return guideMask({0, 4, 8});
        case GuideChord::diminished:     return guideMask({0, 3, 6});
        case GuideChord::aug7:           return guideMask({0, 4, 8, 10});
        case GuideChord::dim7:           return guideMask({0, 3, 6, 9});
        case GuideChord::halfDiminished: return guideMask({0, 3, 6, 10});
        case GuideChord::sus2:           return guideMask({0, 2, 7});
        case GuideChord::sus4:           return guideMask({0, 5, 7});
        case GuideChord::off:            break;
    }
    return 0;
}

inline unsigned guideScaleMask(GuideScale scale)
{
    switch (scale)
    {
        case GuideScale::major:           return guideMask({0, 2, 4, 5, 7, 9, 11});
        case GuideScale::naturalMinor:    return guideMask({0, 2, 3, 5, 7, 8, 10});
        case GuideScale::majorPentatonic: return guideMask({0, 2, 4, 7, 9});
        case GuideScale::minorPentatonic: return guideMask({0, 3, 5, 7, 10});
        case GuideScale::blues:           return guideMask({0, 3, 5, 6, 7, 10});
        case GuideScale::harmonicMinor:   return guideMask({0, 2, 3, 5, 7, 8, 11});
        case GuideScale::melodicMinor:    return guideMask({0, 2, 3, 5, 7, 9, 11});
        case GuideScale::dorian:          return guideMask({0, 2, 3, 5, 7, 9, 10});
        case GuideScale::phrygian:        return guideMask({0, 1, 3, 5, 7, 8, 10});
        case GuideScale::lydian:          return guideMask({0, 2, 4, 6, 7, 9, 11});
        case GuideScale::mixolydian:      return guideMask({0, 2, 4, 5, 7, 9, 10});
        case GuideScale::locrian:         return guideMask({0, 1, 3, 5, 6, 8, 10});
        case GuideScale::off:             break;
    }
    return 0;
}

inline GuideTone guideTone(const NoteGuideState& state, int midiNote)
{
    if (state.root < 0) return GuideTone::normal;
    const auto relative = (midiNote % 12 - state.root + 12) % 12;
    if (relative == 0) return GuideTone::root;
    if ((guideChordMask(state.chord, state.minor) & (1u << relative)) != 0) return GuideTone::chord;
    if ((guideScaleMask(state.scale) & (1u << relative)) != 0) return GuideTone::scale;
    return GuideTone::normal;
}

// One colour per kind of note, the same on the piano, on the guide's own
// octave, on its buttons and in its readout, so each is the legend for the
// others. Notes rather than chrome, which is why they are the same in every
// theme.
inline juce::Colour guideToneColour(GuideTone tone)
{
    switch (tone)
    {
        case GuideTone::root:   return juce::Colour(0xfff0b35a);
        case GuideTone::chord:  return juce::Colour(0xffe78270);
        case GuideTone::scale:  return juce::Colour(0xff67b7af);
        case GuideTone::normal: break;
    }
    return juce::Colours::transparentBlack;
}

// What a key is labelled with. Every C carries its octave, guide or no guide,
// because a C is how a player finds their place on a keyboard this long; with a
// root chosen the other keys are named too, as the guide colours them. The
// octave is counted the way the keyboard counts it: with middle C, MIDI 60, as
// C3 that is octave 5 of MIDI's own count, less two.
inline juce::String guideKeyLabel(int midiNote, bool guideOn, int octaveForMiddleC)
{
    const auto pitch = ((midiNote % 12) + 12) % 12;
    if (pitch == 0) return "C" + juce::String(midiNote / 12 + octaveForMiddleC - 5);
    return guideOn ? juce::String(guideSharpNames[static_cast<size_t>(pitch)]) : juce::String();
}

// The chord as a chord symbol: Cmaj7, Am, F#m7b5. The quality switch reads as
// the symbols do - MAJ leaves a 7th dominant, MIN makes it m7 - and the chords
// that have no quality spell their own.
inline juce::String guideChordSymbol(const NoteGuideState& state)
{
    if (state.root < 0 || state.chord == GuideChord::off) return {};
    const juce::String root = guideSharpNames[static_cast<size_t>(state.root)];
    const auto minor = state.minor ? "m" : "";
    switch (state.chord)
    {
        case GuideChord::triad:          return root + minor;
        case GuideChord::sixth:          return root + minor + "6";
        case GuideChord::seventh:        return root + minor + "7";
        case GuideChord::maj7:           return root + (state.minor ? "m(maj7)" : "maj7");
        case GuideChord::augmented:      return root + "aug";
        case GuideChord::diminished:     return root + "dim";
        case GuideChord::aug7:           return root + "7#5";
        case GuideChord::dim7:           return root + "dim7";
        case GuideChord::halfDiminished: return root + "m7b5";
        case GuideChord::sus2:           return root + "sus2";
        case GuideChord::sus4:           return root + "sus4";
        case GuideChord::off:            break;
    }
    return {};
}

// The chord said in words, under its symbol.
inline juce::String guideChordDescription(const NoteGuideState& state)
{
    switch (state.chord)
    {
        case GuideChord::triad:          return state.minor ? "MINOR TRIAD" : "MAJOR TRIAD";
        case GuideChord::sixth:          return state.minor ? "MINOR SIXTH" : "MAJOR SIXTH";
        case GuideChord::seventh:        return state.minor ? "MINOR SEVENTH" : "DOMINANT SEVENTH";
        case GuideChord::maj7:           return state.minor ? "MINOR-MAJOR SEVENTH" : "MAJOR SEVENTH";
        case GuideChord::augmented:      return "AUGMENTED";
        case GuideChord::diminished:     return "DIMINISHED";
        case GuideChord::aug7:           return "AUGMENTED SEVENTH";
        case GuideChord::dim7:           return "DIMINISHED SEVENTH";
        case GuideChord::halfDiminished: return "HALF-DIMINISHED";
        case GuideChord::sus2:           return "SUSPENDED SECOND";
        case GuideChord::sus4:           return "SUSPENDED FOURTH";
        case GuideChord::off:            break;
    }
    return {};
}

// What a note is to the chord or scale it is in, counted from the root: R 3 5
// b7 for a chord, 1 2 b3 4 for a scale. Spelt by what the set is rather than by
// the distance alone, so a diminished seventh's top is bb7 and not 6, an
// augmented fifth is #5 and not b6, and a scale with a raised fourth instead of
// a fourth (Lydian) says #4 where one that keeps its fourth (Blues, Locrian)
// says b5.
inline juce::String guideDegreeName(int semitones, unsigned mask, bool chord)
{
    const auto has = [mask] (int interval) { return (mask & (1u << interval)) != 0; };
    switch (semitones)
    {
        case 0:  return chord ? "R" : "1";
        case 1:  return "b2";
        case 2:  return "2";
        case 3:  return "b3";
        case 4:  return "3";
        case 5:  return "4";
        case 6:  return chord || has(5) ? "b5" : "#4";
        case 7:  return "5";
        case 8:  return chord && has(4) && !has(7) ? "#5" : "b6";
        case 9:  return chord && has(6) && has(3) && !has(10) ? "bb7" : "6";
        case 10: return "b7";
        case 11: return "7";
        default: break;
    }
    return {};
}

// One note of a chord or a scale as the readout lists it: which key, what it is
// to the root, and which colour it wears on the keys.
struct GuideNote
{
    int pitch = 0;
    juce::String degree;
    GuideTone tone = GuideTone::normal;
};

inline std::vector<GuideNote> guideNotes(const NoteGuideState& state, bool chord)
{
    std::vector<GuideNote> notes;
    if (state.root < 0) return notes;
    const auto mask = chord ? guideChordMask(state.chord, state.minor) : guideScaleMask(state.scale);
    for (int semitones = 0; semitones < 12; ++semitones)
    {
        if ((mask & (1u << semitones)) == 0) continue;
        const auto pitch = (state.root + semitones) % 12;
        notes.push_back({pitch, guideDegreeName(semitones, mask, chord), guideTone(state, pitch)});
    }
    return notes;
}

class NoteGuideKeyboard final : public juce::MidiKeyboardComponent
{
public:
    NoteGuideKeyboard(juce::MidiKeyboardState& state, const NoteGuideState& guide)
        : juce::MidiKeyboardComponent(state, horizontalKeyboard), guideState(guide) {}

    void drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over,
                       juce::Colour lineColour, juce::Colour textColour) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote(note, g, area, down, over, lineColour, textColour.withAlpha(0.0f));
        drawGuide(note, g, area, down, false);
    }

    void drawBlackNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over,
                       juce::Colour fill) override
    {
        // JUCE paints a black key's face in a brighter() copy of its colour,
        // which from any colour at all comes out a mid grey; Merli's keys are
        // near-black, as the mockup draws them. Same construction otherwise:
        // a darker rim, a face inset on the sides and short of the foot, and a
        // lit row along its top.
        if (isMerli())
        {
            auto key = fill;
            if (down) key = key.overlaidWith(findColour(keyDownOverlayColourId));
            if (over) key = key.overlaidWith(findColour(mouseOverKeyOverlayColourId));
            g.setColour(key.darker(0.6f));
            g.fillRect(area);
            if (!down)
            {
                const auto face = area.reduced(area.getWidth() / 8.0f, 0.0f)
                                      .removeFromTop(area.getHeight() * 7.0f / 8.0f);
                g.setColour(key);
                g.fillRect(face);
                g.setColour(key.brighter(0.5f));
                g.fillRect(face.withHeight(1.0f));
            }
            drawGuide(note, g, area, down, true);
            return;
        }
        juce::MidiKeyboardComponent::drawBlackNote(note, g, area, down, over, fill);
        drawGuide(note, g, area, down, true);
    }

private:
    const NoteGuideState& guideState;

    void drawGuide(int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool black)
    {
        const auto guideOn = guideState.root >= 0;
        const auto tone = guideTone(guideState, note);
        const auto colour = guideToneColour(tone);
        if (tone != GuideTone::normal)
        {
            g.setColour(colour.withAlpha(down ? 0.24f : black ? 0.42f : 0.30f));
            g.fillRoundedRectangle(area.reduced(1.0f), 1.5f);
            g.setColour(colour.withAlpha(down ? 0.94f : 0.82f));
            g.fillRect(area.getX() + 1.0f, area.getBottom() - 2.5f, area.getWidth() - 2.0f, 1.5f);
            if (tone == GuideTone::root)
            {
                const auto marker = juce::Rectangle<float>(6.0f, 6.0f).withCentre({area.getCentreX(), area.getY() + 6.0f});
                g.setColour(colour);
                g.fillEllipse(marker);
                g.setColour(juce::Colours::black.withAlpha(0.8f));
                g.setFont(panelFont(Face::emphasis, 5.5f));
                g.drawText("R", marker, juce::Justification::centred);
            }
        }
        const auto name = guideKeyLabel(note, guideOn, getOctaveForMiddleC());
        if (name.isEmpty()) return;
        auto label = area.reduced(1.0f);
        label = black ? label.withTop(label.getBottom() - label.getHeight() * 0.38f)
                      : label.withTop(label.getBottom() - keyLabelHeight);
        g.setColour((tone == GuideTone::normal ? (black ? keyLabelOnBlack : keyLabelOnWhite)
                                                    : colour).withAlpha(down ? 0.95f : tone == GuideTone::normal ? 0.85f : 0.95f));
        g.setFont(panelFont(Face::label, black ? 9.0f : 10.5f));
        g.drawText(name, label, juce::Justification::centred);
    }

    // The strip along a white key's foot its name is centred in.
    static constexpr float keyLabelHeight = 15.0f;
};
}
