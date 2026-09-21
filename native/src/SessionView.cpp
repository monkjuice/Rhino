#include "SessionView.h"
#include <algorithm>

// Session view lifecycle, layout and geometry.

namespace rhino
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
    addTrackButton.setButtonText("+ Track");
    addTrackButton.setTooltip("Add a track: pick audio or MIDI");
    // The same choice the arrangement's + button offers, because the two views
    // share one set of tracks and a track made here is the same track there.
    addTrackButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("NEW TRACK");
        menu.addItem(1, "MIDI Track");
        menu.addItem(2, "Audio Track");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addTrackButton),
            [safe = juce::Component::SafePointer<SessionView>(this)](int choice)
            {
                if (safe == nullptr || choice == 0) return;
                const auto type = choice == 2 ? Session::TrackType::audio : Session::TrackType::midi;
                Session::setLastAddedTrackType(type);
                const auto result = safe->session.addTrack(type);
                if (result.failed() && safe->status) safe->status(result.getErrorMessage());
            });
    };
    mixerButton.setButtonText("Mixer");
    mixerButton.setTooltip("Show or hide the mixer strip");
    mixerButton.setClickingTogglesState(true);
    mixerButton.setToggleState(mixerOpen, juce::dontSendNotification);
    mixerButton.onClick = [this] { setMixerVisible(mixerButton.getToggleState()); };
    mainLabel.setText("MAIN", juce::dontSendNotification);
    mainLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8a969f));
    mainLabel.setFont(juce::FontOptions(11.0f));
    mainVolume.setSliderStyle(juce::Slider::LinearBar);
    mainVolume.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    mainVolume.setRange(Session::minimumVolumeDb, Session::maximumVolumeDb, 0.1);
    mainVolume.setTextValueSuffix(" dB");
    mainVolume.setDoubleClickReturnValue(true, 0.0);
    mainVolume.setTooltip("Main output volume");
    mainVolume.onDragStart = [this] { session.beginMasterVolumeGesture(); };
    mainVolume.onDragEnd = [this] { session.endMasterVolumeGesture(); };
    mainVolume.onValueChange = [this] { session.setMasterVolumeDb(static_cast<float>(mainVolume.getValue())); };
    for (auto* button : std::initializer_list<juce::TextButton*>{&stopAllButton, &addSceneButton,
                                                                &addTrackButton, &mixerButton})
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b31));
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff38505b));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaeb8c1));
        button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffdce5ea));
    }
    sceneScrollBar.setAutoHide(false);
    sceneScrollBar.addListener(this);
    for (auto* component : std::initializer_list<juce::Component*>{
             &stopAllButton, &addSceneButton, &addTrackButton, &mixerButton, &quantiseBox,
             &sceneScrollBar, &mainVolume, &mainLabel})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    session.listeners.add(this);
    if (auto* watcher = session.sceneWatcher())
        watcher->addListener(this);
    syncTrackControls();
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
    mixerButton.setBounds(276, 4, 58, 22);
    addTrackButton.setBounds(340, 4, 66, 22);
    addSceneButton.setBounds(static_cast<int>(sceneColumnX()) + 8, 4, 72, 22);
    sceneScrollBar.setBounds(getWidth() - static_cast<int>(scrollBarWidth), static_cast<int>(toolbarHeight + trackHeaderHeight),
                             static_cast<int>(scrollBarWidth),
                             std::max(0, static_cast<int>(gridBottom() - toolbarHeight - trackHeaderHeight)));
    mainLabel.setVisible(mixerOpen);
    mainVolume.setVisible(mixerOpen);
    if (mixerOpen)
    {
        const auto x = static_cast<int>(sceneColumnX()) + 8;
        const auto width = static_cast<int>(sceneColumnWidth) - 16;
        mainLabel.setBounds(x, static_cast<int>(mixerTop()) + 4, width, 16);
        mainVolume.setBounds(x, static_cast<int>(mixerTop()) + 22, width, 22);
    }
    layOutTrackControls();
    updateScroll();
}

// The mixer occupies a fixed strip under the grid, above the stop row, and can
// be hidden the way Live's View menu hides its Mixer section.
float SessionView::mixerHeight() const
{
    return mixerOpen ? mixerStripHeight : 0.0f;
}

float SessionView::mixerTop() const
{
    return gridBottom();
}

void SessionView::setMixerVisible(bool visible)
{
    if (mixerOpen == visible) return;
    mixerOpen = visible;
    mixerButton.setToggleState(visible, juce::dontSendNotification);
    resized();
    repaint();
}

void SessionView::syncTrackControls()
{
    const auto count = session.trackCount();
    while (static_cast<int>(mute.size()) < count)
    {
        const auto track = static_cast<int>(mute.size());
        auto muteButton = std::make_unique<juce::TextButton>("M");
        auto soloButton = std::make_unique<juce::TextButton>("S");
        muteButton->setTooltip("Mute track");
        soloButton->setTooltip("Solo track");
        muteButton->onClick = [this, track] { session.toggleTrackMute(track); };
        soloButton->onClick = [this, track] { session.toggleTrackSolo(track); };
        muteButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff97634c));
        soloButton->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff657440));
        auto volumeSlider = std::make_unique<juce::Slider>();
        volumeSlider->setSliderStyle(juce::Slider::LinearBar);
        volumeSlider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        volumeSlider->setRange(Session::minimumVolumeDb, Session::maximumVolumeDb, 0.1);
        volumeSlider->setTextValueSuffix(" dB");
        volumeSlider->setDoubleClickReturnValue(true, 0.0);
        volumeSlider->setTooltip("Track volume");
        volumeSlider->onDragStart = [this, track] { session.beginTrackVolumeGesture(track); };
        volumeSlider->onDragEnd = [this, track] { session.endTrackVolumeGesture(track); };
        volumeSlider->onValueChange = [this, track, slider = volumeSlider.get()]
        {
            session.setTrackVolumeDb(track, static_cast<float>(slider->getValue()));
        };
        auto panSlider = std::make_unique<juce::Slider>();
        panSlider->setSliderStyle(juce::Slider::LinearBar);
        panSlider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        panSlider->setRange(-1.0, 1.0, 0.01);
        panSlider->setDoubleClickReturnValue(true, 0.0);
        panSlider->setTooltip("Track pan");
        panSlider->onDragStart = [this, track] { session.beginTrackPanGesture(track); };
        panSlider->onDragEnd = [this, track] { session.endTrackPanGesture(track); };
        panSlider->onValueChange = [this, track, slider = panSlider.get()]
        {
            session.setTrackPan(track, static_cast<float>(slider->getValue()));
        };
        addAndMakeVisible(*muteButton);
        addAndMakeVisible(*soloButton);
        addAndMakeVisible(*volumeSlider);
        addAndMakeVisible(*panSlider);
        mute.push_back(std::move(muteButton));
        solo.push_back(std::move(soloButton));
        volume.push_back(std::move(volumeSlider));
        pan.push_back(std::move(panSlider));
    }
    while (static_cast<int>(mute.size()) > count)
    {
        mute.pop_back();
        solo.pop_back();
        volume.pop_back();
        pan.pop_back();
    }
    for (int track = 0; track < count; ++track)
    {
        const auto mixer = session.trackMixer(track);
        const auto index = static_cast<size_t>(track);
        mute[index]->setToggleState(mixer.muted, juce::dontSendNotification);
        solo[index]->setToggleState(mixer.soloed, juce::dontSendNotification);
        if (!volume[index]->isMouseButtonDown())
            volume[index]->setValue(mixer.volumeDb, juce::dontSendNotification);
        if (!pan[index]->isMouseButtonDown())
            pan[index]->setValue(mixer.pan, juce::dontSendNotification);
    }
    if (!mainVolume.isMouseButtonDown())
        mainVolume.setValue(session.masterVolumeDb(), juce::dontSendNotification);
}

void SessionView::layOutTrackControls()
{
    const auto top = static_cast<int>(mixerTop());
    const auto rightEdge = sceneColumnX();
    for (size_t index = 0; index < mute.size(); ++index)
    {
        const auto column = columnX(static_cast<int>(index));
        const auto onScreen = mixerOpen && column < rightEdge && column + columnWidth > 0.0f;
        mute[index]->setVisible(onScreen);
        solo[index]->setVisible(onScreen);
        volume[index]->setVisible(onScreen);
        pan[index]->setVisible(onScreen);
        if (!onScreen) continue;
        const auto x = static_cast<int>(column) + 4;
        const auto width = static_cast<int>(columnWidth) - 8;
        volume[index]->setBounds(x, top + 4, width, 20);
        pan[index]->setBounds(x, top + 26, width, 18);
        mute[index]->setBounds(x, top + 48, width / 2 - 2, 20);
        solo[index]->setBounds(x + width / 2 + 2, top + 48, width / 2 - 2, 20);
    }
}

float SessionView::sceneColumnX() const
{
    return static_cast<float>(getWidth()) - scrollBarWidth - sceneColumnWidth;
}

// The column is stacked: clip grid, then the mixer strip, then the stop row.
float SessionView::gridBottom() const
{
    return stopRowTop() - mixerHeight();
}

float SessionView::stopRowTop() const
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
    return {columnX(track), stopRowTop(), columnWidth, stopRowHeight};
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
    syncTrackControls();
    layOutTrackControls();
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
    syncTrackControls();
    layOutTrackControls();
    refreshControls();
    updateScroll();
    repaint();
}

}
