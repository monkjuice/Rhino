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
void Editor::takeFxMeters()
{
    // Only the rack on screen is drawn, and only while the FX page shows, so
    // the others are left to hold their peaks rather than be taken unseen.
    if (page != ui::Page::fx) return;
    const auto rack = shownRack();
    for (int slot = 0; slot < fxSlotCount; ++slot)
    {
        const auto taken = processor.takeFxMeter(rack, slot);
        auto& live = fxLive[static_cast<size_t>(rack * fxSlotCount + slot)];
        live.valid = taken.rendered;
        live.phase = taken.phase;
        live.inputPeak = taken.inputPeak;
        live.outputPeak = taken.outputPeak;
        live.reductionDb = taken.reductionDb < live.reductionDb
            ? taken.reductionDb : live.reductionDb + (taken.reductionDb - live.reductionDb) * 0.35f;
    }
}

void Editor::repaintFxDisplays()
{
    for (const auto& module : ui::modules())
        if (isFxModule(module))
            repaint(moduleAreaFor(module));
}

void Editor::paintFxShelves(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& module)
{
    const auto rack = shownRack();
    const auto sizes = ui::fxKnobSizesFor(getLocalBounds());
    {
        juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(ui::fxSlotViewportBounds(area, fxListOpen));
        for (int slot = 0; slot < static_cast<int>(module.rows.size()) && slot < fxSlotCount; ++slot)
        {
            if (fxDisplayRow(rack, slot) < 0) continue;
            const auto rackArea = fxRackAreaFor(area, rack, slot);
            const auto row = ui::rowBounds(rackArea, module, slot);
            const auto strip = fxStripFor(area, module, rack, slot, sizes);
            const auto held = fxSlotOf(rack, slot);
            // Widened past the controls by the module's own padding, so the shelves
            // read as the full width of the rack rather than as a box around the
            // knobs.
            ui::drawFxShelf(g, row.expanded(6, 1), juce::roundToInt(held.type), true);
            const auto bypassed = fxOn(held.bypass);
            paintFxFrame(g, strip, rack, slot, bypassed);
            // A slot that is bypassed still says what is in it, dimmed — the point
            // of a bypass is to hear a rack without it and put it straight back.
            const auto& live = fxLive[static_cast<size_t>(rack * fxSlotCount + slot)];
            paintFxPanels(g, strip, held, bypassed, live);
            for (int well = 0; well < strip.wellCount; ++well)
                ui::drawFxWell(g, strip.wells[static_cast<size_t>(well)], held,
                               processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0,
                               processor.hostTempo(), bypassed ? 0.3f : 1.0f, live);
            paintFxReadings(g, strip, rack, slot, juce::roundToInt(held.type), bypassed);
            if (slot == fxSelectedSlot)
            {
                g.setColour(ui::fxTypeColour(juce::roundToInt(held.type)).withAlpha(0.65f));
                g.drawRoundedRectangle(row.expanded(5, 0).toFloat().reduced(0.5f, 2.5f), 4.0f, 1.2f);
            }
        }
    }
    drawScrollThumb(g, ui::fxRackScrollBounds(area), fxFirstVisibleSlot(),
                    ui::fxVisibleSlotCount(area), fxActiveSlotCount(rack));
}

// A strip's panels, and the chevron between two of them where the type has
// one. A panel's gate and lamp are read from the slot as it stands.
void Editor::paintFxPanels(juce::Graphics& g, const ui::FxStrip& strip, const FxSlot& held, bool bypassed,
                           const ui::FxLive& live)
{
    const auto type = fxTypeOf(held.type);
    const auto colour = ui::fxTypeColour(static_cast<int>(type));
    const auto& info = fxTypeInfo(held.type);
    const auto alpha = bypassed ? 0.5f : 1.0f;
    for (int index = 0; index < strip.panelCount; ++index)
    {
        const auto& panel = strip.panels[static_cast<size_t>(index)];
        auto dim = false;
        if (panel.gate == ui::FxRole::modeA) dim = fxModeOf(info.modeA, held.modeA) == panel.gateOff;
        else if (panel.gate == ui::FxRole::modeB) dim = fxModeOf(info.modeB, held.modeB) == panel.gateOff;
        const auto tint = panel.lamp == ui::FxLamp::highBand ? ui::fxEqHighColour : colour;
        ui::drawFxPanel(g, panel, tint, dim, ui::fxLampLevel(panel, held, live), alpha);
    }
    if (!strip.chevron.isEmpty() && type == FxType::distortion)
    {
        const auto placement = fxDistortionFilterPlacement(held);
        ui::drawFxChevron(g, strip.chevron, placement == 0 ? 0 : placement == 1 ? 1 : -1,
                          placement == 1 ? "PRE" : "POST", colour, alpha);
    }
}

// What every strip draws round its body: the slot's automation name under its
// card, and the hatch its output tail starts with.
void Editor::paintFxFrame(juce::Graphics& g, const ui::FxStrip& strip, int rack, int slot, bool bypassed)
{
    // The name a host's automation lane and the matrix give this slot's
    // controls: MAIN 2 Knob 3 is the third knob on the strip that says MAIN 2.
    g.setFont(ui::panelFont(ui::Face::reading, 8.5f));
    g.setColour(ui::mutedText.withAlpha(0.8f));
    g.drawText(juce::String(rackName(rack)) + " " + juce::String(slot + 1), strip.automation,
               juce::Justification::centredRight, false);
    ui::drawFxHatch(g, strip.hatch, bypassed ? 0.5f : 1.0f);
}

// Every knob's value, printed under it at rest. The rack is the one place on
// the panel that does this: a strip has the height for a line of figures under
// its knobs, and a delay's time or a compressor's threshold is a number you set
// by reading it. The text is the parameter's own, so it is what the bubble
// says and what the DSP is doing.
void Editor::paintFxReadings(juce::Graphics& g, const ui::FxStrip& strip, int rack, int slot, int type,
                             bool bypassed)
{
    g.setFont(ui::panelFont(ui::Face::reading, 9.5f));
    for (int role = ui::fxRoleIndex(ui::FxRole::knob1); role <= ui::fxRoleIndex(ui::FxRole::level); ++role)
    {
        const auto which = static_cast<ui::FxRole>(role);
        const auto& spot = strip.spot(which);
        if (!spot.placed || spot.reading.isEmpty()) continue;
        auto* control = fxControl(rack, slot, which);
        if (control == nullptr || !control->slider.isVisible()) continue;
        const auto hero = spot.knob == ui::FxKnobSize::hero;
        auto colour = hero ? ui::fxTypeColour(type) : ui::valueText.withAlpha(0.72f);
        if (!control->slider.isEnabled()) colour = colour.withMultipliedAlpha(0.45f);
        if (bypassed) colour = colour.withMultipliedAlpha(0.5f);
        g.setColour(colour);
        g.drawText(control->slider.getTextFromValue(control->slider.getValue()), spot.reading,
                   spot.captionLeft ? juce::Justification::centredLeft : juce::Justification::centred, false);
    }
}

void Editor::paintFxList(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module&)
{
    const auto rack = shownRack();
    const auto list = ui::fxListBounds(area, fxListOpen);
    const auto viewport = ui::fxListViewportBounds(area, fxListOpen);
    const auto first = fxListFirstRow();
    g.setColour(ui::shadeFloor);
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
    drawScrollThumb(g, viewport.withLeft(list.getRight() - 4).reduced(0, 2), first,
                    ui::fxListVisibleRowCount(area), active);

    ui::drawFxListViewButton(g, ui::fxListButtonBounds(area), fxListOpen);
    ui::drawFxExpandButton(g, ui::fxExpandButtonBounds(area), fxExpanded);
}
}
