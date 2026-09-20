// The two warp stages under each oscillator: the field that names the mode,
// the menu grouped the way the manual groups it, and the swap at its foot.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
// --- Warp ---------------------------------------------------------------------
//
// An oscillator's two warp fields wear the same component a rack slot's modes
// do and behave differently behind it. A rack mode is a plain 0..1 because what
// it steps through depends on what is in the slot; a warp mode is a fixed list
// of twenty-six, so it is a real choice parameter and the value *is* the index.
// That is the whole of the difference, and it is why these methods sit beside
// the rack's rather than inside them.
bool Editor::isWarpControl(const juce::String& id)
{
    return id.startsWith("osc") && id.contains("Warp");
}

void Editor::setWarpMode(const juce::String& id, int choice)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, warpModeCount - 1, choice))));
}

// What the two fields on each oscillator are showing. Called on the way in, on
// every tab change and on every tick, because a mode can move without the panel
// being touched -- a preset loaded, a host automating it, a second editor on
// the same plugin -- and the field has to say what the engine is running.
//
// Only a field that has actually moved is rebuilt, which is what makes this
// cheap enough to run on a timer.
void Editor::refreshWarpFields()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;
            if (!isWarpControl(control.id)) continue;

            // The depth knob beside a field. Most modes do nothing at nothing,
            // but four of them do nothing at twelve o'clock instead, so a
            // double-click returns the knob to whichever of the two its mode
            // actually means -- which is the only way the middle of a bipolar
            // warp is somewhere the hand can get back to.
            if (control.selector == nullptr)
            {
                const auto mode = warpModeOf(value(control.id + "Mode"));
                control.slider.setDoubleClickReturnValue(true, warpNeutralDepth(mode));
                continue;
            }

            const auto chosen = juce::jlimit(0, warpModeCount - 1, juce::roundToInt(value(control.id)));
            const auto listed = control.selector->count() == warpModeCount;
            if (listed && control.selector->chosen == chosen) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int mode = 0; mode < warpModeCount; ++mode)
                    control.selector->choices.push_back(warpModeName(mode));
            }
            control.selector->chosen = chosen;
            // The field says what it is set to; the tooltip says what that
            // setting does, read out of the same table the engine renders from.
            control.selector->setTooltip(ui::warpTooltipFor(chosen));
            control.selector->repaint();
        }
}

// The list, grouped the way the manual groups it: OFF and SYNC are one item
// each because they are one mode each, and the four families holding more than
// one open a submenu. Twenty-six items in a single column would be a list to
// read rather than a menu to use.
void Editor::showWarpMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = juce::jlimit(0, warpModeCount - 1, juce::roundToInt(value(control.id)));

    juce::PopupMenu menu;
    for (int category = 0; category < warpCategoryCount; ++category)
    {
        const auto group = static_cast<WarpCategory>(category);
        juce::PopupMenu submenu;
        auto held = 0;
        auto only = 0;
        for (int mode = 0; mode < warpModeCount; ++mode)
        {
            if (warpCategoryOf(static_cast<WarpMode>(mode)) != group) continue;
            ++held;
            only = mode;
            juce::PopupMenu::Item item(warpModeName(mode));
            item.itemID = mode + 1;
            item.isTicked = mode == current;
            submenu.addItem(item);
        }
        if (held == 0) continue;
        if (held == 1)
        {
            juce::PopupMenu::Item item(warpModeName(only));
            item.itemID = only + 1;
            item.isTicked = only == current;
            menu.addItem(item);
            continue;
        }
        // The family the current mode belongs to is ticked as well as the mode
        // inside it, so a closed menu still says where the setting lives.
        menu.addSubMenu(warpCategoryName(group), submenu, true, nullptr,
                        warpCategoryOf(warpModeOf(static_cast<float>(current))) == group);
    }

    // At the foot, past a rule, the way the manual's own menu has it: the two
    // stages change places. The modes swap and the depths stay put, which is
    // exactly what the manual describes -- WARP 1's knob is left driving
    // whatever was on the right, and that is the point of it, because the pair
    // of knobs is where the hand already is.
    menu.addSeparator();
    menu.addItem(warpModeCount + 1, "Swap warp 1 and warp 2");

    const auto safe = juce::Component::SafePointer<Editor>(this);
    const auto id = control.id;
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == warpModeCount + 1) { safe->swapWarpModes(id); return; }
        safe->setWarpMode(id, choice - 1);
    });
}

// The two stages of whichever oscillator this field belongs to, exchanged. The
// id says which: "oscBWarp2Mode" is oscillator B, and which of the two stages
// was clicked does not matter because both end up holding the other's mode.
void Editor::swapWarpModes(const juce::String& id)
{
    const auto prefix = id.startsWith("oscB") ? "oscB" : "oscA";
    const auto first = juce::String(prefix) + "Warp1Mode";
    const auto second = juce::String(prefix) + "Warp2Mode";
    const auto held = juce::roundToInt(value(first));
    setWarpMode(first, juce::roundToInt(value(second)));
    setWarpMode(second, held);
    refreshWarpFields();
    repaint();
}

// One oscillator's two stages, resolved the way the engine resolves them, so
// the tube draws the very warp the voice is rendering. The note and the rate
// are the filter modes' business and the display draws none of those, so what
// is handed in for them does not matter.
std::array<WarpStage, warpSlots> Editor::warpStagesOf(const char* prefix) const
{
    std::array<WarpStage, warpSlots> stages {};
    for (int slot = 0; slot < warpSlots; ++slot)
    {
        const auto id = juce::String(prefix) + "Warp" + juce::String(slot + 1);
        stages[static_cast<size_t>(slot)] = warpStageFor(value(id + "Mode"), value(id), 0.0f, 48000.0);
    }
    return stages;
}
}
