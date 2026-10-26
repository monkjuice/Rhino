#pragma once

#include "DuckProcessor.h"
#include "DuckScopeView.h"
#include "../ui/DuckArt.h"
#include "../ui/DuckControls.h"
#include "../ui/DuckLayout.h"
#include "../ui/DuckMeter.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

namespace rhino::duck
{
// The panel. Every control is a child that draws itself; the panel paints
// the plates, the name, the readings and the dividers (DuckEditorPaint.cpp),
// and a 30 Hz tick repaints the scope and follows parameters the host or
// automation moved. The duck meter keeps its own 60 Hz clock.
//
// The strip's four knobs are not parameters: they show and write the chosen
// curve's own memory, so picking another curve shows its values.
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
    // Steps the meter from the engine by hand, as its own clock would.
    void advanceMeter(float seconds);

    enum Param { attack = 0, hold, release, depth };
    // The strip's knobs and what they read, for the tests.
    ui::Knob& paramKnob(Param which) noexcept { return paramKnobs[static_cast<size_t>(which)]; }
    const juce::String& paramReading(Param which) const noexcept { return shownValues[static_cast<size_t>(which)]; }
    const ui::DuckMeter& meterView() const noexcept { return meter; }

private:
    void timerCallback() override { refresh(); }
    void setChoice(const char* id, int index);
    void nudgeShift(float step);
    void stepCurve(int by);
    void showMenu();
    void showCurveList();
    void syncStrip(int curve);
    void paramMoved(Param);
    float valueOf(const char* id) const;
    juce::String textOf(const char* id) const;

    void paintRail(juce::Graphics&);
    void paintCurveBank(juce::Graphics&);
    void paintStrip(juce::Graphics&);
    void paintPresetBar(juce::Graphics&);

    Processor& processor;
    ui::Layout placed;
    juce::Image grain = ui::makeGrain();
    ui::DuckArt art;

    ui::DuckMeter meter;
    ui::Knob mixKnob;
    std::array<ui::Knob, ui::paramCount> paramKnobs;
    ui::CutoffSlider cutoffSlider;
    ui::Chip soloChip { "SOLO" }, loopChip { "LOOP" }, menuButton { "MENU", ui::Glyph::bars };
    ui::Switch wholeSwitch { "WHOLE SIGNAL" };
    std::array<std::unique_ptr<ui::Chip>, 4> lengthChips;
    std::array<std::unique_ptr<ui::Chip>, 3> triggerChips;
    std::array<std::unique_ptr<ui::Chip>, 4> shiftButtons;
    std::array<std::unique_ptr<ui::CurveCell>, curveCount> cells;
    ui::Chip presetBack { "PREVIOUS", ui::Glyph::back }, presetForward { "NEXT", ui::Glyph::forward };
    ui::PresetBox presetBox;
    ui::ScopeView scope;
    juce::TooltipWindow tooltips;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, cutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment, loopAttachment;

    // What the panel last showed, so a tick repaints only what moved.
    int shownCurve = -1, shownRevision = -1, shownTrigger = -1, shownLength = -1;
    double shownCycle = 0.0;
    bool shownBand = false, shownBandOnce = false;
    // Set while the strip is being moved to the model's values, so the
    // knobs' own change callbacks do not write them straight back.
    bool syncing = false;
    juce::String shownMix, shownCutoff;
    std::array<juce::String, ui::paramCount> shownValues, shownLows, shownHighs;
};
}
