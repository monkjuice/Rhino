// The noise module's SOURCE field: the four generators, and the list they are
// chosen from.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// SOURCE wears the component a rack slot's mode fields, an oscillator's warp
// fields and the filter's TYPE wear, and is driven the way the last two are: a
// fixed list, so the parameter is a real choice and its value *is* the index.
//
// It is the simplest of the four. A warp mode moves how much bandwidth the
// oscillator asks for, and the filter's type moves what the knob beside it is
// called; a noise source moves nothing but itself, because TONE and STEREO mean
// the same thing on all four and the crossfade between generators happens in
// the voice rather than up here. So this is the choice written down and nothing
// else — which is worth saying, because the other three look like they ought to
// be this and are not.
void Editor::setNoiseSource(int choice)
{
    auto* parameter = processor.state.getParameter("noiseSource");
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, noiseSourceCount - 1, choice))));
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

            const auto chosen = juce::jlimit(0, noiseSourceCount - 1,
                                             juce::roundToInt(value(control.id)));
            const auto listed = control.selector->count() == noiseSourceCount;
            if (listed && control.selector->chosen == chosen) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int source = 0; source < noiseSourceCount; ++source)
                    control.selector->choices.push_back(noiseSourceName(source));
            }
            control.selector->chosen = chosen;
            // The field says what it is set to; the tooltip says what that
            // setting sounds like, out of the same table the engine renders it
            // from.
            control.selector->setTooltip(ui::noiseSourceTooltipFor(chosen));
            control.selector->repaint();
        }
}

// A flat list rather than a grouped one. Four is a menu you read at a glance,
// and the families the filter's thirty-four are grouped into do not exist here
// — three colours and a click generator share no heading worth having.
void Editor::showNoiseMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, noiseSourceCount - 1, juce::roundToInt(value(control.id)));

    juce::PopupMenu menu;
    for (int source = 0; source < noiseSourceCount; ++source)
    {
        juce::PopupMenu::Item item(noiseSourceName(source));
        item.itemID = source + 1;
        item.isTicked = source == current;
        menu.addItem(item);
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
