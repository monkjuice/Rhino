// The note guide: what a key is labelled, what a chord and a scale are called
// and spelt, where its panel puts things at every size, and the door on the
// shelf that opens it over the modulators.
#include "ForgeTestSupport.h"
#include "../ui/ForgeNoteGuidePanel.h"

#include <memory>
#include <vector>

namespace rhino::forge::tests
{
namespace
{
juce::String degreesOf(const std::vector<ui::GuideNote>& notes)
{
    juce::StringArray degrees;
    for (const auto& note : notes) degrees.add(note.degree);
    return degrees.joinIntoString(" ");
}

// A C is labelled with its octave whether a guide is on or not, counted with
// middle C as C3 as the keyboard counts it; the other keys are named only
// while a root is chosen.
void keyLabelSuite()
{
    requireText(ui::guideKeyLabel(60, false, 3), "C3", "middle C is labelled C3 with no guide");
    requireText(ui::guideKeyLabel(36, false, 3), "C1", "the lowest C on the keys is C1");
    requireText(ui::guideKeyLabel(96, false, 3), "C6", "the highest key is C6");
    requireText(ui::guideKeyLabel(72, true, 3), "C4", "a C keeps its octave with a guide on");
    requireText(ui::guideKeyLabel(61, false, 3), "", "no other key is labelled without a guide");
    requireText(ui::guideKeyLabel(61, true, 3), "C#", "with a guide on every key is named");
    requireText(ui::guideKeyLabel(60, false, 4), "C4", "the octave follows the keyboard's own middle C");
    for (int note = ui::keyboardLowestNote; note <= ui::keyboardHighestNote; ++note)
        require(ui::guideKeyLabel(note, false, 3).isNotEmpty() == (note % 12 == 0),
                "every C on the keys, and nothing else, carries a label with the guide off");
}

void chordAndScaleNamesSuite()
{
    using ui::GuideChord;
    using ui::GuideScale;
    const auto symbol = [] (int root, GuideChord chord, bool minor)
    {
        return ui::guideChordSymbol({root, chord, minor, GuideScale::off});
    };
    requireText(symbol(0, GuideChord::triad, false), "C", "a major triad is its root");
    requireText(symbol(0, GuideChord::triad, true), "Cm", "a minor triad is m");
    requireText(symbol(0, GuideChord::seventh, false), "C7", "MAJ leaves a 7th dominant");
    requireText(symbol(9, GuideChord::seventh, true), "Am7", "MIN makes it m7");
    requireText(symbol(0, GuideChord::maj7, false), "Cmaj7", "Maj7 under MAJ is maj7");
    requireText(symbol(0, GuideChord::maj7, true), "Cm(maj7)", "Maj7 under MIN is m(maj7)");
    requireText(symbol(11, GuideChord::halfDiminished, false), "Bm7b5", "half-diminished is m7b5");
    requireText(symbol(6, GuideChord::aug7, false), "F#7#5", "Aug7 is 7#5");
    requireText(symbol(0, GuideChord::dim7, true), "Cdim7", "a chord with no quality ignores the switch");
    requireText(symbol(0, GuideChord::off, false), "", "no chord has no symbol");
    requireText(ui::guideChordSymbol({-1, GuideChord::triad, false, GuideScale::off}), "",
                "and no root has none either");

    const auto chordDegrees = [] (GuideChord chord, bool minor)
    {
        return degreesOf(ui::guideNotes({0, chord, minor, GuideScale::off}, true));
    };
    requireText(chordDegrees(GuideChord::triad, false), "R 3 5", "a major triad is R 3 5");
    requireText(chordDegrees(GuideChord::sixth, true), "R b3 5 6", "a minor sixth keeps a major sixth");
    requireText(chordDegrees(GuideChord::dim7, false), "R b3 b5 bb7", "a diminished seventh's top is bb7, not 6");
    requireText(chordDegrees(GuideChord::augmented, false), "R 3 #5", "an augmented fifth is #5, not b6");
    requireText(chordDegrees(GuideChord::halfDiminished, false), "R b3 b5 b7", "m7b5 is R b3 b5 b7");
    requireText(chordDegrees(GuideChord::sus4, false), "R 4 5", "sus4 is R 4 5");

    const auto scaleDegrees = [] (GuideScale scale)
    {
        return degreesOf(ui::guideNotes({0, GuideChord::off, false, scale}, false));
    };
    requireText(scaleDegrees(GuideScale::major), "1 2 3 4 5 6 7", "the major scale counts 1 to 7");
    requireText(scaleDegrees(GuideScale::naturalMinor), "1 2 b3 4 5 b6 b7", "natural minor flattens 3, 6 and 7");
    requireText(scaleDegrees(GuideScale::lydian), "1 2 3 #4 5 6 7", "Lydian raises its fourth");
    requireText(scaleDegrees(GuideScale::blues), "1 b3 4 b5 5 b7", "the blues keeps its fourth and adds b5");
    requireText(scaleDegrees(GuideScale::locrian), "1 b2 b3 4 b5 b6 b7", "Locrian has a b5");

    // A minor over A natural minor: the readout lists the scale from its root,
    // each note in the colour it wears on the keys.
    const auto notes = ui::guideNotes({9, GuideChord::triad, true, GuideScale::naturalMinor}, false);
    require(notes.size() == 7, "a seven-note scale lists seven notes");
    if (notes.size() == 7)
    {
        const std::array<int, 7> pitches {9, 11, 0, 2, 4, 5, 7};
        for (size_t i = 0; i < notes.size(); ++i)
            require(notes[i].pitch == pitches[i], "the scale is listed from its root upward");
        require(notes[0].tone == ui::GuideTone::root, "its root wears the root's colour");
        require(notes[1].tone == ui::GuideTone::scale, "B is a scale tone only");
        require(notes[2].tone == ui::GuideTone::chord && notes[4].tone == ui::GuideTone::chord,
                "C and E are the chord's, and say so in the scale too");
    }
}

// The guide's panel at every size the window allows: inside its module, no
// two things on it overlapping, every caption whole at its size, and every key
// of the octave answering a press at its own face. And the door, whose
// longest lines have to fit at the narrowest window.
void guideLayoutSuite()
{
    const ui::Module* guide = nullptr;
    for (const auto& module : ui::modules())
        if (ui::onPage(module, ui::Page::guide)) guide = &module;
    require(guide != nullptr, "the note guide is declared as a module");
    if (guide == nullptr) return;

    const auto cellFont = ui::panelFont(ui::Face::emphasis, ui::guideCellTextSize);
    auto failedAt = juce::String();
    for (int width = ui::minPanelWidth; width <= ui::maxPanelWidth && failedAt.isEmpty(); width += 20)
        for (int height = ui::minPanelHeight; height <= ui::maxPanelHeight && failedAt.isEmpty(); height += 20)
        {
            const auto bounds = juce::Rectangle<int>(0, 0, width, height);
            const auto area = ui::controlArea(ui::moduleBounds(bounds, *guide), *guide);
            const auto layout = ui::noteGuideLayout(area);

            struct Piece { juce::Rectangle<int> box; juce::String caption; };
            std::vector<Piece> pieces {{layout.rootTitle, {}}, {layout.chordTitle, {}}, {layout.scaleTitle, {}},
                                       {layout.rootOff, "OFF"}, {layout.octave, {}},
                                       {layout.major, "MAJ"}, {layout.minor, "MIN"}, {layout.readout, {}}};
            for (size_t i = 0; i < layout.chords.size(); ++i)
                pieces.push_back({layout.chords[i], ui::guideChordCellNames[i]});
            for (size_t i = 0; i < layout.scales.size(); ++i)
                pieces.push_back({layout.scales[i], ui::guideScaleCellNames[i]});

            auto holds = true;
            for (size_t i = 0; i < pieces.size(); ++i)
            {
                const auto& piece = pieces[i];
                holds = holds && !piece.box.isEmpty() && area.contains(piece.box);
                if (piece.caption.isNotEmpty())
                    holds = holds && piece.box.getHeight() >= ui::guideMinCellHeight
                         && juce::GlyphArrangement::getStringWidth(cellFont, piece.caption)
                                <= static_cast<float>(piece.box.getWidth() - 6);
                for (size_t j = i + 1; j < pieces.size(); ++j)
                    if (piece.box.intersects(pieces[j].box)) holds = false;
            }
            holds = holds && layout.readout.contains(layout.chordReadout) && layout.readout.contains(layout.scaleReadout)
                 && !layout.chordReadout.intersects(layout.scaleReadout);
            for (int pitch = 0; pitch < 12; ++pitch)
            {
                const auto key = ui::guideOctaveKey(layout.octave, pitch);
                const auto at = ui::guidePitchIsBlack(pitch) ? key.getCentre()
                                                             : juce::Point<int>(key.getCentreX(), key.getBottom() - 3);
                holds = holds && layout.octave.contains(key) && ui::guideOctaveKeyAt(layout.octave, at) == pitch
                     && ui::noteGuideHit(layout, at) == ui::GuideHit {ui::GuideTarget::root, pitch};
            }
            if (!holds) failedAt << width << "x" << height;
        }
    require(failedAt.isEmpty(), "the note guide's panel holds its parts, apart and legible, at every allowed size");
    if (failedAt.isNotEmpty()) std::cerr << "       at " << failedAt << '\n';

    // The door's two lines at their sizes, in the narrowest door the shelf
    // makes, less the margins it paints inside.
    const auto narrowest = ui::noteGuideFace({0, 0, ui::minPanelWidth, ui::minPanelHeight});
    const auto room = static_cast<float>(narrowest.getWidth() - 20);
    require(juce::GlyphArrangement::getStringWidth(ui::panelFont(ui::Face::header, 16.0f), "Cm(maj7)") <= room,
            "the door's longest chord fits at the narrowest window");
    require(juce::GlyphArrangement::getStringWidth(ui::panelFont(ui::Face::emphasis, 11.5f), "MIXOLYDIAN") <= room,
            "and so does its longest scale");
    require(ui::noteGuidePlateBounds({0, 0, ui::minPanelWidth, ui::minPanelHeight}).contains(narrowest),
            "the door is inside its plate");
}

// What a press on the panel does to the guide, without a panel.
void guidePressSuite()
{
    ui::NoteGuideState state;
    const auto layout = ui::noteGuideLayout({0, 0, 840, 210});
    const auto centre = [] (juce::Rectangle<int> box) { return box.getCentre(); };

    require(!ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.chords[4]))),
            "a chord means nothing until there is a root");
    require(state.chord == ui::GuideChord::off, "and is not chosen behind the player's back");
    const auto g = ui::guideOctaveKey(layout.octave, 7);
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, {g.getCentreX(), g.getBottom() - 3})),
            "a key of the octave chooses the root");
    require(state.root == 7, "and it is the key pressed");
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.chords[4]))) && state.chord == ui::GuideChord::maj7,
            "with a root, a chord button chooses its chord");
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.minor))) && state.minor,
            "MIN makes a Maj7 minor");
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.scales[8])))
                && state.scale == ui::GuideScale::dorian,
            "a scale button chooses its scale");
    require(!ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.scales[8]))),
            "pressing what is already chosen changes nothing");
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.chords[5])))
                && !ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.major))),
            "the switch does nothing for a chord with no quality");
    require(ui::applyNoteGuideHit(state, ui::noteGuideHit(layout, centre(layout.rootOff))) && state.root < 0,
            "OFF switches the guide off");
    require(state.scale == ui::GuideScale::dorian, "and keeps the scale for the next root");
}

// The door opens the panel over the envelopes and LFOs, as the ARP plate opens
// the arp's panes over the same cells; the two close each other, and a press
// on the panel is saved where the guide has always kept its choices.
void guideDoorSuite()
{
    const auto shownLabels = [] (juce::Component& editor, const juce::String& caption)
    {
        auto count = 0;
        for (auto* child : editor.getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                if (label->isVisible() && label->getText() == caption) ++count;
        return count;
    };

    rhino::forge::Processor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
    ui::NoteGuideDoor* door = nullptr;
    ui::NoteGuidePanel* panel = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* found = dynamic_cast<ui::NoteGuideDoor*>(child)) door = found;
        if (auto* found = dynamic_cast<ui::NoteGuidePanel*>(child)) panel = found;
    }
    require(door != nullptr && panel != nullptr, "the editor carries the guide's door and its panel");
    if (door == nullptr || panel == nullptr) return;

    require(door->getBounds() == ui::noteGuideFace(editor->getLocalBounds()), "the door is the well in its plate");
    require(door->isVisible() && !panel->isVisible() && !door->isOpen(), "the panel opens closed");
    require(shownLabels(*editor, "ATTACK") == 1, "with the envelope in view");

    door->onClick();
    require(panel->isVisible() && door->isOpen(), "the door opens the guide");
    require(shownLabels(*editor, "ATTACK") == 0 && shownLabels(*editor, "RATE") == 0,
            "standing over the envelopes and LFOs");
    for (const auto& module : ui::modules())
        if (juce::String(module.id) == "global" || juce::String(module.id) == "macros")
            require(!panel->getBounds().intersects(ui::moduleBounds(editor->getLocalBounds(), module)),
                    "and leaving GLOBAL and the macros in view");

    // The ARP plate's name opens the arp, which puts the guide away.
    const auto openArp = [&editor]
    {
        const auto at = ui::arpPlateNameBounds(editor->getLocalBounds()).getCentre().toFloat();
        const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, editor.get(), editor.get(),
                                     juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
        editor->mouseDown(click);
    };
    openArp();
    require(!panel->isVisible() && !door->isOpen(), "opening the arp closes the guide");
    require(shownLabels(*editor, "SHIFT") == 1, "and the arp's panes are up");
    door->onClick();
    require(panel->isVisible() && shownLabels(*editor, "SHIFT") == 0, "opening the guide closes the arp");

    const auto layout = panel->layout();
    const auto key = ui::guideOctaveKey(layout.octave, 2);
    require(panel->press({key.getCentreX(), key.getBottom() - 3}), "a press on the octave lands");
    require(static_cast<int>(processor.state.state.getProperty("noteGuideRoot", -1)) == 2,
            "and the root is kept with the patch's view state, as the fields kept it");
    require(panel->press(layout.scales[static_cast<size_t>(ui::GuideScale::dorian)].getCentre())
                && static_cast<int>(processor.state.state.getProperty("noteGuideScale", 0))
                       == static_cast<int>(ui::GuideScale::dorian),
            "and so is the scale");

    door->onClick();
    require(!panel->isVisible() && shownLabels(*editor, "ATTACK") == 1, "the door puts the guide away again");
}
}

void guideTests()
{
    keyLabelSuite();
    chordAndScaleNamesSuite();
    guideLayoutSuite();
    guidePressSuite();
    guideDoorSuite();
}
}
