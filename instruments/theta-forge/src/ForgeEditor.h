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
        const char* enabledBy = nullptr;
        int row = 0, index = 0;
        // The slot whose depth this knob's ring sets, or -1 when the ring is
        // not draggable: nothing is pointed here, or more than one thing is and
        // the ring is a sum with no single slot behind it.
        int ringSlot = -1;
        juce::Label label;
        ui::ModKnob slider;
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
    // Which tab is showing. Only the modules that declare a page follow it;
    // everything else stays on screen whichever tab is chosen.
    ui::Page page = ui::Page::oscillators;
    std::vector<std::unique_ptr<ui::PageTab>> tabs;
    // Drag handles live outside the modules: a macro's sits beside its knob, a
    // modulator's in its module header.
    std::vector<std::unique_ptr<ui::SourceHandle>> handles;
    ui::SourceHandle* draggingHandle = nullptr;
    juce::Point<int> dragPosition;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton loadPreset {"LOAD"}, savePreset {"SAVE"};
    juce::Label presetName;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void buildModules();
    void buildTabs();
    void showPage(ui::Page);
    void applyPage();
    void paintTable(juce::Graphics&, juce::Rectangle<int> area, const ui::Module&);
    bool slotIsLive(int slot) const;
    void applyEnableStates();
    float value(const char* id) const;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyStateChanged(bool isKeyDown) override;
    bool keyPressed(const juce::KeyPress&) override;
    void buildHandles();
    void showModulationMenu(const juce::String& parameterId);
    void assignModulation(int source, int destination);
    void setSlotDepth(int slot, float depth);
    void clearSlot(int slot);
    void refreshModulationRings();
    Control* controlAt(juce::Point<int> panelPosition);
    void timerCallback() override;
    void choosePresetToLoad();
    void choosePresetToSave();
    void showPresetResult(const juce::Result&, const juce::File&);
};
}
