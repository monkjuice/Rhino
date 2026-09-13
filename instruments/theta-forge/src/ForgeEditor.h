#pragma once

#include "ForgeProcessor.h"
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
    std::array<Control, 35> controls;
    juce::TextButton synthPage {"SYNTH"}, motionPage {"MOTION / FX"};
    int currentPage = 0;

    void timerCallback() override;
    void showPage(int);
};
}
