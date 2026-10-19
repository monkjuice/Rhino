#pragma once

#include "ForgePresetBrowser.h"

// Private to the browser's own translation units: ForgePresetBrowser.cpp holds
// what it lists and does, ForgePresetBrowserViews.cpp what it draws and how a
// hand moves through it. Nothing else should include this.
namespace rhino::forge
{
namespace browserLook
{
inline constexpr int rowHeight = 24;
inline constexpr int headerHeight = 26;
inline constexpr int topBarHeight = 36;
inline constexpr int gap = 8;
inline constexpr int indent = 14;
inline constexpr int scrollRail = 6;

// A five-pointed star, the favourite mark. The panel's typefaces are Latin
// subsets with no star in them, so it is drawn rather than set.
inline juce::Path star(juce::Rectangle<float> box)
{
    juce::Path path;
    const auto centre = box.getCentre();
    const auto outer = juce::jmin(box.getWidth(), box.getHeight()) * 0.5f;
    const auto inner = outer * 0.45f;
    for (int i = 0; i < 10; ++i)
    {
        const auto radius = i % 2 == 0 ? outer : inner;
        const auto angle = juce::MathConstants<float>::pi * (static_cast<float>(i) / 5.0f) - juce::MathConstants<float>::halfPi;
        const auto point = centre + juce::Point<float>(std::cos(angle), std::sin(angle)) * radius;
        if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
    }
    path.closeSubPath();
    return path;
}

inline void dressField(juce::TextEditor& field, const juce::String& hint, bool multiLine = false)
{
    field.setMultiLine(multiLine, true);
    field.setReturnKeyStartsNewLine(multiLine);
    field.setScrollbarsShown(multiLine);
    field.setFont(ui::panelFont(ui::Face::label, 12.5f));
    field.setIndents(6, multiLine ? 4 : 5);
    field.setTextToShowWhenEmpty(hint, ui::mutedText.withAlpha(0.6f));
    field.setColour(juce::TextEditor::backgroundColourId, ui::fieldFloor);
    field.setColour(juce::TextEditor::textColourId, ui::text);
    field.setColour(juce::TextEditor::highlightColourId, ui::electricBlue.withAlpha(0.35f));
    field.setColour(juce::TextEditor::highlightedTextColourId, ui::text);
    field.setColour(juce::TextEditor::outlineColourId, ui::line);
    field.setColour(juce::TextEditor::focusedOutlineColourId, ui::electricBlue);
    field.setColour(juce::CaretComponent::caretColourId, ui::electricBlue);
}

inline void drawScrollRail(juce::Graphics& g, juce::Rectangle<int> rail, int first, int visible, int total)
{
    if (visible >= total || rail.isEmpty() || total <= 0) return;
    const auto thumbHeight = juce::jmax(18, rail.getHeight() * visible / total);
    const auto travel = rail.getHeight() - thumbHeight;
    const auto top = rail.getY() + travel * first / juce::jmax(1, total - visible);
    g.setColour(ui::line.withAlpha(0.55f));
    g.fillRoundedRectangle(rail.toFloat(), 2.0f);
    g.setColour(ui::signalViolet.withAlpha(0.8f));
    g.fillRoundedRectangle(rail.withY(top).withHeight(thumbHeight).toFloat(), 2.0f);
}
}

// The folders, painted as rows: one component rather than one per row, so a
// library with hundreds of folders costs one paint.
class PresetBrowser::FolderList final : public juce::Component
{
public:
    explicit FolderList(PresetBrowser& owner) : browser(owner) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int rowAt(juce::Point<int>) const;
    juce::Rectangle<int> rowBounds(int row) const;
    void reveal(int row);
    void clampScroll();
    int visibleRows() const;
    // The rows actually drawn: everything not folded away under a collapsed
    // parent, in order.
    std::vector<int> drawn;
    void rebuildDrawn();

private:
    PresetBrowser& browser;
    int first = 0;
    int hover = -1;
    float wheel = 0.0f;
};

// The presets of the chosen folder: a header that sorts, and the rows.
class PresetBrowser::PresetTable final : public juce::Component
{
public:
    explicit PresetTable(PresetBrowser& owner) : browser(owner) { setWantsKeyboardFocus(true); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int rowAt(juce::Point<int>) const;
    juce::Rectangle<int> rowBounds(int shownIndex) const;
    void reveal(int shownIndex);
    void clampScroll();
    int visibleRows() const;
    juce::Rectangle<int> columnBounds(Column, juce::Rectangle<int> row) const;

private:
    PresetBrowser& browser;
    int first = 0;
    int hover = -1;
    float wheel = 0.0f;
    juce::File pressed;
    bool dragStarted = false;
};

// What the loaded patch says about itself, and what its conversion lost.
// Painted, apart from the notes, which are a read-only field so a long list of
// warnings scrolls rather than running off the card.
class PresetBrowser::InfoCard final : public juce::Component
{
public:
    explicit InfoCard(PresetBrowser& owner);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    juce::TextEditor notes;

private:
    PresetBrowser& browser;
    PresetInfo shownInfo;
};
}
