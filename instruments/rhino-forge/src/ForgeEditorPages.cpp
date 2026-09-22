// Which of the panel is showing: the five tabs, the numbered cards that pick
// an envelope or an LFO, the colour a module's plate is painted, and the
// enables that grey out what a setting has made meaningless.
#include "ForgeEditorInternal.h"

namespace rhino::forge
{
void Editor::showPage(ui::Page target)
{
    if (page == target) return;
    page = target;
    for (int i = 0; i < ui::tabCount; ++i)
        tabs[static_cast<size_t>(i)]->setToggleState(ui::tabPages[i] == page, juce::dontSendNotification);
    applyPage();
}

// A module either declares a page or stays put. Hiding rather than rebuilding
// keeps every attachment alive, so a knob the tab is covering is still driven
// by the host and by the matrix while it is out of sight.
//
// What is on screen is worked out in one place, because the tab is not the only
// thing that decides it: a control sharing a cell is also hidden while it is
// not the reading in charge.
void Editor::applyPage()
{
    // Before the layout pass below, because which of a slot's knobs are on
    // screen is what that pass is placing. No applyEnableStates() ahead of it:
    // resized() opens with one, after this has settled what the slots hold, and
    // running it twice per tab switch walks every control on the panel twice.
    refreshFxSlots();
    refreshWarpFields();
    refreshFilterFields();
    if (tablePanel != nullptr) tablePanel->setVisible(page == ui::Page::table);
    // Last, because a macro's handle takes the place of its label and the
    // layout pass is what decides that.
    resized();
    repaint();
}

// The plate beside the keyboard puts the arp's panes up and takes them away.
// The expanded rack and the arp both want the lower row, so opening one closes
// the other: two overlays on one field would leave whichever lost hidden behind
// the winner with its switch still lit.
void Editor::toggleArpOpen()
{
    arpOpen = !arpOpen;
    if (arpOpen) fxExpanded = false;
    applyPage();
}

// The lamp on that same plate. This one is a parameter — whether the arp runs
// saves with the patch and automates — so it goes through the host the way
// every other switch on the panel does rather than being set behind its back.
void Editor::toggleArpEnabled()
{
    auto* parameter = processor.state.getParameter("arpEnable");
    if (parameter == nullptr) return;
    const auto on = parameter->getValue() >= 0.5f;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(on ? 0.0f : 1.0f);
    parameter->endChangeGesture();
    applyEnableStates();
    repaint();
}

juce::Colour Editor::accentOf(const ui::Module& module) const
{
    if (module.display == ui::Display::oscillator)
        return ui::panelColourOf(ui::panelColourFrom(processor.panelColour(module.id)));
    return ui::accentFor(module);
}

void Editor::showPanelColourMenu(const ui::Module& module)
{
    auto menu = ui::panelColourMenu(processor.panelColour(module.id),
                                    juce::String(module.title) + " colour");
    const auto id = module.id;
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withMinimumWidth(150),
                       [safe, id] (int chosen)
                       {
                           if (safe == nullptr || chosen <= 0) return;
                           safe->processor.setPanelColour(id, chosen - 1);
                           safe->applyPanelColours();
                       });
}

// Pushes each module's colour out to everything inside it. The plate and the
// tube are drawn from accentOf on the next repaint, but a control holds its
// colour as component state, so those have to be told.
void Editor::applyPanelColours()
{
    for (auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (descriptor.display != ui::Display::oscillator) continue;
        const auto accent = accentOf(descriptor);

        if (module.enable != nullptr)
        {
            module.enable->accent = accent;
            module.enable->repaint();
        }
        for (auto& held : module.controls)
        {
            held->slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
            held->slider.setColour(juce::Slider::thumbColourId, accent);
            if (held->chip != nullptr) held->chip->accent = accent;
            if (held->rocker != nullptr) held->rocker->accent = accent;
            if (held->waves != nullptr) held->waves->accent = accent;
            if (held->selector != nullptr)
            {
                held->selector->accent = accent;
                held->selector->repaint();
            }
        }
        for (auto& card : module.bankButtons) card->accent = accent;
    }
    // The plates are in the cached layer, so this is what actually redraws
    // them: the key carries every module's colour and has just changed.
    repaint();
}

void Editor::showBank(ModuleUi& module, int bank)
{
    if (module.bank == bank) return;
    module.bank = bank;
    for (int i = 0; i < static_cast<int>(module.bankButtons.size()); ++i)
        module.bankButtons[static_cast<size_t>(i)]->setToggleState(i == bank, juce::dontSendNotification);
    // The handle in the header drags whichever of them is showing, so the
    // layout has to run again to put the right one there.
    applyEnableStates();
    // Switching the rack showing is switching which twelve-control slot set of
    // controls is on screen, and each slot's knobs are named by whatever type
    // that slot holds.
    refreshFxSlots();
    refreshModulationRings();
    resized();
    repaint();
}

// Which LFO the panel is showing. The module is the one that declares the LFO
// display, so nothing here has to know its id.
int Editor::shownLfo() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::lfo)
            return juce::jlimit(0, lfoCount - 1, module.bank);
    return 0;
}

// Which envelope the panel is showing. The module is the one that declares the
// envelope display, so nothing here has to know its id.
int Editor::shownEnv() const
{
    for (const auto& module : moduleUis)
        if (module.descriptor->display == ui::Display::envelope)
            return juce::jlimit(0, envCount - 1, module.bank);
    return ampEnv;
}

// What the envelope module's header says. While a note is sounding that is the
// stage the envelope showing has reached — the reading the knobs cannot give,
// because it is a position in a shape rather than the shape. At rest there is
// no stage to name, so it says what that envelope is for instead: ENV 1 is
// wired to the amplitude, and the other three go nowhere until a slot sends
// them.
juce::String Editor::envHeaderDetail() const
{
    const auto env = shownEnv();
    const juce::String stage = ui::stageName(static_cast<ui::Stage>(
        juce::jlimit(0, 4, processor.envelopeStage(env))));
    if (stage.isNotEmpty()) return stage;
    return env == ampEnv ? "AMP" : "SOURCE";
}

void Editor::applyEnableStates()
{
    for (auto& module : moduleUis)
    {
        const auto onPage = moduleShown(*module.descriptor);
        if (module.enable != nullptr) module.enable->setVisible(onPage);
        for (auto& button : module.bankButtons) button->setVisible(onPage);

        for (auto& control : module.controls)
        {
            auto inFxViewport = true;
            if (isFxModule(*module.descriptor))
            {
                int rack = 0, slot = 0;
                if (fxControlAt(control->id, rack, slot))
                {
                    const auto row = fxDisplayRow(rack, slot);
                    const auto first = fxFirstVisibleSlots[static_cast<size_t>(rack)];
                    inFxViewport = row >= first
                        && row < first + ui::fxIntersectingSlotCount(
                            moduleAreaFor(*module.descriptor));
                }
            }
            // A control is live when its module is on and nothing else has
            // taken it over — polyphony means nothing once mono is switched on,
            // and a tempo division means nothing while the rate is in Hertz.
            const auto on = module.on()
                && (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f)
                && (control->enabledBy == nullptr || value(control->enabledBy) >= 0.5f)
                && !(module.descriptor->display == ui::Display::lfo
                     && control->id.endsWith("Shape")
                     && processor.lfoTableIsCustom(control->bank))
                // A rack knob its modes have made meaningless greys out, the
                // same way polyphony does under mono. It is still here; it just
                // has nothing to do until the mode beside it moves.
                && fxKnobLive(*control);
            // A control that shares its cell leaves rather than greys out: the
            // other reading of the same setting is standing in the same place,
            // and a greyed control would be sitting on top of the live one.
            const auto shown = onPage
                // A module declared in banks has only one of them on screen.
                && control->bank == module.bank
                // Fixed-height rack rows outside the viewport remain attached
                // to their parameters but are not painted on top of neighbours.
                && inFxViewport
                // A rack knob its slot's type does not have is not a control
                // at all while that type is in there.
                && fxControlUsed(*control)
                && (on || !ui::inSharedCell(*module.descriptor, control->row, control->index));

            control->label.setVisible(shown);
            // A macro's name follows its knob on and off the panel, and is
            // never greyed with it: a name is not a setting, and a macro sitting
            // under a module that is switched off is still called what it is
            // called.
            if (control->macroName != nullptr) control->macroName->setVisible(shown);
            if (control->plate != nullptr)
            {
                control->plate->setEnabled(on);
                control->plate->setVisible(shown);
                continue;
            }
            if (control->selector != nullptr)
            {
                control->selector->setEnabled(on);
                control->selector->setVisible(shown);
                continue;
            }
            if (control->chip != nullptr)
            {
                control->chip->setEnabled(on);
                control->chip->setVisible(shown);
                continue;
            }
            if (control->waves != nullptr)
            {
                control->waves->setEnabled(on);
                control->waves->setVisible(shown);
                continue;
            }
            if (control->rocker != nullptr)
            {
                control->rocker->setEnabled(on);
                control->rocker->setVisible(shown);
            }
            else
            {
                control->slider.setEnabled(on);
                control->slider.setVisible(shown);
                control->slider.setColour(juce::Slider::textBoxTextColourId,
                                          ui::text.withAlpha(on ? 1.0f : 0.4f));
            }
            control->label.setColour(juce::Label::textColourId,
                                     ui::labelText.withAlpha(on ? 1.0f : 0.4f));
        }
    }
}

// The rate LFO 1 is actually running at, for its header. Set in beats that is a
// division of the host's tempo, which is the one reading the knob cannot give
// on its own — so the division is named and the Hertz it works out to is put
// beside it.
juce::String Editor::lfoHeaderDetail() const
{
    const auto lfo = shownLfo();
    const auto hertz = juce::String(processor.lfoRateHz(lfo), 2) + " HZ";
    if (value(lfoParameterId(lfo, "RateUnit")) < 0.5f) return hertz;

    const auto division = lfoDivisions()[static_cast<size_t>(
        juce::jlimit(0, lfoDivisionCount - 1,
                     juce::roundToInt(value(lfoParameterId(lfo, "Division")))))].label;
    return juce::String(division) + " // " + hertz;
}
}
