#include "DuckSuites.h"
#include "DuckTestSupport.h"
#include "../src/DuckEditor.h"

#include <set>

// The panel's geometry, without a window: everything inside the panel,
// nothing on top of anything else, every curve in one cell, the editor itself
// opened and painted, the strip editing the chosen curve, and the duck meter
// reading the gain.
namespace rhino::duck::tests
{
namespace
{
std::vector<std::pair<const char*, juce::Rectangle<int>>> parts(const ui::Layout& layout)
{
    std::vector<std::pair<const char*, juce::Rectangle<int>>> all {
        { "brand", layout.brand }, { "meter", layout.meter }, { "mix title", layout.mixTitle }, { "knob", layout.knob },
        { "dry label", layout.dryLabel }, { "wet label", layout.wetLabel }, { "mix reading", layout.mixReading },
        { "loop chip", layout.loopChip }, { "menu", layout.menuButton }, { "scope", layout.scope }, { "curve caption", layout.curveCaption },
        { "band title", layout.bandTitle }, { "solo chip", layout.soloChip }, { "cutoff slider", layout.cutoffSlider },
        { "cutoff low", layout.cutoffLow }, { "cutoff high", layout.cutoffHigh }, { "cutoff reading", layout.cutoffReading },
        { "whole switch", layout.wholeSwitch }, { "preset title", layout.presetTitle }, { "preset back", layout.presetBack },
        { "preset forward", layout.presetForward }, { "preset box", layout.presetBox }, { "footer", layout.footer }
    };
    for (const auto& chip : layout.lengthChips) all.emplace_back("length chip", chip);
    for (const auto& chip : layout.triggerChips) all.emplace_back("trigger chip", chip);
    for (const auto& button : layout.shiftButtons) all.emplace_back("shift button", button);
    for (const auto& cell : layout.curveCells) all.emplace_back("curve cell", cell);
    for (size_t i = 0; i < ui::paramCount; ++i)
    {
        all.emplace_back("param title", layout.paramTitles[i]);
        all.emplace_back("param knob", layout.paramKnobs[i]);
        all.emplace_back("param low", layout.paramLows[i]);
        all.emplace_back("param high", layout.paramHighs[i]);
        all.emplace_back("param value", layout.paramValues[i]);
    }
    return all;
}

void everythingLandsInsideThePanel()
{
    const juce::Rectangle<int> panel(0, 0, ui::panelWidth, ui::panelHeight);
    const auto layout = ui::layoutFor(panel);
    for (const auto& [name, area] : parts(layout))
    {
        require(!area.isEmpty(), name);
        require(panel.contains(area), name);
    }
    const std::array<juce::Rectangle<int>, 4> plates { layout.rail, layout.mainPlate, layout.strip, layout.presetBar };
    for (size_t a = 0; a < plates.size(); ++a)
    {
        require(panel.contains(plates[a]), "a plate lies inside the panel");
        for (size_t b = a + 1; b < plates.size(); ++b) require(!plates[a].intersects(plates[b]), "two plates overlap");
    }
    for (const auto& cell : layout.curveCells)
        require(cell.getWidth() >= 56 && cell.getHeight() >= 36, "a curve cell is big enough to read");
    require(layout.knob.getWidth() == layout.knob.getHeight(), "the Mix knob is square");
    for (const auto& knob : layout.paramKnobs) require(knob.getWidth() == knob.getHeight() && knob.getWidth() >= 48, "a strip knob is square and big enough to turn");
    // The scope is the reason for the panel: nothing takes its room.
    require(layout.scope.getWidth() >= 700 && layout.scope.getHeight() >= 216, "the scope keeps its room");
    require(layout.meter.getHeight() >= 240, "the duck has water to sink in");
}

void nothingOverlaps()
{
    const auto all = parts(ui::layoutFor({ 0, 0, ui::panelWidth, ui::panelHeight }));
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
            if (all[a].second.intersects(all[b].second))
            {
                std::cerr << "  " << all[a].first << " meets " << all[b].first << '\n';
                require(false, "two parts of the panel overlap");
            }
}

void cellsHoldEveryCurveOnce()
{
    std::set<int> seen;
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 8; ++column) seen.insert(ui::curveAtCell(column, row));
    require(seen.size() == 16 && *seen.begin() == 0 && *seen.rbegin() == 15, "the sixteen cells hold the sixteen curves");
    require(ui::curveAtCell(0, 1) == 4 && ui::curveAtCell(4, 0) == 8 && ui::curveAtCell(4, 1) == 9 && ui::curveAtCell(7, 1) == 15,
            "the first eight fill the left in two rows, then a family a column");
}

void editorOpensAtItsSizeAndPaints()
{
    Processor processor;
    setValue(processor, id::trigger, 2.0f);
    setValue(processor, id::loop, 0.0f);
    setValue(processor, id::band, 1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr && editor->getWidth() == ui::panelWidth && editor->getHeight() == ui::panelHeight,
            "the editor opens at the panel's size");
    auto* panel = dynamic_cast<Editor*>(editor.get());
    require(panel != nullptr, "the editor is the panel");
    if (panel == nullptr) return;
    panel->refresh();
    for (auto* child : editor->getChildren())
        require(editor->getLocalBounds().contains(child->getBounds()), "every control lands inside the panel");
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    require(image.isValid() && image.getWidth() == ui::panelWidth, "the panel paints");
    // The scope answers for what lies under a point.
    for (auto* child : editor->getChildren())
        if (auto* scope = dynamic_cast<ui::ScopeView*>(child))
        {
            const auto reset = scope->resetButtonArea().getCentre().toInt();
            require(scope->partAt(reset) == ui::ScopeView::Part::reset, "the reset button is where it is drawn");
            const auto end = juce::Point<int>(juce::roundToInt(scope->plotArea().getRight()) - 1, scope->getHeight() / 2);
            require(scope->partAt(end) == ui::ScopeView::Part::endMarker, "a one-shot curve's End marker can be grabbed");
            const auto span = mobileSpan(0, {});
            const auto onSlope = juce::Point<int>(juce::roundToInt(scope->plotArea().getX() + (span.from + span.to) * 0.5f * scope->plotArea().getWidth()),
                                                   scope->getHeight() / 2);
            require(scope->partAt(onSlope) == ui::ScopeView::Part::slope, "the sloped part can be grabbed");
            for (const auto handle : { ui::ScopeView::Part::holdHandle, ui::ScopeView::Part::releaseHandle })
                require(scope->partAt(scope->handlePoint(handle).toInt()) == handle, "each handle can be grabbed where it is drawn");
        }
}

// The four knobs show and write the chosen curve's own memory, in either
// direction: a knob bends the curve, and a bend made on the curve moves the
// knob.
void theStripEditsTheChosenCurve()
{
    Processor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* panel = dynamic_cast<Editor*>(editor.get());
    require(panel != nullptr, "the editor is the panel");
    if (panel == nullptr) return;
    setValue(processor, id::curve, 2.0f);
    panel->refresh();

    const auto designed = slopedPart(2, {}).length;
    panel->paramKnob(Editor::release).setValue(designed * 0.5f, juce::sendNotificationSync);
    requireClose(slopedPart(2, processor.curveAdjust(2)).length, designed * 0.5f, 1.0e-5f, "Release shortens the chosen curve's sloped part");
    require(processor.curveAdjust(3) == CurveAdjust {}, "and leaves the other curves alone");
    panel->paramKnob(Editor::depth).setValue(0.25, juce::sendNotificationSync);
    requireClose(processor.curveAdjust(2).depth, 0.25f, 1.0e-6f, "Depth writes the chosen curve's Depth");
    require(panel->paramReading(Editor::depth) == "25%", "and reads it as a percentage");
    panel->paramKnob(Editor::attack).setValue(8.0, juce::sendNotificationSync);
    requireClose(processor.curveAttackMs(2), 8.0f, 1.0e-4f, "Attack writes milliseconds");
    require(panel->paramReading(Editor::attack) == "8.0 ms", "and reads them");

    setValue(processor, id::curve, 3.0f);
    panel->refresh();
    requireClose(static_cast<float>(panel->paramKnob(Editor::depth).getValue()), 1.0f, 1.0e-6f, "another curve shows its own Depth");
    processor.setCurveHold(3, 0.25f);
    panel->refresh();
    requireClose(static_cast<float>(panel->paramKnob(Editor::hold).getValue()), 0.25f, 1.0e-6f, "Hold follows a bend made on the curve");
    require(panel->paramReading(Editor::hold) == "125 ms", "and reads it in milliseconds at the song's tempo");
    setValue(processor, id::curve, 2.0f);
    panel->refresh();
    requireClose(static_cast<float>(panel->paramKnob(Editor::depth).getValue()), 0.25f, 1.0e-6f, "and going back finds the first curve as it was left");
}

void theMeterReadsTheGain()
{
    for (int db = 0; db > -60; --db)
        require(ui::meterDepthForDb(static_cast<float>(db - 1)) > ui::meterDepthForDb(static_cast<float>(db)) || db - 1 < ui::meterFloorDb,
                "deeper reductions sit lower on the scale");
    const std::array<std::pair<float, float>, 6> marks { { { 0.0f, 0.0f }, { -3.0f, 0.2f }, { -6.0f, 0.4f }, { -12.0f, 0.6f }, { -18.0f, 0.8f }, { -200.0f, 1.0f } } };
    for (const auto& [db, at] : marks) requireClose(ui::meterDepthForDb(db), at, 1.0e-6f, "a mark on the scale is where it is drawn");
    require(ui::reductionText(-7.84f) == "-7.8 dB" && ui::reductionText(0.0f) == "0.0 dB", "the figure reads to a tenth of a decibel");

    ui::DuckMeter meter;
    meter.setBounds(0, 0, 196, 262);
    const auto waterline = meter.waterlineY();
    const auto resting = meter.duckArea().getY();
    constexpr float frame = 1.0f / 60.0f;
    for (int i = 0; i < 90; ++i) meter.step({ 0.25f, 1 }, frame);
    requireClose(meter.depth(), ui::meterDepthForDb(ui::gainToDb(0.25f)), 0.02f, "at -12 dB the duck settles on the scale's -12");
    require(meter.duckArea().getY() > resting + 5.0f, "lower in the water than at rest");
    require(meter.waterlineY() == waterline, "while the waterline stays where it was");
    require(meter.figure() == "-12.0 dB", "and the figure reads the reduction");

    // No news holds the last reading a moment, then the duck surfaces.
    meter.step({ 2.0f, 1 }, 0.1f);
    require(meter.depth() > 0.5f, "a frame with no block holds the duck where it was");
    for (int i = 0; i < 180; ++i) meter.step({ 2.0f, 1 }, frame);
    require(meter.depth() < 0.02f, "with nothing processed for a while it rises again");
    auto moving = false;
    for (int i = 0; i < 5; ++i) moving = meter.step({ 1.0f, 1 }, frame) || moving;
    require(!moving, "and once it rests it asks for no repaint");
    const auto image = meter.createComponentSnapshot(meter.getLocalBounds());
    require(image.isValid(), "the meter paints");
}

void theEditorFeedsTheMeter()
{
    Processor processor;
    TestPlayHead head;
    processor.setPlayHead(&head);
    processor.prepareToPlay(testRate, 480);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* panel = dynamic_cast<Editor*>(editor.get());
    require(panel != nullptr, "the editor is the panel");
    if (panel == nullptr) return;
    juce::AudioBuffer<float> buffer(4, 480);
    juce::MidiBuffer midi;
    // A cycle's first tenth, Classic cut to nothing, a frame at a time.
    for (int block = 0; block < 5; ++block)
    {
        buffer.clear();
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 480; ++i) buffer.setSample(channel, i, 0.5f);
        head.ppq = block * 480 / testRate * head.bpm / 60.0;
        processor.processBlock(buffer, midi);
        panel->advanceMeter(0.01f);
    }
    require(panel->meterView().targetDepth() > 0.95f, "a full cut sends the duck to the foot of the scale");
}
}

void layoutTests()
{
    everythingLandsInsideThePanel();
    nothingOverlaps();
    cellsHoldEveryCurveOnce();
    editorOpensAtItsSizeAndPaints();
    theStripEditsTheChosenCurve();
    theMeterReadsTheGain();
    theEditorFeedsTheMeter();
}
}
