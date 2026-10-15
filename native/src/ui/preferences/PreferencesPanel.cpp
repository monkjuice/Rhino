#include "PreferencesPanel.h"
#include "ui/shared/Theme.h"

namespace rhino
{

PreferencesPanel::PreferencesPanel(Session& session, Tab first)
    : audio(std::make_unique<juce::AudioDeviceSelectorComponent>(
          session.engine.getDeviceManager().deviceManager, 0, 2, 0, 2, true, false, true, false)),
      midi(session)
{
    // A strip of two keys rather than a juce::TabbedComponent: its tabs draw
    // their own text, off the baseline every other label here is snapped to.
    for (auto* tab : {&audioTab, &midiTab})
    {
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(1);
        addAndMakeVisible(*tab);
    }
    audioTab.onClick = [this] { if (audioTab.getToggleState()) showTab(Tab::audio); };
    midiTab.onClick = [this] { if (midiTab.getToggleState()) showTab(Tab::midi); };
    addChildComponent(*audio);
    addChildComponent(midi);
    showTab(first);
}

void PreferencesPanel::showTab(Tab tab)
{
    shown = tab;
    audioTab.setToggleState(tab == Tab::audio, juce::dontSendNotification);
    midiTab.setToggleState(tab == Tab::midi, juce::dontSendNotification);
    audio->setVisible(tab == Tab::audio);
    midi.setVisible(tab == Tab::midi);
    if (onTabShown)
        onTabShown(tab);
}

void PreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(palette::globalBar);
    g.fillRect(getLocalBounds().removeFromTop(tabStripHeight));
    g.setColour(palette::border);
    g.fillRect(0, tabStripHeight - 1, getWidth(), 1);
}

void PreferencesPanel::resized()
{
    auto area = getLocalBounds();
    auto strip = area.removeFromTop(tabStripHeight).reduced(12, 7);
    audioTab.setBounds(strip.removeFromLeft(84));
    strip.removeFromLeft(4);
    midiTab.setBounds(strip.removeFromLeft(84));
    audio->setBounds(area.reduced(0, 8));
    midi.setBounds(area);
}

}
