// The theme: the one setting the panel has about itself. Chosen from a menu,
// written to the machine's settings, and read the next time Forge starts —
// nothing here repaints, because the tokens were copied into every control
// when this editor was built and a half-recoloured panel is worse than a
// message saying when the new one arrives.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
// The top half of the title plate, less the pieces on it that already answer a
// click: the unit mark, the preset name and the two preset buttons. The same
// rectangle in both themes, so the door is where the hand learned it was.
bool Editor::onThemePlate(juce::Point<int> at) const
{
    const auto bounds = getLocalBounds();
    const auto plate = ui::identityPlateBounds(bounds);
    if (!plate.withHeight(plate.getHeight() / 2).contains(at)) return false;
    return !ui::unitMarkBounds(bounds).contains(at)
        && !presetPlate.getBounds().contains(at)
        && !ui::presetLabelBounds(bounds).contains(at)
        && !ui::presetButtonBounds(bounds, false).contains(at)
        && !ui::presetButtonBounds(bounds, true).contains(at);
}

void Editor::showThemeMenu()
{
    const auto safe = juce::Component::SafePointer<Editor>(this);
    auto menu = ui::themeMenu(ui::currentThemeId);
    menu.showMenuAsync(juce::PopupMenu::Options().withMinimumWidth(150),
        [safe] (int chosen)
        {
            if (safe == nullptr || chosen <= 0) return;
            const auto theme = static_cast<ui::ThemeId>(chosen - 1);
            Processor::saveThemeChoice(theme);
            // Picking the look already on screen changes nothing worth
            // announcing; picking the other one is answered where every
            // transient message on this panel goes, in the colour a failed
            // load uses, because until the restart it is a promise.
            if (theme == ui::currentThemeId) return;
            safe->presetName.setText(juce::String(ui::themeName(theme)).toUpperCase() + " ON RESTART",
                                     juce::dontSendNotification);
            safe->presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        });
}
}
