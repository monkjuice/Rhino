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
        return "A performance macro. Right-click a knob to point this at it";

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

Editor::Editor(Processor& p) : AudioProcessorEditor(&p), processor(p)
{
    buildModules();

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

    setResizable(true, true);
    setResizeLimits(1140, 930, 1900, 1400);
    setSize(1260, 1010);
    applyEnableStates();
    startTimerHz(24);
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
                    // A bar style drags vertically and routes to
                    // drawLinearSlider, where it is painted as a numeric field.
                    control->slider.setSliderStyle(juce::Slider::LinearBarVertical);
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
                addAndMakeVisible(control->label);
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
            // taken it over — polyphony means nothing once mono is switched on.
            const auto on = module.on()
                && (control->disabledBy == nullptr || value(control->disabledBy) < 0.5f);

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
        const auto area = ui::moduleBounds(getLocalBounds(), descriptor);
        const auto on = module.on();
        const auto alpha = on ? 1.0f : 0.35f;
        const auto stage = static_cast<ui::Stage>(juce::jlimit(0, 4, processor.envelopeStage()));
        ui::drawModuleShell(g, area, descriptor, on,
                            descriptor.display == ui::Display::envelope ? ui::stageName(stage) : juce::String());

        const auto display = ui::displayBounds(area, descriptor);
        if (display.isEmpty()) continue;
        ui::drawDisplayWell(g, display);
        const auto accent = ui::accentFor(descriptor);
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
                // Two cycles at the slowest rate through eight at the fastest,
                // so the drawing reads as "faster" without ever becoming a blur.
                ui::drawLfo(g, display, juce::jmap(std::sqrt(juce::jlimit(0.0f, 1.0f, value("lfoRate") / 20.0f)),
                                                   1.5f, 8.0f), accent, alpha);
                break;
            case ui::Display::none:
                break;
        }
    }
}

void Editor::resized()
{
    const auto right = getWidth() - 30;
    savePreset.setBounds(right - 62, 26, 62, 26);
    loadPreset.setBounds(right - 130, 26, 62, 26);
    presetName.setBounds(right - 350, 26, 212, 26);

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

        for (auto& held : module.controls)
        {
            auto& control = *held;
            auto block = ui::controlBlock(area, descriptor, control.row, control.index, diameter);
            switch (control.style)
            {
                case ui::Style::chip:
                    control.chip->setBounds(block);
                    break;
                case ui::Style::stepper:
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

void Editor::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu()) return;
    for (const auto& module : moduleUis)
        for (const auto& control : module.controls)
            if (event.eventComponent == &control->slider)
            {
                showModulationMenu(control->id);
                return;
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
    // A depth of zero would look like nothing happened. Half is audible and
    // easy to walk back; the ring that makes this draggable arrives next.
    if (value(slotParameter(target, "Depth").toRawUTF8()) == 0.0f)
        set(slotParameter(target, "Depth"), 0.5f);
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
    // host automation as well as from the panel. setEnabled only repaints when
    // the value actually changes.
    applyEnableStates();
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
