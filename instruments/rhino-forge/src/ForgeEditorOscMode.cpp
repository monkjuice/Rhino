// An oscillator's MODE field: what the oscillator is, and the list it is
// chosen from.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// MODE wears the component the filter's TYPE and the noise module's SOURCE
// wear, and is driven the way SOURCE is rather than the way TYPE is, for the
// same reason SOURCE needs it: **what is stored and what is shown are two
// orders.**
//
// The parameter holds Serum's own numbering — Wavetable 0, Multisample 1,
// Sample 2, Granular 3, Spectral 4 (p. 104) — because a choice parameter's list
// is part of the plugin's published interface, and inserting the three middle
// modes later would renumber what a host had already automated. Forge renders
// two of those five. The field therefore lists what is built and nothing else,
// so stepping it with the arrows moves between wavetable and spectral instead
// of walking through three modes that would do nothing.
//
// oscModeAt and oscModePosition are where the two orders meet, and this file is
// the only thing that calls them.
void Editor::setOscMode(const juce::String& id, int mode)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, oscModeCount - 1, mode))));
}

// What the field is showing. Called on the way in, on every tab change and on
// every tick, for the reason the filter's and the noise module's are: the
// parameter can move without the panel being touched — a preset loaded, a host
// automating it, a second editor on the same plugin — and the field has to say
// which engine is actually rendering.
void Editor::refreshOscModeFields()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;
            if (control.selector == nullptr || !control.id.endsWith("Mode")) continue;
            if (oscillatorIndexFromId(control.id) < 0) continue;
            // The warp stages and the loop end in "Mode" too, and are
            // different lists.
            if (isWarpControl(control.id) || isLoopModeControl(control.id)) continue;

            const auto mode = juce::jlimit(0, oscModeCount - 1, juce::roundToInt(value(control.id)));
            const auto position = oscModePosition(mode);
            const auto listed = control.selector->count() == oscModeBuiltCount;
            if (listed && control.selector->chosen == position) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int i = 0; i < oscModeBuiltCount; ++i)
                    control.selector->choices.push_back(oscModeName(oscModeAt(i)));
            }
            control.selector->chosen = position;
            control.selector->setTooltip(ui::tooltipFor(control.id));
            control.selector->repaint();
        }
}

// The list. Two items, so no submenus and no families: this is a menu because
// the arrows need something to open, not because the choice is large.
void Editor::showOscModeMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, oscModeCount - 1, juce::roundToInt(value(control.id)));

    juce::PopupMenu menu;
    for (int i = 0; i < oscModeBuiltCount; ++i)
    {
        const auto mode = oscModeAt(i);
        juce::PopupMenu::Item item(juce::String(oscModeName(mode)).toLowerCase().substring(0, 1).toUpperCase()
                                   + juce::String(oscModeName(mode)).toLowerCase().substring(1));
        item.itemID = mode + 1;
        item.isTicked = mode == current;
        menu.addItem(item);
    }

    const auto id = control.id;
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setOscMode(id, choice - 1);
        safe->refreshOscModeFields();
        safe->repaint();
    });
}
}
