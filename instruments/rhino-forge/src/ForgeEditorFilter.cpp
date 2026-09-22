// The filter's TYPE field: the menu grouped by family, and the one knob beside
// it whose label the chosen type decides.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// --- The filter ---------------------------------------------------------------
//
// TYPE wears the component a rack slot's mode fields and an oscillator's warp
// fields wear, and behaves like the warp behind it rather than like the rack: a
// rack mode is a plain 0..1 because what it steps through depends on what is in
// the slot, while the filter's thirty-four types are a fixed list, so it is a
// real choice parameter and the value *is* the index.
//
// FREQ is the other half of the story. It is one parameter with one range and
// eight or nine meanings, exactly as a rack slot's knobs are, so what it is
// called and what it reads as are looked up from the type rather than declared
// with it. Both halves read ForgeFilter.h, which is also what the engine reads,
// so a field cannot be labelled as one thing and rendered as another.
void Editor::setFilterType(int choice)
{
    auto* parameter = processor.state.getParameter("filterType");
    if (parameter == nullptr) return;
    const auto was = filterTypeOf(value("filterType"));
    const auto now = filterTypeOf(static_cast<float>(choice));
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, filterTypeCount - 1, choice))));

    // FREQ moves with the type, but only when the type has changed what that
    // knob is for. Stepping LOW to HIGH leaves it alone because FAT means the
    // same thing in both; stepping LOW to LP+HP resets it, because a second
    // corner set from a saturation amount is a corner nobody chose. The rack
    // does the same thing when a slot's type changes, and for the same reason
    // — see initialiseFxSlot.
    if (juce::String(filterSecondLabel(was)) == filterSecondLabel(now)) return;
    if (auto* second = processor.state.getParameter("filterFreq"))
        second->setValueNotifyingHost(second->convertTo0to1(filterSecondInit(now)));
}

// What the two fields are showing. Called on the way in, on every tab change
// and on every tick, because either can move without the panel being touched —
// a preset loaded, a host automating it, a second editor on the same plugin —
// and the field has to say what the engine is running.
void Editor::refreshFilterFields()
{
    const auto type = filterTypeOf(value("filterType"));

    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;

            if (control.id == "filterFreq")
            {
                // The label is the type's word for this knob, and the
                // double-click is the setting that type opens on — which is
                // the only way the middle of a field this overloaded is
                // somewhere the hand can get back to.
                control.label.setText(filterSecondLabel(type), juce::dontSendNotification);
                control.slider.setDoubleClickReturnValue(true, filterSecondInit(type));
                // A slider only re-reads its parameter's text when its value
                // moves, and this reading changes when the *type* moves, so it
                // is pushed rather than waited for.
                control.slider.updateText();
                control.slider.setTooltip(ui::filterSecondTooltipFor(static_cast<int>(type)));
                continue;
            }

            if (control.id != "filterType" || control.selector == nullptr) continue;

            const auto chosen = juce::jlimit(0, filterTypeCount - 1, juce::roundToInt(value(control.id)));
            const auto listed = control.selector->count() == filterTypeCount;
            if (listed && control.selector->chosen == chosen) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int i = 0; i < filterTypeCount; ++i)
                    control.selector->choices.push_back(filterTypeName(i));
            }
            control.selector->chosen = chosen;
            // The field says what it is set to; the tooltip says what that
            // setting does, read out of the same table the engine renders from.
            control.selector->setTooltip(ui::filterTooltipFor(chosen));
            control.selector->repaint();
        }
}

// The list, grouped by family. Thirty-four items in a single column would be a
// list to read rather than a menu to use, and the families are how a filter is
// actually chosen: you know you want a ladder before you know which one.
void Editor::showFilterMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, filterTypeCount - 1, juce::roundToInt(value(control.id)));
    const auto currentCategory = filterCategoryOf(filterTypeOf(static_cast<float>(current)));

    juce::PopupMenu menu;
    for (int category = 0; category < filterCategoryCount; ++category)
    {
        const auto group = static_cast<FilterCategory>(category);
        juce::PopupMenu submenu;
        for (int type = 0; type < filterTypeCount; ++type)
        {
            if (filterCategoryOf(filterTypeOf(static_cast<float>(type))) != group) continue;
            juce::PopupMenu::Item item(filterTypeName(type));
            item.itemID = type + 1;
            item.isTicked = type == current;
            submenu.addItem(item);
        }
        if (submenu.getNumItems() == 0) continue;
        // The family the current type belongs to is ticked as well as the type
        // inside it, so a closed menu still says where the setting lives.
        menu.addSubMenu(filterCategoryName(group), submenu, true, nullptr,
                        currentCategory == group);
    }

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setFilterType(choice - 1);
        safe->refreshFilterFields();
        safe->repaint();
    });
}

// The settings the display is drawn from, resolved the way the engine resolves
// them, so the curve on the tube is the filter the voice is rendering. The
// sample rate is the one the engine is actually running at where there is one:
// how low a comb can be tuned depends on it, and so therefore does where the
// display puts the comb's teeth.
FilterShape Editor::filterShape() const
{
    const auto rate = processor.getSampleRate();
    return {filterTypeOf(value("filterType")), value("cutoff"), value("resonance"),
            value("filterFreq"), rate > 0.0 ? rate : 48000.0};
}
}
