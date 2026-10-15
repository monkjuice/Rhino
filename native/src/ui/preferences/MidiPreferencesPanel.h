#pragma once
#include "model/Session.h"
#include <memory>
#include <vector>

namespace rhino
{

// The preferences' MIDI tab: every keyboard on the machine, each with the
// velocity curve Rhino plays it through and a picture of that curve with the
// last note played on it, so a curve is chosen by playing rather than by
// guessing. The model does the work (SessionKeyboards.cpp); this reads it.
class MidiPreferencesPanel final : public juce::Component, private juce::Timer
{
public:
    explicit MidiPreferencesPanel(Session&);
    ~MidiPreferencesPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class KeyboardRow;
    void timerCallback() override;
    void rebuildRows();

    Session& session;
    juce::Viewport viewport;
    juce::Component rowHolder;
    std::vector<std::unique_ptr<KeyboardRow>> rows;
    juce::StringArray shownPorts;
    int ticksToRescan = 0;
};

}
