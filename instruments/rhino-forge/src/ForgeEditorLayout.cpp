// Where everything lands. resized() walks the declared modules and gives each
// component the rectangle ForgeLayout.h computes for it; the rest is the
// handful of areas the paint and input paths ask for by name.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
bool Editor::moduleShown(const ui::Module& module) const
{
    // The arp is an overlay rather than a tab. Its panes appear when it is
    // opened, whichever tab is showing, and the modules they stand on step
    // aside for exactly as long as they are there — which is why this is asked
    // before the page is consulted at all.
    if (ui::onPage(module, ui::Page::arp)) return arpOpen;
    if (arpOpen && ui::coveredByArp(module)) return false;
    if (!ui::onPage(module, page)) return false;
    return page != ui::Page::fx || !fxExpanded || isFxModule(module);
}

ui::Page Editor::pageOf(const ui::Module& module) const
{
    return ui::onPage(module, ui::Page::arp) ? ui::Page::arp : page;
}

juce::Rectangle<int> Editor::moduleAreaFor(const ui::Module& module) const
{
    if (isFxModule(module))
        return ui::fxModuleBounds(getLocalBounds(), module, fxExpanded);
    return ui::moduleBounds(getLocalBounds(), module);
}

void Editor::resized()
{
    // A taller expanded rack may reveal every slot; a later compact resize can
    // reduce the window again. Keep each rack's remembered top slot legal and
    // refresh visibility before positioning the child controls.
    clampFxScroll();
    applyEnableStates();

    savePreset.setBounds(ui::presetButtonBounds(getLocalBounds(), true));
    loadPreset.setBounds(ui::presetButtonBounds(getLocalBounds(), false));
    presetName.setBounds(ui::presetLabelBounds(getLocalBounds()));

    for (int i = 0; i < ui::tabCount && i < static_cast<int>(tabs.size()); ++i)
        tabs[static_cast<size_t>(i)]->setBounds(ui::tabBounds(i, getWidth()));

    const auto keys = ui::keyboardBounds(getLocalBounds());
    // Sized so the seventy-six keys span what is left of the shelf exactly,
    // rather than running out partway and leaving a blank stretch. The divisor
    // is the count actually drawn: the octave the ARP plate took is gone from
    // the range as well as from the width, so a key is the size it always was.
    keyboard.setKeyWidth(static_cast<float>(keys.getWidth())
                         / static_cast<float>(ui::keyboardWhiteKeys));
    keyboard.setBounds(keys);

    // Handles are positioned after their modules, because a macro's handle sits
    // on top of its knob's label.
    const auto handleFor = [this] (int source) -> ui::SourceHandle*
    {
        for (auto& handle : handles)
            if (handle->source == source) return handle.get();
        return nullptr;
    };

    if (tablePanel != nullptr)
        for (const auto& descriptor : ui::modules())
            if (juce::String(descriptor.id) == "table")
                tablePanel->setBounds(ui::controlArea(ui::moduleBounds(getLocalBounds(), descriptor),
                                                      descriptor));

    const auto diameter = ui::uniformKnobDiameter(getLocalBounds());
    for (auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        const auto visible = moduleShown(descriptor);
        const auto area = moduleAreaFor(descriptor);
        const auto rackModule = isFxModule(descriptor);

        if (rackModule)
            fxControlViewport.setBounds(
                ui::fxSlotViewportBounds(area).getIntersection(ui::fxRackBounds(area, fxListOpen)));

        if (module.enable != nullptr)
        {
            const auto led = juce::Rectangle<int>(ui::headerHeight, ui::headerHeight)
                .withPosition(area.getX() + 8, area.getY());
            module.enable->setBounds(led);
        }
        // A module that is itself a source puts its handle where the enable LED
        // would be, ahead of the title. A module showing one of several sources
        // puts the one it is showing there, and leaves the rest off the panel.
        if (descriptor.handleSource != 0)
        {
            const auto showing = descriptor.handleSource + module.bank;
            for (int bank = 0; bank < ui::bankCount(descriptor); ++bank)
                if (auto* other = handleFor(descriptor.handleSource + bank))
                    other->setVisible(visible && descriptor.handleSource + bank == showing);
            // Flush with the module's top edge and the full height of the
            // header: the handle is a card hanging off that edge, not a button
            // floating inside it.
            if (auto* handle = handleFor(showing))
                handle->setBounds(area.getX() + (descriptor.enableId != nullptr ? ui::headerHeight + 8 : 10),
                                  area.getY(), ui::handleWidth, ui::headerHeight);
        }
        for (int bank = 0; bank < static_cast<int>(module.bankButtons.size()); ++bank)
            module.bankButtons[static_cast<size_t>(bank)]
                ->setBounds(ui::bankButtonBounds(area, descriptor, bank, ui::handleWidth));

        for (auto& held : module.controls)
        {
            auto& control = *held;
            auto controlsArea = area;
            if (rackModule)
            {
                int rack = 0, slot = 0;
                if (fxControlAt(control.id, rack, slot))
                    controlsArea = fxRackAreaFor(area, rack, slot);
            }
            auto block = ui::controlBlock(controlsArea, descriptor, control.row, control.index, diameter);
            if (rackModule)
                block = block.translated(-fxControlViewport.getX(), -fxControlViewport.getY());

            // A macro's cell is laid out as nothing else on the panel is: the
            // knob, the plate beside it carrying the number you drag and the
            // count of what that number reaches, and the macro's own name
            // under both. The declared label — the bare number — is what the
            // plate replaced, so it stays off the panel.
            if (control.macroName != nullptr)
            {
                const auto macro = control.id.getTrailingIntValue();
                const auto cell = ui::cellBounds(controlsArea, descriptor,
                                                 control.row, control.index);
                if (auto* handle = handleFor(static_cast<int>(ModSource::macro1) + macro - 1))
                {
                    handle->setVisible(visible && control.bank == module.bank);
                    handle->setBounds(ui::macroPlateBounds(cell, diameter));
                }
                control.label.setVisible(false);
                control.macroName->setBounds(ui::macroNameBounds(cell, diameter));
                control.slider.setBounds(ui::macroKnobBounds(cell, diameter));
                continue;
            }
            switch (control.style)
            {
                case ui::Style::chip:
                    control.chip->setBounds(block);
                    break;
                case ui::Style::bar:
                    control.slider.setBounds(block);
                    break;
                case ui::Style::plate:
                    control.plate->setBounds(block);
                    break;
                case ui::Style::wave:
                    control.waves->setBounds(block);
                    break;
                case ui::Style::selector:
                    control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.selector->setBounds(block);
                    break;
                case ui::Style::fader:
                    // The same label line a knob's sits on, so a row of faders
                    // and a row of knobs line up across the mixer.
                    control.label.setBounds(ui::knobLabelBounds(block));
                    control.slider.setBounds(block.withTrimmedTop(ui::knobLabelHeight));
                    break;
                case ui::Style::stepper:
                    // Inside a table the column title is the label, so the
                    // field takes the whole block rather than the half of it
                    // left under a label strip.
                    if (descriptor.columnHeaderHeight == 0)
                        control.label.setBounds(block.removeFromTop(ui::stepperLabelHeight));
                    control.slider.setBounds(block);
                    break;
                case ui::Style::rocker:
                    // Same label line as the knobs either side, so the switch
                    // sits exactly where their circles do.
                    control.label.setBounds(ui::knobLabelBounds(block));
                    control.rocker->setBounds(ui::rockerBounds(block.withTrimmedTop(ui::knobLabelHeight)));
                    break;
                case ui::Style::knob:
                    control.label.setBounds(ui::knobLabelBounds(block));
                    control.slider.setBounds(block.withTrimmedTop(ui::knobLabelHeight));
                    break;
            }
        }
    }
}

// The envelope module is painted straight onto the editor rather than being a
// component of its own, so the things on it that can be clicked or scrolled are
// hit-tested here, against the same geometry the painter lays them out with.
const ui::Module* Editor::envelopeModule() const
{
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (descriptor.display != ui::Display::envelope) continue;
        return moduleShown(descriptor) ? &descriptor : nullptr;
    }
    return nullptr;
}

juce::Rectangle<int> Editor::envelopeDisplayBounds() const
{
    const auto* module = envelopeModule();
    return module == nullptr ? juce::Rectangle<int>()
                             : ui::displayBounds(ui::moduleBounds(getLocalBounds(), *module), *module);
}

// The window belongs to the envelope showing, not to the module: four
// envelopes are not the same length, so a window set on one is no reading of
// another.
void Editor::setEnvelopeZoom(int zoom)
{
    auto& current = envelopeZoom[static_cast<size_t>(shownEnv())];
    const auto clamped = juce::jlimit(0, ui::envelopeZoomCount - 1, zoom);
    if (clamped == current) return;
    current = clamped;
    repaint();
}
}
