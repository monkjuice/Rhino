// Which of the panel is showing: the five tabs, the numbered cards that pick
// an envelope or an LFO, the colour a module's plate is painted, and the
// enables that grey out what a setting has made meaningless.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
void Editor::showPage(ui::Page target)
{
    // A tab is a request to see the panel, so the browser standing over it
    // goes, whichever page was asked for.
    if (presetBrowserOpen()) closePresetBrowser();
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
    refreshNoiseField();
    refreshOscModeFields();
    refreshLoopFields();
    if (tablePanel != nullptr) tablePanel->setVisible(page == ui::Page::table);
    // Every toggle that opens or closes an overlay ends here, so the door lit
    // for the guide is always the guide's own state.
    guideDoor.setOpen(guideOpen);
    // Last, because a macro's handle takes the place of its label and the
    // layout pass is what decides that.
    resized();
    repaint();
}

// The plate beside the keyboard puts the arp's panes up and takes them away.
// The expanded rack, the arp and the note guide all want the lower row, so
// opening one closes the others: two overlays on one field would leave
// whichever lost hidden behind the winner with its switch still lit.
void Editor::toggleArpOpen()
{
    arpOpen = !arpOpen;
    if (arpOpen)
    {
        fxExpanded = false;
        guideOpen = false;
    }
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

// One of the four slot buttons on the plate. The slot is a parameter, so it
// goes through the host the way the lamp does; the Processor then moves the
// pattern's parameters to match, and is asked to do so now rather than on its
// next tick so the knobs follow the press.
void Editor::chooseArpSlot(int slot)
{
    auto* parameter = processor.state.getParameter("arpSlot");
    if (parameter == nullptr) return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(
        static_cast<float>(juce::jlimit(0, arpSlotCount - 1, slot))));
    parameter->endChangeGesture();
    processor.applyArpSlot();
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
        // The table field is the editor's own rather than a declared control,
        // so it is told here as well.
        const auto oscillator = oscillatorIndexFromId(juce::String(descriptor.id));
        if (oscillator >= 0)
            if (auto& field = tableFields[static_cast<size_t>(oscillator)])
            {
                field->accent = accent;
                field->repaint();
            }
    }
    // The plates are in the cached layer, so this is what actually redraws
    // them: the key carries every module's colour and has just changed.
    repaint();
}

void Editor::attachEnable(ModuleUi& module)
{
    if (module.enable == nullptr) return;
    const auto* id = ui::enableIdFor(*module.descriptor, module.bank);
    // The old attachment goes first, so the button is never listened to by two
    // parameters at once; the new one reads its own parameter into the LED.
    module.enableAttachment.reset();
    module.enable->setTooltip(ui::tooltipFor(id));
    module.enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.state, id, *module.enable);
}

void Editor::showBank(ModuleUi& module, int bank)
{
    if (module.bank == bank) return;
    module.bank = bank;
    for (int i = 0; i < static_cast<int>(module.bankButtons.size()); ++i)
        module.bankButtons[static_cast<size_t>(i)]->setToggleState(i == bank, juce::dontSendNotification);
    // The LED of a module whose banks are switched one by one is the switch of
    // the one showing.
    if (!module.descriptor->bankEnables.empty()) attachEnable(module);
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
    // The matrix shows a window of its rows, and only as many rows as the
    // patch has earned. Both are read once here rather than per control.
    const auto matrixShown = matrixShownRowCount();
    for (auto& module : moduleUis)
    {
        const auto onPage = moduleShown(*module.descriptor);
        if (module.enable != nullptr) module.enable->setVisible(onPage);
        for (auto& button : module.bankButtons) button->setVisible(onPage);

        auto matrixWindowStart = 0;
        auto matrixWindowEnd = std::numeric_limits<int>::max();
        if (ui::isMatrixModule(*module.descriptor))
        {
            matrixWindowStart = matrixFirstRow;
            matrixWindowEnd = juce::jmin(matrixShown,
                                         matrixFirstRow + ui::matrixVisibleRowCount(
                                             moduleAreaFor(*module.descriptor), *module.descriptor));
        }

        for (auto& control : module.controls)
        {
            // A matrix row outside the window, or past the rows the table
            // shows, stays attached to its parameters and off the panel.
            const auto inMatrixWindow = control->row >= matrixWindowStart
                                     && control->row < matrixWindowEnd;
            auto inFxViewport = true;
            auto inFxSlot = false, fxSlotBypassed = false;
            if (isFxModule(*module.descriptor))
            {
                int rack = 0, slot = 0;
                if (fxControlAt(control->id, rack, slot))
                {
                    inFxSlot = true;
                    fxSlotBypassed = fxOn(value(fxParameterId(rack, slot, "Bypass")));
                    const auto row = fxDisplayRow(rack, slot);
                    const auto first = fxFirstVisibleSlots[static_cast<size_t>(rack)];
                    inFxViewport = row >= first
                        && row < first + ui::fxIntersectingSlotCount(
                            moduleAreaFor(*module.descriptor));
                }
            }
            // A control is in charge when nothing else has taken it over —
            // polyphony means nothing once mono is switched on, and a tempo
            // division means nothing while the rate is in Hertz — and live when
            // it is in charge of a module that is on.
            const auto inCharge =
                (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f)
                && (control->enabledBy == nullptr || value(control->enabledBy) >= 0.5f)
                // An LFO's SHAPE is in charge only while there is a shape to
                // choose: a NORMAL LFO on a built-in rather than a drawn
                // table. Its SMOOTH is the sample and hold's alone.
                && !(module.descriptor->display == ui::Display::lfo
                     && control->id.endsWith("Shape")
                     && (processor.lfoTableIsCustom(control->bank)
                         || typeOfLfo(control->bank) != LfoType::normal))
                && !(module.descriptor->display == ui::Display::lfo
                     && control->id.endsWith("Smooth")
                     && typeOfLfo(control->bank) != LfoType::sampleHold)
                // A rack knob its modes have made meaningless greys out, the
                // same way polyphony does under mono. It is still here; it just
                // has nothing to do until the mode beside it moves.
                && fxKnobLive(*control);
            const auto on = module.on() && inCharge;
            // A control that shares its cell leaves rather than greys out: the
            // other reading of the same setting is standing in the same place,
            // and a greyed control would be sitting on top of the live one.
            //
            // Asked of the control rather than of its module. Switching a
            // module off takes both of a pair out of service at once, and
            // hiding both would leave the cell empty — an oscillator switched
            // off lost POSITION, DETUNE and BLEND that way. The one in charge
            // stays and greys out with the rest of the module.
            const auto shown = onPage
                // A module declared in banks has only one of them on screen.
                && control->bank == module.bank
                // Fixed-height rack rows outside the viewport remain attached
                // to their parameters but are not painted on top of neighbours.
                && inFxViewport
                && inMatrixWindow
                // A rack knob its slot's type does not have is not a control
                // at all while that type is in there.
                && fxControlUsed(*control)
                // A control belonging to another mode is not here at all. The
                // mode chooses between control sets rather than switching one
                // off, so the set that is not chosen leaves the panel instead
                // of greying out on top of the set that is.
                && (control->modeBy == nullptr
                    || juce::roundToInt(value(control->modeBy)) == control->modeIs)
                && (inCharge || !ui::inSharedCell(*module.descriptor, control->row, control->index));

            // A control seated inside a display carries no label strip. It is
            // inside the thing it would have named — the filter's TYPE field
            // lies along the top edge of the response it chooses — and a
            // caption over it would be a word where the curve should be. A
            // rack control has a caption only where its strip gave it one.
            control->label.setVisible(
                shown && !ui::seatedOutsideBody(*module.descriptor, control->row) && control->fxCaptionShown);
            // A bypassed effect's controls stay where they are and fade with
            // it; its card goes dark, which is the switch saying so.
            if (inFxSlot)
            {
                const auto faded = fxSlotBypassed ? 0.5f : 1.0f;
                for (auto* component : {static_cast<juce::Component*>(&control->label),
                                        static_cast<juce::Component*>(&control->slider),
                                        static_cast<juce::Component*>(control->selector.get())})
                    if (component != nullptr && component->getAlpha() != faded) component->setAlpha(faded);
            }
            // A macro's name follows its knob on and off the panel, and is
            // never greyed with it: a name is not a setting, and a macro sitting
            // under a module that is switched off is still called what it is
            // called.
            if (control->macroName != nullptr) control->macroName->setVisible(shown);
            if (control->plate != nullptr)
            {
                control->plate->setEnabled(on);
                control->plate->setVisible(shown);
                if (control->plate->lit == fxSlotBypassed)
                {
                    control->plate->lit = !fxSlotBypassed;
                    control->plate->repaint();
                }
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
                // A rack slot's bypass is its card. The chip stays attached to
                // the parameter and off the panel.
                control->chip->setVisible(shown && !inFxSlot);
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
    // The note guide's panel is a component rather than declared controls, so
    // it follows its module on and off the panel here.
    guidePanel.setVisible(guideOpen);
    // A PATH LFO hangs a second card in its header and the bank cards move
    // along for it. Noticed here rather than only on a click, so a type
    // changed by a preset or by the host moves the header the same way.
    if (const auto pathShown = shownLfoType() == LfoType::path; pathShown != lfoPathHandleShown)
    {
        lfoPathHandleShown = pathShown;
        resized();
    }
    applyTableFieldStates();
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
