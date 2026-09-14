#pragma once
#include "Session.h"
#include <array>
#include <optional>
#include <vector>

namespace theta
{
class BrowserPanel final : public juce::Component,
                           private juce::ListBoxModel
{
public:
    explicit BrowserPanel(Session&);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusSearch();
    std::function<void(juce::String)> status;

private:
    struct Item
    {
        juce::String category;
        juce::String name;
        juce::String detail;
        std::optional<Session::PatternPreset> preset;
        std::optional<Session::AudioEffect> effect;
        std::optional<Session::Instrument> instrument;
        std::optional<Session::MidiEffect> midiEffect;
        std::optional<Session::BuiltInSample> sample;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;
    bool mayDragToExternalWindows() const override { return false; }
    void rebuildRows();
    void applyRow(int row);
    juce::String dragDescriptionFor(const Item&) const;

    Session& session;
    juce::Label title, categoriesTitle, soundsTitle;
    juce::TextButton apply {"Apply"};
    juce::TextEditor search;
    juce::ListBox list {"Browser", this};
    juce::String selectedCategory = "Sounds";
    std::vector<Item> items;
    std::vector<int> rows;
    std::array<juce::TextButton, 5> categories;
};
}
