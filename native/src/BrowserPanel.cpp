#include "BrowserPanel.h"
#include "ContentLibrary.h"

namespace rhino
{
namespace
{
// "VinylDrums" reads better as "Vinyl Drums". Only a lowercase letter
// followed by an uppercase one is a word break, so "TR808" is left alone.
juce::String spacedFolderName(const juce::String& raw)
{
    juce::String spaced;
    for (int i = 0; i < raw.length(); ++i)
    {
        const auto character = raw[i];
        if (i > 0 && juce::CharacterFunctions::isUpperCase(character)
                  && juce::CharacterFunctions::isLowerCase(raw[i - 1]))
            spaced << ' ';
        spaced << character;
    }
    return spaced;
}

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


juce::String drumKitId(DrumKit kit)
{
    switch (kit)
    {
        case DrumKit::Rhino808: return "Rhino808";
        case DrumKit::House:    return "HouseKit";
        case DrumKit::Break:    return "BreakKit";
        case DrumKit::Minimal:  return "MinimalKit";
        case DrumKit::Clap:     return "ClapKit";
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
    tree.setDefaultOpenness(false);
    tree.setRootItemVisible(false);
    tree.setIndentSize(13);
    tree.setMultiSelectEnabled(false);
    // Rows always fill the width, so the horizontal bar has nothing to scroll.
    tree.getViewport()->setScrollBarsShown(true, false);

    // Grouped by what a row is, then by family inside that, the way Live's
    // library separates Drums from Instruments and both from Clips.
    // Grouped by what a row is, then by family inside that, the way Live's
    // library separates Drums from Instruments and both from Clips.
    //
    // Devices are not listed here: they come from the catalog, so a device
    // added to src/devices appears in the browser with no change to this file.
    items = {
        {"Patterns", "Synth", "Warm pulse", "Soft one-bar 4OSC chord pulse", Session::PatternPreset::WarmPulse},
        {"Patterns", "Synth", "Acid steps", "Tight 16-step synth riff", Session::PatternPreset::AcidSteps},
        {"Patterns", "Synth", "Arp run", "Held chord made for Rhino Arp", Session::PatternPreset::ArpRun},
        {"Patterns", "Synth", "Chord pad", "Soft sustaining 4OSC chord synth", Session::PatternPreset::ChordPad},
        {"Patterns", "Bass", "Sub bass", "Clean mono low-end bass line", Session::PatternPreset::SubBass},
        {"Patterns", "Bass", "Reese bass", "Wide detuned electronic bass", Session::PatternPreset::ReeseBass},
        {"Patterns", "Bass", "Wave bass", "Rhino Wave rounded low pulse", Session::PatternPreset::WaveBass},
        {"Patterns", "Lead", "Siren lead", "Rising and falling emergency lead", Session::PatternPreset::SirenLead},
        {"Patterns", "Lead", "Wave pluck", "Rhino Wave bright moving pluck", Session::PatternPreset::WavePluck},
        {"Patterns", "Pad", "Wave pad", "Rhino Wave wide glassy chords", Session::PatternPreset::WavePad},
        {"Patterns", "Drums", "House kit", "Four-on-floor kick, backbeat, hats", Session::PatternPreset::HouseKit},
        {"Patterns", "Drums", "Break kit", "Syncopated kick/snare/hats groove", Session::PatternPreset::BreakKit},
        {"Patterns", "Drums", "Minimal kit", "Sparse kick/snare/hats sketch", Session::PatternPreset::MinimalKit},
        {"Patterns", "Drums", "Clap kit", "Kick, clap backbeat, tight hats", Session::PatternPreset::ClapKit},
        {"Instruments", "Drum Rack", "Rhino 808", "TR-808 kit: kick, snare, toms, hats", std::nullopt, {}, std::nullopt, {}, DrumKit::Rhino808},
        {"Instruments", "Drum Rack", "House Kit", "Deep kick, tight hats, for the House pattern", std::nullopt, {}, std::nullopt, {}, DrumKit::House},
        {"Instruments", "Drum Rack", "Break Kit", "Snappy snare, bright hats, for the Break pattern", std::nullopt, {}, std::nullopt, {}, DrumKit::Break},
        {"Instruments", "Drum Rack", "Minimal Kit", "Short, quiet pads, for the Minimal pattern", std::nullopt, {}, std::nullopt, {}, DrumKit::Minimal},
        {"Instruments", "Drum Rack", "Clap Kit", "Clap on the backbeat, for the Clap pattern", std::nullopt, {}, std::nullopt, {}, DrumKit::Clap},
        {"Samples", "Built-in", "Whistle", "Built-in audio sample", std::nullopt, {}, Session::BuiltInSample::Whistle},
        {"Samples", "Built-in", "Siren", "Built-in audio sample", std::nullopt, {}, Session::BuiltInSample::Siren},
    };

    // The sample library on disk. Factory content today; a user root will sit
    // beside it, which is why the pack name is carried on every row rather
    // than assumed.
    for (const auto& sample : ContentLibrary::samples())
    {
        const auto pack = spacedFolderName(sample.pack);
        const auto folder = sample.group.isEmpty() ? pack : pack + " / " + spacedFolderName(sample.group);
        items.push_back({"Samples", folder, sample.name, pack + " one-shot",
                         std::nullopt, {}, std::nullopt, sample.file});
    }

    // Instruments, Audio FX and MIDI FX, in catalog order.
    for (const auto& device : DeviceCatalog::all())
    {
        if (!device.browsable)
            continue;
        const auto section = device.kind == DeviceKind::Instrument ? "Instruments"
            : device.kind == DeviceKind::MidiEffect ? "MIDI FX" : "Audio FX";
        items.push_back({section, device.category, DeviceCatalog::labelFor(device),
                         device.description, std::nullopt, device.id});
    }

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
    if (item.sample || item.file != juce::File()) return juce::Colour(0xffe09a70);
    if (item.preset) return juce::Colour(0xffc6d58c);
    if (const auto* device = DeviceCatalog::byId(item.deviceId))
    {
        if (device->kind == DeviceKind::AudioEffect) return juce::Colour(0xffffb15f);
        if (device->kind == DeviceKind::MidiEffect)  return juce::Colour(0xffbda4ff);
        return juce::Colour(0xff8cc5d2);
    }
    if (item.drumKit) return juce::Colour(0xff8cc5d2);
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
    // Folders start closed, so a section opens as a short list of folders. A
    // search is the exception: its hits are the point, so they are shown.
    for (const auto& [name, node] : folders)
        node->setOpen(searching);
    if (openness != nullptr && !searching)
        tree.restoreOpennessState(*openness, false);
    if (restored != nullptr && restored->getParentItem() != nullptr && restored->getParentItem()->isOpen())
        restored->setSelected(true, true);
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
        return "rhino-browser:preset:" + presetId(*item.preset);
    // The kind still appears in the description because each drop target
    // accepts only some of them; the catalog is what decides which it is.
    if (const auto* device = DeviceCatalog::byId(item.deviceId))
    {
        const auto kind = device->kind == DeviceKind::Instrument ? "instrument"
            : device->kind == DeviceKind::MidiEffect ? "midi-effect" : "effect";
        return "rhino-browser:" + juce::String(kind) + ":" + device->id;
    }
    if (item.file != juce::File())
        return "rhino-browser:file:" + item.file.getFullPathName();
    if (item.sample)
        return "rhino-browser:sample:" + sampleId(*item.sample);
    if (item.drumKit)
        return "rhino-browser:drumkit:" + drumKitId(*item.drumKit);
    return "rhino-browser:info:" + item.name;
}

// A document can hold a single track, so no fixed index is safe: an unanswered
// or stale target falls back to the first track rather than the main row.
int BrowserPanel::selectedTargetTrack() const
{
    const auto track = targetTrack ? targetTrack() : 0;
    return juce::isPositiveAndBelow(track, session.trackCount()) ? track : 0;
}

void BrowserPanel::applyItem(const Item& item)
{
    if (item.preset)
    {
        session.applyPatternPreset(*item.preset);
        if (status) status("Loaded " + item.name);
    }
    else if (item.deviceId.isNotEmpty())
    {
        const auto track = selectedTargetTrack();
        const auto result = session.addDevice(item.deviceId, track);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(track)
                                          : result.getErrorMessage());
    }
    else if (item.drumKit)
    {
        const auto track = selectedTargetTrack();
        const auto result = session.addDrumKit(*item.drumKit, track);
        if (status) status(result.wasOk() ? "Added " + item.name + " to " + session.trackName(track) : result.getErrorMessage());
    }
    else if (item.file != juce::File())
    {
        const auto result = session.importAudio(item.file);
        if (status) status(result.wasOk() ? "Added " + item.name : result.getErrorMessage());
    }
    else if (item.sample)
    {
        const auto result = session.importBuiltInSample(*item.sample);
        if (status) status(result.wasOk() ? "Added " + item.name : result.getErrorMessage());
    }
    else if (status)
    {
        status(item.name + " is already part of this starter session.");
    }
}

}
