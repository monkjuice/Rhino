#include "ForgeEditor.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
namespace
{
// The destination index a parameter corresponds to, or 0 if the matrix cannot
// point at it.
int destinationFor(const juce::String& parameterId)
{
    for (int i = 1; i < destinationCount; ++i)
        if (parameterId == destinations()[static_cast<size_t>(i)].id) return i;
    return 0;
}

juce::String slotParameter(int slot, const char* suffix)
{
    return "mod" + juce::String(slot + 1) + suffix;
}

// How many semitones of the piano the computer keys can reach at once. JUCE's
// default qwerty mapping is "awsedftgyhujkolp;" laid on the note offsets 0..16
// from the C of the mapping octave, so the reach is a C to the E an octave and
// a third above it.
constexpr int computerKeySpan = 17;
}

Editor::Editor(Processor& p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    buildModules();
    buildHandles();
    buildTabs();
    buildTablePanel();

    keyboard.setAvailableRange(21, 108);
    keyboard.setLowestVisibleKey(21);
    // Middle C is C4 here because middle C is C4 in Rhino's own grid, which
    // names its rows with the same octave number — see StepGridPainter. JUCE's
    // keyboard defaults to 3 instead, so without this the same key reads C3 on
    // the panel and C4 on the grid that is driving it, and a player comparing
    // the two concludes the synth is an octave out when only the label is.
    keyboard.setOctaveForMiddleC(4);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setKeyPressBaseOctave(computerKeyOctave);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd8dcea));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff10131f));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff05070e));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, ui::electricBlue.withAlpha(0.75f));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, ui::electricBlue.withAlpha(0.3f));
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(0.4f));
    keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, ui::panelRaised);
    keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, ui::mutedText);
    addAndMakeVisible(keyboard);

    for (auto* button : {&loadPreset, &savePreset})
    {
        button->setColour(juce::TextButton::buttonColourId, ui::panelRaised);
        button->setColour(juce::TextButton::textColourOffId, ui::text);
        addAndMakeVisible(button);
    }
    loadPreset.onClick = [this] { choosePresetToLoad(); };
    savePreset.onClick = [this] { choosePresetToSave(); };

    presetName.setText("INIT SIGNAL", juce::dontSendNotification);
    presetName.setJustificationType(juce::Justification::centredRight);
    presetName.setFont(juce::FontOptions(10.0f));
    presetName.setColour(juce::Label::textColourId, ui::mutedText);
    addAndMakeVisible(presetName);

    // The default tooltip is dark text furniture on a dark panel, which leaves
    // the words floating with no edge to read them against. Given the same
    // well-and-hairline treatment the rest of the panel uses.
    tooltips.setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff05070e));
    tooltips.setColour(juce::TooltipWindow::textColourId, ui::text);
    tooltips.setColour(juce::TooltipWindow::outlineColourId, ui::electricBlue.withAlpha(0.6f));

    // So the panel itself can hold focus when nothing in it does, and the keys
    // still have somewhere to arrive from.
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    // The matrix moving into the tabbed row took a whole grid row off the
    // bottom of the panel, so the window is shorter than it was at every limit.
    setResizeLimits(ui::minPanelWidth, ui::minPanelHeight, ui::maxPanelWidth, ui::maxPanelHeight);
    setSize(ui::defaultPanelWidth, ui::defaultPanelHeight);
    addChildComponent(valueBubble);
    applyEnableStates();
    applyTableCounts();
    applyPage();
    startTimerHz(24);
}

void Editor::buildTablePanel()
{
    tablePanel = std::make_unique<ui::TablePanel>(processor.tableStore());
    tablePanel->importer = [this] (int oscillator, const juce::File& file)
    {
        return processor.importTable(oscillator, file);
    };
    tablePanel->onTableChanged = [this]
    {
        // A table with a different number of frames changes what POSITION steps
        // through and what its readout says, on both tabs.
        applyTableCounts();
        if (tablePanel->status.isNotEmpty())
        {
            presetName.setText(tablePanel->status, juce::dontSendNotification);
            presetName.setColour(juce::Label::textColourId, ui::signalViolet);
            tablePanel->status.clear();
        }
        repaint();
    };
    addChildComponent(*tablePanel);
}

// POSITION picks a frame out of a table, so a hand on it should land on one
// rather than a hair short of it — which means its detents follow whatever
// table that oscillator is now reading. The readout is pushed too: a slider
// only re-reads its parameter's text when its value moves, and here the value
// has stayed put while the text it should show has changed.
void Editor::applyTableCounts()
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            if (!control->id.endsWith("Position")) continue;
            const auto oscillator = control->id.startsWith("oscB") ? 1 : 0;
            control->slider.gestureSteps = processor.tableStore().frameCount(oscillator);
            control->slider.updateText();
        }
}

void Editor::buildTabs()
{
    for (int i = 0; i < ui::tabCount; ++i)
    {
        const auto target = ui::tabPages[i];
        auto tab = std::make_unique<ui::PageTab>(ui::pageName(target));
        tab->accent = target == ui::Page::matrix ? ui::signalViolet : ui::electricBlue;
        tab->setTooltip(target == ui::Page::matrix
                            ? "Show the modulation matrix in place of the oscillators"
                            : "Show the oscillators");
        tab->setToggleState(target == page, juce::dontSendNotification);
        tab->onClick = [this, target] { showPage(target); };
        addAndMakeVisible(*tab);
        tabs.push_back(std::move(tab));
    }
}

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
    applyEnableStates();
    if (tablePanel != nullptr) tablePanel->setVisible(page == ui::Page::table);
    // Last, because a macro's handle takes the place of its label and the
    // layout pass is what decides that.
    resized();
    repaint();
}

void Editor::buildModules()
{
    for (const auto& descriptor : ui::modules())
    {
        ModuleUi module;
        module.descriptor = &descriptor;
        const auto accent = ui::accentFor(descriptor);

        if (descriptor.enableId != nullptr)
        {
            module.enable = std::make_unique<ui::EnableLed>();
            module.enable->accent = accent;
            module.enable->setTooltip(ui::tooltipFor(descriptor.enableId));
            // Repaint the whole panel: switching a module off dims its shell,
            // its display and every knob inside it, not just the dot.
            module.enable->onClick = [this] { applyEnableStates(); repaint(); };
            addAndMakeVisible(*module.enable);
            module.enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.state, descriptor.enableId, *module.enable);
        }

        for (int r = 0; r < static_cast<int>(descriptor.rows.size()); ++r)
        {
            const auto& row = descriptor.rows[static_cast<size_t>(r)];
            for (int i = 0; i < static_cast<int>(row.controls.size()); ++i)
            {
                const auto& declared = row.controls[static_cast<size_t>(i)];
                auto control = std::make_unique<Control>();
                control->style = declared.style;
                control->id = declared.id;
                control->disabledBy = declared.disabledBy;
                control->enabledBy = declared.enabledBy;
                control->row = r;
                control->index = i;
                control->bank = ui::bankOf(descriptor, r, i);

                if (declared.style == ui::Style::chip)
                {
                    control->chip = std::make_unique<ui::ToggleChip>(declared.label);
                    control->chip->accent = accent;
                    control->chip->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->chip);
                    addAndMakeVisible(*control->chip);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                control->label.setText(declared.label, juce::dontSendNotification);
                control->label.setJustificationType(juce::Justification::centred);
                control->label.setColour(juce::Label::textColourId, ui::mutedText);
                control->label.setFont(juce::FontOptions(declared.style == ui::Style::stepper ? 9.0f : 10.0f));

                if (declared.style == ui::Style::rocker)
                {
                    // No ON/OFF readout: the switch shows its own state, by
                    // moving as well as lighting up.
                    control->rocker = std::make_unique<ui::RockerSwitch>(declared.label);
                    control->rocker->accent = accent;
                    control->rocker->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->rocker);

                    addAndMakeVisible(*control->rocker);
                    addAndMakeVisible(control->label);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::knob)
                {
                    control->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    // No printed value. The reading appears in the bubble while
                    // the knob is being turned, which is the only time anyone
                    // was reading it.
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                }
                else
                {
                    // Both bar styles route to drawLinearSlider. A stepper is
                    // vertical and painted as a numeric field; a matrix amount
                    // is horizontal, and drags along the fill it draws.
                    control->slider.setSliderStyle(declared.style == ui::Style::bar
                                                       ? juce::Slider::LinearBar
                                                       : juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                }
                control->slider.setLookAndFeel(&lookAndFeel);
                control->slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
                control->slider.setColour(juce::Slider::rotarySliderOutlineColourId, ui::line);
                control->slider.setColour(juce::Slider::thumbColourId, accent);
                control->slider.setColour(juce::Slider::textBoxTextColourId, ui::text);
                control->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                control->slider.setTooltip(ui::tooltipFor(declared.id));
                if (declared.style == ui::Style::knob)
                {
                    // onDragStart/onDragEnd/onValueChange are the editor's to
                    // use: the parameter attachment listens as a Slider
                    // ::Listener and leaves these alone.
                    auto* held = control.get();
                    control->slider.onDragStart = [this, held] { bubbleHeld = true; showValueBubble(*held); };
                    control->slider.onDragEnd = [this] { bubbleHeld = false; };
                    // Fires for a wheel notch and a double-click as well as for
                    // a drag, and for host automation, which is why it only
                    // refreshes a bubble the hand has already opened — or opens
                    // one for a gesture that never started a drag.
                    control->slider.onValueChange = [this, held]
                    {
                        if (bubbleHeld || held->slider.isMouseOverOrDragging()) showValueBubble(*held);
                        else if (bubbleControl == held) showValueBubble(*held);
                    };
                }
                // The attachment installs the parameter's own text formatting,
                // so it must be created before anything reads the slider's text.
                control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.state, declared.id, control->slider);
                // Double-click returns a control to its default. The attachment
                // has already applied the parameter's range, so the default can
                // be read back from it here.
                if (auto* parameter = processor.state.getParameter(declared.id))
                {
                    control->slider.setDoubleClickReturnValue(
                        true, parameter->convertFrom0to1(parameter->getDefaultValue()));
                    // Semitone is a continuous parameter so the matrix can
                    // sweep pitch smoothly through it, but a tuning field
                    // should still land on whole semitones under the hand.
                    if (juce::String(declared.id).endsWith("Semitone"))
                        control->slider.setRange(-12.0, 12.0, 1.0);
                    // Position is the same bargain one step further on. It picks
                    // a frame out of the oscillator's table, so a hand on it
                    // lands on a shape rather than a hair short of one, while
                    // the matrix still sweeps the whole table smoothly. Hold the
                    // fine modifier to stop between two frames on purpose.
                    if (juce::String(declared.id).endsWith("Position"))
                        control->slider.gestureSteps = waveShapeCount;  // refined in applyTableCounts
                }

                // The editor handles right-click so a knob can offer its
                // modulation menu without each slider knowing about the matrix.
                control->slider.addMouseListener(this, false);
                // The ring knows the gesture; the editor knows which slot it
                // belongs to, and that is settled afresh on every refresh.
                control->slider.onRingDrag = [this, knob = control.get()] (float depth)
                {
                    if (knob->ringSlot >= 0) setSlotDepth(knob->ringSlot, depth);
                };
                // A table names its columns once, in the strip above its rows,
                // so its controls carry no label of their own.
                if (descriptor.columnHeaderHeight == 0) addAndMakeVisible(control->label);
                addAndMakeVisible(control->slider);
                module.controls.push_back(std::move(control));
            }
        }

        moduleUis.push_back(std::move(module));
    }
    buildBankButtons();
}

// A module declared in banks carries one numbered button per bank in its
// header. Which bank is showing is the panel's business and not the host's: it
// says which LFO you are looking at, not what the synth is doing, so it is no
// more a parameter than which tab is open.
void Editor::buildBankButtons()
{
    for (auto& module : moduleUis)
    {
        const auto banks = ui::bankCount(*module.descriptor);
        if (banks <= 1) continue;
        const auto accent = ui::accentFor(*module.descriptor);
        for (int bank = 0; bank < banks; ++bank)
        {
            auto button = std::make_unique<ui::BankCard>(juce::String(bank + 1));
            button->accent = accent;
            button->setClickingTogglesState(false);
            button->setToggleState(bank == module.bank, juce::dontSendNotification);
            button->setTooltip("Show " + juce::String(module.descriptor->title) + " "
                               + juce::String(bank + 1));
            button->onClick = [this, which = &module, bank] { showBank(*which, bank); };
            addAndMakeVisible(*button);
            module.bankButtons.push_back(std::move(button));
        }
    }
}

void Editor::showBank(ModuleUi& module, int bank)
{
    if (module.bank == bank) return;
    module.bank = bank;
    for (int i = 0; i < static_cast<int>(module.bankButtons.size()); ++i)
        module.bankButtons[static_cast<size_t>(i)]->setToggleState(i == bank, juce::dontSendNotification);
    // The handle in the header drags whichever LFO is showing, so the layout
    // has to run again to put the right one there.
    applyEnableStates();
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

void Editor::applyEnableStates()
{
    for (auto& module : moduleUis)
    {
        const auto onPage = ui::onPage(*module.descriptor, page);
        if (module.enable != nullptr) module.enable->setVisible(onPage);
        for (auto& button : module.bankButtons) button->setVisible(onPage);

        for (auto& control : module.controls)
        {
            // A control is live when its module is on and nothing else has
            // taken it over — polyphony means nothing once mono is switched on,
            // and a tempo division means nothing while the rate is in Hertz.
            const auto on = module.on()
                && (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f)
                && (control->enabledBy == nullptr || value(control->enabledBy) >= 0.5f);
            // A control that shares its cell leaves rather than greys out: the
            // other reading of the same setting is standing in the same place,
            // and a greyed control would be sitting on top of the live one.
            const auto shown = onPage
                // A module declared in banks has only one of them on screen.
                && control->bank == module.bank
                && (on || !ui::inSharedCell(*module.descriptor, control->row, control->index));

            control->label.setVisible(shown);
            if (control->chip != nullptr)
            {
                control->chip->setEnabled(on);
                control->chip->setVisible(shown);
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
                                     ui::mutedText.withAlpha(on ? 1.0f : 0.4f));
        }
    }
}

float Editor::value(const juce::String& id) const
{
    const auto* raw = processor.state.getRawParameterValue(id);
    return raw == nullptr ? 0.0f : raw->load();
}

void Editor::paint(juce::Graphics& g)
{
    ui::drawBackdrop(g, getLocalBounds());

    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (!ui::onPage(descriptor, page)) continue;
        const auto area = ui::moduleBounds(getLocalBounds(), descriptor);
        const auto on = module.on();
        const auto alpha = on ? 1.0f : 0.35f;
        const auto stage = static_cast<ui::Stage>(juce::jlimit(0, 4, processor.envelopeStage()));
        // A module's header carries what it is doing rather than what it is:
        // the envelope names its stage, and the LFO names the rate it is
        // actually running at, which in sync is a tempo division and so cannot
        // be read off the greyed-out rate knob.
        const auto tableModule = juce::String(descriptor.id) == "table";
        ui::drawModuleShell(g, area, descriptor, on,
                            descriptor.display == ui::Display::envelope ? ui::stageName(stage)
                            : descriptor.display == ui::Display::lfo ? lfoHeaderDetail()
                            : tableModule && tablePanel != nullptr ? tablePanel->headerDetail()
                                : juce::String());

        if (descriptor.columnHeaderHeight > 0) paintTable(g, area, descriptor);

        const auto display = ui::displayBounds(area, descriptor);
        if (display.isEmpty()) continue;
        const auto accent = ui::accentFor(descriptor);
        // An oscillator shows its wave on a picture tube; the other displays
        // stay flat wells, which is what keeps the tubes reading as screens.
        if (descriptor.display == ui::Display::oscillator)
            ui::drawCrtScreen(g, display, accent, alpha);
        else if (descriptor.display == ui::Display::envelope)
            // Short of the full strip: the zoom control has its own well beside
            // this one, and draws it with the curve.
            ui::drawDisplayWell(g, ui::envelopePlotBounds(display));
        else
            ui::drawDisplayWell(g, display);
        switch (descriptor.display)
        {
            case ui::Display::oscillator:
                // Each oscillator draws the table it is actually reading, taken
                // from the frames as authored rather than from a band-limited
                // copy — so the tube shows the table and not a formula, and not
                // whichever copy the note being held happens to want.
                if (const auto* source = ui::displaySourceId(descriptor))
                    ui::drawWaveform(g, display,
                                     processor.tableStore().edit(juce::String(descriptor.id) == "oscB" ? 1 : 0),
                                     value(source), accent, alpha);
                break;
            case ui::Display::envelope:
                ui::drawEnvelope(g, display, value("attack"), value("decay"),
                                 value("sustain"), value("release"), accent, alpha,
                                 stage, processor.envelopeLevel(), envelopeZoom);
                break;
            case ui::Display::lfo:
            {
                const auto lfo = shownLfo();
                ui::drawLfo(g, display,
                            static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
                                                               juce::roundToInt(value(lfoParameterId(lfo, "Shape"))))),
                            processor.lfoPhase(lfo), processor.lfoValue(lfo), accent, alpha);
                break;
            }
            case ui::Display::filter:
                ui::drawFilterResponse(g, display, filterTypeOf(value("filterType")),
                                       value("cutoff"), value("resonance"), accent, alpha);
                break;
            case ui::Display::none:
                break;
        }
    }

    // The line follows a source handle to the cursor, and the knob under it
    // lights up, so a drop lands where it looks like it will.
    if (draggingHandle != nullptr)
    {
        const auto from = draggingHandle->getBounds().toFloat().getCentre();
        const auto to = dragPosition.toFloat();
        g.setColour(ui::signalViolet.withAlpha(0.55f));
        g.drawLine({from, to}, 2.0f);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(to));

        if (const auto* target = const_cast<Editor*>(this)->controlAt(dragPosition))
        {
            const auto reachable = destinationFor(target->id) != 0;
            g.setColour((reachable ? ui::signalViolet : ui::mutedText).withAlpha(0.9f));
            g.drawRoundedRectangle(target->slider.getBounds().toFloat().reduced(2.0f), 4.0f, 1.6f);
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

// A slot counts as live once it has both ends: something driving it and
// somewhere to go. Depth is left out deliberately, so a slot parked at zero
// still reads as a routing you set up rather than as an empty row.
bool Editor::slotIsLive(int slot) const
{
    return juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) > 0
        && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) > 0;
}

// The furniture that makes eight rows of fields read as a table: the column
// titles, once, above the rows; a rule under them; and every row numbered in
// the gutter, against a band on alternate rows.
void Editor::paintTable(juce::Graphics& g, juce::Rectangle<int> area, const ui::Module& descriptor)
{
    const auto accent = ui::accentFor(descriptor);
    const auto titles = ui::columnTitleBounds(area, descriptor);

    ui::drawColumnTitle(g, titles.withWidth(descriptor.rowGutter), "#");
    const auto& first = descriptor.rows.front().controls;
    for (int i = 0; i < static_cast<int>(first.size()); ++i)
    {
        const auto cell = ui::cellBounds(area, descriptor, 0, i);
        ui::drawColumnTitle(g, titles.withX(cell.getX()).withWidth(cell.getWidth()),
                            first[static_cast<size_t>(i)].label);
    }
    ui::drawColumnTitleRule(g, titles);

    for (int row = 0; row < static_cast<int>(descriptor.rows.size()); ++row)
        ui::drawTableRow(g, ui::rowBounds(area, descriptor, row),
                         ui::rowGutterBounds(area, descriptor, row),
                         row + 1, slotIsLive(row), accent);
}

void Editor::resized()
{
    const auto right = getWidth() - ui::windowMargin;
    savePreset.setBounds(right - 62, 26, 62, 26);
    loadPreset.setBounds(right - 130, 26, 62, 26);
    presetName.setBounds(right - 350, 26, 212, 26);

    for (int i = 0; i < ui::tabCount && i < static_cast<int>(tabs.size()); ++i)
        tabs[static_cast<size_t>(i)]->setBounds(ui::tabBounds(i));

    const auto keys = ui::keyboardBounds(getLocalBounds());
    // Sized so the full eighty-eight keys span the panel exactly, rather than
    // running out partway and leaving a blank stretch.
    keyboard.setKeyWidth(static_cast<float>(keys.getWidth()) / 52.0f);
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
        const auto area = ui::moduleBounds(getLocalBounds(), descriptor);

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
                    other->setVisible(descriptor.handleSource + bank == showing);
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
            auto block = ui::controlBlock(area, descriptor, control.row, control.index, diameter);

            // A macro's drag handle replaces its numeric label: the number is
            // the thing you grab, and the knob keeps its own drag gesture.
            if (control.id.startsWith("macro"))
            {
                const auto macro = control.id.getTrailingIntValue();
                if (auto* handle = handleFor(static_cast<int>(ModSource::macro1) + macro - 1))
                {
                    const auto labelRow = block.removeFromTop(ui::knobLabelHeight);
                    handle->setBounds(juce::Rectangle<int>(26, ui::knobLabelHeight)
                                          .withCentre(labelRow.getCentre()));
                    control.label.setVisible(false);
                    control.slider.setBounds(block);
                    continue;
                }
            }
            switch (control.style)
            {
                case ui::Style::chip:
                    control.chip->setBounds(block);
                    break;
                case ui::Style::bar:
                    control.slider.setBounds(block);
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
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    control.rocker->setBounds(ui::rockerBounds(block));
                    break;
                case ui::Style::knob:
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    control.slider.setBounds(block);
                    break;
            }
        }
    }
}

void Editor::buildHandles()
{
    const auto add = [this] (int source, const juce::String& caption, juce::Colour accent,
                             bool card = false)
    {
        auto handle = std::make_unique<ui::SourceHandle>(source, caption);
        handle->accent = accent;
        handle->card = card;
        handle->setTooltip("Drag " + juce::String(modSourceName(source)) + " onto a knob");
        handle->addMouseListener(this, false);
        addAndMakeVisible(*handle);
        handles.push_back(std::move(handle));
    };

    add(static_cast<int>(ModSource::env1), "ENV 1", ui::electricBlue, true);
    // One per LFO, though only the one on screen is ever placed: the handle is
    // the module's own title, and the module is showing one LFO at a time.
    for (int lfo = 0; lfo < lfoCount; ++lfo)
        add(static_cast<int>(ModSource::lfo1) + lfo, "LFO " + juce::String(lfo + 1), ui::signalViolet, true);
    for (int macro = 0; macro < macroCount; ++macro)
        add(static_cast<int>(ModSource::macro1) + macro, juce::String(macro + 1), ui::electricBlue);
}

Editor::Control* Editor::controlAt(juce::Point<int> panelPosition)
{
    for (auto& module : moduleUis)
        for (auto& control : module.controls)
            if (control->style == ui::Style::knob && control->slider.isVisible()
                && control->slider.getBounds().contains(panelPosition))
                return control.get();
    return nullptr;
}

// ENV 1's module is painted straight onto the editor rather than being a
// component of its own, so the things on it that can be clicked or scrolled are
// hit-tested here, against the same geometry the painter lays them out with.
const ui::Module* Editor::envelopeModule() const
{
    for (const auto& module : moduleUis)
    {
        const auto& descriptor = *module.descriptor;
        if (descriptor.display != ui::Display::envelope) continue;
        return ui::onPage(descriptor, page) ? &descriptor : nullptr;
    }
    return nullptr;
}

juce::Rectangle<int> Editor::envelopeDisplayBounds() const
{
    const auto* module = envelopeModule();
    return module == nullptr ? juce::Rectangle<int>()
                             : ui::displayBounds(ui::moduleBounds(getLocalBounds(), *module), *module);
}

void Editor::setEnvelopeZoom(int zoom)
{
    const auto clamped = juce::jlimit(0, ui::envelopeZoomCount - 1, zoom);
    if (clamped == envelopeZoom) return;
    envelopeZoom = clamped;
    repaint();
}

void Editor::mouseDown(const juce::MouseEvent& event)
{
    if (auto* handle = dynamic_cast<ui::SourceHandle*>(event.eventComponent))
    {
        draggingHandle = handle;
        handle->dragging = true;
        dragPosition = event.getEventRelativeTo(this).getPosition();
        repaint();
        return;
    }
    // Only for a click that landed on the panel itself: the editor listens to
    // every knob and handle as well, and those events carry their own component.
    if (event.eventComponent == this && !event.mods.isPopupMenu())
    {
        const auto at = event.getEventRelativeTo(this).getPosition();
        if (const auto display = envelopeDisplayBounds(); !display.isEmpty())
        {
            if (ui::envelopeZoomIn(display).contains(at)) { setEnvelopeZoom(envelopeZoom - 1); return; }
            if (ui::envelopeZoomOut(display).contains(at)) { setEnvelopeZoom(envelopeZoom + 1); return; }
            // Double-clicking the plot puts the window back to three seconds,
            // which is the gesture a knob already uses to go back to the value
            // it started at.
            if (event.getNumberOfClicks() >= 2 && ui::envelopePlotBounds(display).contains(at))
            {
                setEnvelopeZoom(ui::envelopeDefaultZoom);
                return;
            }
        }
    }
    if (!event.mods.isPopupMenu()) return;
    for (const auto& module : moduleUis)
        for (const auto& control : module.controls)
            if (event.eventComponent == &control->slider)
            {
                showModulationMenu(control->id);
                return;
            }
}

// The computer keys play Forge wherever the focus happens to be. A key event is
// walked up from whatever holds focus to its parents, so the letter keys reach
// here once a knob has taken focus under the hand, and are handed on to the
// keyboard from here. Without this, touching any control silenced the keys until
// the keyboard itself was clicked back into focus — which is the wrong trade for
// a synth, where turning something while playing it is the whole point.
//
// Forwarding is safe when the keyboard already has focus and has handled the
// event itself: it tracks which notes its keys are holding down, so a second
// pass over the same key state starts and stops nothing.
bool Editor::keyStateChanged(bool isKeyDown)
{
    return keyboard.keyStateChanged(isKeyDown);
}

bool Editor::keyPressed(const juce::KeyPress& key)
{
    // Undo belongs to the editor rather than to the panel because a plugin
    // window gives its keyboard focus to whichever child last took it, and the
    // panel is often not that child. Scoped to the tab that has something to
    // undo, so Ctrl+Z is not swallowed anywhere else.
    if (page == ui::Page::table && tablePanel != nullptr
        && key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        tablePanel->undo();
        applyTableCounts();
        repaint();
        return true;
    }
    // z and x walk the computer keys up and down the piano. Neither letter is
    // in the keyboard's own mapping, so playing loses nothing by lending them.
    if (key == juce::KeyPress('z')) { shiftComputerKeyOctave(-1); return true; }
    if (key == juce::KeyPress('x')) { shiftComputerKeyOctave(1); return true; }
    return keyboard.keyPressed(key);
}

void Editor::shiftComputerKeyOctave(int delta)
{
    // The reach has to land inside the keys that exist, so the lowest octave is
    // the first whose C is on the piano and the highest is the last whose whole
    // reach still fits.
    const auto lowest = (keyboard.getRangeStart() + 11) / 12;
    const auto highest = (keyboard.getRangeEnd() - (computerKeySpan - 1)) / 12;
    const auto shifted = juce::jlimit(lowest, highest, computerKeyOctave + delta);
    if (shifted == computerKeyOctave) return;

    // Notes are tracked by number, so a key held across the shift would be
    // asked to stop on a number nothing started — the note hangs. This is the
    // keyboard's own way of letting everything go, and it is what it does when
    // the focus leaves it mid-chord.
    keyboard.focusLost(juce::Component::focusChangedDirectly);
    computerKeyOctave = shifted;
    keyboard.setKeyPressBaseOctave(computerKeyOctave);
    repaint();
}

// A wash across the keys the letters can reach, with a brighter line under
// them. Faint enough to read as a shadow on the piano rather than as another
// lit thing competing with the notes actually being held.
void Editor::paintOverChildren(juce::Graphics& g)
{
    const auto first = computerKeyOctave * 12;
    const auto last = first + computerKeySpan - 1;
    if (first < keyboard.getRangeStart() || last > keyboard.getRangeEnd()) return;

    const auto reach = (keyboard.getRectangleForKey(first)
                            .getUnion(keyboard.getRectangleForKey(last)))
                           .translated(static_cast<float>(keyboard.getX()),
                                       static_cast<float>(keyboard.getY()));

    g.setColour(ui::electricBlue.withAlpha(0.07f));
    g.fillRect(reach);
    g.setColour(ui::electricBlue.withAlpha(0.3f));
    g.fillRect(reach.withTop(reach.getBottom() - 2.0f));
}

// The wheel over ENV 1's display changes how much time it spans. Up shortens
// the window, so the shape grows: up is in, the way round every other zoom
// works. The notches are accumulated because a trackpad sends a stream of small
// deltas where a mouse sends one large one, and without this a flick would run
// through the whole ladder before the hand came off it.
void Editor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto display = envelopeDisplayBounds();
    if (display.isEmpty()
        || !ui::envelopePlotBounds(display).contains(event.getEventRelativeTo(this).getPosition()))
    {
        Component::mouseWheelMove(event, wheel);
        return;
    }

    envelopeWheel += wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
    const auto notches = static_cast<int>(envelopeWheel / wheelPerZoomStep);
    if (notches == 0) return;
    envelopeWheel -= static_cast<float>(notches) * wheelPerZoomStep;
    setEnvelopeZoom(envelopeZoom - notches);
}

void Editor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingHandle == nullptr) return;
    dragPosition = event.getEventRelativeTo(this).getPosition();
    repaint();
}

void Editor::mouseUp(const juce::MouseEvent& event)
{
    if (draggingHandle == nullptr) return;
    const auto source = draggingHandle->source;
    draggingHandle->dragging = false;
    draggingHandle = nullptr;

    if (auto* control = controlAt(event.getEventRelativeTo(this).getPosition()))
    {
        const auto destination = destinationFor(control->id);
        if (destination != 0) assignModulation(source, destination);
        else
        {
            presetName.setText("THAT CONTROL CANNOT BE MODULATED", juce::dontSendNotification);
            presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        }
    }
    repaint();
}

// Each modulated knob is told three things: how far its slots can move it, how
// far they are moving it right now, and whether anything is sounding at all. Its
// look draws them without knowing anything about the matrix.
void Editor::refreshModulationRings()
{
    // A source only reaches a destination through a voice, so with nothing
    // sounding there is no modulated value to show. The knobs go quiet at the
    // same moment ENV 1's playhead does, and for the same reason.
    const auto live = processor.envelopeStage() != 0;
    std::array<float, destinationCount> depths {};
    // A ring can only be dragged when one slot is behind it. Pointed at by two,
    // it is a sum, and there is nothing a single drag could honestly mean.
    std::array<int, destinationCount> slots {};
    std::array<int, destinationCount> only {};
    only.fill(-1);
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        const auto destination = juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8()));
        if (source <= 0 || destination <= 0 || destination >= destinationCount) continue;
        depths[static_cast<size_t>(destination)] += value(slotParameter(slot, "Depth").toRawUTF8());
        // Counted whatever its depth, so a routing sitting at zero can still be
        // dialled up by its ring rather than only in the matrix.
        ++slots[static_cast<size_t>(destination)];
        only[static_cast<size_t>(destination)] = slot;
    }

    for (auto& module : moduleUis)
        for (auto& control : module.controls)
        {
            if (control->style != ui::Style::knob) continue;
            const auto destination = destinationFor(control->id);
            const auto depth = destination == 0 ? 0.0f : depths[static_cast<size_t>(destination)];
            const auto offset = destination == 0 ? 0.0f : processor.modulationOffset(destination);

            control->ringSlot = destination != 0 && slots[static_cast<size_t>(destination)] == 1
                ? only[static_cast<size_t>(destination)] : -1;
            control->slider.ringDraggable = control->ringSlot >= 0;
            control->slider.ringDepth = depth;
            auto& properties = control->slider.getProperties();
            if (static_cast<float>(properties.getWithDefault("modDepth", 0.0)) == depth
                && static_cast<float>(properties.getWithDefault("modOffset", 0.0)) == offset
                && static_cast<bool>(properties.getWithDefault("modLive", false)) == live)
                continue;
            properties.set("modDepth", depth);
            properties.set("modOffset", offset);
            properties.set("modLive", live);
            control->slider.repaint();
        }
}

void Editor::showModulationMenu(const juce::String& parameterId)
{
    const auto destination = destinationFor(parameterId);
    if (destination == 0) return;
    const auto label = juce::String(destinations()[static_cast<size_t>(destination)].label);

    juce::PopupMenu menu;
    menu.addSectionHeader("Modulate " + label);

    // Anything already pointed here can be taken away from the same menu, so a
    // routing can be undone where it was made rather than only in the matrix.
    juce::PopupMenu existing;
    auto found = 0;
    for (int slot = 0; slot < modSlotCount; ++slot)
    {
        const auto source = juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8()));
        if (source <= 0 || juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) != destination)
            continue;
        existing.addItem(1000 + slot, juce::String(modSourceName(source)));
        ++found;
    }

    juce::PopupMenu sources;
    for (int source = 1; source < modSourceCount; ++source)
        sources.addItem(source, modSourceName(source));
    menu.addSubMenu("Add source", sources);
    if (found > 0) menu.addSubMenu("Remove", existing);

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [safe, destination] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice >= 1000) safe->clearSlot(choice - 1000);
        else safe->assignModulation(choice, destination);
    });
}

void Editor::assignModulation(int source, int destination)
{
    // Reuse a slot already joining this pair rather than spending a second one
    // on the same routing.
    auto target = -1;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) == source
            && juce::roundToInt(value(slotParameter(slot, "Dest").toRawUTF8())) == destination)
            target = slot;
    for (int slot = 0; slot < modSlotCount && target < 0; ++slot)
        if (juce::roundToInt(value(slotParameter(slot, "Source").toRawUTF8())) <= 0) target = slot;

    if (target < 0)
    {
        presetName.setText("ALL " + juce::String(modSlotCount) + " MOD SLOTS ARE IN USE",
                           juce::dontSendNotification);
        presetName.setColour(juce::Label::textColourId, ui::signalViolet);
        return;
    }

    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(target, "Source"), static_cast<float>(source));
    set(slotParameter(target, "Dest"), static_cast<float>(destination));
    // A depth of zero would look like nothing happened. Half is audible, and
    // the ring it lands on is now the way to take it anywhere else.
    if (value(slotParameter(target, "Depth").toRawUTF8()) == 0.0f)
        setSlotDepth(target, 0.5f);
}

// Written through the parameter rather than into the slot, so a depth dragged
// on a ring reaches the host's automation lane and the matrix field by the same
// path a depth typed into the matrix does.
void Editor::setSlotDepth(int slot, float depth)
{
    if (slot < 0 || slot >= modSlotCount) return;
    if (auto* parameter = processor.state.getParameter(slotParameter(slot, "Depth")))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(-1.0f, 1.0f, depth)));
}

void Editor::clearSlot(int slot)
{
    const auto set = [this] (const juce::String& id, float plain)
    {
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
    };
    set(slotParameter(slot, "Source"), 0.0f);
    set(slotParameter(slot, "Dest"), 0.0f);
    set(slotParameter(slot, "Depth"), 0.0f);
}

void Editor::showValueBubble(Control& control)
{
    bubbleControl = &control;
    // A macro's label is its bare number, and it is hidden anyway — the drag
    // handle stands in its place — so the bubble spells the name out. Same
    // test as the one that puts the handle there in the first place.
    valueBubble.caption = control.id.startsWith("macro")
                              ? "MACRO " + juce::String(control.id.getTrailingIntValue())
                              : control.label.getText();
    valueBubble.reading = control.slider.getTextFromValue(control.slider.getValue());
    valueBubble.accent = control.slider.findColour(juce::Slider::rotarySliderFillColourId);

    const auto knob = control.slider.getBounds();
    const auto size = juce::Rectangle<int>(valueBubble.widthFor(), ui::ValueBubble::heightFor());
    // Above the knob by preference, below it when the knob is near the top of
    // the panel, and never off either side.
    auto placed = size.withCentre({knob.getCentreX(), knob.getY() - size.getHeight() / 2 - 6});
    if (placed.getY() < 4) placed.setY(knob.getBottom() + 6);
    placed.setX(juce::jlimit(4, juce::jmax(4, getWidth() - size.getWidth() - 4), placed.getX()));
    valueBubble.setBounds(placed);

    valueBubble.setVisible(true);
    valueBubble.toFront(false);
    valueBubble.repaint();
    bubbleUntil = juce::Time::getMillisecondCounter() + bubbleTailMs;
}

void Editor::fadeValueBubble()
{
    if (!valueBubble.isVisible() || bubbleHeld) return;
    if (juce::Time::getMillisecondCounter() < bubbleUntil) return;
    valueBubble.setVisible(false);
    bubbleControl = nullptr;
}

void Editor::timerCallback()
{
    fadeValueBubble();
    // Cheap to re-apply every tick, and it catches a dependency changing from
    // host automation as well as from the panel. Both of these only repaint
    // when something has actually changed.
    applyEnableStates();
    refreshModulationRings();

    // A table replaced from outside the panel — a preset loaded, a project
    // opened, a second editor on the same plugin — bumps its revision, and this
    // is where the panel notices and redraws.
    auto moved = false;
    for (int oscillator = 0; oscillator < oscillatorCount; ++oscillator)
    {
        const auto revision = processor.tableStore().revision(oscillator);
        if (revision == tableRevisions[static_cast<size_t>(oscillator)]) continue;
        tableRevisions[static_cast<size_t>(oscillator)] = revision;
        moved = true;
    }
    if (moved)
    {
        if (tablePanel != nullptr) tablePanel->refresh();
        applyTableCounts();
    }
    // Frees whatever the audio thread has demonstrably moved past. Publishing
    // does this too; here it catches the table retired by the last edit of a
    // session, which would otherwise sit there until the next one.
    processor.tableStore().collect();
    repaint();
}

void Editor::choosePresetToLoad()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load Rhino Forge preset", juce::File {}, "*.forgepreset", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File {}) return;
            safe->showPresetResult(safe->processor.loadPreset(file), file);
        });
}

void Editor::choosePresetToSave()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save Rhino Forge preset", juce::File {}, "*.forgepreset", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File {}) return;
            file = file.withFileExtension("forgepreset");
            safe->showPresetResult(safe->processor.savePreset(file, file.getFileNameWithoutExtension()), file);
        });
}

void Editor::showPresetResult(const juce::Result& result, const juce::File& file)
{
    presetName.setText(result.wasOk() ? file.getFileNameWithoutExtension().toUpperCase()
                                      : "ERROR // " + result.getErrorMessage(), juce::dontSendNotification);
    presetName.setColour(juce::Label::textColourId, result.wasOk() ? ui::mutedText : ui::signalViolet);
    // Loading a preset can switch modules on or off behind the enable LEDs.
    applyEnableStates();
    repaint();
}
}
