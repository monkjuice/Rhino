#include "DrumRackWindow.h"
#include "BrowserIds.h"
#include "DrumKitFile.h"
#include "Theme.h"
#include <algorithm>

namespace rhino
{
namespace
{
// The margin between the window's edge and the face.
constexpr int margin = 6;
}

DrumRackView::DrumRackView(Session& s, te::EditItemID device) : session(s), rack(device), face(s)
{
    setOpaque(true);
    face.status = [this] (const juce::String& message) { if (status) status(message); };
    face.presetsChanged = [this] { if (presetsChanged) presetsChanged(); };
    // Nothing here drags the device along a chain, which is what the face's
    // own tooltip offers.
    face.setTooltip({});
    addAndMakeVisible(face);
    follow();
    // The rate the rack follows its lanes at.
    startTimerHz(30);
}

bool DrumRackView::follow()
{
    const auto search = [this] (int onTrack) -> std::optional<Session::DeviceSlot>
    {
        for (const auto& slot : session.deviceSlots(onTrack))
            if (const auto* plugin = session.devicePlugin(onTrack, slot.pluginIndex); plugin != nullptr && plugin->itemID == rack)
                return slot;
        return std::nullopt;
    };
    // Where it was first: nearly every change leaves it there.
    auto found = juce::isPositiveAndNotGreaterThan(track, session.masterTrackIndex()) ? search(track) : std::nullopt;
    for (int candidate = 0; !found.has_value() && candidate <= session.masterTrackIndex(); ++candidate)
        if (candidate != track)
            if ((found = search(candidate)).has_value())
                track = candidate;
    if (!found.has_value())
        return false;
    shown = *found;
    trackName = session.trackName(track);
    face.setTarget(track, shown, false);
    if (auto* window = findParentComponentOfClass<DrumRackWindow>(); window != nullptr && window->getName() != title())
        window->setName(title());
    return true;
}

void DrumRackView::refreshTouched()
{
    const auto touched = session.lastTouchedDeviceParameter();
    if (touched.track == track && touched.slot == shown.pluginIndex)
        face.setTarget(track, shown, false);
}

void DrumRackView::followAutomation()
{
    if (face.followsAutomation())
        face.setTarget(track, shown, false);
}

void DrumRackView::timerCallback()
{
    const auto playing = session.edit->getTransport().isPlaying();
    // Once more as the transport stops, so the knobs settle where the curve
    // left them rather than a frame short of it.
    if (playing || wasPlaying)
        followAutomation();
    wasPlaying = playing;
}

juce::String DrumRackView::title() const
{
    return shown.name + " - " + trackName;
}

void DrumRackView::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
}

void DrumRackView::resized()
{
    face.setBounds(getLocalBounds().reduced(margin));
}

int DrumRackView::padAt(juce::Point<int> position) const
{
    return face.drumPadAt(face.getLocalPoint(this, position));
}

bool DrumRackView::isInterestedInDragSource(const SourceDetails& details)
{
    const auto kind = browserDropKind(details.description.toString());
    return kind == "file" || kind == "drum-preset" || kind == "drumkit";
}

void DrumRackView::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void DrumRackView::itemDragMove(const SourceDetails& details)
{
    // A kit goes to the whole rack, so no one pad lights for it.
    const auto kind = browserDropKind(details.description.toString());
    if (kind == "file" || kind == "drum-preset")
        face.showDrumDropTarget(padAt(details.localPosition));
}

void DrumRackView::itemDragExit(const SourceDetails&)
{
    face.showDrumDropTarget(-1);
}

void DrumRackView::itemDropped(const SourceDetails& details)
{
    face.showDrumDropTarget(-1);
    const auto description = details.description.toString();
    const auto kind = browserDropKind(description);
    if (kind == "drumkit")
    {
        const auto kit = browserDropKitFile(description);
        const auto report = status;
        const auto result = session.loadDrumKit(track, shown.pluginIndex, kit);
        if (report)
            report(result.wasOk() ? "Loaded " + DrumFiles::nameOf(kit) + " on " + session.trackName(track)
                                  : result.getErrorMessage());
        return;
    }
    // A sound lands on the pad under the pointer, or between pads on the
    // selected one.
    if (kind == "file")
        face.dropDrumSounds(padAt(details.localPosition), {browserDropFile(description)}, false);
    else if (kind == "drum-preset")
        face.dropDrumSounds(padAt(details.localPosition), {browserDropDrumPresetFile(description)}, true);
}

bool DrumRackView::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [] (const juce::String& path)
    {
        return isDroppedSoundFile(juce::File(path));
    });
}

void DrumRackView::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void DrumRackView::fileDragMove(const juce::StringArray&, int x, int y)
{
    face.showDrumDropTarget(padAt({x, y}));
}

void DrumRackView::fileDragExit(const juce::StringArray&)
{
    face.showDrumDropTarget(-1);
}

void DrumRackView::filesDropped(const juce::StringArray& files, int x, int y)
{
    face.showDrumDropTarget(-1);
    std::vector<juce::File> sounds;
    for (const auto& path : files)
        if (const juce::File file(path); isDroppedSoundFile(file))
            sounds.push_back(file);
    if (!sounds.empty())
        face.dropDrumSounds(padAt({x, y}), sounds, false);
}

// ---- the window ----------------------------------------------------------------

DrumRackWindow::DrumRackWindow(Session& session, te::EditItemID rack)
    : DocumentWindow("Drum Rack", palette::sideSurface, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new DrumRackView(session, rack), false);
    setName(view().title());
    setResizable(true, false);
    setResizeLimits(minimumWidth, minimumHeight, 16384, 16384);
}

DrumRackView& DrumRackWindow::view()
{
    return *static_cast<DrumRackView*>(getContentComponent());
}

void DrumRackWindow::show(juce::Component& over)
{
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(over.getScreenBounds());
    const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int>(0, 0, 1280, 800);
    setBounds(area.withSizeKeepingCentre(std::max(minimumWidth, area.getWidth() * 85 / 100),
                                         std::max(minimumHeight, area.getHeight() * 85 / 100)));
    setVisible(true);
    setFullScreen(true);
    toFront(true);
}

void DrumRackWindow::closeButtonPressed()
{
    setVisible(false);
    if (onClose) onClose();
}

bool DrumRackWindow::keyPressed(const juce::KeyPress& key)
{
    // A shortcut can close this window -- an undo can take its rack away --
    // so the call runs on a copy, and nothing of the window is touched after.
    if (const auto forward = shortcut; forward != nullptr && forward(key))
        return true;
    return DocumentWindow::keyPressed(key);
}
}
