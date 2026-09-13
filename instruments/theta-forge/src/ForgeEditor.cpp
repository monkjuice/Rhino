#include "ForgeEditor.h"
#include "../ui/ForgeVisuals.h"

namespace theta::forge
{
namespace
{
constexpr std::array<const char*, 39> ids {"oscAPosition", "oscBPosition", "oscBLevel", "oscBTune", "subLevel", "noiseLevel", "unison", "detune", "cutoff", "resonance", "attack", "decay", "sustain", "release", "filterEnvAmount", "filterAttack", "filterDecay", "filterSustain", "filterRelease", "lfoRate", "lfoCutoff", "drive", "output", "lfoPosition", "lfoPitch", "chorusMix", "chorusRate", "chorusDepth", "delayMix", "delayTime", "delayFeedback", "polyphony", "mono", "legato", "glide", "macroShape", "macroMotion", "macroWeight", "macroSpace"};
constexpr std::array<const char*, 39> names {"A POS", "B POS", "B LEVEL", "B TUNE", "SUB", "NOISE", "UNISON", "DETUNE", "CUTOFF", "RES", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "ENV > FILTER", "F ATTACK", "F DECAY", "F SUSTAIN", "F RELEASE", "LFO RATE", "LFO > FILTER", "DRIVE", "OUTPUT", "LFO > POS", "LFO > PITCH", "CHORUS", "CH RATE", "CH DEPTH", "DELAY", "TIME", "FEEDBACK", "POLY", "MONO", "LEGATO", "GLIDE", "M1 SHAPE", "M2 MOTION", "M3 WEIGHT", "M4 SPACE"};
constexpr std::array<const char*, 39> tips {
    "Scan oscillator A's harmonic shape", "Scan oscillator B's harmonic shape", "Set oscillator B's level", "Tune oscillator B in semitones",
    "Blend a grounded sub oscillator", "Add a little heat and air", "Stack voices for width", "Spread stacked oscillator voices",
    "Open or close the low-pass filter", "Emphasise the filter edge", "Set how the sound begins", "Set the fall after the attack",
    "Set the held level", "Set how the sound fades", "Push the filter envelope up or invert it",
    "Set the filter envelope attack", "Set the filter envelope decay", "Set the filter envelope sustain",
    "Set the filter envelope release", "Set free-running LFO speed", "Move cutoff with the LFO",
    "Add saturation and density", "Set Forge's final level", "Animate both oscillator positions",
    "Add vibrato or wide pitch movement", "Blend the stereo chorus", "Set chorus movement speed",
    "Set chorus delay sweep", "Blend the ping-pong delay", "Set delay time in seconds", "Set delay regeneration",
    "Limit simultaneous notes", "Use one voice for basses and leads", "Keep envelopes running across overlapping mono notes",
    "Slide smoothly between monophonic notes", "Push oscillator shape and harmonic drive",
    "Animate wavetable position and stereo movement", "Add sub weight and darken the signal",
    "Open the chorus and ping-pong delay chain"};
}

Editor::Editor(Processor& p) : AudioProcessorEditor(&p), processor(p)
{
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& control = controls[i];
        control.label.setText(names[i], juce::dontSendNotification);
        control.label.setJustificationType(juce::Justification::centred);
        control.label.setColour(juce::Label::textColourId, ui::mutedText);
        control.label.setFont(juce::FontOptions(11.0f));
        control.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        control.slider.setLookAndFeel(&lookAndFeel);
        control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        control.slider.setColour(juce::Slider::rotarySliderFillColourId, ui::accentForParameter(static_cast<int>(i)));
        control.slider.setColour(juce::Slider::rotarySliderOutlineColourId, ui::line);
        control.slider.setColour(juce::Slider::thumbColourId, ui::accentForParameter(static_cast<int>(i)));
        control.slider.setColour(juce::Slider::textBoxTextColourId, ui::text);
        control.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        control.slider.setTooltip(tips[i]);
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, ids[i], control.slider);
        addAndMakeVisible(control.label);
        addAndMakeVisible(control.slider);
    }
    for (auto* button : {&synthPage, &motionPage, &loadPreset, &savePreset})
    {
        button->setClickingTogglesState(false);
        button->setColour(juce::TextButton::buttonColourId, ui::panelRaised);
        button->setColour(juce::TextButton::textColourOffId, ui::text);
        addAndMakeVisible(button);
    }
    synthPage.onClick = [this] { showPage(0); };
    motionPage.onClick = [this] { showPage(1); };
    loadPreset.onClick = [this] { choosePresetToLoad(); };
    savePreset.onClick = [this] { choosePresetToSave(); };
    presetName.setText("INIT SIGNAL", juce::dontSendNotification);
    presetName.setJustificationType(juce::Justification::centredRight);
    presetName.setFont(juce::FontOptions(10.0f));
    presetName.setColour(juce::Label::textColourId, ui::mutedText);
    addAndMakeVisible(presetName);
    setResizable(true, true);
    setResizeLimits(900, 600, 1600, 1000);
    setSize(1100, 700);
    startTimerHz(24);
    showPage(0);
}

void Editor::paint(juce::Graphics& g)
{
    ui::paint(g, getLocalBounds(), [this](int index)
    {
        return processor.state.getRawParameterValue(ids[static_cast<size_t>(index)])->load();
    });
}

void Editor::resized()
{
    synthPage.setBounds(330, 38, 86, 28);
    motionPage.setBounds(422, 38, 116, 28);
    const auto right = getWidth() - 38;
    savePreset.setBounds(right - 66, 38, 62, 28);
    loadPreset.setBounds(right - 134, 38, 62, 28);
    presetName.setBounds(right - 356, 38, 212, 28);
    for (int i = 0; i < static_cast<int>(controls.size()); ++i)
    {
        const auto visible = ui::isParameterVisible(i, currentPage);
        controls[static_cast<size_t>(i)].label.setVisible(visible);
        controls[static_cast<size_t>(i)].slider.setVisible(visible);
        if (!visible) continue;
        auto cell = ui::controlCell(getLocalBounds(), i, currentPage).reduced(7);
        controls[static_cast<size_t>(i)].label.setBounds(cell.removeFromTop(20));
        controls[static_cast<size_t>(i)].slider.setBounds(cell);
    }
}

void Editor::showPage(int page)
{
    currentPage = juce::jlimit(0, 1, page);
    synthPage.setColour(juce::TextButton::buttonColourId, currentPage == 0 ? ui::electricBlue : ui::panelRaised);
    motionPage.setColour(juce::TextButton::buttonColourId, currentPage == 1 ? ui::signalViolet : ui::panelRaised);
    resized();
    repaint();
}

void Editor::timerCallback()
{
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
}
}
