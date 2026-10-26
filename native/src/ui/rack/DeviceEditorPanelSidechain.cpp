#include "DeviceEditorPanel.h"
#include "ui/shared/Theme.h"

// The Sidechain strip: which track's audio drives a device, for every device
// that takes a sidechain and has no face of its own - an external plugin with
// a sidechain input (Rhino Duck's Audio trigger), Tracktion's compressor, a
// device on the SDK that declares one. It stands at the left of the face, as
// Live's plug-in devices carry theirs, and holds what the engine can route:
// a source track, tapped after its fader and before its mute. Live's Gain,
// Mix and tap point are not here because Tracktion's sidechain has none of
// them; a device with its own sidechain gain (the compressor's) shows it
// among its knobs.
//
// Drawn and hit-tested by rectangle, as the Vocoder's chooser is, because the
// rack rebuilds its panels whenever the chain changes. The menu is shared with
// the Vocoder's face.
namespace rhino
{
void DeviceEditorPanel::layoutSidechain()
{
    sidechainArea = sidechainSource = {};
    if (!sidechained)
        return;
    sidechainArea = contentArea.removeFromLeft(sidechainStripWidth);
    contentArea.removeFromLeft(sidechainGap);
    sidechainSource = {sidechainArea.getX() + 6, sidechainArea.getY() + 33, sidechainArea.getWidth() - 12, 22};
}

void DeviceEditorPanel::paintSidechain(juce::Graphics& g)
{
    const auto source = session.deviceSidechainSource(track, pluginSlot);
    const auto routed = source >= 0;
    const auto area = sidechainArea.toFloat();
    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);

    const auto left = sidechainArea.getX() + 6;
    const auto width = sidechainArea.getWidth() - 12;
    g.setColour(routed ? faceAccent() : palette::textDim);
    g.setFont(uiFontBold(8.5f));
    drawSnappedText(g, "SIDECHAIN", {left, sidechainArea.getY() + 5, width, 12}, juce::Justification::centredLeft);
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.0f));
    drawSnappedText(g, "Audio From", {left, sidechainArea.getY() + 20, width, 11}, juce::Justification::centredLeft);

    // The chooser: the source's colour and name, or None, and the arrow that
    // says a click opens a list.
    const auto box = sidechainSource.toFloat();
    g.setColour(palette::control);
    g.fillRoundedRectangle(box, 2.5f);
    g.setColour(routed ? faceAccent() : palette::border);
    g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, 1.0f);
    auto label = sidechainSource.reduced(6, 0);
    const auto arrow = label.removeFromRight(8).toFloat().withSizeKeepingCentre(7.0f, 4.0f);
    juce::Path chevron;
    chevron.addTriangle(arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(), arrow.getCentreX(), arrow.getBottom());
    g.setColour(palette::textDim);
    g.fillPath(chevron);
    if (routed)
    {
        g.setColour(session.trackColour(source));
        g.fillEllipse(label.removeFromLeft(6).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
        label.removeFromLeft(5);
    }
    g.setColour(routed ? palette::text : palette::textDim);
    g.setFont(routed ? uiFontBold(10.0f) : uiFont(10.0f));
    drawSnappedText(g, routed ? session.trackName(source) : juce::String("None"), label.withTrimmedRight(4),
                    juce::Justification::centredLeft, true);

    // What the routing means, or what to do, so a device sitting silent on a
    // sidechain that was never chosen says why on its own face.
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.5f));
    g.drawFittedText(routed ? "Taken after its fader, and still heard while it is muted."
                            : "Pick the track whose audio drives this device.",
                     {left, sidechainSource.getBottom() + 7, width, sidechainArea.getBottom() - sidechainSource.getBottom() - 11},
                     juce::Justification::topLeft, 5);
}

bool DeviceEditorPanel::handleSidechainClick(const juce::MouseEvent& event)
{
    if (!sidechainArea.contains(event.getPosition()))
        return false;
    showSidechainMenu(sidechainSource, [this] (int chosen) { chooseSidechainSource(chosen); });
    return true;
}

juce::String DeviceEditorPanel::sidechainTooltip() const
{
    const auto source = session.deviceSidechainSource(track, pluginSlot);
    return (source >= 0 ? deviceName + " is driven by " + session.trackName(source) + "'s audio"
                        : deviceName + " can be driven by another track's audio")
         + ", taken after that track's fader and before its mute. Click to choose";
}

void DeviceEditorPanel::showSidechainMenu(juce::Rectangle<int> area, std::function<void(int)> chosen)
{
    const auto sources = session.deviceSidechainSources(track, pluginSlot);
    const auto current = session.deviceSidechainSource(track, pluginSlot);
    juce::PopupMenu menu;
    menu.addSectionHeader("AUDIO FROM");
    menu.addItem(1, "None", true, current < 0);
    if (!sources.empty())
        menu.addSeparator();
    for (size_t i = 0; i < sources.size(); ++i)
        menu.addItem(static_cast<int>(i) + 2, sources[i].name, true, sources[i].track == current);
    if (sources.empty())
        menu.addItem(1000, "No other track to take audio from", false, false);

    // The menu is asynchronous, so the panel cannot be assumed to still be
    // there when it closes: `chosen` is only called through a pointer that
    // knows, and finds the device again through the panel.
    const auto safe = juce::Component::SafePointer<DeviceEditorPanel>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(area)),
        [safe, sources, chosen = std::move(chosen)] (int result)
        {
            if (safe == nullptr || result == 0 || result >= 1000) return;
            chosen(result == 1 ? -1 : sources[static_cast<size_t>(result - 2)].track);
        });
}

juce::Result DeviceEditorPanel::chooseSidechainSource(int sourceTrack)
{
    // The routing announces a change, and the rack may answer one by
    // rebuilding its panels, so nothing of this one is read after the call
    // except through a pointer that knows.
    auto& document = session;
    const auto report = status;
    const auto name = deviceName;
    const auto safe = juce::Component::SafePointer<DeviceEditorPanel>(this);
    const auto done = document.setDeviceSidechainSource(track, pluginSlot, sourceTrack);
    if (safe != nullptr)
        safe->repaint();
    if (report)
        report(done.failed() ? done.getErrorMessage()
               : sourceTrack < 0 ? name + " takes no audio from another track"
               : name + " is driven by " + document.trackName(sourceTrack) + "'s audio");
    return done;
}
}
