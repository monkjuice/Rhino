#include "BrowserPanel.h"
#include "BrowserIds.h"
#include "ContentLibrary.h"
#include "Theme.h"
#include <algorithm>
#include <map>

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

juce::String sectionOf(const DeviceDescriptor& device)
{
    return device.kind == DeviceKind::Instrument ? "Instruments"
         : device.kind == DeviceKind::MidiEffect ? "MIDI FX" : "Audio FX";
}

// The arrow beside a row that holds others: a folder, or a device holding its
// presets.
void paintDisclosure(juce::Graphics& g, const juce::Rectangle<float>& area, bool open)
{
    juce::Path arrow;
    const auto centre = area.getCentre();
    constexpr auto size = 3.4f;
    if (open)
        arrow.addTriangle(centre.x - size, centre.y - size * 0.6f,
                          centre.x + size, centre.y - size * 0.6f,
                          centre.x, centre.y + size * 0.9f);
    else
        arrow.addTriangle(centre.x - size * 0.6f, centre.y - size,
                          centre.x - size * 0.6f, centre.y + size,
                          centre.x + size * 0.9f, centre.y);
    g.setColour(palette::textDim);
    g.fillPath(arrow);
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
            g.setColour(palette::hover);
            g.fillRect(0, 0, width, height);
        }
        g.setColour(palette::text);
        g.setFont(uiFont(10.5f));
        drawSnappedText(g, name, {2, 0, width - 6, height}, juce::Justification::centredLeft, true);
    }

    void paintOpenCloseButton(juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool) override
    {
        if (name.isEmpty()) return;
        paintDisclosure(g, area, isOpen());
    }

    BrowserPanel& panel;
    juce::String name;
};

// A single library row. This is what carries the drag payload. A device's row
// holds its presets, the way Live files a device's presets under it.
class BrowserPanel::ItemNode final : public juce::TreeViewItem
{
public:
    ItemNode(BrowserPanel& p, Item i) : panel(p), item(std::move(i)) {}

    bool isPreset() const { return item.devicePreset != juce::File(); }
    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override
    {
        return isPreset() ? "preset:" + item.deviceId + "/" + item.name : "item:" + item.category + "/" + item.name;
    }
    int getItemHeight() const override { return isPreset() ? 22 : 30; }

    void paintOpenCloseButton(juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool) override
    {
        paintDisclosure(g, area, isOpen());
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (isSelected())
        {
            g.setColour(palette::hover);
            g.fillRect(0, 0, width, height);
        }
        // A preset is one line under its device: the device already says what
        // it is.
        if (isPreset())
        {
            g.setColour(panel.colourFor(item).withAlpha(0.7f));
            g.fillRect(4, height / 2 - 2, 4, 4);
            g.setColour(palette::text);
            g.setFont(uiFont(10.0f));
            drawSnappedText(g, item.name, {14, 0, width - 18, height}, juce::Justification::centredLeft, true);
            return;
        }
        g.setColour(panel.colourFor(item));
        g.fillRect(2, height / 2 - 4, 7, 7);
        g.setColour(palette::text);
        g.setFont(uiFont(10.5f));
        drawSnappedText(g, item.name, {15, 1, width - 19, 15}, juce::Justification::centredLeft, true);
        g.setColour(palette::textDim);
        g.setFont(uiFont(8.5f));
        drawSnappedText(g, item.detail, {15, 15, width - 19, 13}, juce::Justification::centredLeft, true);
    }

    void itemSelectionChanged(bool nowSelected) override
    {
        if (nowSelected) panel.reportSelection(item);
    }

    // Auditioning follows the click rather than the selection: the tree is
    // rebuilt on every search and resize, and each rebuild restores the
    // selection, which would otherwise replay the sound each time.
    void itemClicked(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu()) panel.previewItem(item);
    }

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
    title.setColour(juce::Label::textColourId, palette::text);
    title.setFont(uiFont(10.0f));
    search.setTextToShowWhenEmpty("Search", palette::disabled);
    search.onTextChange = [this] { rebuildTree(); };
    search.setColour(juce::TextEditor::backgroundColourId, palette::control);
    search.setColour(juce::TextEditor::outlineColourId, palette::border);
    search.setFont(uiFont(10.0f));

    categoryList.setRowHeight(categoryRowHeight);
    categoryList.setColour(juce::ListBox::backgroundColourId, palette::sideSurface);
    categoryList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    categoryList.setMultipleSelectionEnabled(false);
    categoryList.selectRow(0, juce::dontSendNotification);

    tree.setColour(juce::TreeView::backgroundColourId, palette::sideSurface);
    tree.setColour(juce::TreeView::linesColourId, palette::border);
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
        {"Patterns", "Lead", "Siren lead", "Rising and falling emergency lead", Session::PatternPreset::SirenLead},
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

    // Instruments, Audio FX and MIDI FX, in catalog order, then their presets.
    for (const auto& device : DeviceCatalog::all())
    {
        if (!device.browsable)
            continue;
        items.push_back({sectionOf(device), device.category, DeviceCatalog::labelFor(device),
                         device.description, std::nullopt, device.id});
    }
    addPresetItems();

    for (auto* component : std::initializer_list<juce::Component*>{&title, &search, &categoryList, &tree})
        addAndMakeVisible(component);
    rebuildTree();
}

BrowserPanel::~BrowserPanel()
{
    tree.setRootItem(nullptr);
}

void BrowserPanel::addPresetItems()
{
    for (const auto& preset : ContentLibrary::presets())
    {
        const auto* device = DeviceCatalog::byId(preset.deviceId);
        if (device == nullptr || !device->browsable)
            continue;
        Item item;
        item.category = sectionOf(*device);
        item.folder = device->category;
        item.name = preset.name;
        item.detail = (preset.user ? "Your " : "") + DeviceCatalog::labelFor(*device) + " preset";
        item.deviceId = device->id;
        item.devicePreset = preset.file;
        items.push_back(std::move(item));
    }
}

void BrowserPanel::refreshPresets()
{
    items.erase(std::remove_if(items.begin(), items.end(),
                               [] (const Item& item) { return item.devicePreset != juce::File(); }),
                items.end());
    addPresetItems();
    rebuildTree();
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(palette::border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.0f));
    drawSnappedText(g, "LIBRARY", {10, 62, getWidth() - 20, 14}, juce::Justification::centredLeft, true);
}

void BrowserPanel::resized()
{
    const auto width = getWidth();
    title.setBounds(10, 6, width - 20, 16);
    search.setBounds(8, 26, width - 16, 26);
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

int BrowserPanel::getNumRows()
{
    return static_cast<int>(categories.size());
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (!juce::isPositiveAndBelow(row, static_cast<int>(categories.size()))) return;
    if (selected)
    {
        g.setColour(palette::hover);
        g.fillRect(0, 0, width, height);
        g.setColour(palette::selection);
        g.fillRect(0, 0, 2, height);
    }
    g.setColour(selected ? palette::text : palette::textDim);
    g.setFont(uiFont(10.5f));
    drawSnappedText(g, categories[static_cast<size_t>(row)], {10, 0, width - 14, height},
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
    const auto shown = [&] (const Item& item)
    {
        if (!searching)
            return item.category == category;
        return item.name.toLowerCase().contains(query) || item.detail.toLowerCase().contains(query)
            || item.folder.toLowerCase().contains(query);
    };
    // Each device's row in this tree, which its presets go under.
    std::map<juce::String, ItemNode*> deviceRows;
    const auto addRow = [&] (const Item& item)
    {
        const auto groupName = searching ? item.category + " / " + item.folder : item.folder;
        auto* node = new ItemNode(*this, item);
        folderFor(groupName.trimCharactersAtEnd(" /"))->addSubItem(node);
        if (item.deviceId.isNotEmpty())
            deviceRows[item.deviceId] = node;
        return node;
    };
    for (const auto& item : items)
    {
        if (item.devicePreset != juce::File() || !shown(item))
            continue;
        auto* node = addRow(item);
        if (item.name == selectionToRestore)
            restored = node;
    }
    // A preset a search finds brings out its device's row too, open, even
    // when the device itself did not match.
    for (const auto& item : items)
    {
        if (item.devicePreset == juce::File() || !shown(item))
            continue;
        auto row = deviceRows.find(item.deviceId);
        if (row == deviceRows.end())
        {
            const auto device = std::find_if(items.begin(), items.end(), [&item] (const Item& candidate)
            {
                return candidate.deviceId == item.deviceId && candidate.devicePreset == juce::File();
            });
            if (device == items.end())
                continue;
            addRow(*device);
            row = deviceRows.find(item.deviceId);
        }
        auto* node = new ItemNode(*this, item);
        row->second->addSubItem(node);
        if (searching)
            row->second->setOpen(true);
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
    if (restored != nullptr && restored->getParentItem() != nullptr && restored->getParentItem()->isOpen()
        && (restored->getParentItem()->getParentItem() == nullptr || restored->getParentItem()->getParentItem()->isOpen()))
        restored->setSelected(true, true);
}

void BrowserPanel::reportSelection(const Item& item)
{
    if (status) status(item.name + " - " + item.detail);
}

// Only rows that are a sound have one to play. An instrument or an effect is
// a thing to put on a track, and there is nothing to hear until it is there.
void BrowserPanel::previewItem(const Item& item)
{
    const auto result = item.sample      ? session.previewBuiltInSample(*item.sample)
                      : item.file != juce::File() ? session.previewSample(item.file)
                                        : juce::Result::ok();
    if (result.failed() && status) status(result.getErrorMessage());
}

juce::String BrowserPanel::dragDescriptionFor(const Item& item) const
{
    if (item.preset)
        return "rhino-browser:preset:" + presetId(*item.preset);
    if (item.devicePreset != juce::File())
        return "rhino-browser:device-preset:" + item.devicePreset.getFullPathName();
    // The kind still appears in the description because each drop target
    // accepts only some of them; the catalog is what decides which it is.
    if (const auto* device = DeviceCatalog::byId(item.deviceId))
        return "rhino-browser:" + deviceDropKind(*device) + ":" + device->id;
    if (item.file != juce::File())
        return "rhino-browser:file:" + item.file.getFullPathName();
    if (item.sample)
        return "rhino-browser:sample:" + sampleId(*item.sample);
    if (item.drumKit)
        return "rhino-browser:drumkit:" + drumKitId(*item.drumKit);
    return "rhino-browser:info:" + item.name;
}

}
