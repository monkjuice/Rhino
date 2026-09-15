#pragma once
#include "Session.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace theta
{
// The browser is a category rail over a folder tree, the shape Live uses. The
// rail picks a library section; the tree below shows that section's folders and
// the rows inside them. Rows are dragged onto tracks or applied in place.
class BrowserPanel final : public juce::Component,
                           private juce::ListBoxModel
{
public:
    explicit BrowserPanel(Session&);
    ~BrowserPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusSearch();
    std::function<void(juce::String)> status;
    // Double-clicking a browser item acts on the track the user is looking at.
    // A dragged item names its own target; a double-click has to be told.
    std::function<int()> targetTrack;

    // One row of the library. `folder` is the subfolder inside `category`; an
    // empty folder puts the row at the top level of that category.
    struct Item
    {
        juce::String category;
        juce::String folder;
        juce::String name;
        juce::String detail;
        std::optional<Session::PatternPreset> preset;
        std::optional<Session::AudioEffect> effect;
        std::optional<Session::Instrument> instrument;
        std::optional<Session::MidiEffect> midiEffect;
        std::optional<Session::BuiltInSample> sample;
        std::optional<DrumDevice::Kit> drumKit;
    };

private:
    class FolderNode;
    class ItemNode;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void rebuildTree();
    void applyItem(const Item&);
    int selectedTargetTrack() const;
    void reportSelection(const Item&);
    juce::String dragDescriptionFor(const Item&) const;
    juce::Colour colourFor(const Item&) const;
    const Item* selectedItem() const;

    Session& session;
    juce::Label title;
    juce::TextEditor search;
    juce::TextButton apply {"Add"};
    juce::ListBox categoryList {"Library", this};
    juce::TreeView tree;
    std::unique_ptr<FolderNode> root;
    std::vector<Item> items;
    std::array<juce::String, 5> categories {"Instruments", "Patterns", "Samples", "Audio FX", "MIDI FX"};
    int selectedCategory = 0;
    int lastLayoutWidth = 0;
    juce::String selectionToRestore;
    static constexpr int categoryRowHeight = 22;
};
}
