// The matrix from the panel's side: assigning a source to a destination,
// the depth rings that show how far one is being moved, and clearing a
// slot. The engine's half of this is ForgeCore.h's.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
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
