// The hand and the keyboard: what a press, a drag, a wheel notch or a key
// does, the bubble that says what a control now reads, and the 24Hz timer
// that carries the engine's live values back onto the panel.
#include "ForgeEditorInternal.h"

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
    const auto display = lfoDisplayBounds();
    const auto column = ui::lfoColumnBounds(display);
    const auto row = ui::lfoRowBounds(display);
    const auto overGridStep = ui::lfoGridStepBounds(column, true).contains(at)
        || ui::lfoGridStepBounds(column, false).contains(at)
        || ui::lfoGridStepBounds(row, true).contains(at)
        || ui::lfoGridStepBounds(row, false).contains(at);
    const auto overShapeControl = ui::lfoNameBounds(display).contains(at)
        || ui::lfoPreviousBounds(display).contains(at)
        || ui::lfoNextBounds(display).contains(at)
        || (ui::lfoPlotBounds(display).contains(at) && lfoPointAt(at) >= 0);
    if (overGridStep || overShapeControl) setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else if (column.contains(at) || row.contains(at)) setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else setMouseCursor(juce::MouseCursor::NormalCursor);
}

void Editor::mouseDown(const juce::MouseEvent& event)
{
    if (event.eventComponent == this)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        const auto display = lfoDisplayBounds();
        if (display.contains(at))
        {
            if (ui::lfoNameBounds(display).contains(at) && !event.mods.isPopupMenu())
            { showLfoMenu(); return; }
            if (ui::lfoPreviousBounds(display).contains(at) && !event.mods.isPopupMenu())
            { stepLfoShape(-1); return; }
            if (ui::lfoNextBounds(display).contains(at) && !event.mods.isPopupMenu())
            { stepLfoShape(1); return; }
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
                const auto point = lfoPointAt(at);
                if (event.mods.isPopupMenu()) { removeLfoPoint(shownLfo(), point); return; }
                if (event.getNumberOfClicks() >= 2)
                {
                    if (point >= 0) removeLfoPoint(shownLfo(), point);
                    else addLfoPoint(shownLfo(), at);
                    return;
                }
                lfoDragPoint = point;
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
                const auto list = ui::fxListBounds(area, fxListOpen);
                const auto rack = shownRack();
                for (int slot = 0; slot < fxSlotCount; ++slot)
                {
                    const auto displayRow = fxDisplayRow(rack, slot);
                    if (displayRow < 0) continue;
                    const auto item = ui::fxListItemBounds(area, module, displayRow, fxListOpen,
                                                            fxFirstVisibleSlot());
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
                showModulationMenu(control->id);
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
    if (page == ui::Page::fx)
        for (const auto& module : ui::modules())
        {
            if (!isFxModule(module)) continue;
            const auto area = moduleAreaFor(module);
            if (!ui::fxListBounds(area, fxListOpen).contains(at)) break;
            fxWheel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
            const auto notches = static_cast<int>(fxWheel / wheelPerZoomStep);
            if (notches == 0) return;
            fxWheel -= static_cast<float>(notches) * wheelPerZoomStep;
            setFxFirstVisibleSlot(fxFirstVisibleSlot() - notches);
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
    if (lfoGridDragAxis >= 0)
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        setLfoGridCount(lfoGridDragBank, lfoGridDragAxis == 0,
                        lfoGridStartCount + (lfoGridStartY - at.y) / 8);
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
            const auto first = fxFirstVisibleSlot();
            const auto rack = shownRack();
            const auto end = juce::jmin(fxActiveSlotCount(rack),
                                         first + ui::fxIntersectingSlotCount(area));
            auto nearest = fxSlotAtDisplayRow(rack, first);
            auto distance = std::numeric_limits<int>::max();
            for (int row = first; row < end; ++row)
            {
                const auto slot = fxSlotAtDisplayRow(rack, row);
                if (slot < 0) continue;
                const auto centre = ui::fxListItemBounds(area, module, row, fxListOpen, first).getCentreY();
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
    if (lfoGridDragAxis >= 0)
    {
        lfoGridDragAxis = lfoGridDragBank = -1;
        return;
    }
    if (lfoDragBank >= 0)
    {
        lfoDragPoint = lfoDragBank = -1;
        return;
    }
    if (fxDragSlot >= 0)
    {
        const auto from = fxDragSlot;
        const auto to = fxDropSlot;
        const auto moved = event.getEventRelativeTo(this).getPosition().getDistanceFrom(fxDragStart) >= 4.0f;
        fxDragSlot = fxDropSlot = -1;
        if (moved && to >= 0 && to != from) moveFxSlot(shownRack(), from, to);
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

void Editor::timerCallback()
{
    fadeValueBubble();
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

    applyEnableStates();

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
    // places, so the field that reports it is refreshed on the same tick.
    refreshWarpFields();
    refreshModulationRings();

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
    // does this too; here it catches the table retired by the last edit of a
    // session, which would otherwise sit there until the next one.
    processor.tableStore().collect();
    repaint();
}
}
