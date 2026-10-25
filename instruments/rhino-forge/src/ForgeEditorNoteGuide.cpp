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
}

void Editor::buildNoteGuide()
{
    const auto& state = processor.state.state;
    noteGuide.root = juce::jlimit(-1, 11, static_cast<int>(state.getProperty(guideRootProperty, -1)));
    noteGuide.chord = static_cast<ui::GuideChord>(juce::jlimit(0, 11, static_cast<int>(state.getProperty(guideChordProperty, 0))));
    noteGuide.minor = static_cast<bool>(state.getProperty(guideMinorProperty, false));
    noteGuide.scale = static_cast<ui::GuideScale>(juce::jlimit(0, 12, static_cast<int>(state.getProperty(guideScaleProperty, 0))));

    // The door is on the shelf on every tab; the panel only while it is open,
    // which applyEnableStates decides with everything else that comes and goes.
    addAndMakeVisible(guideDoor);
    addChildComponent(guidePanel);
    guideDoor.onClick = [this] { toggleGuideOpen(); };
    guidePanel.onChange = [this] { saveNoteGuide(); refreshNoteGuide(); };
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

// Everything that draws the guide reads noteGuide as it paints, so a change is
// only a matter of asking each of them to.
void Editor::refreshNoteGuide()
{
    guideDoor.repaint();
    guidePanel.repaint();
    keyboard.repaint();
}

// The door on the shelf. The guide and the arp stand in the same cells and the
// expanded rack takes the whole lower row, so opening the guide closes both.
void Editor::toggleGuideOpen()
{
    guideOpen = !guideOpen;
    if (guideOpen)
    {
        arpOpen = false;
        fxExpanded = false;
    }
    applyPage();
}
}
