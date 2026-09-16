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
        {"lfoRate", "Free-running LFO speed"},
        {"lfoCutoff", "How far the LFO moves the cutoff"},
        {"lfoPosition", "How far the LFO scans both oscillator shapes"},
        {"lfoPitch", "How far the LFO bends pitch"},
        {"polyphony", "Limit simultaneous notes"},
        {"mono", "Collapse to one voice for basses and leads"},
        {"legato", "Keep the envelope running across overlapping mono notes"},
        {"glide", "Slide between monophonic notes"},
        {"output", "Forge's final level"},
    };
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
    setResizeLimits(1060, 760, 1800, 1200);
    setSize(1180, 820);
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
                    control->slider.setDoubleClickReturnValue(
                        true, parameter->convertFrom0to1(parameter->getDefaultValue()));

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
