// The matrix as it is seen: how many of its thirty rows are on the table,
// where the window over them is scrolled to, and painting the table itself.
// Everything here is view state; what a slot routes, and every edit to one,
// is ForgeEditorModulation.cpp's.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
const ui::Module* Editor::matrixModule() const
{
    for (const auto& module : ui::modules())
        if (ui::isMatrixModule(module)) return &module;
    return nullptr;
}

// A slot with either end set counts, not only a live one: a row the hand is
// halfway through setting up keeps a blank row after it, so there is always
// somewhere to start the next routing until the thirty are spent.
int Editor::matrixShownRowCount() const
{
    auto last = -1;
    for (int slot = 0; slot < modSlotCount; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) > 0
            || juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) > 0)
            last = slot;
    return ui::matrixShownRowCount(last);
}

juce::String Editor::matrixHeaderDetail() const
{
    auto live = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
        if (slotIsLive(slot)) ++live;
    return juce::String(live) + " ROUTED // " + juce::String(modSlotCount) + " SLOTS";
}

bool Editor::clampMatrixScroll()
{
    const auto* module = matrixModule();
    if (module == nullptr) return false;
    const auto clamped = juce::jlimit(
        0, ui::matrixMaxFirstRow(moduleAreaFor(*module), *module, matrixShownRowCount()), matrixFirstRow);
    if (clamped == matrixFirstRow) return false;
    matrixFirstRow = clamped;
    return true;
}

void Editor::setMatrixFirstRow(int row)
{
    const auto* module = matrixModule();
    if (module == nullptr) return;
    const auto clamped = juce::jlimit(
        0, ui::matrixMaxFirstRow(moduleAreaFor(*module), *module, matrixShownRowCount()), row);
    if (clamped == matrixFirstRow) return;
    matrixFirstRow = clamped;
    resized();
    repaint();
}

void Editor::revealMatrixSlot(int slot)
{
    const auto* module = matrixModule();
    if (module == nullptr || slot < 0 || slot >= modSlotCount) return;
    const auto visible = ui::matrixVisibleRowCount(moduleAreaFor(*module), *module);
    setMatrixFirstRow(slot < matrixFirstRow ? slot
                      : slot >= matrixFirstRow + visible ? slot - visible + 1 : matrixFirstRow);
}

// The furniture that makes rows of fields read as a table: the column titles,
// once, above the rows; a rule under them; every row in the window numbered in
// the gutter, against a band on alternate rows; and the thumb that says how
// far down the thirty the window is. The titles and the rail are laid out from
// the module's own area, the rows from the area scrolled past them.
void Editor::paintTable(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& descriptor)
{
    const auto accent = accentOf(descriptor);
    const auto titles = ui::columnTitleBounds(area, descriptor);

    ui::drawColumnTitle(g, titles.withWidth(descriptor.rowGutter), "#");
    const auto& first = descriptor.rows.front().controls;
    for (int i = 0; i < static_cast<int>(first.size()); ++i)
    {
        const auto cell = ui::cellBounds(area, descriptor, 0, i);
        ui::drawColumnTitle(g, titles.withX(cell.getX()).withWidth(cell.getWidth()),
                            first[static_cast<size_t>(i)].label);
    }
    ui::drawColumnTitleRule(g, titles);

    const auto shown = matrixShownRowCount();
    const auto visible = ui::matrixVisibleRowCount(area, descriptor);
    const auto scrolled = ui::matrixScrolledBounds(area, descriptor, matrixFirstRow);
    const auto last = juce::jmin(shown, matrixFirstRow + visible);
    for (int row = matrixFirstRow; row < last; ++row)
        ui::drawTableRow(g, ui::rowBounds(scrolled, descriptor, row),
                         ui::rowGutterBounds(scrolled, descriptor, row),
                         row + 1, slotIsLive(row), accent);
    drawScrollThumb(g, ui::matrixScrollBounds(area, descriptor), matrixFirstRow, visible, shown);
}
}
