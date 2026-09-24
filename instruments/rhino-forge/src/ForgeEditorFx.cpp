// The effects racks, from the panel's side: which slots are filled, what each
// one holds, the edits the list down the left makes to the chain, and the menus
// that choose a type and a mode. What a type *means* is ForgeFx.h's; what it
// does to the signal is ForgeFxDsp.h's; where each view is scrolled to and how
// it is drawn is ForgeEditorFxView.cpp's. This decides none of that and only
// edits it.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
// "fx2s3Knob1" is rack 2, slot 3. Parsed rather than carried on the Control,
// because the id is where those numbers are actually written down and a second
// copy of them is a second thing to keep in step.
bool Editor::fxControlAt(const juce::String& id, int& rack, int& slot)
{
    if (!id.startsWith("fx")) return false;
    const auto body = id.substring(2);
    const auto split = body.indexOfChar('s');
    if (split <= 0) return false;
    const auto rackNumber = body.substring(0, split).getIntValue();
    const auto slotNumber = body.substring(split + 1).getIntValue();
    if (rackNumber < 1 || rackNumber > rackCount || slotNumber < 1 || slotNumber > fxSlotCount)
        return false;
    rack = rackNumber - 1;
    slot = slotNumber - 1;
    return true;
}

int Editor::shownRack() const
{
    for (const auto& module : moduleUis)
        if (isFxModule(*module.descriptor))
            return juce::jlimit(0, rackCount - 1, module.bank);
    return 0;
}

int Editor::fxActiveSlotCount(int rack) const
{
    auto count = 0;
    for (int slot = 0; slot < fxSlotCount; ++slot)
        if (fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)] != 0) ++count;
    return count;
}

int Editor::fxDisplayRow(int rack, int slot) const
{
    if (slot < 0 || slot >= fxSlotCount
        || fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)] == 0) return -1;
    auto row = 0;
    for (int before = 0; before < slot; ++before)
        if (fxTypesShown[static_cast<size_t>(rack * fxSlotCount + before)] != 0) ++row;
    return row;
}

int Editor::fxSlotAtDisplayRow(int rack, int row) const
{
    for (int slot = 0; slot < fxSlotCount; ++slot)
        if (fxDisplayRow(rack, slot) == row) return slot;
    return -1;
}

Editor::Control* Editor::fxTypeControl(int rack, int slot)
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            int heldRack = 0, heldSlot = 0;
            if (control->id.endsWith("Type") && fxControlAt(control->id, heldRack, heldSlot)
                && heldRack == rack && heldSlot == slot)
                return control.get();
        }
    return nullptr;
}

void Editor::toggleFxExpanded()
{
    if (page != ui::Page::fx) return;
    fxExpanded = !fxExpanded;
    // The rack and the arp both want the lower row; whichever was opened last
    // has it.
    if (fxExpanded) arpOpen = false;
    fxDragSlot = fxDropSlot = -1;
    clampFxScroll();
    applyPage();
}

void Editor::toggleFxList()
{
    if (page != ui::Page::fx) return;
    fxListOpen = !fxListOpen;
    fxDragSlot = fxDropSlot = -1;
    resized();
    repaint();
}

void Editor::addFxSlot()
{
    const auto rack = shownRack();
    for (int slot = 0; slot < fxSlotCount; ++slot)
    {
        if (fxTypeOf(value(fxParameterId(rack, slot, "Type"))) != FxType::off) continue;
        for (const auto& module : ui::modules())
            if (isFxModule(module))
                if (auto* control = fxTypeControl(rack, slot))
                    showFxTypeMenu(*control,
                                   ui::fxAddButtonBounds(moduleAreaFor(module), fxListOpen));
        return;
    }
}

void Editor::setFxSlotBypassed(int rack, int slot, bool bypassed)
{
    if (auto* parameter = processor.state.getParameter(fxParameterId(rack, slot, "Bypass")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(bypassed ? 1.0f : 0.0f));
        parameter->endChangeGesture();
        repaintFxDisplays();
    }
}

void Editor::removeFxSlot(int rack, int slot)
{
    const auto removedRow = fxDisplayRow(rack, slot);
    if (removedRow < 0) return;

    // Closing a module closes the chain around it. Parameter identities remain
    // fixed; the values move through those identities exactly as drag reorder
    // already does, and the vacated final slot becomes OFF.
    if (slot < fxSlotCount - 1) moveFxSlot(rack, slot, fxSlotCount - 1);
    if (auto* parameter = processor.state.getParameter(
            fxParameterId(rack, fxSlotCount - 1, "Type")))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
        parameter->endChangeGesture();
        refreshFxSlots();
        const auto remaining = fxActiveSlotCount(rack);
        fxSelectedSlot = remaining > 0
            ? fxSlotAtDisplayRow(rack, juce::jmin(removedRow, remaining - 1)) : 0;
        clampFxScroll();
        applyEnableStates();
        resized();
        repaintFxDisplays();
    }
}

// Move one module in the left-hand signal-flow list. The parameters are fixed
// to slots for host automation, so reordering means moving every value held by
// those slots while leaving the parameter list itself stable.
void Editor::moveFxSlot(int rack, int from, int to)
{
    from = juce::jlimit(0, fxSlotCount - 1, from);
    to = juce::jlimit(0, fxSlotCount - 1, to);
    if (from == to) return;

    std::array<FxSlot, fxSlotCount> ordered;
    for (int slot = 0; slot < fxSlotCount; ++slot) ordered[static_cast<size_t>(slot)] = fxSlotOf(rack, slot);
    const auto moved = ordered[static_cast<size_t>(from)];
    if (from < to)
        for (int slot = from; slot < to; ++slot)
            ordered[static_cast<size_t>(slot)] = ordered[static_cast<size_t>(slot + 1)];
    else
        for (int slot = from; slot > to; --slot)
            ordered[static_cast<size_t>(slot)] = ordered[static_cast<size_t>(slot - 1)];
    ordered[static_cast<size_t>(to)] = moved;

    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    for (int slot = 0; slot < fxSlotCount; ++slot)
    {
        const auto& held = ordered[static_cast<size_t>(slot)];
        const auto id = [rack, slot] (const char* suffix) { return fxParameterId(rack, slot, suffix); };
        set(id("Type"), held.type);
        set(id("ModeA"), held.modeA);
        set(id("ModeB"), held.modeB);
        set(id("Bypass"), held.bypass);
        for (int knob = 0; knob < fxKnobCount; ++knob)
            set(id("Knob") + juce::String(knob + 1), held.knobs[static_cast<size_t>(knob)]);
        set(id("Mix"), held.mix);
        set(id("Level"), held.level);
    }
    fxSelectedSlot = to;
    refreshFxSlots();
    applyEnableStates();
    resized();
    repaint();
}

juce::String Editor::fxHeaderDetail() const
{
    const auto rack = shownRack();
    juce::StringArray held;
    for (int slot = 0; slot < fxSlotCount; ++slot)
    {
        const auto type = value(fxParameterId(rack, slot, "Type"));
        if (fxTypeOf(type) != FxType::off) held.add(fxTypeName(juce::roundToInt(type)));
    }
    return held.isEmpty() ? "EMPTY" : held.joinIntoString(" > ");
}

bool Editor::fxKnobLive(const Control& control) const
{
    int rack = 0, slot = 0;
    if (!control.id.contains("Knob") || !fxControlAt(control.id, rack, slot)) return true;
    return rhino::forge::fxKnobLive(fxSlotOf(rack, slot), control.id.getTrailingIntValue() - 1);
}

bool Editor::fxControlUsed(const Control& control) const
{
    int rack = 0, slot = 0;
    if (!fxControlAt(control.id, rack, slot)) return true;
    const auto& info = processor.fxSlotType(rack, slot);
    if (control.id.contains("Knob"))
    {
        const auto knob = control.id.getTrailingIntValue() - 1;
        return knob >= 0 && knob < fxKnobCount && info.knobs[static_cast<size_t>(knob)] != nullptr;
    }
    if (control.id.endsWith("ModeA")) return info.modeA.count > 0;
    if (control.id.endsWith("ModeB")) return info.modeB.count > 0;
    // OFF is absence, not an empty module. Adding is the fixed + at the top of
    // the list, so an unassigned slot contributes no plate, shelf or controls.
    return fxTypeOf(value(fxParameterId(rack, slot, "Type"))) != FxType::off;
}

// A slot's twelve controls are always declared and always attached; what
// changes with the type is what they are called and what they explain.
// Whether they are on screen is applyEnableStates's to decide, because that
// runs on a timer and would otherwise put back whatever this took away.
//
// Every rack is refreshed, not only the one showing, so switching banks reveals
// a slot that is already right rather than one that corrects itself a frame
// later.
void Editor::refreshFxSlots()
{
    for (int rack = 0; rack < rackCount; ++rack)
        for (int slot = 0; slot < fxSlotCount; ++slot)
            fxTypesShown[static_cast<size_t>(rack * fxSlotCount + slot)] =
                juce::roundToInt(value(fxParameterId(rack, slot, "Type")));

    for (auto& module : moduleUis)
    {
        if (!isFxModule(*module.descriptor)) continue;
        for (auto& held : module.controls)
        {
            auto& control = *held;
            int rack = 0, slot = 0;
            if (!fxControlAt(control.id, rack, slot)) continue;
            const auto& info = processor.fxSlotType(rack, slot);
            const auto type = juce::roundToInt(value(fxParameterId(rack, slot, "Type")));
            const auto colour = ui::fxTypeColour(type);

            // Everything in the slot takes the type's colour, so a rack is read
            // by colour down its rows before a single word on it is.
            control.slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
            control.slider.setColour(juce::Slider::thumbColourId, colour);
            if (control.chip != nullptr) control.chip->accent = colour;
            if (control.plate != nullptr)
            {
                control.plate->type = type;
                control.plate->setName(fxTypeName(type));
                control.plate->repaint();
            }

            const auto apply = [&control] (const char* label, const juce::String& tip)
            {
                if (label != nullptr) control.label.setText(label, juce::dontSendNotification);
                control.slider.setTooltip(tip);
            };

            if (control.id.contains("Knob"))
            {
                // A knob's reading depends on the type and on the mode fields
                // beside it — a delay's TIME is milliseconds or a division —
                // and a slider only re-reads its parameter's text when its
                // value moves, so it is pushed here instead.
                control.slider.updateText();
                const auto knob = control.id.getTrailingIntValue() - 1;
                const auto* label = knob >= 0 && knob < fxKnobCount
                    ? info.knobs[static_cast<size_t>(knob)] : nullptr;
                apply(label, label == nullptr ? juce::String()
                          : juce::String(label) + ", on the " + info.name + " in this slot");
                continue;
            }
            if (control.id.endsWith("ModeA") || control.id.endsWith("ModeB"))
            {
                const auto& mode = control.id.endsWith("ModeB") ? info.modeB : info.modeA;
                if (control.selector != nullptr)
                {
                    // Copied out of the table rather than pointed into it: the
                    // type in this slot changes underneath the selector, and a
                    // pointer to the old type's choices is a dangling read on
                    // the next repaint.
                    control.selector->choices.assign(mode.choices.begin(),
                                                     mode.choices.begin() + juce::jmax(0, mode.count));
                    control.selector->chosen = fxModeOf(mode, value(control.id));
                    control.selector->accent = colour;
                    control.selector->setTooltip(
                        mode.label == nullptr ? juce::String()
                            : juce::String(mode.label) + ", on the " + info.name + " in this slot");
                    control.selector->repaint();
                }
                apply(mode.label, mode.label == nullptr ? juce::String()
                          : juce::String(mode.label) + ", on the " + info.name + " in this slot");
                continue;
            }
        }
    }
}

// A mode parameter is a plain 0..1, because what it steps through changes with
// the type and a host's choice list is fixed when the parameter is made. So a
// choice is spread across that range, and fxModeOf reads it back the same way —
// one arithmetic, two directions.
void Editor::setFxMode(const juce::String& id, int count, int choice)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr || count <= 1) return;
    const auto at = static_cast<float>(juce::jlimit(0, count - 1, choice))
                  / static_cast<float>(count - 1);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(at));
}

// The list, for a mode field with more choices than fit across it — the
// distortion's eight shapes are the only one so far.
void Editor::showFxModeMenu(Control& control)
{
    if (control.selector == nullptr || control.selector->count() <= 0) return;
    const auto count = control.selector->count();

    juce::PopupMenu menu;
    for (int i = 0; i < count; ++i)
    {
        juce::PopupMenu::Item item(control.selector->choices[static_cast<size_t>(i)]);
        item.itemID = i + 1;
        item.isTicked = i == control.selector->chosen;
        menu.addItem(item);
    }

    const auto safe = juce::Component::SafePointer<Editor>(this);
    const auto id = control.id;
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id, count] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setFxMode(id, count, choice - 1);
    });
}

// One slot set to what its type opens on. The values live beside the type in
// ForgeFx.h rather than here, because what a reverb should open on is a fact
// about reverbs and not about this panel.
void Editor::initialiseFxSlot(int rack, int slot)
{
    const auto& info = processor.fxSlotType(rack, slot);
    const auto set = [this] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    for (int knob = 0; knob < fxKnobCount; ++knob)
        set(fxParameterId(rack, slot, "Knob") + juce::String(knob + 1),
            info.init[static_cast<size_t>(knob)]);
    set(fxParameterId(rack, slot, "Mix"), info.initMix);
    // The modes stay where they are: their first choice is the ordinary one for
    // every type, and a slot that has just been given a type is already on it.
    refreshFxSlots();
    clampFxScroll();
    resized();
    repaint();
}

// Filling a slot. The list is the types in the order they are declared, with
// OFF at the top as the way to empty a slot again — the same list the type
// parameter holds, so nothing here can offer a type the engine does not have.
void Editor::showFxTypeMenu(Control& control, juce::Rectangle<int> target)
{
    auto* parameter = processor.state.getParameter(control.id);
    if (parameter == nullptr) return;

    juce::PopupMenu menu;
    const auto adding = !target.isEmpty();
    menu.addSectionHeader(adding ? "Add effect" : "Replace effect");
    const auto current = juce::roundToInt(control.slider.getValue());
    for (int type = adding ? 1 : 0; type < fxTypeCount; ++type)
    {
        juce::PopupMenu::Item item(fxTypeName(type));
        item.itemID = type + 1;
        item.isTicked = type == current;
        // The list is read by colour as much as by name, so it carries the same
        // colours the plates do.
        item.colour = type == 0 ? ui::mutedText : ui::fxTypeColour(type);
        menu.addItem(item);
    }

    int rack = 0, slot = 0;
    if (!fxControlAt(control.id, rack, slot)) return;

    const auto safe = juce::Component::SafePointer<Editor>(this);
    auto options = juce::PopupMenu::Options {};
    if (target.isEmpty()) options = options.withTargetComponent(control.plate.get());
    else options = options.withTargetScreenArea(localAreaToGlobal(target));
    menu.showMenuAsync(options,
                       [safe, parameter, rack, slot] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        const auto type = choice - 1;
        if (type == 0)
        {
            safe->removeFxSlot(rack, slot);
            return;
        }
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(type)));
        // Putting an effect into a slot sets that effect up, the way adding a
        // module in Serum loads its default preset. Only from here: a type
        // arriving from a preset or from a host's automation lane must land
        // with the values that came with it, not with these on top.
        //
        // OFF is left alone deliberately, so emptying a slot and putting the
        // same type back finds it as it was.
        safe->initialiseFxSlot(rack, slot);
        // A new effect lands at the end of the chain, which is often below
        // the fold of both views.
        safe->fxSelectedSlot = slot;
        safe->revealFxSlot(slot);
        safe->repaintFxDisplays();
    });
}

FxSlot Editor::fxSlotOf(int rack, int slot) const
{
    const auto id = [rack, slot] (const char* suffix) { return fxParameterId(rack, slot, suffix); };
    FxSlot held;
    held.type = value(id("Type"));
    held.modeA = value(id("ModeA"));
    held.modeB = value(id("ModeB"));
    held.bypass = value(id("Bypass"));
    for (int knob = 0; knob < fxKnobCount; ++knob)
        held.knobs[static_cast<size_t>(knob)] = value(id("Knob") + juce::String(knob + 1));
    held.mix = value(id("Mix"));
    held.level = value(id("Level"));
    return held;
}
}
