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
    void focusSearch();
    // Reads the presets on disk again, after one has been saved.
    void refreshPresets();
    std::function<void(juce::String)> status;

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
        // A Drum Rack kit (.rdk), or a drum preset (.rdp), dragged by path.
        juce::File drumKit;
        juce::File drumPreset;
        // A .rnd preset of the device named by deviceId. Its row sits under
        // the device's own, which is dragged for the device at its defaults.
        juce::File devicePreset;
    };

    // What the browser offers, in the order it offers it. Exposed so a test
    // can check that the device catalog really is what fills the library.
    const std::vector<Item>& libraryItems() const { return items; }
    // What dragging a row carries: "rhino-browser:<kind>:<id>" (BrowserIds.h).
    juce::String dragDescriptionFor(const Item&) const;

private:
    class FolderNode;
    class ItemNode;

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void rebuildTree();
    void addPresetItems();
    // The Drums section: the blank Drum Rack first, then the kits, then each
    // kind of drum with its presets before its samples. Read from disk, so a
    // kit or a preset just saved is there the next time it is built.
    void addDrumItems();
    void previewItem(const Item&);
    void reportSelection(const Item&);
    juce::Colour colourFor(const Item&) const;
    const Item* selectedItem() const;

    Session& session;
    juce::Label title;
    juce::TextEditor search;
    juce::ListBox categoryList {"Library", this};
    juce::TreeView tree;
    std::unique_ptr<FolderNode> root;
    std::vector<Item> items;
    // Drums comes before Instruments, as it does in Live: it is the one
    // section that holds every kind of thing -- a device, kits, presets and
    // samples -- for one job.
    std::array<juce::String, 6> categories {"Drums", "Instruments", "Patterns", "Samples", "Audio FX", "MIDI FX"};
    int selectedCategory = 0;
    int lastLayoutWidth = 0;
    juce::String selectionToRestore;
    static constexpr int categoryRowHeight = 22;
};
}
