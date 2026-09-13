#pragma once

#include "ForgeProcessor.h"
#include "../ui/ForgeVisuals.h"
#include <array>

namespace theta::forge
{
class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(Processor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Processor& processor;
    ui::LookAndFeel lookAndFeel;
    std::array<Control, 39> controls;
    juce::TextButton synthPage {"SYNTH"}, motionPage {"MOTION / FX"};
    juce::TextButton loadPreset {"LOAD"}, savePreset {"SAVE"};
    juce::Label presetName;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int currentPage = 0;

    void timerCallback() override;
    void showPage(int);
    void choosePresetToLoad();
    void choosePresetToSave();
    void showPresetResult(const juce::Result&, const juce::File&);
};
}
