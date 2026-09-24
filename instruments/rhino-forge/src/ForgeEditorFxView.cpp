// The effects rack as it is seen: where the list and the rack are each
// scrolled to, bringing a slot into view in both, and painting the shelves, the
// list and their scroll thumbs. Everything here is view state; what the slots
// hold, and every edit to the chain, is ForgeEditorFx.cpp's.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
juce::Rectangle<int> Editor::fxRackAreaFor(juce::Rectangle<int> moduleArea, int rack, int slot) const
{
    const auto displayRow = fxDisplayRow(rack, slot);
    const auto first = fxFirstVisibleSlots[static_cast<size_t>(rack)];
    return ui::fxScrolledRackBounds(moduleArea, fxListOpen, first)
        .translated(0, (displayRow - slot) * ui::fxSlotHeight);
}

int Editor::fxFirstVisibleSlot() const
{
    return fxFirstVisibleSlots[static_cast<size_t>(shownRack())];
}

int Editor::fxListFirstRow() const
{
    return fxListFirstRows[static_cast<size_t>(shownRack())];
}

void Editor::clampFxScroll()
{
    for (const auto& module : ui::modules())
    {
        if (!isFxModule(module)) continue;
        const auto area = moduleAreaFor(module);
        for (int rack = 0; rack < rackCount; ++rack)
        {
            const auto active = fxActiveSlotCount(rack);
            auto& first = fxFirstVisibleSlots[static_cast<size_t>(rack)];
            first = juce::jlimit(0, ui::fxMaxFirstSlot(area, active), first);
            auto& listFirst = fxListFirstRows[static_cast<size_t>(rack)];
            listFirst = juce::jlimit(0, ui::fxListMaxFirstRow(area, active), listFirst);
        }
        return;
    }
}

// The list is painted rather than built of components, so scrolling it moves
// nothing but pixels and the rack beside it is left exactly where it was.
void Editor::setFxListFirstRow(int row)
{
    for (const auto& module : ui::modules())
    {
        if (!isFxModule(module)) continue;
        auto& first = fxListFirstRows[static_cast<size_t>(shownRack())];
        const auto clamped = juce::jlimit(
            0, ui::fxListMaxFirstRow(moduleAreaFor(module), fxActiveSlotCount(shownRack())), row);
        if (first == clamped) return;
        first = clamped;
        fxDragSlot = fxDropSlot = -1;
        repaintFxDisplays();
        return;
    }
}

// The least movement that puts a slot on screen in both views: neither scrolls
// at all if the slot is already showing in it.
void Editor::revealFxSlot(int slot)
{
    const auto row = fxDisplayRow(shownRack(), slot);
    if (row < 0) return;
    for (const auto& module : ui::modules())
    {
        if (!isFxModule(module)) continue;
        const auto area = moduleAreaFor(module);
        const auto nearest = [row] (int first, int visible)
        {
            return row < first ? row : row >= first + visible ? row - visible + 1 : first;
        };
        setFxListFirstRow(nearest(fxListFirstRow(), ui::fxListVisibleRowCount(area)));
        setFxFirstVisibleSlot(nearest(fxFirstVisibleSlot(), ui::fxVisibleSlotCount(area)));
        return;
    }
}

void Editor::setFxFirstVisibleSlot(int slot)
{
    for (const auto& module : ui::modules())
    {
        if (!isFxModule(module)) continue;
        auto& first = fxFirstVisibleSlots[static_cast<size_t>(shownRack())];
        const auto clamped = juce::jlimit(
            0, ui::fxMaxFirstSlot(moduleAreaFor(module), fxActiveSlotCount(shownRack())), slot);
        if (first == clamped) return;
        first = clamped;
        fxDragSlot = fxDropSlot = -1;
        resized();
        applyEnableStates();
        repaintFxDisplays();
        return;
    }
}

// A shelf behind each slot, lit down its left edge in the type's colour. Drawn
// here rather than by the module shell because there are several inside
// one module, and which colour each takes is a parameter rather than a
// declaration.
// Only the rack's own box, rather than the whole panel: this runs for every
// step of a knob being turned.
void Editor::repaintFxDisplays()
{
    for (const auto& module : ui::modules())
        if (isFxModule(module))
            repaint(moduleAreaFor(module));
}

// A thin rail and a thumb, for whichever of the two views has more rows than
// it shows.
static void paintFxScrollThumb(juce::Graphics& g, juce::Rectangle<int> rail,
                               int first, int visible, int total)
{
    if (visible >= total || rail.isEmpty()) return;
    const auto thumbHeight = juce::jmax(18, rail.getHeight() * visible / total);
    const auto travel = rail.getHeight() - thumbHeight;
    const auto top = rail.getY() + travel * first / (total - visible);
    g.setColour(ui::line.withAlpha(0.55f));
    g.fillRoundedRectangle(rail.toFloat(), 1.5f);
    g.setColour(ui::signalViolet.withAlpha(0.8f));
    g.fillRoundedRectangle(rail.withY(top).withHeight(thumbHeight).toFloat(), 1.5f);
}

void Editor::paintFxShelves(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& module)
{
    const auto rack = shownRack();
    {
        juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(ui::fxSlotViewportBounds(area, fxListOpen));
        for (int slot = 0; slot < static_cast<int>(module.rows.size()) && slot < fxSlotCount; ++slot)
        {
            if (fxDisplayRow(rack, slot) < 0) continue;
            const auto rackArea = fxRackAreaFor(area, rack, slot);
            const auto row = ui::rowBounds(rackArea, module, slot);
            const auto held = fxSlotOf(rack, slot);
            // Widened past the controls by the module's own padding, so the shelves
            // read as the full width of the rack rather than as a box around the
            // knobs.
            ui::drawFxShelf(g, row.expanded(6, 1), juce::roundToInt(held.type), true);
            // A slot that is bypassed still says what is in it, dimmed — the point
            // of a bypass is to hear a rack without it and put it straight back.
            ui::drawFxDisplay(g, ui::rowDisplayBounds(rackArea, module, slot).reduced(4, 6), held,
                              processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0,
                              processor.hostTempo(), fxOn(held.bypass) ? 0.3f : 1.0f);
            if (slot == fxSelectedSlot)
            {
                g.setColour(ui::fxTypeColour(juce::roundToInt(held.type)).withAlpha(0.65f));
                g.drawRoundedRectangle(row.expanded(5, 0).toFloat().reduced(0.5f, 2.5f), 4.0f, 1.2f);
            }
        }
    }
    paintFxScrollThumb(g, ui::fxRackScrollBounds(area), fxFirstVisibleSlot(),
                       ui::fxVisibleSlotCount(area), fxActiveSlotCount(rack));
}

void Editor::paintFxList(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module&)
{
    const auto rack = shownRack();
    const auto list = ui::fxListBounds(area, fxListOpen);
    const auto viewport = ui::fxListViewportBounds(area, fxListOpen);
    const auto first = fxListFirstRow();
    g.setColour(juce::Colour(0xff070a12));
    g.fillRoundedRectangle(list.toFloat(), 4.0f);
    g.setColour(ui::line.withAlpha(0.7f));
    g.drawRoundedRectangle(list.toFloat(), 4.0f, 1.0f);
    const auto active = fxActiveSlotCount(rack);
    ui::drawFxAddButton(g, ui::fxAddButtonBounds(area, fxListOpen), fxListOpen,
                        active < fxSlotCount);

    {
        juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(viewport);
        for (int slot = 0; slot < fxSlotCount; ++slot)
        {
            const auto displayRow = fxDisplayRow(rack, slot);
            if (displayRow < 0) continue;
            const auto held = fxSlotOf(rack, slot);
            ui::drawFxListItem(g, ui::fxListItemBounds(area, displayRow, fxListOpen, first),
                               juce::roundToInt(held.type), fxOn(held.bypass),
                               slot == fxSelectedSlot, fxListOpen);
        }

        if (fxDragSlot >= 0 && fxDropSlot >= 0 && fxDropSlot != fxDragSlot)
        {
            const auto target = ui::fxListItemBounds(area, fxDisplayRow(rack, fxDropSlot),
                                                      fxListOpen, first);
            const auto y = fxDropSlot > fxDragSlot ? target.getBottom() : target.getY();
            g.setColour(ui::signalViolet);
            g.fillRoundedRectangle(static_cast<float>(list.getX() + 5), static_cast<float>(y - 1),
                                   static_cast<float>(list.getWidth() - 10), 2.0f, 1.0f);
        }
    }
    paintFxScrollThumb(g, viewport.withLeft(list.getRight() - 4).reduced(0, 2), first,
                       ui::fxListVisibleRowCount(area), active);

    ui::drawFxListViewButton(g, ui::fxListButtonBounds(area), fxListOpen);
    ui::drawFxExpandButton(g, ui::fxExpandButtonBounds(area), fxExpanded);
}
}
