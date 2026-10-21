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
// --- Dragging a picture by its node -----------------------------------------------
//
// The rack's pictures are painted by the editor rather than being components,
// so a press on one arrives here. Only the rack on screen can be taken hold
// of, and only where its viewport shows it; what a press takes and what a
// drag sets is ForgeFxGestures.h's.
bool Editor::fxWellAt(juce::Point<int> at, int& rack, int& slot, ui::FxWell& well, ui::FxWellGrab& grab) const
{
    if (page != ui::Page::fx) return false;
    for (const auto& module : ui::modules())
    {
        if (!isFxModule(module)) continue;
        const auto area = moduleAreaFor(module);
        if (!ui::fxSlotViewportBounds(area, fxListOpen).contains(at)) continue;
        rack = shownRack();
        const auto sizes = ui::fxKnobSizesFor(getLocalBounds());
        for (slot = 0; slot < static_cast<int>(module.rows.size()) && slot < fxSlotCount; ++slot)
        {
            if (fxDisplayRow(rack, slot) < 0) continue;
            const auto strip = fxStripFor(area, module, rack, slot, sizes);
            if (!strip.strip.contains(at)) continue;
            const auto held = fxSlotOf(rack, slot);
            for (int index = 0; index < strip.wellCount; ++index)
            {
                well = strip.wells[static_cast<size_t>(index)];
                if (ui::fxWellGrabAt(well, held, at.toFloat(), grab)) return true;
            }
            return false;
        }
    }
    return false;
}

bool Editor::beginFxWellDrag(juce::Point<int> at)
{
    FxWellDrag drag;
    if (!fxWellAt(at, drag.rack, drag.slot, drag.well, drag.grab)) return false;
    drag.active = true;
    fxWellDrag = drag;
    // A node goes to where it was pressed, as a slider's thumb does; a handle
    // dragged by how far it travels waits for the pointer to move.
    if (ui::fxWellFollowsPointer(drag.grab.kind)) dragFxWell(at);
    return true;
}

void Editor::dragFxWell(juce::Point<int> at)
{
    if (!fxWellDrag.active) return;
    const auto edit = ui::fxWellDragTo(fxWellDrag.well, fxWellDrag.grab, at.toFloat(),
                                       processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0,
                                       processor.hostTempo());
    for (int index = 0; index < edit.count; ++index)
    {
        const auto knob = edit.knobs[static_cast<size_t>(index)];
        auto* parameter = processor.state.getParameter(fxParameterId(fxWellDrag.rack, fxWellDrag.slot, "Knob")
                                                       + juce::String(knob + 1));
        if (parameter == nullptr) continue;
        auto& touched = fxWellDrag.touched[static_cast<size_t>(knob)];
        if (!touched)
        {
            parameter->beginChangeGesture();
            touched = true;
        }
        parameter->setValueNotifyingHost(parameter->convertTo0to1(edit.values[static_cast<size_t>(index)]));
    }
    repaintFxDisplays();
}

void Editor::endFxWellDrag()
{
    if (!fxWellDrag.active) return;
    for (int knob = 0; knob < fxKnobCount; ++knob)
        if (fxWellDrag.touched[static_cast<size_t>(knob)])
            if (auto* parameter = processor.state.getParameter(fxParameterId(fxWellDrag.rack, fxWellDrag.slot, "Knob")
                                                               + juce::String(knob + 1)))
                parameter->endChangeGesture();
    fxWellDrag = {};
}

// One well: its picture blitted from the copy kept of it, drawn again only
// when something it is drawn from has moved, and what moves with the sound
// drawn over it.
void Editor::paintFxWell(juce::Graphics& g, int rack, int slot, int index, const ui::FxWell& well,
                         const FxSlot& held, bool bypassed, const ui::FxLive& live)
{
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    const auto bpm = processor.hostTempo();
    const auto alpha = bypassed ? 0.3f : 1.0f;
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    auto local = well;
    local.area = well.area.withZeroOrigin();
    local.plot = well.plot - well.area.getPosition();
    const auto pictureLive = ui::fxWellPictureLive(well.kind, live);

    ui::FxPictureKey key;
    key.kind = well.kind;
    key.area = local.area;
    key.plot = local.plot;
    key.type = held.type;
    key.modeA = held.modeA;
    key.modeB = held.modeB;
    key.knobs = held.knobs;
    key.sampleRate = sampleRate;
    key.bpm = bpm;
    key.alpha = alpha;
    key.scale = scale;
    key.live = pictureLive.valid;
    key.phase = pictureLive.phase;

    auto& cache = fxPictures[static_cast<size_t>((rack * fxSlotCount + slot) * ui::maxFxWells + index)];
    // A picture whose size is still changing -- a window edge being dragged --
    // is drawn straight onto the panel, since a copy would be thrown away the
    // next frame. Its size is remembered, and once it holds for a frame the
    // picture is kept.
    if (cache.drawnFrom.area != key.area || cache.drawnFrom.scale != key.scale)
    {
        cache.drawnFrom = key;
        cache.drawn = false;
        ui::drawFxWell(g, well, held, sampleRate, bpm, alpha, live);
        return;
    }
    if (!cache.drawn || !(cache.drawnFrom == key))
    {
        const auto width = juce::jmax(1, static_cast<int>(std::ceil(static_cast<float>(local.area.getWidth()) * scale)));
        const auto height = juce::jmax(1, static_cast<int>(std::ceil(static_cast<float>(local.area.getHeight()) * scale)));
        if (cache.image.getWidth() != width || cache.image.getHeight() != height)
            cache.image = juce::Image(juce::Image::ARGB, width, height, true);
        else
            cache.image.clear(cache.image.getBounds());
        juce::Graphics into(cache.image);
        into.addTransform(juce::AffineTransform::scale(scale));
        ui::drawFxWellPicture(into, local, held, sampleRate, bpm, alpha, pictureLive);
        cache.drawnFrom = key;
        cache.drawn = true;
    }
    g.drawImageTransformed(cache.image, juce::AffineTransform::scale(1.0f / scale)
                                            .translated(well.area.getPosition().toFloat()));
    ui::drawFxWellLive(g, well, held, bpm, alpha, live);
}

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
                paintFxWell(g, rack, slot, well, strip.wells[static_cast<size_t>(well)], held, bypassed, live);
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
