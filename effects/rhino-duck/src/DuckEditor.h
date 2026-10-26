#pragma once

#include "DuckProcessor.h"
#include "DuckScopeView.h"
#include "../ui/DuckControls.h"
#include "../ui/DuckLayout.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace rhino::duck
{
// The panel. Every control is a child that draws itself; the panel paints
// the name, the readings and the dividers, and a 30 Hz tick repaints the
// scope and follows parameters the host or automation moved.
class Editor final : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit Editor(Processor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Where everything stands, for the tests.
    const ui::Layout& layout() const noexcept { return placed; }
    // What the tick does, run by hand where no message loop runs.
    void refresh();

private:
    void timerCallback() override { refresh(); }
    void setChoice(const char* id, int index);
    void nudgeShift(float step);
    void showMenu();
    float valueOf(const char* id) const;
    juce::String textOf(const char* id) const;

    Processor& processor;
    ui::Layout placed;

    ui::MixKnob mixKnob;
    ui::CutoffSlider cutoffSlider;
    ui::Chip bandChip { "BAND" }, soloChip { "SOLO" }, loopChip { "LOOP" }, menuButton { "MENU" };
    std::array<std::unique_ptr<ui::Chip>, 4> lengthChips;
    std::array<std::unique_ptr<ui::Chip>, 3> triggerChips;
    std::array<std::unique_ptr<ui::Chip>, 4> shiftButtons;
    std::array<std::unique_ptr<ui::CurveCell>, curveCount> cells;
    ui::ScopeView scope;
    juce::TooltipWindow tooltips;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, cutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bandAttachment, soloAttachment, loopAttachment;

    int shownCurve = -1, shownRevision = -1, shownTrigger = -1, shownLength = -1;
    bool shownBand = false, shownBandOnce = false;
    juce::String shownMix, shownCutoff;
};
}
