// The filters' TYPE fields: the menu grouped by family, and the one knob beside
// each whose label the chosen type decides.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// --- The filters --------------------------------------------------------------
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
//
// FILTER 1 and FILTER 2 are the same pair of fields twice, each answering to
// its own type; nothing here is shared between them but the code.
void Editor::setFilterType(int filter, int choice)
{
    const auto typeId = filterParameterId(filter, "Type");
    auto* parameter = processor.state.getParameter(typeId);
    if (parameter == nullptr) return;
    const auto was = filterTypeOf(value(typeId));
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
    if (auto* second = processor.state.getParameter(filterParameterId(filter, "Freq")))
        second->setValueNotifyingHost(second->convertTo0to1(filterSecondInit(now)));
}

// What the fields are showing. Called on the way in, on every tab change and
// on every tick, because any of them can move without the panel being touched
// — a preset loaded, a host automating it, a second editor on the same plugin
// — and the field has to say what the engine is running.
void Editor::refreshFilterFields()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;

            if (const auto filter = filterOfFreqControl(control.id); filter >= 0)
            {
                const auto type = filterTypeOf(value(filterParameterId(filter, "Type")));
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

            if (!isFilterTypeControl(control.id) || control.selector == nullptr) continue;

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
    const auto filter = filterOfTypeControl(control.id);
    if (control.selector == nullptr || filter < 0) return;
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
                       [safe, filter] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setFilterType(filter, choice - 1);
        safe->refreshFilterFields();
        safe->repaint();
    });
}

// The settings a display is drawn from, resolved the way the engine resolves
// them, so the curve on the tube is the filter the voice is rendering. The
// sample rate is the one the engine is actually running at where there is one:
// how low a comb can be tuned depends on it, and so therefore does where the
// display puts the comb's teeth.
FilterShape Editor::filterShape(int filter) const
{
    const auto rate = processor.getSampleRate();
    const auto id = [filter] (const char* suffix) { return filterParameterId(filter, suffix); };
    return {filterTypeOf(value(id("Type"))), value(id("Cutoff")), value(id("Resonance")),
            value(id("Freq")), rate > 0.0 ? rate : 48000.0};
}

// Which filter the panel is showing. The module is the one that declares the
// filter display, so nothing here has to know its id.
int Editor::shownFilter() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::filter)
            return juce::jlimit(0, filterCount - 1, module.bank);
    return 0;
}
}
