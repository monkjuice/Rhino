#include "BrowserPanel.h"

namespace theta
{
namespace
{
juce::String presetId(Session::PatternPreset preset)
{
    switch (preset)
    {
        case Session::PatternPreset::WarmPulse:  return "WarmPulse";
        case Session::PatternPreset::AcidSteps:  return "AcidSteps";
        case Session::PatternPreset::ArpRun:     return "ArpRun";
        case Session::PatternPreset::ChordPad:   return "ChordPad";
        case Session::PatternPreset::SubBass:    return "SubBass";
        case Session::PatternPreset::ReeseBass:  return "ReeseBass";
        case Session::PatternPreset::SirenLead:  return "SirenLead";
        case Session::PatternPreset::WavePad:    return "WavePad";
        case Session::PatternPreset::WaveBass:   return "WaveBass";
        case Session::PatternPreset::WavePluck:  return "WavePluck";
        case Session::PatternPreset::HouseKit:   return "HouseKit";
        case Session::PatternPreset::BreakKit:   return "BreakKit";
        case Session::PatternPreset::MinimalKit: return "MinimalKit";
        case Session::PatternPreset::ClapKit:    return "ClapKit";
    }
    return {};
}

juce::String effectId(Session::AudioEffect effect)
{
    switch (effect)
    {
        case Session::AudioEffect::Equaliser:  return "Equaliser";
        case Session::AudioEffect::Reverb:     return "Reverb";
        case Session::AudioEffect::Delay:      return "Delay";
        case Session::AudioEffect::Compressor: return "Compressor";
        case Session::AudioEffect::ThetaSpace: return "ThetaSpace";
        case Session::AudioEffect::ThetaBloom: return "ThetaBloom";
    }
    return {};
}

juce::String instrumentId(Session::Instrument instrument)
{
    switch (instrument)
    {
        case Session::Instrument::FourOsc:   return "FourOsc";
        case Session::Instrument::ThetaWave: return "ThetaWave";
        case Session::Instrument::ThetaForge: return "ThetaForge";
        case Session::Instrument::Drums:     return "Drums";
        case Session::Instrument::Utility:   return "Utility";
    }
    return {};
}

juce::String midiEffectId(Session::MidiEffect effect)
{
    switch (effect)
    {
        case Session::MidiEffect::ThetaArp: return "ThetaArp";
    }
    return {};
}

juce::String sampleId(Session::BuiltInSample sample)
{
    switch (sample)
    {
        case Session::BuiltInSample::Whistle: return "Whistle";
        case Session::BuiltInSample::Siren:   return "Siren";
    }
    return {};
}
}

BrowserPanel::BrowserPanel(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    title.setText("BROWSER", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffd5dde4));
    categoriesTitle.setText("Categories", juce::dontSendNotification);
    categoriesTitle.setColour(juce::Label::textColourId, juce::Colour(0xff8f9aa4));
    soundsTitle.setText("Name", juce::dontSendNotification);
    soundsTitle.setColour(juce::Label::textColourId, juce::Colour(0xffaeb8c0));
    search.setTextToShowWhenEmpty("Search", juce::Colour(0xff6f7b85));
    search.onTextChange = [this] { rebuildRows(); };
    search.onReturnKey = [this] { applyRow(list.getSelectedRow()); };
    search.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff262c32));
    search.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff46515a));
    apply.onClick = [this] { applyRow(list.getSelectedRow()); };

    const std::array labels {"Instruments", "Patterns", "Samples", "Audio FX", "MIDI FX"};
    for (size_t i = 0; i < categories.size(); ++i)
    {
        categories[i].setButtonText(labels[i]);
        categories[i].setRadioGroupId(18, juce::dontSendNotification);
        categories[i].setClickingTogglesState(true);
        categories[i].onClick = [this, i]
        {
            selectedCategory = categories[i].getButtonText();
            rebuildRows();
        };
        addAndMakeVisible(categories[i]);
    }
    categories[0].setToggleState(true, juce::dontSendNotification);

    // Categories say what a row is, not what it is for: an instrument changes
    // the track, a pattern fills a clip, a sample is audio. Mixing them was the
    // reason a drum kit and a drum instrument looked like the same kind of row.
    items = {
        {"Instruments", "4OSC synth", "Subtractive synth: drop on a track to play it", std::nullopt, std::nullopt, Session::Instrument::FourOsc},
        {"Instruments", "Theta Wave", "Morphing wavetable-style synth", std::nullopt, std::nullopt, Session::Instrument::ThetaWave},
        {"Instruments", "Theta Forge", "Two-oscillator Forge synth", std::nullopt, std::nullopt, Session::Instrument::ThetaForge},
        {"Instruments", "Theta Drums", "TR-808 analog kit: kick, snare, toms, closed/open hats", std::nullopt, std::nullopt, Session::Instrument::Drums},
        {"Patterns", "Warm pulse", "Soft one-bar 4OSC chord pulse", Session::PatternPreset::WarmPulse},
        {"Patterns", "Acid steps", "Tight 16-step synth riff", Session::PatternPreset::AcidSteps},
        {"Patterns", "Arp run", "Held chord made for Theta Arp", Session::PatternPreset::ArpRun},
        {"Patterns", "Chord pad", "Soft sustaining 4OSC chord synth", Session::PatternPreset::ChordPad},
        {"Patterns", "Sub bass", "Clean mono low-end bass line", Session::PatternPreset::SubBass},
        {"Patterns", "Reese bass", "Wide detuned electronic bass", Session::PatternPreset::ReeseBass},
        {"Patterns", "Siren lead", "Rising and falling emergency lead", Session::PatternPreset::SirenLead},
        {"Patterns", "Wave pad", "Theta Wave wide glassy chords", Session::PatternPreset::WavePad},
        {"Patterns", "Wave bass", "Theta Wave rounded low pulse", Session::PatternPreset::WaveBass},
        {"Patterns", "Wave pluck", "Theta Wave bright moving pluck", Session::PatternPreset::WavePluck},
        {"Patterns", "House kit", "Four-on-floor kick, backbeat, hats", Session::PatternPreset::HouseKit},
        {"Patterns", "Break kit", "Syncopated kick/snare/hats groove", Session::PatternPreset::BreakKit},
        {"Patterns", "Minimal kit", "Sparse kick/snare/hats sketch", Session::PatternPreset::MinimalKit},
        {"Patterns", "Clap kit", "Kick, clap backbeat, tight hats", Session::PatternPreset::ClapKit},
        {"Samples", "Whistle", "Built-in audio sample", std::nullopt, std::nullopt, std::nullopt, std::nullopt, Session::BuiltInSample::Whistle},
        {"Samples", "Siren", "Built-in audio sample", std::nullopt, std::nullopt, std::nullopt, std::nullopt, Session::BuiltInSample::Siren},
        {"Audio FX", "EQ", "Insert Tracktion 4-band EQ", std::nullopt, Session::AudioEffect::Equaliser},
        {"Audio FX", "Reverb", "Insert Tracktion reverb", std::nullopt, Session::AudioEffect::Reverb},
        {"Audio FX", "Delay", "Insert Tracktion delay", std::nullopt, Session::AudioEffect::Delay},
        {"Audio FX", "Compressor", "Insert Tracktion compressor", std::nullopt, Session::AudioEffect::Compressor},
        {"Audio FX", "Theta Space", "Floating multi FX: smear, drive, width", std::nullopt, Session::AudioEffect::ThetaSpace},
        {"Audio FX", "Theta Bloom", "Chorus, clouds, plate, colour", std::nullopt, Session::AudioEffect::ThetaBloom},
        {"Audio FX", "Utility gain", "Drop on a track for gain", std::nullopt, std::nullopt, Session::Instrument::Utility},
        {"MIDI FX", "Theta Arp", "Drop before an instrument to arpeggiate it", std::nullopt, std::nullopt, std::nullopt, Session::MidiEffect::ThetaArp}
    };

    list.setRowHeight(38);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff20262c));
    list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff323a42));
    list.setMultipleSelectionEnabled(false);
    for (auto* component : std::initializer_list<juce::Component*>{&title, &categoriesTitle, &soundsTitle, &search, &apply, &list})
        addAndMakeVisible(component);
    rebuildRows();
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2026));
    g.setColour(juce::Colour(0xff303840));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.setColour(juce::Colour(0xff252b31));
    g.fillRect(0, 96, getWidth(), 1);
}

void BrowserPanel::resized()
{
    title.setBounds(14, 12, getWidth() - 28, 24);
    search.setBounds(12, 44, getWidth() - 82, 30);
    apply.setBounds(getWidth() - 64, 44, 52, 30);
    categoriesTitle.setBounds(12, 84, getWidth() - 24, 22);
    int y = 112;
    for (auto& button : categories)
    {
        button.setBounds(12, y, getWidth() - 24, 28);
        y += 32;
    }
    soundsTitle.setBounds(12, y + 8, getWidth() - 24, 22);
    list.setBounds(12, y + 34, getWidth() - 24, getHeight() - y - 46);
}

void BrowserPanel::focusSearch()
{
    search.grabKeyboardFocus();
    search.selectAll();
}

bool BrowserPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        applyRow(list.getSelectedRow());
        return true;
    }
    return false;
}

int BrowserPanel::getNumRows()
{
    return static_cast<int>(rows.size());
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    g.fillAll(selected ? juce::Colour(0xff34424a) : juce::Colour(row % 2 == 0 ? 0xff20262c : 0xff242a31));
    g.setColour(item.sample ? juce::Colour(0xffe09a70) : item.preset ? juce::Colour(0xffc6d58c) : item.effect ? juce::Colour(0xffffb15f) : item.instrument ? juce::Colour(0xff8cc5d2) : item.midiEffect ? juce::Colour(0xffbda4ff) : juce::Colour(0xff6f7b85));
    g.fillRect(8, height / 2 - 4, 8, 8);
    g.setFont(juce::FontOptions(14.0f));
    g.setColour(juce::Colour(0xffe5ebef));
    g.drawText(item.name, 24, 3, width - 30, 17, juce::Justification::centredLeft, true);
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colour(0xff94a0aa));
    g.drawText(item.detail, 24, 20, width - 30, 15, juce::Justification::centredLeft, true);
}

void BrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    list.selectRow(row, juce::dontSendNotification);
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    if (status) status(item.name + " - " + item.detail);
}

void BrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    applyRow(row);
}

void BrowserPanel::selectedRowsChanged(int lastRowSelected)
{
    if (!juce::isPositiveAndBelow(lastRowSelected, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(lastRowSelected)])];
    if (status) status(item.name + " - " + item.detail);
}

juce::var BrowserPanel::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
{
    if (rowsToDescribe.isEmpty()) return {};
    const auto row = rowsToDescribe[0];
    if (!juce::isPositiveAndBelow(row, rows.size())) return {};
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    const auto description = dragDescriptionFor(item);
    return description.isEmpty() ? juce::var{} : juce::var(description);
}

void BrowserPanel::rebuildRows()
{
    rows.clear();
    const auto query = search.getText().trim().toLowerCase();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        if (item.category != selectedCategory) continue;
        if (query.isNotEmpty()
            && !item.name.toLowerCase().contains(query)
            && !item.detail.toLowerCase().contains(query))
            continue;
        rows.push_back(static_cast<int>(i));
    }
    list.updateContent();
    if (!rows.empty())
        list.selectRow(0, juce::dontSendNotification);
    list.repaint();
    apply.setEnabled(!rows.empty());
}

juce::String BrowserPanel::dragDescriptionFor(const Item& item) const
{
    if (item.preset)
        return "theta-browser:preset:" + presetId(*item.preset);
    if (item.effect)
        return "theta-browser:effect:" + effectId(*item.effect);
    if (item.instrument)
        return "theta-browser:instrument:" + instrumentId(*item.instrument);
    if (item.midiEffect)
        return "theta-browser:midi-effect:" + midiEffectId(*item.midiEffect);
    if (item.sample)
        return "theta-browser:sample:" + sampleId(*item.sample);
    return "theta-browser:info:" + item.name;
}

void BrowserPanel::applyRow(int row)
{
    if (!juce::isPositiveAndBelow(row, rows.size())) return;
    const auto& item = items[static_cast<size_t>(rows[static_cast<size_t>(row)])];
    if (item.preset)
    {
        session.applyPatternPreset(*item.preset);
        if (status) status("Loaded " + item.name);
    }
    else if (item.effect)
    {
        const auto result = session.addAudioEffect(*item.effect);
        if (status) status(result.wasOk() ? "Added " + item.name + " to Audio 1" : result.getErrorMessage());
    }
    else if (item.instrument)
    {
        const auto targetTrack = *item.instrument == Session::Instrument::ThetaForge ? 0 : 1;
        const auto result = session.addInstrument(*item.instrument, targetTrack);
        if (status) status(result.wasOk() ? "Added " + item.name + (targetTrack == 0 ? " to Pattern synth" : " to Audio 1") : result.getErrorMessage());
    }
    else if (item.midiEffect)
    {
        const auto result = session.addMidiEffect(*item.midiEffect, 0);
        if (status) status(result.wasOk() ? "Added " + item.name + " to Pattern synth" : result.getErrorMessage());
    }
    else if (item.sample)
    {
        const auto result = session.importBuiltInSample(*item.sample);
        if (status) status(result.wasOk() ? "Added " + item.name + " audio sample to Audio 1" : result.getErrorMessage());
    }
    else if (status)
    {
        status(item.name + " is already part of this starter session.");
    }
}
}
