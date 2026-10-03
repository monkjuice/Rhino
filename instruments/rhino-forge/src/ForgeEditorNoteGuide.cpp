// The player-facing note guide. It stores only editor choices in the existing
// state tree; the audio thread and incoming MIDI never consult it.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
namespace
{
constexpr const char* guideRootProperty = "noteGuideRoot";
constexpr const char* guideChordProperty = "noteGuideChord";
constexpr const char* guideMinorProperty = "noteGuideMinor";
constexpr const char* guideScaleProperty = "noteGuideScale";

void dressGuideLabel(juce::Label& label, const char* text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(ui::panelFont(ui::Face::label, 8.5f));
    label.setColour(juce::Label::textColourId, ui::mutedText);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
}
}

void Editor::buildNoteGuide()
{
    const auto& state = processor.state.state;
    noteGuide.root = juce::jlimit(-1, 11, static_cast<int>(state.getProperty(guideRootProperty, -1)));
    noteGuide.chord = static_cast<ui::GuideChord>(juce::jlimit(0, 11, static_cast<int>(state.getProperty(guideChordProperty, 0))));
    noteGuide.minor = static_cast<bool>(state.getProperty(guideMinorProperty, false));
    noteGuide.scale = static_cast<ui::GuideScale>(juce::jlimit(0, 12, static_cast<int>(state.getProperty(guideScaleProperty, 0))));

    for (auto* field : {&guideRoot, &guideChord, &guideScale}) addAndMakeVisible(field);
    for (auto* label : {&guideRootLabel, &guideChordLabel, &guideScaleLabel}) addAndMakeVisible(label);
    addAndMakeVisible(guideQuality);
    dressGuideLabel(guideRootLabel, "ROOT");
    dressGuideLabel(guideChordLabel, "CHORD");
    dressGuideLabel(guideScaleLabel, "SCALE");
    guideRoot.setTooltip("Shared root for the chord and scale note guide");
    guideChord.setTooltip("Chord tones repeat across every visible octave");
    guideScale.setTooltip("Scale notes repeat across every visible octave");
    guideQuality.setTooltip("MAJ → 7; MIN → m7. Maj7: MAJ → maj7; MIN → m(maj7)");
    guideRoot.onClick = [this] { showNoteGuideMenu(0); };
    guideChord.onClick = [this] { showNoteGuideMenu(1); };
    guideScale.onClick = [this] { showNoteGuideMenu(2); };
    guideQuality.onClick = [this]
    {
        noteGuide.minor = guideQuality.getToggleState();
        saveNoteGuide(); refreshNoteGuide();
    };
    refreshNoteGuide();
}

void Editor::saveNoteGuide()
{
    auto& state = processor.state.state;
    state.setProperty(guideRootProperty, noteGuide.root, nullptr);
    state.setProperty(guideChordProperty, static_cast<int>(noteGuide.chord), nullptr);
    state.setProperty(guideMinorProperty, noteGuide.minor, nullptr);
    state.setProperty(guideScaleProperty, static_cast<int>(noteGuide.scale), nullptr);
}

void Editor::refreshNoteGuide()
{
    guideRoot.setButtonText(noteGuide.root < 0 ? "Off" : ui::guideRootNames[static_cast<size_t>(noteGuide.root)]);
    guideChord.setButtonText(ui::guideChordNames[static_cast<size_t>(noteGuide.chord)]);
    guideScale.setButtonText(ui::guideScaleNames[static_cast<size_t>(noteGuide.scale)]);
    const auto active = noteGuide.root >= 0;
    guideChord.setEnabled(active); guideScale.setEnabled(active);
    guideQuality.setToggleState(noteGuide.minor, juce::dontSendNotification);
    guideQuality.setEnabled(active && ui::guideChordHasQuality(noteGuide.chord));
    keyboard.repaint();
}

void Editor::showNoteGuideMenu(int which)
{
    juce::PopupMenu menu;
    if (which == 0)
    {
        menu.addItem(1, "Off", true, noteGuide.root < 0);
        for (int i = 0; i < 12; ++i) menu.addItem(i + 2, ui::guideRootNames[static_cast<size_t>(i)], true, noteGuide.root == i);
    }
    else if (which == 1)
        for (int i = 0; i < 12; ++i) menu.addItem(i + 1, ui::guideChordMenuNames[static_cast<size_t>(i)], true, static_cast<int>(noteGuide.chord) == i);
    else
        for (int i = 0; i < 13; ++i) menu.addItem(i + 1, ui::guideScaleMenuNames[static_cast<size_t>(i)], true, static_cast<int>(noteGuide.scale) == i);

    const auto safe = juce::Component::SafePointer<Editor>(this);
    auto options = juce::PopupMenu::Options {}.withMinimumWidth(175);
    options = options.withTargetComponent(which == 0 ? &guideRoot : which == 1 ? &guideChord : &guideScale);
    menu.showMenuAsync(options, [safe, which] (int chosen)
    {
        if (safe == nullptr || chosen == 0) return;
        if (which == 0) safe->noteGuide.root = chosen - 2;
        else if (which == 1) safe->noteGuide.chord = static_cast<ui::GuideChord>(chosen - 1);
        else safe->noteGuide.scale = static_cast<ui::GuideScale>(chosen - 1);
        safe->saveNoteGuide(); safe->refreshNoteGuide();
    });
}
}
