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
    "C", "C♯/D♭", "D", "D♯/E♭", "E", "F", "F♯/G♭", "G", "G♯/A♭", "A", "A♯/B♭", "B"
};
inline constexpr std::array<const char*, 12> guideSharpNames {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
inline constexpr std::array<const char*, 12> guideChordNames {
    "Off", "Triad", "6th", "7th", "Maj7", "Augmented", "Diminished", "Aug7", "Dim7", "Half-dim", "Sus2", "Sus4"
};
inline constexpr std::array<const char*, 12> guideChordMenuNames {
    "Off", "Triad", "6th", "7th", "Maj7", "Augmented", "Diminished", "Aug7 (7♯5)", "Dim7", "Half-Diminished (m7♭5)", "Sus2", "Sus4"
};
inline constexpr std::array<const char*, 13> guideScaleNames {
    "Off", "Major", "Nat Minor", "Maj Penta", "Min Penta", "Blues", "Harm Minor", "Mel Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian"
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
        juce::MidiKeyboardComponent::drawBlackNote(note, g, area, down, over, fill);
        drawGuide(note, g, area, down, true);
    }

private:
    const NoteGuideState& guideState;

    void drawGuide(int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool black)
    {
        if (guideState.root < 0) return;
        const auto tone = guideTone(guideState, note);
        const auto colour = tone == GuideTone::root ? juce::Colour(0xfff0b35a)
                          : tone == GuideTone::chord ? juce::Colour(0xffe78270)
                          : tone == GuideTone::scale ? juce::Colour(0xff67b7af) : juce::Colours::transparentBlack;
        if (tone != GuideTone::normal)
        {
            g.setColour(colour.withAlpha(down ? 0.18f : black ? 0.34f : 0.22f));
            g.fillRoundedRectangle(area.reduced(1.0f), 1.5f);
            g.setColour(colour.withAlpha(down ? 0.9f : 0.72f));
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
        auto label = area.reduced(1.0f);
        label = black ? label.withTop(label.getBottom() - label.getHeight() * 0.38f)
                      : label.withTop(label.getBottom() - 13.0f);
        g.setColour((tone == GuideTone::normal ? (black ? juce::Colour(0xff8c92a0) : juce::Colour(0xff555c69))
                                                    : colour).withAlpha(down ? 0.95f : tone == GuideTone::normal ? 0.72f : 0.95f));
        g.setFont(panelFont(Face::label, black ? 8.0f : 9.0f));
        g.drawText(guideSharpNames[static_cast<size_t>(note % 12)], label, juce::Justification::centred);
    }
};

class NoteGuideField final : public juce::Button
{
public:
    explicit NoteGuideField(const juce::String& label) : juce::Button(label) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto box = getLocalBounds().toFloat().reduced(0.5f);
        const auto alpha = isEnabled() ? 1.0f : 0.35f;
        g.setColour(juce::Colour(down ? 0xff151c26 : 0xff090d14).withAlpha(alpha));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour((over ? electricBlue : line).withAlpha(alpha));
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        g.setColour(rhino::forge::ui::text.withAlpha(alpha));
        g.setFont(panelFont(Face::label, 10.0f));
        g.drawText(getButtonText(), box.withTrimmedRight(12.0f).reduced(4.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour(mutedText.withAlpha(alpha));
        const auto x = box.getRight() - 7.0f, y = box.getCentreY();
        juce::Path arrow; arrow.startNewSubPath(x - 3, y - 1.5f); arrow.lineTo(x + 3, y - 1.5f); arrow.lineTo(x, y + 2.0f); arrow.closeSubPath();
        g.fillPath(arrow);
    }
};

class NoteGuideQuality final : public juce::Button
{
public:
    NoteGuideQuality() : juce::Button("quality") { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto box = getLocalBounds().toFloat().reduced(0.5f);
        const auto enabled = isEnabled(); const auto alpha = enabled ? 1.0f : 0.35f;
        g.setColour(juce::Colour(0xff090d14).withAlpha(alpha)); g.fillRoundedRectangle(box, 3.0f);
        const auto half = box.withWidth(box.getWidth() * 0.5f);
        g.setColour(electricBlue.withAlpha(getToggleState() ? 0.08f : 0.22f));
        g.fillRoundedRectangle(getToggleState() ? box.withX(half.getRight()).withWidth(half.getWidth()) : half, 2.5f);
        g.setColour((over ? electricBlue : line).withAlpha(alpha)); g.drawRoundedRectangle(box, 3.0f, 1.0f);
        g.setColour(rhino::forge::ui::text.withAlpha(alpha)); g.setFont(panelFont(Face::label, 9.0f));
        g.drawText("MAJ", half, juce::Justification::centred); g.drawText("MIN", box.withX(half.getRight()).withWidth(half.getWidth()), juce::Justification::centred);
    }
};
}
