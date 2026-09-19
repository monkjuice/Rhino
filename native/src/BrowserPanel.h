#pragma once
#include "Session.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace rhino
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
        // A catalog id, empty when the row is not a device. One field covers
        // instruments, audio FX and MIDI FX, because the catalog knows which
        // of those a given id is.
        juce::String deviceId;
        std::optional<Session::BuiltInSample> sample;
        // A file in the content library. Set for library samples, empty for
        // everything else. Dragged and applied by path.
        juce::File file;
        std::optional<DrumKit> drumKit;
    };

    // What the browser offers, in the order it offers it. Exposed so a test
    // can check that the device catalog really is what fills the library.
    const std::vector<Item>& libraryItems() const { return items; }

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
