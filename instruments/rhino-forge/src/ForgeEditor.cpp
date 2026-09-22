// The editor itself: what it owns, what it builds out of the declared
// modules, and the preset buttons in its title bar. Everything the built
// components then do lives in one of the ForgeEditor*.cpp files beside this
// one, which define the same class — see ForgeEditorInternal.h.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
namespace
{
// The name over a control, wherever it sits. One size and one weight for all of
// them: the reference draws the word over a stepper exactly as large as the one
// over the knob beside it, and the two sizes this had drifted to only made a
// row of mixed controls look unaligned.
//
// Medium, not semibold. Semibold was read off a close-up of the reference by
// eye and it was the wrong call: measuring ink coverage against cap height
// instead puts the reference's lettering at 1.71 and semibold at 2.14, a
// quarter heavier. Rajdhani Medium is 23% lighter than semibold at every size
// and barely a pixel narrower over a word, which lands at 1.64 — near enough
// the reference, and without giving back any of the size.
//
// A label's text is rasterised once and blitted after. The panel repaints whole
// at 24Hz, so without the buffer every label on it lays its glyphs out again
// every frame for a string that has not changed — and setText still repaints,
// so a label that does change is still right.
void dressControlLabel(juce::Label& label, const juce::String& caption)
{
    label.setText(caption, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    // A label names the control under it and is never clicked itself. Saying so
    // matters now that a knob's label is seated over the empty margin its look
    // leaves above the circle: without this, the top few pixels of a knob would
    // swallow the drag that started on them.
    label.setInterceptsMouseClicks(false, false);
    label.setColour(juce::Label::textColourId, ui::labelText);
    label.setFont(ui::panelFont(ui::Face::label, ui::controlLabelSize));
    label.setBufferedToImage(true);
}
}

Editor::Editor(Processor& p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // The viewport itself is transparent to the rack background and list; its
    // children remain interactive and JUCE clips them to these bounds.
    fxControlViewport.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(fxControlViewport);
    buildModules();
    buildHandles();
    buildTabs();
    buildTablePanel();

    keyboard.setAvailableRange(ui::keyboardLowestNote, ui::keyboardHighestNote);
    keyboard.setLowestVisibleKey(ui::keyboardLowestNote);
    // Middle C is C3, which is what Ableton, Serum, FL and Logic all call MIDI
    // 60. Rhino's own grid says the same — see StepGridPainter, which was moved
    // with this — so the panel, the grid driving it and the DAW next to it all
    // name one note the same way.
    //
    // It used to be C4 here and on the grid. Both were self-consistent and both
    // disagreed with everything outside Rhino, so a player checking Forge
    // against Serum pressed two keys marked C3, got notes an octave apart, and
    // reasonably concluded the synth was out of tune. It was not; only the
    // label was. This is JUCE's own default, so the call stays to say so.
    keyboard.setOctaveForMiddleC(3);
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
    pitchWheel.onChange = [this] (float value)
    {
        processor.setPitchWheel(juce::roundToInt(value * 16383.0f));
    };
    modulationWheel.onChange = [this] (float value)
    {
        processor.setModWheel(juce::roundToInt(value * 127.0f));
    };
    pitchWheel.setDisplayValue(processor.pitchWheelValue() / 16383.0f);
    modulationWheel.setDisplayValue(processor.modWheelValue() / 127.0f);
    addAndMakeVisible(pitchWheel);
    addAndMakeVisible(modulationWheel);

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
    presetName.setFont(ui::panelFont(ui::Face::label, 10.0f));
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
    refreshWarpFields();
    applyEnableStates();
    applyTableCounts();
    // Before the first layout pass, so a project that opens with named macros
    // shows them rather than filling them in a tick later. The rings and the
    // macro counts are read for the same reason: both used to wait for the
    // first timer tick, which nobody sees live and which a headless snapshot
    // never gets at all.
    applyMacroNames();
    refreshModulationRings();
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
        // Violet for the two tabs that stand something else in the
        // oscillators' place, blue for the signal path itself. The mixer takes
        // the whole row rather than standing in for a pair of modules, so it
        // keeps the signal path's colour.
        tab->accent = target == ui::Page::matrix || target == ui::Page::table
                       || target == ui::Page::fx
                          ? ui::signalViolet : ui::electricBlue;
        tab->setTooltip([target]
        {
            switch (target)
            {
                case ui::Page::matrix: return "Show the modulation matrix in place of the oscillators";
                case ui::Page::table:  return "Draw on the oscillators' wavetables";
                case ui::Page::mix:    return "Balance and route every source, the filter and the two busses";
                case ui::Page::fx:     return "The effects racks: one on the main output and one on each bus";
                case ui::Page::oscillators: break;
            }
            return "Show the oscillators";
        }());
        tab->setToggleState(target == page, juce::dontSendNotification);
        tab->onClick = [this, target] { showPage(target); };
        addAndMakeVisible(*tab);
        tabs.push_back(std::move(tab));
    }
}

void Editor::buildModules()
{
    for (const auto& descriptor : ui::modules())
    {
        ModuleUi module;
        module.descriptor = &descriptor;
        const auto accent = accentOf(descriptor);
        const auto rackModule = isFxModule(descriptor);
        const auto addControl = [this, rackModule] (juce::Component& component)
        {
            if (rackModule) fxControlViewport.addAndMakeVisible(component);
            else addAndMakeVisible(component);
        };

        if (descriptor.enableId != nullptr)
        {
            module.enable = std::make_unique<ui::EnableLed>();
            module.enable->accent = accent;
            module.enable->setTooltip(ui::tooltipFor(descriptor.enableId));
            if (descriptor.display == ui::Display::oscillator)
            {
                const auto* held = &descriptor;
                module.enable->onColourMenu = [this, held] { showPanelColourMenu(*held); };
            }
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

                if (declared.style == ui::Style::selector)
                {
                    control->selector = std::make_unique<ui::FxSelector>();
                    auto* held = control.get();
                    // Two fields wear the same component. A rack slot's mode
                    // means whatever the type in that slot says it means, so it
                    // is stored as a plain 0..1 and spread across the choices;
                    // an oscillator's warp mode is a fixed list, so it is a
                    // genuine choice parameter and the index is the value.
                    if (isWarpControl(declared.id))
                    {
                        control->selector->accent = accent;
                        control->selector->onChoose = [this, held] (int choice)
                        {
                            setWarpMode(held->id, choice);
                        };
                        control->selector->onOpenList = [this, held] { showWarpMenu(*held); };
                    }
                    else
                    {
                        control->selector->onChoose = [this, held] (int choice)
                        {
                            setFxMode(held->id, held->selector->count(), choice);
                        };
                        control->selector->onOpenList = [this, held] { showFxModeMenu(*held); };
                    }
                    // As with the plate: the slider is here for its attachment,
                    // never shown and never added as a child. The attachment is
                    // what a host reads and writes the mode through.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    if (isWarpControl(declared.id))
                        control->slider.onValueChange = [this] { refreshWarpFields(); repaint(); };
                    else
                        control->slider.onValueChange = [this] { refreshFxSlots(); repaintFxDisplays(); };
                    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, declared.id, control->slider);

                    dressControlLabel(control->label, declared.label);
                    addControl(control->label);
                    addControl(*control->selector);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::wave)
                {
                    control->waves = std::make_unique<ui::WaveGrid>();
                    control->waves->accent = accent;
                    control->waves->choices = subShapeCount;
                    control->waves->shapeAt = [] (int shape, float phase) { return subShape(shape, phase); };
                    control->waves->setTooltip(ui::tooltipFor(declared.id));
                    auto* held = control.get();
                    control->waves->onChoose = [this, held] (int choice)
                    {
                        auto* parameter = processor.state.getParameter(held->id);
                        if (parameter == nullptr) return;
                        parameter->setValueNotifyingHost(parameter->convertTo0to1(
                            static_cast<float>(juce::jlimit(0, subShapeCount - 1, choice))));
                    };
                    // As with the plate: the slider is here for its attachment,
                    // which is what a host reads and writes the shape through.
                    // The grid follows it rather than the other way round, so a
                    // preset load or an automation lane lights the right cell.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    control->slider.onValueChange = [held]
                    {
                        held->waves->chosen = juce::jlimit(0, subShapeCount - 1,
                                                           juce::roundToInt(held->slider.getValue()));
                        held->waves->repaint();
                    };
                    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, declared.id, control->slider);
                    // The attachment only fires the callback above when it moves
                    // the slider, and a patch already on the first shape does
                    // not move it, so the grid is told once outright.
                    control->waves->chosen = juce::jlimit(0, subShapeCount - 1,
                                                          juce::roundToInt(control->slider.getValue()));
                    addControl(*control->waves);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::plate)
                {
                    control->plate = std::make_unique<ui::FxPlate>();
                    control->plate->setTooltip(ui::tooltipFor(declared.id));
                    auto* held = control.get();
                    control->plate->onPlateClick = [this, held]
                    {
                        int rack = 0, slot = 0;
                        if (fxControlAt(held->id, rack, slot)) fxSelectedSlot = slot;
                        showFxTypeMenu(*held);
                        repaintFxDisplays();
                    };
                    // The slider is never shown and never added as a child: it
                    // is here for its attachment, which is what a host reads
                    // and writes the type through. The plate draws the value
                    // and the menu sets it.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
                    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    control->slider.onValueChange = [this] { refreshFxSlots(); resized(); repaint(); };
                    control->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, declared.id, control->slider);
                    addControl(*control->plate);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                if (declared.style == ui::Style::chip)
                {
                    control->chip = std::make_unique<ui::ToggleChip>(declared.label);
                    control->chip->accent = accent;
                    control->chip->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->chip);
                    addControl(*control->chip);
                    module.controls.push_back(std::move(control));
                    continue;
                }

                dressControlLabel(control->label, declared.label);

                if (declared.style == ui::Style::rocker)
                {
                    // No ON/OFF readout: the switch shows its own state, by
                    // moving as well as lighting up.
                    control->rocker = std::make_unique<ui::RockerSwitch>(declared.label);
                    control->rocker->accent = accent;
                    control->rocker->setTooltip(ui::tooltipFor(declared.id));
                    control->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, declared.id, *control->rocker);

                    addControl(*control->rocker);
                    addControl(control->label);
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
                    // Every one of these routes to drawLinearSlider. A stepper
                    // is a bar painted as a numeric field; a matrix amount is a
                    // horizontal bar, and drags along the fill it draws; a
                    // fader is a genuine vertical slider with a thumb that
                    // travels, and is the only one of the three that prints no
                    // value of its own.
                    control->slider.setSliderStyle(
                        declared.style == ui::Style::bar     ? juce::Slider::LinearBar
                        : declared.style == ui::Style::fader ? juce::Slider::LinearVertical
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
                if (declared.style == ui::Style::knob || declared.style == ui::Style::fader)
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
                    const auto rackControl = control->id.startsWith("fx");
                    control->slider.onValueChange = [this, held, rackControl]
                    {
                        if (bubbleHeld || held->slider.isMouseOverOrDragging()) showValueBubble(*held);
                        else if (bubbleControl == held) showValueBubble(*held);
                        // A knob repaints itself, not the panel around it, so a
                        // slot's display would sit still while its own knob was
                        // being turned.
                        if (rackControl) repaintFxDisplays();
                    };
                }
                // A mode field changes what the knobs beside it read — a
                // delay's time is milliseconds or a division depending on it —
                // so the readouts are pushed when it moves.
                if (control->id.startsWith("fx")
                    && (control->id.endsWith("ModeA") || control->id.endsWith("ModeB")))
                    control->slider.onValueChange = [this] { refreshFxSlots(); repaintFxDisplays(); };
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
                if (descriptor.columnHeaderHeight == 0) addControl(control->label);
                addControl(control->slider);

                // A macro's number moved to the plate beside its knob, which
                // leaves the strip under the knob free for the macro's own
                // name. That name is not declared anywhere: it is given here,
                // and kept on the state tree so a preset carries it.
                if (juce::String(declared.id).startsWith("macro"))
                {
                    const auto macro = juce::String(declared.id).getTrailingIntValue() - 1;
                    control->macroName = std::make_unique<ui::MacroName>();
                    control->macroName->setTooltip(
                        "Double-click to name this macro. The matrix goes on calling it MACRO "
                        + juce::String(macro + 1));
                    control->macroName->onTextChange = [this, macro, held = control->macroName.get()]
                    {
                        processor.setMacroName(macro, held->getText());
                        // Read back rather than kept: the processor trims,
                        // upper-cases and cuts what it was given, and the strip
                        // should show what was actually stored rather than what
                        // was typed at it.
                        held->setText(processor.macroName(macro), juce::dontSendNotification);
                        applyMacroNames();
                    };
                    addControl(*control->macroName);
                }
                module.controls.push_back(std::move(control));
            }
        }

        moduleUis.push_back(std::move(module));
    }
    buildBankButtons();
}

// A module declared in banks carries one numbered button per bank in its
// header. Which bank is showing is the panel's business and not the host's: it
// says which envelope or which LFO you are looking at, not what the synth is
// doing, so it is no more a parameter than which tab is open.
void Editor::buildBankButtons()
{
    for (auto& module : moduleUis)
    {
        const auto banks = ui::bankCount(*module.descriptor);
        if (banks <= 1) continue;
        const auto accent = accentOf(*module.descriptor);
        // A bank is usually one of several numbered copies of one thing — ENV 3,
        // LFO 5 — and the card says the number. The rack's three banks are not
        // copies: they are the main output and the two busses, and a card
        // reading "2" would not say which. A module that asks for wider cards
        // is one whose banks have names.
        const auto named = module.descriptor->bankWidth > 0;
        for (int bank = 0; bank < banks; ++bank)
        {
            const auto caption = named ? juce::String(rackName(bank)) : juce::String(bank + 1);
            auto button = std::make_unique<ui::BankCard>(caption);
            button->accent = accent;
            button->setClickingTogglesState(false);
            button->setToggleState(bank == module.bank, juce::dontSendNotification);
            button->setTooltip(named ? "Show the effects rack on " + caption
                                     : "Show " + juce::String(module.descriptor->title) + " " + caption);
            button->onClick = [this, which = &module, bank] { showBank(*which, bank); };
            addAndMakeVisible(*button);
            module.bankButtons.push_back(std::move(button));
        }
    }
}

// Read from the parameter itself rather than from the cached atomic beside it.
//
// That atomic is kept up to date by one of the parameter's listeners, and the
// panel's own attachments are others. The order listeners are called in is not
// defined — so a panel answering a change it made itself could be called first
// and read the value from *before* that change. It corrected itself the next
// time anything refreshed, which is why a mode field appeared to wait for an
// unrelated click or a tab switch before it caught up.
//
// The parameter's own value has no such race: setValueNotifyingHost stores it
// before it tells anybody. This costs a lookup by name per read, which at the
// rate the panel refreshes is nothing, and the atomic stays what the audio
// thread reads.
float Editor::value(const juce::String& id) const
{
    if (const auto* parameter = processor.state.getParameter(id))
        return parameter->convertFrom0to1(parameter->getValue());
    const auto* raw = processor.state.getRawParameterValue(id);
    return raw == nullptr ? 0.0f : raw->load();
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

    // One per envelope and one per LFO, though only the one on screen is ever
    // placed: the handle is the module's own title, and each module is showing
    // one of them at a time.
    for (int env = 0; env < envCount; ++env)
        add(static_cast<int>(ModSource::env1) + env, "ENV " + juce::String(env + 1),
            ui::electricBlue, true);
    for (int lfo = 0; lfo < lfoCount; ++lfo)
        add(static_cast<int>(ModSource::lfo1) + lfo, "LFO " + juce::String(lfo + 1), ui::signalViolet, true);
    // A macro carries a plate instead of a tag: the number on top is the grip,
    // and under it is how many slots this macro is driving. It is still a
    // SourceHandle, because the drag that starts on it is the same drag, and
    // the editor finds what is being dragged by that type.
    for (int macro = 0; macro < macroCount; ++macro)
    {
        auto plate = std::make_unique<ui::MacroPlate>(static_cast<int>(ModSource::macro1) + macro,
                                                      juce::String(macro + 1));
        plate->accent = ui::electricBlue;
        plate->setTooltip(macroTooltip(macro));
        plate->addMouseListener(this, false);
        addAndMakeVisible(*plate);
        handles.push_back(std::move(plate));
    }
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
