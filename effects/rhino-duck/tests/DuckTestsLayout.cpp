#include "DuckSuites.h"
#include "DuckTestSupport.h"
#include "../src/DuckEditor.h"

#include <set>

// The panel's geometry, without a window: everything inside the panel,
// nothing on top of anything else, every curve in one cell, and the editor
// itself opened and painted.
namespace rhino::duck::tests
{
namespace
{
std::vector<std::pair<const char*, juce::Rectangle<int>>> parts(const ui::Layout& layout)
{
    std::vector<std::pair<const char*, juce::Rectangle<int>>> all {
        { "title", layout.title }, { "tagline", layout.tagline }, { "knob", layout.knob }, { "mix reading", layout.mixReading },
        { "band chip", layout.bandChip }, { "solo chip", layout.soloChip }, { "cutoff slider", layout.cutoffSlider },
        { "cutoff reading", layout.cutoffReading }, { "loop chip", layout.loopChip }, { "menu", layout.menuButton },
        { "scope", layout.scope }, { "curve caption", layout.curveCaption }
    };
    for (const auto& chip : layout.lengthChips) all.emplace_back("length chip", chip);
    for (const auto& chip : layout.triggerChips) all.emplace_back("trigger chip", chip);
    for (const auto& button : layout.shiftButtons) all.emplace_back("shift button", button);
    for (const auto& cell : layout.curveCells) all.emplace_back("curve cell", cell);
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
    for (int i = 0; i < 16; ++i)
        require(layout.curveCells[static_cast<size_t>(i)].getWidth() >= 56 && layout.curveCells[static_cast<size_t>(i)].getHeight() >= 44,
                "a curve cell is big enough to read");
    require(layout.knob.getWidth() == layout.knob.getHeight(), "the knob is square");
}

void nothingOverlaps()
{
    const auto all = parts(ui::layoutFor({ 0, 0, ui::panelWidth, ui::panelHeight }));
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
            require(!all[a].second.intersects(all[b].second), "two parts of the panel overlap");
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
        }
}
}

void layoutTests()
{
    everythingLandsInsideThePanel();
    nothingOverlaps();
    cellsHoldEveryCurveOnce();
    editorOpensAtItsSizeAndPaints();
}
}
