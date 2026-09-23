// The noise module's SOURCE field: the nineteen generators, the families they
// are grouped into, and the list they are chosen from.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// SOURCE wears the component a rack slot's mode fields, an oscillator's warp
// fields and the filter's TYPE wear, and is driven the way the last two are: a
// fixed list, so the parameter is a real choice and its value *is* the index.
//
// It differs from all three in one respect, and it is the only thing in this
// file that is not obvious. **What is stored and what is shown are two orders.**
// The value is an index into the enum, which is appended to and never
// reordered, so GEIGER is 3 because it arrived with the first four and BLUE is
// 4 because it did not. Reading down the field in that order would step WHITE,
// PINK, BROWN, GEIGER, BLUE — a transient in the middle of the colours — so
// everything the panel does walks noiseSourceOrder() instead, which is by
// family, and the two only ever meet at noiseSourceAt and noiseSourcePosition.
//
// The alternative was to reorder the enum and migrate the stored value, which
// buys a tidier automation lane and costs every preset already saved.
void Editor::setNoiseSource(int source)
{
    auto* parameter = processor.state.getParameter("noiseSource");
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, noiseSourceCount - 1, source))));
}

// What the field is showing. Called on the way in, on every tab change and on
// every tick, for the reason the filter's is: the parameter can move without
// the panel being touched — a preset loaded, a host automating it, a second
// editor on the same plugin — and the field has to say what the engine is
// rendering.
void Editor::refreshNoiseField()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;
            if (control.id != "noiseSource" || control.selector == nullptr) continue;

            const auto source = juce::jlimit(0, noiseSourceCount - 1,
                                             juce::roundToInt(value(control.id)));
            const auto position = noiseSourcePosition(source);
            const auto listed = control.selector->count() == noiseSourceCount;
            if (listed && control.selector->chosen == position) continue;
            if (!listed)
            {
                // The short names, in family order. Short because the plate is
                // two columns of twenty-four and will not hold "Vintage Poly
                // HP"; the menu below spells them out in full.
                control.selector->choices.clear();
                for (int i = 0; i < noiseSourceCount; ++i)
                    control.selector->choices.push_back(noiseSourceName(noiseSourceAt(i)));
            }
            control.selector->chosen = position;
            // The field says what it is set to; the tooltip says what that
            // setting sounds like, out of the same table the engine renders it
            // from.
            control.selector->setTooltip(ui::noiseSourceTooltipFor(source));
            control.selector->repaint();
        }
}

// The list, grouped by family. Nineteen items in a single column would be a
// list to read rather than a menu to use, and the families are how a source is
// actually chosen: you know you want something analog before you know which.
// The same shape as the filter's thirty-four, for the same reason.
void Editor::showNoiseMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, noiseSourceCount - 1, juce::roundToInt(value(control.id)));
    const auto currentCategory = noiseCategoryOf(static_cast<NoiseSource>(current));

    juce::PopupMenu menu;
    for (int category = 0; category < noiseCategoryCount; ++category)
    {
        const auto group = static_cast<NoiseCategory>(category);
        juce::PopupMenu submenu;
        // Walked in display order rather than by value, so a family reads the
        // way the field steps through it.
        for (int i = 0; i < noiseSourceCount; ++i)
        {
            const auto source = noiseSourceAt(i);
            if (noiseCategoryOf(static_cast<NoiseSource>(source)) != group) continue;
            // The full name here. A menu has the room, and "Vintage Poly HP"
            // says what "POLY HP" only hints at.
            juce::PopupMenu::Item item(noiseSourceFullName(source));
            item.itemID = source + 1;
            item.isTicked = source == current;
            submenu.addItem(item);
        }
        if (submenu.getNumItems() == 0) continue;
        // The family the current source belongs to is ticked as well as the
        // source inside it, so a closed menu still says where the setting lives.
        menu.addSubMenu(noiseCategoryName(group), submenu, true, nullptr,
                        currentCategory == group);
    }

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setNoiseSource(choice - 1);
        safe->refreshNoiseField();
        safe->repaint();
    });
}
}
