#include "ForgeEditor.h"

namespace theta::forge
{
namespace
{
// Tooltips are keyed by parameter id so a module can be reordered, renamed or
// moved to another row without disturbing them.
juce::String tooltipFor(const juce::String& id)
{
    // Both oscillators expose the same controls, so their tooltips are keyed by
    // the suffix and the oscillator's letter is filled in below.
    static const std::map<juce::String, juce::String> perOscillator {
        {"Position", "Scan oscillator %'s harmonic shape"},
        {"Octave", "Transpose oscillator % in octaves"},
        {"Semitone", "Transpose oscillator % in semitones"},
        {"Fine", "Detune oscillator % in cents"},
        {"Unison", "Stack detuned copies of oscillator %"},
        {"Detune", "Spread oscillator %'s stack in pitch and across the stereo field"},
        {"Blend", "Balance the centre of oscillator %'s stack against its edges"},
        {"Pan", "Place oscillator % in the stereo field"},
        {"Level", "Set oscillator %'s level"},
    };
    for (const auto& letter : {"A", "B"})
        if (id.startsWith("osc" + juce::String(letter)))
        {
            const auto found = perOscillator.find(id.fromFirstOccurrenceOf("osc" + juce::String(letter), false, false));
            if (found != perOscillator.end()) return found->second.replace("%", letter);
        }

    static const std::map<juce::String, juce::String> tips {
        {"subLevel", "Blend in a sine one octave below the note"},
        {"noiseLevel", "Blend in broadband noise"},
        {"cutoff", "Open or close the filter"},
        {"resonance", "Emphasise the filter edge"},
        {"drive", "Saturate what is routed into the filter"},
        {"filterType", "Low pass, high pass or band pass"},
        {"routeA", "Send oscillator A through the filter"},
        {"routeB", "Send oscillator B through the filter"},
        {"routeSub", "Send the sub through the filter"},
        {"routeNoise", "Send the noise through the filter"},
        {"attack", "How the note begins"},
        {"decay", "The fall from the attack peak"},
        {"sustain", "The level a held note settles at"},
        {"release", "How the note fades once released"},
        {"lfoRate", "Free-running LFO speed. Point it somewhere in the matrix"},
        {"polyphony", "Limit simultaneous notes"},
        {"mono", "Collapse to one voice for basses and leads"},
        {"legato", "Keep the envelope running across overlapping mono notes"},
        {"glide", "Slide between monophonic notes"},
        {"output", "Forge's final level"},
    };
    if (id.startsWith("macro"))
        return "A performance macro. Drag its number onto a knob, or right-click the knob";

    // Matrix slots: eight of each, all reading the same way.
    if (id.startsWith("mod"))
    {
        if (id.endsWith("Source")) return "What drives this slot";
        if (id.endsWith("Dest")) return "Which control this slot moves";
        if (id.endsWith("Depth")) return "How far, and in which direction, the source moves the target";
    }
    const auto found = tips.find(id);
    return found == tips.end() ? juce::String() : found->second;
}
}

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
}

Editor::Editor(Processor& p)
    : AudioProcessorEditor(&p), processor(p),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    buildModules();
    buildHandles();
    buildTabs();

    keyboard.setAvailableRange(21, 108);
    keyboard.setLowestVisibleKey(21);
    keyboard.setScrollButtonsVisible(false);
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

    // So the panel itself can hold focus when nothing in it does, and the keys
    // still have somewhere to arrive from.
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    // The matrix moving into the tabbed row took a whole grid row off the
    // bottom of the panel, so the window is shorter than it was at every limit.
    setResizeLimits(1140, 980, 1900, 1500);
    setSize(1260, 1060);
    applyEnableStates();
    applyPage();
    startTimerHz(24);
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
void Editor::applyPage()
{
    for (auto& module : moduleUis)
    {
        const auto shown = ui::onPage(*module.descriptor, page);
        if (module.enable != nullptr) module.enable->setVisible(shown);
        for (auto& control : module.controls)
        {
            control->label.setVisible(shown);
            control->slider.setVisible(shown);
            if (control->chip != nullptr) control->chip->setVisible(shown);
            if (control->rocker != nullptr) control->rocker->setVisible(shown);
        }
    }
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
            module.enable->setTooltip("Switch " + juce::String(descriptor.title) + " on or off");
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

                if (declared.style == ui::Style::chip)
                {
                    control->chip = std::make_unique<ui::ToggleChip>(declared.label);
                    control->chip->accent = accent;
                    control->chip->setTooltip(tooltipFor(declared.id));
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
                    control->rocker->setTooltip(tooltipFor(declared.id));
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
                    control->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, ui::readoutHeight);
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
                control->slider.setTooltip(tooltipFor(declared.id));
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
}

void Editor::applyEnableStates()
{
    for (auto& module : moduleUis)
    {
        for (auto& control : module.controls)
        {
            // A control is live when its module is on and nothing else has
            // taken it over — polyphony means nothing once mono is switched on,
            // and a tempo division means nothing until the LFO is synced.
            const auto on = module.on()
                && (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f)
                && (control->enabledBy == nullptr || value(control->enabledBy) >= 0.5f);

            if (control->chip != nullptr) { control->chip->setEnabled(on); continue; }
            if (control->rocker != nullptr)
            {
                control->rocker->setEnabled(on);
            }
            else
            {
                control->slider.setEnabled(on);
                control->slider.setColour(juce::Slider::textBoxTextColourId,
                                          ui::text.withAlpha(on ? 1.0f : 0.4f));
            }
            control->label.setColour(juce::Label::textColourId,
                                     ui::mutedText.withAlpha(on ? 1.0f : 0.4f));
        }
    }
}

float Editor::value(const char* id) const
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
        ui::drawModuleShell(g, area, descriptor, on,
                            descriptor.display == ui::Display::envelope ? ui::stageName(stage)
                            : descriptor.display == ui::Display::lfo
                                ? juce::String(processor.lfoRateHz(), 2) + " HZ"
                                : juce::String());

        if (descriptor.columnHeaderHeight > 0) paintTable(g, area, descriptor);

        const auto display = ui::displayBounds(area, descriptor);
        if (display.isEmpty()) continue;
        const auto accent = ui::accentFor(descriptor);
        // An oscillator shows its wave on a picture tube; the other displays
        // stay flat wells, which is what keeps the tubes reading as screens.
        if (descriptor.display == ui::Display::oscillator)
            ui::drawCrtScreen(g, display, accent, alpha);
        else
            ui::drawDisplayWell(g, display);
        switch (descriptor.display)
        {
            case ui::Display::oscillator:
                if (const auto* source = ui::displaySourceId(descriptor))
                    ui::drawWaveform(g, display, value(source), accent, alpha);
                break;
            case ui::Display::envelope:
                ui::drawEnvelope(g, display, value("attack"), value("decay"),
                                 value("sustain"), value("release"), accent, alpha,
                                 stage, processor.envelopeLevel());
                break;
            case ui::Display::lfo:
                ui::drawLfo(g, display,
                            static_cast<LfoShape>(juce::jlimit(0, lfoShapeCount - 1,
                                                               juce::roundToInt(value("lfoShape")))),
                            processor.lfoPhase(), processor.lfoValue(), accent, alpha);
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
        // would be, ahead of the title.
        if (descriptor.handleSource != 0)
            if (auto* handle = handleFor(descriptor.handleSource))
                handle->setBounds(area.getX() + (descriptor.enableId != nullptr ? ui::headerHeight + 8 : 10),
                                  area.getY() + 4, ui::handleWidth, ui::headerHeight - 8);

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
                    // Same label line as the knobs either side, and the same
                    // gap beneath it, so the switch sits exactly where their
                    // circles do. The readout line is left empty rather than
                    // reclaimed, which is what keeps the row aligned.
                    control.label.setBounds(block.removeFromTop(ui::knobLabelHeight));
                    block.removeFromBottom(ui::readoutHeight);
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
    const auto add = [this] (int source, const juce::String& caption, juce::Colour accent)
    {
        auto handle = std::make_unique<ui::SourceHandle>(source, caption);
        handle->accent = accent;
        handle->setTooltip("Drag " + juce::String(modSourceName(source)) + " onto a knob");
        handle->addMouseListener(this, false);
        addAndMakeVisible(*handle);
        handles.push_back(std::move(handle));
    };

    add(static_cast<int>(ModSource::env1), "ENV 1", ui::electricBlue);
    add(static_cast<int>(ModSource::lfo1), "LFO 1", ui::signalViolet);
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
    return keyboard.keyPressed(key);
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

void Editor::timerCallback()
{
    // Cheap to re-apply every tick, and it catches a dependency changing from
    // host automation as well as from the panel. Both of these only repaint
    // when something has actually changed.
    applyEnableStates();
    refreshModulationRings();
    repaint();
}

void Editor::choosePresetToLoad()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load Theta Forge preset", juce::File {}, "*.forgepreset", true);
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
    fileChooser = std::make_unique<juce::FileChooser>("Save Theta Forge preset", juce::File {}, "*.forgepreset", true);
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
