#pragma once

#include "ForgeProcessor.h"
#include "../ui/ForgeLayout.h"
#include "../ui/ForgeVisuals.h"
#include <memory>
#include <vector>

namespace theta::forge
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // A control is a knob or a stepper (a slider with a label above) or a chip
    // (a button that carries its own label). Only the members its style needs
    // are made visible.
    struct Control
    {
        ui::Style style = ui::Style::knob;
        juce::String id;
        const char* disabledBy = nullptr;
        int row = 0, index = 0;
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<ui::ToggleChip> chip;
        std::unique_ptr<ui::RockerSwitch> rocker;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    // One of these per declared module. Controls are held by pointer because
    // Component addresses must not move once they are children.
    struct ModuleUi
    {
        const ui::Module* descriptor = nullptr;
        std::vector<std::unique_ptr<Control>> controls;
        std::unique_ptr<ui::EnableLed> enable;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
        bool on() const { return enable == nullptr || enable->getToggleState(); }
    };

    Processor& processor;
    ui::LookAndFeel lookAndFeel;
    std::vector<ModuleUi> moduleUis;
    juce::TextButton loadPreset {"LOAD"}, savePreset {"SAVE"};
    juce::Label presetName;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void buildModules();
    void applyEnableStates();
    float value(const char* id) const;
    void mouseDown(const juce::MouseEvent&) override;
    void showModulationMenu(const juce::String& parameterId);
    void assignModulation(int source, int destination);
    void clearSlot(int slot);
    void timerCallback() override;
    void choosePresetToLoad();
    void choosePresetToSave();
    void showPresetResult(const juce::Result&, const juce::File&);
};
}
