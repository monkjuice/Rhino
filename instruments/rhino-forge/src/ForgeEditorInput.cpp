// The hand and the keyboard: what a press, a drag, a wheel notch or a key
// does, the bubble that says what a control now reads, and the 24Hz timer
// that carries the engine's live values back onto the panel.
#include "ForgeEditorInternal.h"

#include <algorithm>

namespace rhino::forge
{
// What a dragged source can be dropped on. A knob always answers, whether or not
// the matrix can reach it, so a drop on one it cannot reach is refused out loud
// rather than passing through to whatever is behind it. Every other style that
// can show a routing — the numeric fields — is a target only when it really is
// a destination, which keeps the ones that merely look alike out of the way of
// a drag crossing the panel: an LFO's shape, a channel's routing and the
// matrix's own columns are all fields, and none of them is modulated.
Editor::Control* Editor::controlAt(juce::Point<int> panelPosition)
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
            if (control->slider.isVisible() && ui::showsModulation(control->style)
                && control->slider.getBounds().contains(panelPosition)
                && (control->style == ui::Style::knob || destinationFor(control->id) != 0))
                return control.get();
    return nullptr;
}

void Editor::mouseMove(const juce::MouseEvent& event)
{
    if (event.eventComponent != this) return;
    const auto at = event.getEventRelativeTo(this).getPosition();
    if (envelopeNodeAt(at) != ui::EnvelopeNode::none)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (const auto spectral = spectralDisplayAt(at); spectral >= 0)
    {
        const auto marker = spectralMarkerAt(spectral, at);
        setMouseCursor(marker == SpectralMarker::none ? juce::MouseCursor::NormalCursor
                       : marker == SpectralMarker::loop ? juce::MouseCursor::DraggingHandCursor
                                                        : juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    const auto display = lfoDisplayBounds();
    const auto type = shownLfoType();
    const auto column = ui::lfoColumnBounds(display);
    const auto row = ui::lfoRowBounds(display);
    const auto overGridStep = ui::lfoGridStepBounds(column, true).contains(at)
        || ui::lfoGridStepBounds(column, false).contains(at)
        || ui::lfoGridStepBounds(row, true).contains(at)
        || ui::lfoGridStepBounds(row, false).contains(at);
    const auto overShapeControl = ui::lfoTypeBounds(display).contains(at)
        || (ui::lfoFooterShowsName(type) && ui::lfoNameBounds(display, type).contains(at))
        || (ui::lfoFooterShowsChevrons(type)
            && (ui::lfoPreviousBounds(display).contains(at) || ui::lfoNextBounds(display).contains(at)))
        || (ui::lfoFooterShowsDirection(type) && ui::lfoDirectionBounds(display).contains(at))
        || (ui::lfoPlotBounds(display).contains(at)
            && (lfoPointAt(at) >= 0 || lfoCurveHandleAt(at) >= 0));
    if (overGridStep || overShapeControl) setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else if (column.contains(at) || row.contains(at)) setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else setMouseCursor(juce::MouseCursor::NormalCursor);
}

void Editor::mouseDown(const juce::MouseEvent& event)
{
    if (event.eventComponent == this)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (!event.mods.isPopupMenu() && event.getNumberOfClicks() < 2)
        {
            const auto node = envelopeNodeAt(at);
            if (node != ui::EnvelopeNode::none)
            {
                envelopeDragStart = at;
                beginEnvelopeNodeDrag(node);
                return;
            }
        }
        // A spectral oscillator's display is how its sample is chosen. There
        // is no cell free in the module for a chooser field — see the mode
        // decision in ui/ForgeModules.h — and the picture is what the sample
        // is, so clicking it is the most direct thing available. The same hit
        // test answers the tooltip, so the two cannot disagree about where the
        // picture is.
        if (const auto spectral = spectralDisplayAt(at); spectral >= 0)
        {
            // Unless the press is on one of its markers, which it picks up
            // instead: the picture is the chooser everywhere the markers are
            // not.
            const auto marker = spectralMarkerAt(spectral, at);
            if (marker != SpectralMarker::none && !event.mods.isPopupMenu())
            {
                beginSpectralMarkerDrag(spectral, marker, at);
                return;
            }
            showSampleMenu(spectral);
            return;
        }

        const auto display = lfoDisplayBounds();
        if (display.contains(at))
        {
            // The footer's fields, only the ones the type has a use for —
            // the same rule the painter draws them by.
            const auto type = shownLfoType();
            const auto plain = !event.mods.isPopupMenu();
            if (ui::lfoTypeBounds(display).contains(at) && plain)
            { showLfoTypeMenu(); return; }
            if (ui::lfoFooterShowsName(type) && ui::lfoNameBounds(display, type).contains(at) && plain)
            { showLfoMenu(); return; }
            if (ui::lfoFooterShowsChevrons(type) && ui::lfoPreviousBounds(display).contains(at) && plain)
            { stepLfoShape(-1); return; }
            if (ui::lfoFooterShowsChevrons(type) && ui::lfoNextBounds(display).contains(at) && plain)
            { stepLfoShape(1); return; }
            if (ui::lfoFooterShowsDirection(type) && ui::lfoDirectionBounds(display).contains(at) && plain)
            { showLfoDirectionMenu(); return; }
            for (int axis = 0; axis < 2; ++axis)
            {
                const auto field = axis == 0 ? ui::lfoColumnBounds(display) : ui::lfoRowBounds(display);
                if (!field.contains(at) || event.mods.isPopupMenu()) continue;
                const auto bank = shownLfo();
                const auto table = processor.lfoTable(bank);
                const auto current = axis == 0 ? table.columns : table.rows;
                if (ui::lfoGridStepBounds(field, true).contains(at))
                { setLfoGridCount(bank, axis == 0, current + 1); return; }
                if (ui::lfoGridStepBounds(field, false).contains(at))
                { setLfoGridCount(bank, axis == 0, current - 1); return; }
                if (event.getNumberOfClicks() >= 2)
                { setLfoGridCount(bank, axis == 0, 8); return; }
                lfoGridDragAxis = axis;
                lfoGridDragBank = bank;
                lfoGridStartCount = current;
                lfoGridStartY = at.y;
                return;
            }
            if (ui::lfoPlotBounds(display).expanded(5, 0).contains(at))
            {
                // A position node first, then the curve handle of the gap it
                // would otherwise sit in, then the empty well. Straightening a
                // segment is to a curve handle what removing is to a point, so
                // the right-click and the double-click read the same either way.
                const auto point = lfoPointAt(at);
                const auto curve = point >= 0 ? -1 : lfoCurveHandleAt(at);
                if (event.mods.isPopupMenu())
                {
                    if (curve >= 0) resetLfoCurve(shownLfo(), curve);
                    else removeLfoPoint(shownLfo(), point);
                    return;
                }
                if (event.getNumberOfClicks() >= 2)
                {
                    if (curve >= 0) resetLfoCurve(shownLfo(), curve);
                    else if (point >= 0) removeLfoPoint(shownLfo(), point);
                    else addLfoPoint(shownLfo(), at);
                    return;
                }
                lfoDragPoint = point;
                lfoDragCurve = curve;
                lfoDragBank = shownLfo();
                lfoDragStart = at;
                return;
            }
        }
    }
    if (auto* handle = dynamic_cast<ui::SourceHandle*>(event.eventComponent))
    {
        // A macro's plate is two things in one small rectangle: the number is
        // the grip, and the count under it is a door onto what this macro is
        // driving. Which one the press meant is decided by where it landed,
        // and the plate itself owns that geometry.
        if (auto* plate = dynamic_cast<ui::MacroPlate*>(handle))
            if (plate->opensMenu(event.getPosition(), event.mods.isPopupMenu()))
            {
                showMacroMenu(macroIndexOf(plate->source));
                return;
            }
        draggingHandle = handle;
        handle->dragging = true;
        dragPosition = event.getEventRelativeTo(this).getPosition();
        repaint();
        return;
    }
    // Only for a click that landed on the panel itself: the editor listens to
    // every knob and handle as well, and those events carry their own component.
    if (event.eventComponent == this && !event.mods.isPopupMenu())
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        // The plate beside the keys, before anything a tab owns: it is on the
        // chassis rather than on a page, and it is reachable from every one of
        // them.
        if (ui::arpPlateBounds(getLocalBounds()).contains(at))
        {
            if (ui::arpPlateLedBounds(getLocalBounds()).contains(at)) toggleArpEnabled();
            else if (const auto slot = ui::arpSlotAt(getLocalBounds(), at); slot >= 0) chooseArpSlot(slot);
            else toggleArpOpen();
            return;
        }
        if (page == ui::Page::fx)
            for (const auto& module : ui::modules())
            {
                if (!isFxModule(module)) continue;
                const auto area = moduleAreaFor(module);
                if (ui::fxExpandButtonBounds(area).contains(at)) { toggleFxExpanded(); return; }
                if (ui::fxListButtonBounds(area).contains(at)) { toggleFxList(); return; }
                if (ui::fxAddButtonBounds(area, fxListOpen).contains(at)) { addFxSlot(); return; }
                const auto list = ui::fxListViewportBounds(area, fxListOpen);
                const auto rack = shownRack();
                for (int slot = 0; slot < fxSlotCount; ++slot)
                {
                    const auto displayRow = fxDisplayRow(rack, slot);
                    if (displayRow < 0) continue;
                    const auto item = ui::fxListItemBounds(area, displayRow, fxListOpen,
                                                            fxListFirstRow());
                    if (!list.contains(at) || !item.contains(at)) continue;
                    if (fxListOpen && ui::fxListBypassBounds(item).contains(at))
                    {
                        const auto held = fxSlotOf(rack, slot);
                        if (fxTypeOf(held.type) != FxType::off)
                            setFxSlotBypassed(rack, slot, !fxOn(held.bypass));
                        return;
                    }
                    if (fxListOpen && ui::fxListRemoveBounds(item).contains(at))
                    {
                        if (fxTypeOf(fxSlotOf(rack, slot).type) != FxType::off)
                            removeFxSlot(rack, slot);
                        return;
                    }
                    fxSelectedSlot = slot;
                    // On the press rather than the release, so the strip is
                    // already in front of you by the time the button comes
                    // up. Before the drag starts, because scrolling ends one.
                    revealFxSlot(slot);
                    fxDragSlot = fxDropSlot = slot;
                    fxDragStart = at;
                    repaintFxDisplays();
                    // A single click selects/drags; a deliberate double-click
                    // opens replacement choices for the module already here.
                    if (event.getNumberOfClicks() >= 2)
                    {
                        fxDragSlot = fxDropSlot = -1;
                        if (auto* control = fxTypeControl(rack, slot)) showFxTypeMenu(*control);
                    }
                    return;
                }
            }
        if (const auto display = envelopeDisplayBounds(); !display.isEmpty())
        {
            const auto zoom = envelopeZoom[static_cast<size_t>(shownEnv())];
            if (ui::envelopeZoomIn(display).contains(at)) { setEnvelopeZoom(zoom - 1); return; }
            if (ui::envelopeZoomOut(display).contains(at)) { setEnvelopeZoom(zoom + 1); return; }
            // Double-clicking the plot puts the window back to three seconds,
            // which is the gesture a knob already uses to go back to the value
            // it started at.
            if (event.getNumberOfClicks() >= 2 && ui::envelopePlotBounds(display).contains(at))
            {
                setEnvelopeZoom(ui::envelopeDefaultZoom);
                return;
            }
        }
    }
    if (!event.mods.isPopupMenu()) return;
    for (const auto& module : moduleUis)
        for (const auto& control : module.controls)
            if (event.eventComponent == &control->slider)
            {
                showControlMenu(control->id);
                return;
            }
}

// The computer keys play Forge wherever the focus happens to be. A key event is
// walked up from whatever holds focus to its parents, so the letter keys reach
// here once a knob has taken focus under the hand, and are handed on to the
// keyboard from here. Without this, touching any control silenced the keys until
// the keyboard itself was clicked back into focus — which is the wrong trade for
// a synth, where turning something while playing it is the whole point.
//
// Forwarding is safe when the keyboard already has focus and has handled the
// event itself: it tracks which notes its keys are holding down, so a second
// pass over the same key state starts and stops nothing.
bool Editor::keyStateChanged(bool isKeyDown)
{
    return keyboard.keyStateChanged(isKeyDown);
}

bool Editor::keyPressed(const juce::KeyPress& key)
{
    if (page == ui::Page::fx
        && key == juce::KeyPress('f', juce::ModifierKeys::altModifier, 0))
    {
        toggleFxExpanded();
        return true;
    }
    // Undo belongs to the editor rather than to the panel because a plugin
    // window gives its keyboard focus to whichever child last took it, and the
    // panel is often not that child. Scoped to the tab that has something to
    // undo, so Ctrl+Z is not swallowed anywhere else.
    if (page == ui::Page::table && tablePanel != nullptr
        && key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        tablePanel->undo();
        applyTableCounts();
        repaint();
        return true;
    }
    // z and x walk the computer keys up and down the piano. Neither letter is
    // in the keyboard's own mapping, so playing loses nothing by lending them.
    if (key == juce::KeyPress('z')) { shiftComputerKeyOctave(-1); return true; }
    if (key == juce::KeyPress('x')) { shiftComputerKeyOctave(1); return true; }
    return keyboard.keyPressed(key);
}

void Editor::shiftComputerKeyOctave(int delta)
{
    // The reach has to land inside the keys that exist, so the lowest octave is
    // the first whose C is on the piano and the highest is the last whose whole
    // reach still fits.
    const auto lowest = (keyboard.getRangeStart() + 11) / 12;
    const auto highest = (keyboard.getRangeEnd() - (computerKeySpan - 1)) / 12;
    const auto shifted = juce::jlimit(lowest, highest, computerKeyOctave + delta);
    if (shifted == computerKeyOctave) return;

    // Notes are tracked by number, so a key held across the shift would be
    // asked to stop on a number nothing started — the note hangs. This is the
    // keyboard's own way of letting everything go, and it is what it does when
    // the focus leaves it mid-chord.
    keyboard.focusLost(juce::Component::focusChangedDirectly);
    computerKeyOctave = shifted;
    keyboard.setKeyPressBaseOctave(computerKeyOctave);
    repaint();
}

// The wheel over an LFO grid field changes its divisions; over the envelope
// display it changes how much time that display spans. Up
// shortens the window, so the shape grows: up is in, the way round every other
// zoom works. The notches are accumulated because a trackpad sends a stream of small
// deltas where a mouse sends one large one, and without this a flick would run
// through the whole ladder before the hand came off it.
void Editor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto at = event.getEventRelativeTo(this).getPosition();
    const auto lfoDisplay = lfoDisplayBounds();
    for (int axis = 0; axis < 2; ++axis)
    {
        const auto field = axis == 0 ? ui::lfoColumnBounds(lfoDisplay) : ui::lfoRowBounds(lfoDisplay);
        if (!field.contains(at)) continue;
        auto& travel = lfoGridWheel[static_cast<size_t>(axis)];
        travel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        const auto notches = static_cast<int>(travel / wheelPerZoomStep);
        if (notches != 0)
        {
            travel -= static_cast<float>(notches) * wheelPerZoomStep;
            const auto table = processor.lfoTable(shownLfo());
            setLfoGridCount(shownLfo(), axis == 0,
                            (axis == 0 ? table.columns : table.rows) + notches);
        }
        return;
    }
    // The matrix scrolls under the wheel wherever a field is not taking it. A
    // field steps through its list on the wheel, and the editor hears about
    // that because it listens to every slider, with the event naming the
    // slider; the gutter, the rail, the titles, the BI chips and the gaps
    // between fields hand the wheel up, and there the table itself moves.
    if (page == ui::Page::matrix)
        if (const auto* module = matrixModule())
            if (ui::moduleInterior(moduleAreaFor(*module)).contains(at))
            {
                if (dynamic_cast<juce::Slider*>(event.eventComponent) != nullptr) return;
                matrixWheel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
                const auto notches = static_cast<int>(matrixWheel / wheelPerZoomStep);
                if (notches == 0) return;
                matrixWheel -= static_cast<float>(notches) * wheelPerZoomStep;
                setMatrixFirstRow(matrixFirstRow - notches);
                return;
            }
    // The list and the rack each scroll whichever one is under the pointer, and
    // only that one. Over the rack, a knob keeps the wheel for itself: this
    // hears about it because it listens to every knob, and the event then
    // names the knob rather than the panel. A mode field or an empty stretch
    // of shelf hands the wheel up, and that arrives naming the panel.
    if (page == ui::Page::fx)
        for (const auto& module : ui::modules())
        {
            if (!isFxModule(module)) continue;
            const auto area = moduleAreaFor(module);
            const auto overList = ui::fxListBounds(area, fxListOpen).contains(at);
            if (!overList && !ui::fxSlotViewportBounds(area, fxListOpen).contains(at)) break;
            if (!overList && event.eventComponent != this) return;
            auto& travel = overList ? fxListWheel : fxWheel;
            travel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
            const auto notches = static_cast<int>(travel / wheelPerZoomStep);
            if (notches == 0) return;
            travel -= static_cast<float>(notches) * wheelPerZoomStep;
            if (overList) setFxListFirstRow(fxListFirstRow() - notches);
            else setFxFirstVisibleSlot(fxFirstVisibleSlot() - notches);
            return;
        }

    const auto display = envelopeDisplayBounds();
    if (display.isEmpty()
        || !ui::envelopePlotBounds(display).contains(at))
    {
        Component::mouseWheelMove(event, wheel);
        return;
    }

    envelopeWheel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    const auto notches = static_cast<int>(envelopeWheel / wheelPerZoomStep);
    if (notches == 0) return;
    envelopeWheel -= static_cast<float>(notches) * wheelPerZoomStep;
    setEnvelopeZoom(envelopeZoom[static_cast<size_t>(shownEnv())] - notches);
}

void Editor::mouseDrag(const juce::MouseEvent& event)
{
    if (markerDrag != SpectralMarker::none)
    {
        dragSpectralMarker(event.getEventRelativeTo(this).getPosition());
        return;
    }
    if (envelopeDragNode != ui::EnvelopeNode::none)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (!envelopeDragMoved && at.getDistanceFrom(envelopeDragStart) < 2.0f) return;
        envelopeDragMoved = true;
        editEnvelopeNode(at);
        return;
    }
    if (lfoGridDragAxis >= 0)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        setLfoGridCount(lfoGridDragBank, lfoGridDragAxis == 0,
                        lfoGridStartCount + (lfoGridStartY - at.y) / 8);
        return;
    }
    if (lfoDragCurve >= 0)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (at.getDistanceFrom(lfoDragStart) >= 2.0f)
            editLfoCurve(lfoDragBank, lfoDragCurve, at, event.mods.isAltDown());
        return;
    }
    if (lfoDragPoint >= 0)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (at.getDistanceFrom(lfoDragStart) >= 2.0f)
            editLfoPoint(lfoDragBank, lfoDragPoint, at, event.mods.isAltDown());
        return;
    }
    if (fxDragSlot >= 0)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (at.getDistanceFrom(fxDragStart) < 4.0f) return;
        for (const auto& module : ui::modules())
        {
            if (!isFxModule(module)) continue;
            const auto area = moduleAreaFor(module);
            const auto first = fxListFirstRow();
            const auto rack = shownRack();
            const auto end = juce::jmin(fxActiveSlotCount(rack),
                                         first + ui::fxListIntersectingRowCount(area));
            auto nearest = fxSlotAtDisplayRow(rack, first);
            auto distance = std::numeric_limits<int>::max();
            for (int row = first; row < end; ++row)
            {
                const auto slot = fxSlotAtDisplayRow(rack, row);
                if (slot < 0) continue;
                const auto centre = ui::fxListItemBounds(area, row, fxListOpen, first).getCentreY();
                const auto next = std::abs(at.y - centre);
                if (next < distance) { distance = next; nearest = slot; }
            }
            fxDropSlot = nearest;
            repaintFxDisplays();
            return;
        }
    }
    if (draggingHandle == nullptr) return;
    dragPosition = event.getEventRelativeTo(this).getPosition();
    repaint();
}

void Editor::mouseUp(const juce::MouseEvent& event)
{
    if (markerDrag != SpectralMarker::none)
    {
        endSpectralMarkerDrag();
        return;
    }
    if (envelopeDragNode != ui::EnvelopeNode::none)
    {
        if (envelopeDragMoved)
            editEnvelopeNode(event.getEventRelativeTo(this).getPosition());
        endEnvelopeNodeDrag();
        return;
    }
    if (lfoGridDragAxis >= 0)
    {
        lfoGridDragAxis = lfoGridDragBank = -1;
        return;
    }
    if (lfoDragBank >= 0)
    {
        lfoDragPoint = lfoDragCurve = lfoDragBank = -1;
        return;
    }
    if (fxDragSlot >= 0)
    {
        const auto from = fxDragSlot;
        const auto to = fxDropSlot;
        const auto moved = event.getEventRelativeTo(this).getPosition().getDistanceFrom(fxDragStart) >= 4.0f;
        fxDragSlot = fxDropSlot = -1;
        if (moved && to >= 0 && to != from)
        {
            moveFxSlot(shownRack(), from, to);
            revealFxSlot(to);
        }
        else repaintFxDisplays();
        return;
    }
    if (draggingHandle == nullptr) return;
    const auto source = draggingHandle->source;
    draggingHandle->dragging = false;
    draggingHandle = nullptr;

    if (auto* control = controlAt(event.getEventRelativeTo(this).getPosition()))
    {
        const auto destination = destinationFor(control->id);
        if (destination != 0) assignModulation(source, destination);
        else
        {
            presetName.setText("THAT CONTROL CANNOT BE MODULATED", juce::dontSendNotification);
            presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        }
    }
    repaint();
}

void Editor::showValueBubble(Control& control)
{
    bubbleControl = &control;
    // A macro's declared label is its bare number and is off the panel anyway —
    // the plate carries the number now — so the bubble spells the macro out,
    // including whatever it has been named. A control that has a name strip is
    // a macro; nothing else on the panel has one.
    valueBubble.caption = control.macroName != nullptr
                              ? macroLabel(control.id.getTrailingIntValue() - 1)
                              : control.label.getText();
    valueBubble.reading = control.slider.getTextFromValue(control.slider.getValue());
    valueBubble.accent = control.slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Rack controls live inside their clipped viewport, while every other
    // control is a direct child. Convert either case into editor coordinates
    // before placing the editor-owned bubble.
    const auto knob = getLocalArea(&control.slider, control.slider.getLocalBounds());
    const auto size = juce::Rectangle<int>(valueBubble.widthFor(), ui::ValueBubble::heightFor());
    juce::Rectangle<int> placed;
    if (control.style == ui::Style::fader)
    {
        // Beside a fader rather than above it. A fader is as tall as its cell,
        // so "above" is above the label and halfway up the knob over it —
        // covering two things to report a third. Beside the thumb covers
        // nothing and follows the hand up and down the travel.
        const auto thumb = juce::roundToInt(
            ui::faderThumbY(knob.toFloat(),
                            ui::faderProportion(control.slider.getRange(), control.slider.getValue())));
        placed = size.withCentre({knob.getRight() + 6 + size.getWidth() / 2, thumb});
        // Flipped to the other side when the right-hand one would run off the
        // panel, which is what the last channel of the mixer needs.
        if (placed.getRight() > getWidth() - 4)
            placed = size.withCentre({knob.getX() - 6 - size.getWidth() / 2, thumb});
        placed.setY(juce::jlimit(4, juce::jmax(4, getHeight() - size.getHeight() - 4), placed.getY()));
    }
    else
    {
        // Above the knob by preference, below it when the knob is near the top
        // of the panel, and never off either side.
        placed = size.withCentre({knob.getCentreX(), knob.getY() - size.getHeight() / 2 - 6});
        if (placed.getY() < 4) placed.setY(knob.getBottom() + 6);
    }
    placed.setX(juce::jlimit(4, juce::jmax(4, getWidth() - size.getWidth() - 4), placed.getX()));
    valueBubble.setBounds(placed);

    valueBubble.setVisible(true);
    valueBubble.toFront(false);
    valueBubble.repaint();
    bubbleUntil = juce::Time::getMillisecondCounter() + bubbleTailMs;
}

void Editor::fadeValueBubble()
{
    if (!valueBubble.isVisible() || bubbleHeld) return;
    if (juce::Time::getMillisecondCounter() < bubbleUntil) return;
    valueBubble.setVisible(false);
    bubbleControl = nullptr;
}

// The notes the voices were told to play since the last tick become sparks in
// the plate's well, the ones whose notes have stopped are let go, and the
// sparks that have reached the floor are dropped. Where a spark falls is the
// note's place on the keyboard, so a rising arpeggio drifts across the well
// the way it walks along the keys; how big it is, is the velocity; the rest is
// a roll of the dice, so a chord falls as several lights rather than one.
void Editor::gatherNoteSparks()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(),
                                [now] (const ui::NoteSpark& spark)
                                { return ui::sparkProgress(spark, now) >= 1.0f; }),
                 sparks.end());

    auto& random = juce::Random::getSystemRandom();
    const auto span = static_cast<float>(ui::keyboardHighestNote - ui::keyboardLowestNote);
    NoteFlash flash;
    while (processor.popNoteFlash(flash))
    {
        ui::NoteSpark spark;
        spark.note = flash.note;
        spark.across = juce::jlimit(0.0f, 1.0f,
                                    static_cast<float>(flash.note - ui::keyboardLowestNote) / span
                                        + (random.nextFloat() - 0.5f) * 0.02f);
        spark.weight = juce::jlimit(0.0f, 1.0f, flash.velocity);
        spark.pace = 0.85f + random.nextFloat() * 0.3f;
        spark.warmth = random.nextFloat();
        spark.seed = static_cast<std::uint32_t>(random.nextInt());
        spark.bornMs = now;
        ui::addNoteSpark(sparks, spark);
    }
    // After the queue, never before it: a note is marked held before its flash
    // is queued, so asking now cannot find one just popped already let go.
    ui::releaseNoteSparks(sparks, now, [this] (int note) { return processor.noteHeld(note); });
}

void Editor::timerCallback()
{
    fadeValueBubble();
    gatherNoteSparks();
    pitchWheel.setDisplayValue(processor.pitchWheelValue() / 16383.0f);
    modulationWheel.setDisplayValue(processor.modWheelValue() / 127.0f);
    // Cheap to re-apply every tick, and it catches a dependency changing from
    // host automation as well as from the panel. Both of these only repaint
    // when something has actually changed.
    // A type can change without the panel being touched: a preset loaded, a
    // host automating it, a second editor on the same plugin. Noticed the same
    // way a replaced wavetable is, by comparing against what is on screen, so
    // the labels are only rebuilt when one has actually moved.
    for (int rack = 0; rack < rackCount; ++rack)
        for (int slot = 0; slot < fxSlotCount; ++slot)
            if (fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)]
                != juce::roundToInt(value(fxParameterId(rack, slot, "Type"))))
            {
                refreshFxSlots();
                resized();
                repaint();
            }

    // A routing cleared from outside this editor can leave the matrix scrolled
    // past its last row, and a layout pass is what brings the rows back up;
    // resized runs the enable pass itself.
    if (clampMatrixScroll()) resized();
    else applyEnableStates();

    // A panel colour can move without this editor being touched: a preset
    // loaded, a project opened, a second editor on the same plugin. Noticed
    // the same way a replaced wavetable is — by comparing against what is on
    // screen — so the controls are recoloured only when one has actually
    // changed rather than every tick.
    juce::String colours;
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::oscillator)
            colours << processor.panelColour(module.descriptor->id) << ',';
    if (colours != panelColoursShown)
    {
        panelColoursShown = colours;
        applyPanelColours();
    }

    // A macro's name moves from the same places for the same reasons, and is
    // noticed the same way. applyMacroNames writes what it found back into
    // macroNamesShown, so a tick on which nothing moved costs eight property
    // reads and a string compare.
    juce::String names;
    for (int macro = 0; macro < macroCount; ++macro) names << processor.macroName(macro) << "\n";
    if (names != macroNamesShown) applyMacroNames();

    // A warp mode moves the same way a slot's type does, and from the same
    // places, so the field that reports it is refreshed on the same tick. The
    // filter's type and the field whose label it decides go with them.
    refreshWarpFields();
    refreshFilterFields();
    refreshNoiseField();
    refreshOscModeFields();
    refreshLoopFields();
    refreshTableFields();
    refreshModulationRings();
    refreshMidiLearn();

    // A table replaced from outside the panel — a preset loaded, a project
    // opened, a second editor on the same plugin — bumps its revision, and this
    // is where the panel notices and redraws.
    auto moved = false;
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
    {
        const auto revision = processor.tableStore().revision(oscillator);
        if (revision == tableRevisions[static_cast<size_t>(oscillator)]) continue;
        tableRevisions[static_cast<size_t>(oscillator)] = revision;
        moved = true;
    }
    if (moved)
    {
        if (tablePanel != nullptr) tablePanel->refresh();
        applyTableCounts();
    }
    // Frees whatever the audio thread has demonstrably moved past. Publishing
    // does this too; here it catches the table or sample retired by the last
    // edit of a session, which would otherwise sit there until the next one -
    // and a replaced spectral sample can be twenty megabytes.
    processor.tableStore().collect();
    processor.sampleStore().collect();
    repaint();
}
}
