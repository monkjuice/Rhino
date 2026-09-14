#include "SessionView.h"
#include <algorithm>

// Session view lifecycle, layout and geometry.

namespace theta
{
namespace
{
// Offered in the launch quantisation box, coarse to fine. Ids are the index
// into this table plus one, because a ComboBox id of 0 means "nothing chosen".
struct QuantiseOption
{
    te::LaunchQType type;
    const char* name;
};
constexpr QuantiseOption quantiseOptions[] {
    {te::LaunchQType::none, "None"},
    {te::LaunchQType::eightBars, "8 Bars"},
    {te::LaunchQType::fourBars, "4 Bars"},
    {te::LaunchQType::twoBars, "2 Bars"},
    {te::LaunchQType::bar, "1 Bar"},
    {te::LaunchQType::half, "1/2"},
    {te::LaunchQType::quarter, "1/4"},
    {te::LaunchQType::eighth, "1/8"},
    {te::LaunchQType::sixteenth, "1/16"},
};
constexpr int quantiseOptionCount = static_cast<int>(std::size(quantiseOptions));
}

SessionView::SessionView(Session& s) : session(s)
{
    setOpaque(true);
    setWantsKeyboardFocus(false);
    stopAllButton.setButtonText(juce::String(L"■") + " Stop all");
    stopAllButton.setTooltip("Stop every playing clip");
    stopAllButton.onClick = [this] { session.stopAllSlots(); repaint(); };
    addSceneButton.setButtonText("+ Scene");
    addSceneButton.setTooltip("Add a scene row");
    addSceneButton.onClick = [this]
    {
        const auto result = session.addScene();
        if (result.failed() && status) status(result.getErrorMessage());
    };
    for (int i = 0; i < quantiseOptionCount; ++i)
        quantiseBox.addItem(quantiseOptions[i].name, i + 1);
    quantiseBox.setTooltip("Launch quantisation: when a clip actually starts");
    quantiseBox.onChange = [this]
    {
        if (updatingQuantisation) return;
        const auto index = quantiseBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow(index, quantiseOptionCount))
            session.setLaunchQuantisation(quantiseOptions[index].type);
    };
    for (auto* button : std::initializer_list<juce::TextButton*>{&stopAllButton, &addSceneButton})
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b31));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaeb8c1));
    }
    sceneScrollBar.setAutoHide(false);
    sceneScrollBar.addListener(this);
    for (auto* component : std::initializer_list<juce::Component*>{
             &stopAllButton, &addSceneButton, &quantiseBox, &sceneScrollBar})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    session.listeners.add(this);
    if (auto* watcher = session.sceneWatcher())
        watcher->addListener(this);
    refreshControls();
}

SessionView::~SessionView()
{
    if (auto* watcher = session.sceneWatcher())
        watcher->removeListener(this);
    sceneScrollBar.removeListener(this);
    session.removeChangeListener(this);
    session.listeners.remove(this);
}

void SessionView::resized()
{
    stopAllButton.setBounds(8, 4, 88, 22);
    quantiseBox.setBounds(190, 4, 78, 22);
    addSceneButton.setBounds(static_cast<int>(sceneColumnX()) + 8, 4, 72, 22);
    sceneScrollBar.setBounds(getWidth() - static_cast<int>(scrollBarWidth), static_cast<int>(toolbarHeight + trackHeaderHeight),
                             static_cast<int>(scrollBarWidth),
                             std::max(0, static_cast<int>(gridBottom() - toolbarHeight - trackHeaderHeight)));
    updateScroll();
}

float SessionView::sceneColumnX() const
{
    return static_cast<float>(getWidth()) - scrollBarWidth - sceneColumnWidth;
}

float SessionView::gridBottom() const
{
    return static_cast<float>(getHeight()) - stopRowHeight;
}

float SessionView::columnX(int track) const
{
    return static_cast<float>(track) * columnWidth - static_cast<float>(trackScroll);
}

float SessionView::rowY(int scene) const
{
    return toolbarHeight + trackHeaderHeight + static_cast<float>(scene) * slotHeight - static_cast<float>(sceneScroll);
}

juce::Rectangle<float> SessionView::slotBounds(int track, int scene) const
{
    return {columnX(track), rowY(scene), columnWidth, slotHeight};
}

juce::Rectangle<float> SessionView::trackHeaderBounds(int track) const
{
    return {columnX(track), toolbarHeight, columnWidth, trackHeaderHeight};
}

juce::Rectangle<float> SessionView::sceneLaunchBounds(int scene) const
{
    return {sceneColumnX(), rowY(scene), sceneColumnWidth, slotHeight};
}

juce::Rectangle<float> SessionView::trackStopBounds(int track) const
{
    return {columnX(track), gridBottom(), columnWidth, stopRowHeight};
}

juce::Rectangle<float> SessionView::sceneColumnBounds() const
{
    return {sceneColumnX(), toolbarHeight, sceneColumnWidth, static_cast<float>(getHeight()) - toolbarHeight};
}

void SessionView::selectTrack(int track)
{
    if (!juce::isPositiveAndBelow(track, session.trackCount())) return;
    if (selectedTrack == track) return;
    selectedTrack = track;
    if (trackSelected) trackSelected(track);
    repaint();
}

void SessionView::refreshControls()
{
    const juce::ScopedValueSetter<bool> scope(updatingQuantisation, true);
    const auto current = session.launchQuantisation();
    for (int i = 0; i < quantiseOptionCount; ++i)
        if (quantiseOptions[i].type == current)
        {
            quantiseBox.setSelectedId(i + 1, juce::dontSendNotification);
            return;
        }
    // A document saved with a quantisation this box does not list keeps its
    // engine setting; show the nearest coarse entry rather than an empty box.
    quantiseBox.setSelectedId(5, juce::dontSendNotification);
}

void SessionView::updateScroll()
{
    const auto visibleHeight = std::max(0.0f, gridBottom() - toolbarHeight - trackHeaderHeight);
    const auto total = static_cast<double>(session.sceneCount()) * slotHeight;
    sceneScroll = std::clamp(sceneScroll, 0.0, std::max(0.0, total - visibleHeight));
    sceneScrollBar.setRangeLimits(0.0, std::max(static_cast<double>(visibleHeight), total), juce::dontSendNotification);
    sceneScrollBar.setCurrentRange(sceneScroll, visibleHeight, juce::dontSendNotification);
    sceneScrollBar.setVisible(total > visibleHeight + 1.0);
    const auto trackTotal = static_cast<double>(session.trackCount()) * columnWidth;
    trackScroll = std::clamp(trackScroll, 0.0, std::max(0.0, trackTotal - static_cast<double>(sceneColumnX())));
}

void SessionView::scrollBarMoved(juce::ScrollBar* bar, double start)
{
    if (bar != &sceneScrollBar) return;
    sceneScroll = start;
    repaint();
}

void SessionView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    refreshControls();
    updateScroll();
    repaint();
}

void SessionView::slotUpdated(int, int)
{
    // Called by the engine's scene watcher whenever a slot's play or queue
    // state changes. Only the cells' fill and glyph depend on it, so a repaint
    // is enough; nothing here touches the edit.
    repaint();
}

void SessionView::editWillChange()
{
    if (auto* watcher = session.sceneWatcher())
        watcher->removeListener(this);
}

void SessionView::editDidChange()
{
    if (auto* watcher = session.sceneWatcher())
        watcher->addListener(this);
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    sceneScroll = 0.0;
    trackScroll = 0.0;
    refreshControls();
    updateScroll();
    repaint();
}

}
