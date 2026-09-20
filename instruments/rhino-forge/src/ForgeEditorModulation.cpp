// The matrix from the panel's side: assigning a source to a destination,
// the depth rings that show how far one is being moved, and clearing a
// slot. The engine's half of this is ForgeCore.h's.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
namespace
{
// Menu ids. A slot's own actions start past everything else, so one number says
// both "take a routing away" and which routing it was.
constexpr int slotItemBase = 1000;
constexpr int nothingRoutedItem = 1;
constexpr int clearMacroItem = 2;
}

// A slot counts as live once it has both ends: something driving it and
// somewhere to go. Depth is left out deliberately, so a slot parked at zero
// still reads as a routing you set up rather than as an empty row.
bool Editor::slotIsLive(int slot) const
{
    return juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) > 0
        && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) > 0;
}

// Each modulated knob is told three things: how far its slots can move it, how
// far they are moving it right now, and whether anything is sounding at all. Its
// look draws them without knowing anything about the matrix.
void Editor::refreshModulationRings()
{
    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value to show. The knobs go quiet at the
    // same moment ENV 1's playhead does, and for the same reason.
    const auto live = processor.envelopeStage() != 0;
    std::array<float, destinationCount> depths {};
    // A ring can only be dragged when one slot is behind it. Pointed at by two,
    // it is a sum, and there is nothing a single drag could honestly mean.
    std::array<int, destinationCount> slots {};
    std::array<int, destinationCount> only {};
    only.fill(-1);
    // How many slots each macro is driving, tallied in the same pass and for
    // the same reason the rings are: it is the one thing on the panel that
    // cannot be read off the control it reaches, because a macro is never a
    // destination itself.
    std::array<int, macroCount> reach {};
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        const auto destination = juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8()));
        if (source <= 0 || destination <= 0 || destination >= destinationCount) continue;
        depths[static_cast<size_t>(destination)] += value(slotParameter(slot, "Depth").toRawUTF8());
        // Counted whatever its depth, so a routing sitting at zero can still be
        // dialled up by its ring rather than only in the matrix.
        ++slots[static_cast<size_t>(destination)];
        only[static_cast<size_t>(destination)] = slot;
        if (const auto macro = macroIndexOf(source); macro >= 0)
            ++reach[static_cast<size_t>(macro)];
    }

    for (auto& handle : handles)
    {
        auto* plate = dynamic_cast<ui::MacroPlate*>(handle.get());
        if (plate == nullptr) continue;
        const auto macro = macroIndexOf(plate->source);
        if (macro < 0 || plate->destinations == reach[static_cast<size_t>(macro)]) continue;
        plate->destinations = reach[static_cast<size_t>(macro)];
        plate->setTooltip(macroTooltip(macro));
        plate->repaint();
    }

    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            // Only the styles that draw a routing are worth walking. One the
            // matrix cannot reach is still visited, because that is what takes
            // a ring back off a control whose slot has just been cleared.
            if (!ui::showsModulation(control->style)) continue;
            const auto destination = destinationFor(control->id);
            const auto depth = destination == 0 ? 0.0f : depths[static_cast<size_t>(destination)];
            const auto offset = destination == 0 ? 0.0f : processor.modulationOffset(destination);

            control->ringSlot = destination != 0 && slots[static_cast<size_t>(destination)] == 1
                ? only[static_cast<size_t>(destination)] : -1;
            control->slider.ringDraggable = control->ringSlot >= 0;
            control->slider.ringDepth = depth;
            auto& properties = control->slider.getProperties();
            if (static_cast<float>(properties.getWithDefault("modDepth", 0.0)) == depth
                && static_cast<float>(properties.getWithDefault("modOffset", 0.0)) == offset
                && static_cast<bool>(properties.getWithDefault("modLive", false)) == live)
                continue;
            properties.set("modDepth", depth);
            properties.set("modOffset", offset);
            properties.set("modLive", live);
            control->slider.repaint();
        }
}

// --- A macro, from its own end ------------------------------------------------
//
// Everything below answers the question a knob's own ring cannot. Not "what is
// moving this control", which showModulationMenu covers, but "what is this
// macro moving" — and a macro is never a destination, so without this nothing
// on the panel would say.

int Editor::macroReach(int macro) const
{
    const auto source = static_cast<int>(ModSource::macro1) + macro;
    auto found = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) != source) continue;
        const auto destination = juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8()));
        if (destination > 0 && destination < destinationCount) ++found;
    }
    return found;
}

// The number is never dropped from this. It is what the matrix calls the macro
// and what is printed on its plate, so a name that replaced it would leave the
// panel and the matrix talking about two different things.
juce::String Editor::macroLabel(int macro) const
{
    const auto number = "MACRO " + juce::String(macro + 1);
    const auto given = processor.macroName(macro);
    return given.isEmpty() ? number : given + " (" + number + ")";
}

juce::String Editor::macroTooltip(int macro) const
{
    const auto reach = macroReach(macro);
    if (reach == 0)
        return macroLabel(macro) + ". Drag the number onto a knob to point it there. "
                                   "It is not driving anything yet";
    return macroLabel(macro) + ". Driving " + juce::String(reach)
         + (reach == 1 ? " control" : " controls")
         + " — click the count to list them. Drag the number onto a knob to add another";
}

void Editor::showMacroMenu(int macro)
{
    const auto source = static_cast<int>(ModSource::macro1) + macro;
    juce::PopupMenu menu;
    menu.addSectionHeader(macroLabel(macro));

    // The routings are readings rather than actions: a menu whose every line
    // takes something away is a menu you cannot open merely to look. What acts
    // sits at the foot, where it is read before it is reached.
    juce::PopupMenu remove;
    std::vector<int> live;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) != source) continue;
        const auto destination = juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8()));
        if (destination <= 0 || destination >= destinationCount) continue;
        const auto depth = value(slotParameter(slot, "Depth").toRawUTF8());
        const auto reading = juce::String(destinations()[static_cast<size_t>(destination)].label)
                           + "    " + (depth > 0.0f ? "+" : "")
                           + juce::String(juce::roundToInt(depth * 100.0f)) + "%";
        menu.addItem(slotItemBase + slot, reading, false, false);
        remove.addItem(slotItemBase + slot, reading);
        live.push_back(slot);
    }

    if (live.empty())
    {
        menu.addItem(nothingRoutedItem, "Nothing is routed from here yet", false, false);
    }
    else
    {
        menu.addSeparator();
        menu.addSubMenu("Remove", remove);
        menu.addItem(clearMacroItem, "Remove all " + juce::String(static_cast<int>(live.size())));
    }

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [safe, live] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= slotItemBase) safe->clearSlot(choice - slotItemBase);
        else if (choice == clearMacroItem)
            for (const auto slot : live) safe->clearSlot(slot);
        safe->refreshModulationRings();
    });
}

// Pushes what the state tree holds out onto the strips and the plates. A name
// can move without this editor being touched — a preset loaded, a project
// opened, a second editor on the same plugin — and is noticed on the timer the
// same way a panel colour is.
void Editor::applyMacroNames()
{
    macroNamesShown.clear();
    for (int macro = 0; macro < macroCount; ++macro)
        macroNamesShown << processor.macroName(macro) << "\n";

    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            if (control->macroName == nullptr) continue;
            const auto given = processor.macroName(control->id.getTrailingIntValue() - 1);
            if (control->macroName->getText() != given)
                control->macroName->setText(given, juce::dontSendNotification);
        }

    for (auto& handle : handles)
        if (auto* plate = dynamic_cast<ui::MacroPlate*>(handle.get()))
            plate->setTooltip(macroTooltip(macroIndexOf(plate->source)));
}

void Editor::showModulationMenu(const juce::String& parameterId)
{
    const auto destination = destinationFor(parameterId);
    if (destination == 0) return;
    const auto label = juce::String(destinations()[static_cast<size_t>(destination)].label);

    juce::PopupMenu menu;
    menu.addSectionHeader("Modulate " + label);

    // Anything already pointed here can be taken away from the same menu, so a
    // routing can be undone where it was made rather than only in the matrix.
    juce::PopupMenu existing;
    auto found = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        if (source <= 0 || juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) != destination)
            continue;
        existing.addItem(1000 + slot, juce::String(modSourceName(source)));
        ++found;
    }

    juce::PopupMenu sources;
    for (int source = 1; source < modSourceCount; ++source)
        sources.addItem(source, modSourceName(source));
    menu.addSubMenu("Add source", sources);
    if (found > 0) menu.addSubMenu("Remove", existing);

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [safe, destination] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= 1000) safe->clearSlot(choice - 1000);
        else safe->assignModulation(choice, destination);
    });
}

void Editor::assignModulation(int source, int destination)
{
    // Reuse a slot already joining this pair rather than spending a second one
    // on the same routing.
    auto target = -1;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) == source
            && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) == destination)
            target = slot;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) <= 0) target = slot;

    if (target < 0)
    {
        presetName.setText("ALL " + juce::String(modSlotCount) + " MOD SLOTS ARE IN USE",
                           juce::dontSendNotification);
        presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        return;
    }

    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(target, "Source"), static_cast<float>(source));
    set(slotParameter(target, "Dest"), static_cast<float>(destination));
    // A depth of zero would look like nothing happened. Half is audible, and
    // the ring it lands on is now the way to take it anywhere else.
    if (value(slotParameter(target, "Depth").toRawUTF8()) == 0.0f)
        setSlotDepth(target, 0.5f);
}

// Written through the parameter rather than into the slot, so a depth dragged
// on a ring reaches the host's automation lane and the matrix field by the same
// path a depth typed into the matrix does.
void Editor::setSlotDepth(int slot, float depth)
{
    if (slot < 0 || slot >= modSlotCount) return;
    if (auto* parameter = processor.state.getParameter(slotParameter(slot, "Depth")))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(-1.0f, 1.0f, depth)));
}

void Editor::clearSlot(int slot)
{
    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(slot, "Source"), 0.0f);
    set(slotParameter(slot, "Dest"), 0.0f);
    set(slotParameter(slot, "Depth"), 0.0f);
}
}
