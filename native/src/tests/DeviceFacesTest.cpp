#include "model/Session.h"
#include "ui/rack/DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include <source_location>
#include <stdexcept>

// Every device the browser offers, on a track of its own, shown on the face the
// rack would give it. A face paints rather than composes, so a render is the
// only coverage its drawing has; its controls are checked to land on the face.
// RHINO_DEVICE_SNAPSHOTS names a folder that gets <id>.png for each.
namespace rhino
{
namespace
{
void require(bool valid, const juce::String& what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(("Device faces: " + what + " (line " + juce::String(static_cast<int>(location.line()))
                                  + ")").toStdString());
}
}

void checkDeviceFaces(Session& session)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("RHINO_DEVICE_SNAPSHOTS", {});
    auto checked = 0;
    for (const auto& descriptor : DeviceCatalog::all())
    {
        // An external plugin is whatever this machine has built, and loading
        // one is a scan; the faces under test are Rhino's own.
        if (!descriptor.browsable || descriptor.external)
            continue;
        const auto type = descriptor.kind == DeviceKind::AudioEffect ? Session::TrackType::audio
                                                                     : Session::TrackType::midi;
        auto track = -1;
        require(session.addTrack(type, -1, &track).wasOk(), descriptor.id + ": a track to put it on");
        require(session.addDevice(descriptor.id, track).wasOk(), descriptor.id + ": the session adds it");
        std::optional<Session::DeviceSlot> slot;
        for (const auto& candidate : session.deviceSlots(track))
            if (candidate.deviceId == descriptor.id)
                slot = candidate;
        require(slot.has_value(), descriptor.id + ": the rack shows it");
        // The first two controls follow a lane, so the picture shows where an
        // A sits on a chooser, a switch or a knob; the rest show none.
        const auto controls = session.deviceParameters(track, slot->pluginIndex);
        for (int parameter = 0; parameter < std::min(2, static_cast<int>(controls.size())); ++parameter)
        {
            const auto& control = controls[static_cast<size_t>(parameter)];
            require(session.setTrackAutomationPoints({ track, slot->pluginIndex, parameter },
                                                     { { 0.0, control.minimum }, { 4.0, control.maximum } }).wasOk(),
                    descriptor.id + ": a lane goes on a control");
        }

        auto panel = std::make_unique<DeviceEditorPanel>(session);
        panel->setTarget(track, *slot, true);
        panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);
        std::vector<juce::Component*> shown;
        for (auto* child : panel->getChildren())
            if (child->isVisible())
                shown.push_back(child);
        for (auto* child : shown)
            require(panel->getLocalBounds().contains(child->getBounds()), descriptor.id + ": every control is on the face");
        // Nothing stands on anything else, but an A on its control's corner
        // and a reading laid over its own bar.
        const auto isBadge = [] (juce::Component* child)
        {
            const auto* button = dynamic_cast<juce::TextButton*>(child);
            return button != nullptr && button->getButtonText() == "A";
        };
        for (size_t a = 0; a < shown.size(); ++a)
            for (size_t b = a + 1; b < shown.size(); ++b)
            {
                const auto first = shown[a]->getBounds(), second = shown[b]->getBounds();
                require(isBadge(shown[a]) || isBadge(shown[b]) || first == second || !first.intersects(second),
                        descriptor.id + ": its controls stand clear of each other");
            }
        Theme theme;
        panel->setLookAndFeel(&theme);
        const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
        panel->setLookAndFeel(nullptr);
        require(snapshot.getWidth() == panel->getWidth(), descriptor.id + ": the face paints");
        if (folder.isNotEmpty())
        {
            const auto file = juce::File(folder).getChildFile(descriptor.id + ".png");
            file.deleteFile();
            auto stream = file.createOutputStream();
            require(stream != nullptr && juce::PNGImageFormat().writeImageToStream(snapshot, *stream),
                    descriptor.id + ": the snapshot is writable");
        }
        // Left in the edit, every device keeps running in the live graph, and
        // the DJ checks after this one slowed past the runner's limit.
        panel.reset();
        require(session.removeAudioTrack(track).wasOk(), descriptor.id + ": its track comes off again");
        ++checked;
    }
    juce::Logger::writeToLog("Rhino: " + juce::String(checked) + " device faces painted, every control on its face and clear of the others");
}
}
