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

juce::String drumKitId(DrumDevice::Kit kit)
{
    switch (kit)
    {
        case DrumDevice::Kit::Theta808: return "Theta808";
        case DrumDevice::Kit::House:    return "HouseKit";
        case DrumDevice::Kit::Break:    return "BreakKit";
        case DrumDevice::Kit::Minimal:  return "MinimalKit";
        case DrumDevice::Kit::Clap:     return "ClapKit";
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


// A folder in the tree. Holds child folders and rows; it carries no Item of its
// own, so dragging a folder does nothing.
class BrowserPanel::FolderNode final : public juce::TreeViewItem
{
public:
    FolderNode(BrowserPanel& p, juce::String folderName) : panel(p), name(std::move(folderName)) {}

    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return "folder:" + name; }
    int getItemHeight() const override { return name.isEmpty() ? 0 : 22; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (name.isEmpty()) return;
        if (isSelected())
        {
            g.setColour(juce::Colour(0xff34424a));
            g.fillRect(0, 0, width, height);
        }
        g.setColour(juce::Colour(0xffb9c4cd));
        g.setFont(juce::FontOptions(12.5f));
        g.drawText(name, 2, 0, width - 6, height, juce::Justification::centredLeft, true);
    }

    void paintOpenCloseButton(juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool) override
    {
        if (name.isEmpty()) return;
        juce::Path arrow;
        const auto centre = area.getCentre();
        constexpr auto size = 3.4f;
        if (isOpen())
            arrow.addTriangle(centre.x - size, centre.y - size * 0.6f,
                              centre.x + size, centre.y - size * 0.6f,
                              centre.x, centre.y + size * 0.9f);
        else
            arrow.addTriangle(centre.x - size * 0.6f, centre.y - size,
                              centre.x - size * 0.6f, centre.y + size,
                              centre.x + size * 0.9f, centre.y);
        g.setColour(juce::Colour(0xff8f9aa4));
        g.fillPath(arrow);
    }

    BrowserPanel& panel;
    juce::String name;
};

// A single library row. This is what carries the drag payload.
class BrowserPanel::ItemNode final : public juce::TreeViewItem
{
public:
    ItemNode(BrowserPanel& p, Item i) : panel(p), item(std::move(i)) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override { return "item:" + item.category + "/" + item.name; }
    int getItemHeight() const override { return 30; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
        {
            g.setColour(juce::Colour(0xff34424a));
            g.fillRect(0, 0, width, height);
        }
        g.setColour(panel.colourFor(item));
        g.fillRect(2, height / 2 - 4, 7, 7);
        g.setColour(juce::Colour(0xffe5ebef));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(item.name, 15, 1, width - 19, 15, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff8d99a3));
        g.setFont(juce::FontOptions(10.5f));
        g.drawText(item.detail, 15, 15, width - 19, 13, juce::Justification::centredLeft, true);
    }

    void itemSelectionChanged(bool nowSelected) override
    {
        if (nowSelected) panel.reportSelection(item);
    }

    void itemDoubleClicked(const juce::MouseEvent&) override { panel.applyItem(item); }

    juce::var getDragSourceDescription() override
    {
        const auto description = panel.dragDescriptionFor(item);
        return description.isEmpty() ? juce::var{} : juce::var(description);
    }

    BrowserPanel& panel;
    Item item;
};

BrowserPanel::BrowserPanel(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    title.setText("BROWSER", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffd5dde4));
    title.setFont(juce::FontOptions(12.0f));
    search.setTextToShowWhenEmpty("Search", juce::Colour(0xff6f7b85));
    search.onTextChange = [this] { rebuildTree(); };
    search.onReturnKey = [this] { if (auto* item = selectedItem()) applyItem(*item); };
    search.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff262c32));
    search.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff46515a));
    search.setFont(juce::FontOptions(12.0f));
    apply.setTooltip("Add the selected item to a track");
    apply.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b333a));
    apply.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc2ccd4));
    apply.onClick = [this] { if (auto* item = selectedItem()) applyItem(*item); };

    categoryList.setRowHeight(categoryRowHeight);
    categoryList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1b2026));
    categoryList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    categoryList.setMultipleSelectionEnabled(false);
    categoryList.selectRow(0, juce::dontSendNotification);

    tree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff20262c));
    tree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff323a42));
    tree.setDefaultOpenness(true);
    tree.setRootItemVisible(false);
    tree.setIndentSize(13);
    tree.setMultiSelectEnabled(false);
    // Rows always fill the width, so the horizontal bar has nothing to scroll.
    tree.getViewport()->setScrollBarsShown(true, false);

    // Grouped by what a row is, then by family inside that, the way Live's
    // library separates Drums from Instruments and both from Clips.
    items = {
        {"Instruments", "Synths", "4OSC synth", "Subtractive synth", std::nullopt, std::nullopt, Session::Instrument::FourOsc},
        {"Instruments", "Synths", "Theta Wave", "Morphing wavetable-style synth", std::nullopt, std::nullopt, Session::Instrument::ThetaWave},
        {"Instruments", "Synths", "Theta Forge", "Two-oscillator Forge synth", std::nullopt, std::nullopt, Session::Instrument::ThetaForge},
        {"Instruments", "Drum Rack", "Theta 808", "TR-808 kit: kick, snare, toms, hats", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, DrumDevice::Kit::Theta808},
        {"Instruments", "Drum Rack", "House Kit", "Deep kick, tight hats, for the House pattern", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, DrumDevice::Kit::House},
        {"Instruments", "Drum Rack", "Break Kit", "Snappy snare, bright hats, for the Break pattern", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, DrumDevice::Kit::Break},
        {"Instruments", "Drum Rack", "Minimal Kit", "Short, quiet pads, for the Minimal pattern", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, DrumDevice::Kit::Minimal},
        {"Instruments", "Drum Rack", "Clap Kit", "Clap on the backbeat, for the Clap pattern", std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, DrumDevice::Kit::Clap},
        {"Patterns", "Synth", "Warm pulse", "Soft one-bar 4OSC chord pulse", Session::PatternPreset::WarmPulse},
        {"Patterns", "Synth", "Acid steps", "Tight 16-step synth riff", Session::PatternPreset::AcidSteps},
        {"Patterns", "Synth", "Arp run", "Held chord made for Theta Arp", Session::PatternPreset::ArpRun},
        {"Patterns", "Synth", "Chord pad", "Soft sustaining 4OSC chord synth", Session::PatternPreset::ChordPad},
        {"Patterns", "Bass", "Sub bass", "Clean mono low-end bass line", Session::PatternPreset::SubBass},
        {"Patterns", "Bass", "Reese bass", "Wide detuned electronic bass", Session::PatternPreset::ReeseBass},
        {"Patterns", "Bass", "Wave bass", "Theta Wave rounded low pulse", Session::PatternPreset::WaveBass},
        {"Patterns", "Lead", "Siren lead", "Rising and falling emergency lead", Session::PatternPreset::SirenLead},
        {"Patterns", "Lead", "Wave pluck", "Theta Wave bright moving pluck", Session::PatternPreset::WavePluck},
        {"Patterns", "Pad", "Wave pad", "Theta Wave wide glassy chords", Session::PatternPreset::WavePad},
        {"Patterns", "Drums", "House kit", "Four-on-floor kick, backbeat, hats", Session::PatternPreset::HouseKit},
        {"Patterns", "Drums", "Break kit", "Syncopated kick/snare/hats groove", Session::PatternPreset::BreakKit},
        {"Patterns", "Drums", "Minimal kit", "Sparse kick/snare/hats sketch", Session::PatternPreset::MinimalKit},
        {"Patterns", "Drums", "Clap kit", "Kick, clap backbeat, tight hats", Session::PatternPreset::ClapKit},
        {"Samples", "Built-in", "Whistle", "Built-in audio sample", std::nullopt, std::nullopt, std::nullopt, std::nullopt, Session::BuiltInSample::Whistle},
        {"Samples", "Built-in", "Siren", "Built-in audio sample", std::nullopt, std::nullopt, std::nullopt, std::nullopt, Session::BuiltInSample::Siren},
        {"Audio FX", "EQ and Filters", "EQ", "Tracktion 4-band EQ", std::nullopt, Session::AudioEffect::Equaliser},
        {"Audio FX", "Dynamics", "Compressor", "Tracktion compressor", std::nullopt, Session::AudioEffect::Compressor},
        {"Audio FX", "Dynamics", "Utility gain", "Level trim inside a chain", std::nullopt, std::nullopt, Session::Instrument::Utility},
        {"Audio FX", "Delay and Reverb", "Reverb", "Tracktion reverb", std::nullopt, Session::AudioEffect::Reverb},
        {"Audio FX", "Delay and Reverb", "Delay", "Tracktion delay", std::nullopt, Session::AudioEffect::Delay},
        {"Audio FX", "Theta", "Theta Space", "Floating multi FX: smear, drive, width", std::nullopt, Session::AudioEffect::ThetaSpace},
        {"Audio FX", "Theta", "Theta Bloom", "Chorus, clouds, plate, colour", std::nullopt, Session::AudioEffect::ThetaBloom},
        {"MIDI FX", "", "Theta Arp", "Drop before an instrument to arpeggiate it", std::nullopt, std::nullopt, std::nullopt, Session::MidiEffect::ThetaArp}
    };

    for (auto* component : std::initializer_list<juce::Component*>{&title, &search, &apply, &categoryList, &tree})
        addAndMakeVisible(component);
    rebuildTree();
}

BrowserPanel::~BrowserPanel()
{
    tree.setRootItem(nullptr);
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2026));
    g.setColour(juce::Colour(0xff303840));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.setColour(juce::Colour(0xff8f9aa4));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("LIBRARY", 10, 62, getWidth() - 20, 14, juce::Justification::centredLeft);
}

void BrowserPanel::resized()
{
    const auto width = getWidth();
    title.setBounds(10, 6, width - 20, 16);
    search.setBounds(8, 26, width - 58, 26);
    apply.setBounds(width - 46, 26, 38, 26);
    categoryList.setBounds(4, 78, width - 8, categoryRowHeight * static_cast<int>(categories.size()));
    const auto treeTop = categoryList.getBottom() + 8;
    tree.setBounds(4, treeTop, width - 8, std::max(0, getHeight() - treeTop - 6));
    // The tree's own relayout runs on an async update, so a tree first built
    // while the panel had no size keeps those row widths until the message loop
    // turns. Rebuilding on a width change settles it synchronously instead.
    if (tree.getWidth() != lastLayoutWidth)
    {
        lastLayoutWidth = tree.getWidth();
        rebuildTree();
    }
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
        if (auto* item = selectedItem()) applyItem(*item);
        return true;
    }
    return false;
}

int BrowserPanel::getNumRows()
{
    return static_cast<int>(categories.size());
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(categories.size()))) return;
    if (selected)
    {
        g.setColour(juce::Colour(0xff2f3a43));
        g.fillRect(0, 0, width, height);
        g.setColour(juce::Colour(0xffc6d58c));
        g.fillRect(0, 0, 2, height);
    }
    g.setColour(juce::Colour(selected ? 0xffe5ebef : 0xffa8b3bd));
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(categories[static_cast<size_t>(row)], 10, 0, width - 14, height,
               juce::Justification::centredLeft, true);
}

void BrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(categories.size()))) return;
    selectedCategory = row;
    rebuildTree();
}

void BrowserPanel::selectedRowsChanged(int lastRowSelected)
{
    if (!juce::isPositiveAndBelow(lastRowSelected, static_cast<int>(categories.size()))) return;
    if (selectedCategory == lastRowSelected) return;
    selectedCategory = lastRowSelected;
    rebuildTree();
}

const BrowserPanel::Item* BrowserPanel::selectedItem() const
{
    if (auto* node = dynamic_cast<ItemNode*>(tree.getSelectedItem(0)))
        return &node->item;
    return nullptr;
}

juce::Colour BrowserPanel::colourFor(const Item& item) const
{
    if (item.sample) return juce::Colour(0xffe09a70);
    if (item.preset) return juce::Colour(0xffc6d58c);
    if (item.effect) return juce::Colour(0xffffb15f);
    if (item.midiEffect) return juce::Colour(0xffbda4ff);
    if (item.instrument || item.drumKit) return juce::Colour(0xff8cc5d2);
    return juce::Colour(0xff6f7b85);
}

void BrowserPanel::rebuildTree()
{
    if (auto* current = selectedItem())
        selectionToRestore = current->name;
    // Folders open by default, but a folder the user collapsed stays collapsed
    // through a rebuild, which happens whenever the panel is resized.
    auto openness = tree.getRootItem() != nullptr ? tree.getOpennessState(false) : nullptr;
    tree.setRootItem(nullptr);
    root = std::make_unique<FolderNode>(*this, juce::String());
    const auto query = search.getText().trim().toLowerCase();
    const auto& category = categories[static_cast<size_t>(juce::jlimit(0, static_cast<int>(categories.size()) - 1,
                                                                      selectedCategory))];
    // Searching looks across the whole library, as Live's does, so a hit in a
    // section you are not looking at is still reachable.
    const auto searching = query.isNotEmpty();
    std::vector<std::pair<juce::String, FolderNode*>> folders;
    ItemNode* restored = nullptr;
    const auto folderFor = [&folders, this](const juce::String& name) -> FolderNode*
    {
        if (name.isEmpty()) return root.get();
        for (const auto& [existing, node] : folders)
            if (existing == name)
                return node;
        auto owned = std::make_unique<FolderNode>(*this, name);
        auto* node = owned.get();
        folders.emplace_back(name, node);
        root->addSubItem(owned.release());
        return node;
    };
    for (const auto& item : items)
    {
        if (!searching && item.category != category) continue;
        if (searching
            && !item.name.toLowerCase().contains(query)
            && !item.detail.toLowerCase().contains(query)
            && !item.folder.toLowerCase().contains(query))
            continue;
        const auto groupName = searching ? item.category + " / " + item.folder : item.folder;
        auto* node = new ItemNode(*this, item);
        folderFor(groupName.trimCharactersAtEnd(" /"))->addSubItem(node);
        if (item.name == selectionToRestore)
            restored = node;
    }
    tree.setRootItem(root.get());
    root->setOpen(true);
    for (const auto& [name, node] : folders)
        node->setOpen(true);
    if (openness != nullptr)
        tree.restoreOpennessState(*openness, false);
    if (restored != nullptr)
        restored->setSelected(true, true);
    else if (root->getNumSubItems() > 0)
        if (auto* first = root->getSubItem(0))
            (first->getNumSubItems() > 0 ? first->getSubItem(0) : first)->setSelected(true, true);
    apply.setEnabled(selectedItem() != nullptr);
}

void BrowserPanel::reportSelection(const Item& item)
{
    apply.setEnabled(true);
    if (status) status(item.name + " - " + item.detail);
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
    if (item.drumKit)
        return "theta-browser:drumkit:" + drumKitId(*item.drumKit);
    return "theta-browser:info:" + item.name;
}

void BrowserPanel::applyItem(const Item& item)
{
    if (item.preset)
    {
        session.applyPatternPreset(*item.preset);
        if (status) status("Loaded " + item.name);
    }
    else if (item.effect)
    {
        const auto result = session.addAudioEffect(*item.effect);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(1) : result.getErrorMessage());
    }
    else if (item.instrument)
    {
        const auto targetTrack = *item.instrument == Session::Instrument::Utility ? 1 : 0;
        const auto result = session.addInstrument(*item.instrument, targetTrack);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(targetTrack)
                                          : result.getErrorMessage());
    }
    else if (item.midiEffect)
    {
        const auto result = session.addMidiEffect(*item.midiEffect, 0);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(0) : result.getErrorMessage());
    }
    else if (item.drumKit)
    {
        const auto result = session.addDrumKit(*item.drumKit, 0);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(0) : result.getErrorMessage());
    }
    else if (item.sample)
    {
        const auto result = session.importBuiltInSample(*item.sample);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(1) : result.getErrorMessage());
    }
    else if (status)
    {
        status(item.name + " is already part of this starter session.");
    }
}

}
