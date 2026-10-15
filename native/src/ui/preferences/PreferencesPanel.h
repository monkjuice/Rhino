#pragma once
#include "MidiPreferencesPanel.h"
#include <functional>
#include <memory>

namespace rhino
{

// The preferences window's content: what belongs to this machine rather than
// to a project, one tab per subject. Audio is the device chooser Audio
// settings always opened; MIDI is the keyboards and their velocity curves.
class PreferencesPanel final : public juce::Component
{
public:
    enum class Tab { audio, midi };

    PreferencesPanel(Session&, Tab first);
    void showTab(Tab);
    Tab shownTab() const { return shown; }
    void paint(juce::Graphics&) override;
    void resized() override;

    // So the shell can open the window on the tab last looked at.
    std::function<void(Tab)> onTabShown;

    static constexpr int defaultWidth = 560;
    static constexpr int defaultHeight = 480;

private:
    static constexpr int tabStripHeight = 40;

    juce::TextButton audioTab {"Audio"}, midiTab {"MIDI"};
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audio;
    MidiPreferencesPanel midi;
    Tab shown = Tab::audio;
};

}
