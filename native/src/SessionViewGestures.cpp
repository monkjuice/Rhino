#include "SessionView.h"
#include "BrowserIds.h"
#include <algorithm>

// Session view pointer handling, slot menus and drops.

namespace rhino
{
namespace
{
bool isSupportedAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aiff" || extension == ".aif"
        || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}

struct PresetMenuEntry
{
    Session::PatternPreset preset;
    const char* name;
};
constexpr PresetMenuEntry presetMenu[] {
    {Session::PatternPreset::WarmPulse, "Warm Pulse"},
    {Session::PatternPreset::AcidSteps, "Acid Steps"},
    {Session::PatternPreset::ArpRun, "Arp Run"},
    {Session::PatternPreset::ChordPad, "Chord Pad"},
    {Session::PatternPreset::SubBass, "Sub Bass"},
    {Session::PatternPreset::ReeseBass, "Reese Bass"},
    {Session::PatternPreset::SirenLead, "Siren Lead"},
    {Session::PatternPreset::WavePad, "Wave Pad"},
    {Session::PatternPreset::WaveBass, "Wave Bass"},
    {Session::PatternPreset::WavePluck, "Wave Pluck"},
    {Session::PatternPreset::HouseKit, "House Kit"},
    {Session::PatternPreset::BreakKit, "Break Kit"},
    {Session::PatternPreset::MinimalKit, "Minimal Kit"},
    {Session::PatternPreset::ClapKit, "Clap Kit"},
};
constexpr int presetMenuCount = static_cast<int>(std::size(presetMenu));
}

SessionView::Hit SessionView::hitTest(juce::Point<float> point) const
{
    const auto gridTop = toolbarHeight + trackHeaderHeight;
    if (point.y < toolbarHeight) return {};
    const auto sceneCount = session.sceneCount();
    const auto trackCount = session.trackCount();
    if (point.x >= sceneColumnX())
    {
        if (point.y < gridTop || point.y >= gridBottom()) return {};
        const auto scene = static_cast<int>((point.y - gridTop + static_cast<float>(sceneScroll)) / slotHeight);
        if (!juce::isPositiveAndBelow(scene, sceneCount)) return {};
        return {Region::sceneLaunch, -1, scene};
    }
    const auto track = static_cast<int>((point.x + static_cast<float>(trackScroll)) / columnWidth);
    if (!juce::isPositiveAndBelow(track, trackCount)) return {};
    if (point.y < gridTop) return {Region::trackHeader, track, -1};
    if (point.y >= stopRowTop()) return {Region::trackStop, track, -1};
    if (point.y >= gridBottom()) return {};
    const auto scene = static_cast<int>((point.y - gridTop + static_cast<float>(sceneScroll)) / slotHeight);
    if (!juce::isPositiveAndBelow(scene, sceneCount)) return {};
    return {Region::slot, track, scene};
}

void SessionView::mouseMove(const juce::MouseEvent& event)
{
    const auto next = hitTest(event.position);
    if (next == hovered) return;
    hovered = next;
    repaint();
}

void SessionView::mouseExit(const juce::MouseEvent&)
{
    if (hovered.region == Region::none) return;
    hovered = {};
    repaint();
}

void SessionView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        trackScroll -= static_cast<double>(wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * columnWidth;
    }
    else
    {
        sceneScroll -= static_cast<double>(wheel.deltaY) * slotHeight * 3.0;
    }
    updateScroll();
    layOutTrackControls();
    hovered = hitTest(event.position);
    repaint();
}

void SessionView::launchSlot(int track, int scene)
{
    const auto result = session.launchSlot(track, scene);
    if (result.failed() && status) status(result.getErrorMessage());
    repaint();
}

void SessionView::mouseDown(const juce::MouseEvent& event)
{
    const auto hit = hitTest(event.position);
    switch (hit.region)
    {
        case Region::trackHeader:
            selectTrack(hit.track);
            break;
        case Region::trackStop:
            selectTrack(hit.track);
            session.stopTrackSlots(hit.track);
            repaint();
            break;
        case Region::sceneLaunch:
        {
            if (event.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Delete scene", session.sceneCount() > 1);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                                       .withTargetScreenArea({event.getScreenX(), event.getScreenY(), 1, 1}),
                    [safe = juce::Component::SafePointer<SessionView>(this), scene = hit.scene](int result)
                    {
                        if (safe == nullptr || result != 1) return;
                        const auto deleted = safe->session.deleteScene(scene);
                        if (deleted.failed() && safe->status) safe->status(deleted.getErrorMessage());
                    });
                break;
            }
            const auto result = session.launchScene(hit.scene);
            if (result.failed() && status) status(result.getErrorMessage());
            repaint();
            break;
        }
        case Region::slot:
            selectTrack(hit.track);
            if (event.mods.isPopupMenu()) showSlotMenu(hit.track, hit.scene);
            else launchSlot(hit.track, hit.scene);
            break;
        case Region::none:
            break;
    }
}

void SessionView::showSlotMenu(int track, int scene)
{
    const auto info = session.slotClip(track, scene);
    juce::PopupMenu presets;
    for (int i = 0; i < presetMenuCount; ++i)
        presets.addItem(100 + i, presetMenu[i].name);
    juce::PopupMenu instruments;
    instruments.addItem(10, "4OSC synth");
    instruments.addItem(11, "Rhino Wave");
    instruments.addItem(12, "Rhino Drums");
    instruments.addItem(13, "Rhino Forge", session.isForgeAvailable());
    juce::PopupMenu menu;
    menu.addSectionHeader(session.trackName(track) + "  /  " + session.sceneName(scene));
    menu.addSubMenu("Insert pattern", presets);
    menu.addSubMenu("Insert empty instrument clip", instruments);
    menu.addSeparator();
    menu.addItem(20, "Insert Rhino Whistle");
    menu.addItem(21, "Insert Rhino Siren");
    menu.addSeparator();
    menu.addItem(40, "Copy to arrangement at playhead", info.hasClip);
    menu.addSeparator();
    menu.addItem(30, "Delete clip", info.hasClip);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(slotBounds(track, scene)).toNearestInt()),
        [safe = juce::Component::SafePointer<SessionView>(this), track, scene](int result)
        {
            if (safe == nullptr || result == 0) return;
            auto& session = safe->session;
            juce::Result outcome = juce::Result::ok();
            if (result >= 100 && result < 100 + presetMenuCount)
                outcome = session.insertPatternPresetInSlot(presetMenu[result - 100].preset, track, scene);
            else if (result == 10) outcome = session.insertInstrumentClipInSlot(Session::Instrument::FourOsc, track, scene);
            else if (result == 11) outcome = session.insertInstrumentClipInSlot(Session::Instrument::RhinoWave, track, scene);
            else if (result == 12) outcome = session.insertInstrumentClipInSlot(Session::Instrument::Drums, track, scene);
            else if (result == 13) outcome = session.insertInstrumentClipInSlot(Session::Instrument::RhinoForge, track, scene);
            else if (result == 20) outcome = session.insertBuiltInSampleInSlot(Session::BuiltInSample::Whistle, track, scene);
            else if (result == 21) outcome = session.insertBuiltInSampleInSlot(Session::BuiltInSample::Siren, track, scene);
            else if (result == 30) outcome = session.deleteSlotClip(track, scene);
            else if (result == 40)
            {
                const auto at = session.edit->getTransport().getPosition().inSeconds();
                outcome = session.copySlotClipToArrangement(track, scene, at);
                if (outcome.wasOk() && safe->status)
                    safe->status("Copied the clip into the arrangement on " + session.trackName(track));
            }
            if (outcome.failed() && safe->status) safe->status(outcome.getErrorMessage());
            safe->repaint();
        });
}

bool SessionView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    return false;
}

void SessionView::filesDropped(const juce::StringArray& files, int x, int y)
{
    dropTarget = {};
    const auto hit = hitTest({static_cast<float>(x), static_cast<float>(y)});
    if (hit.region != Region::slot)
    {
        if (status) status("Drop audio on a clip slot.");
        repaint();
        return;
    }
    auto scene = hit.scene;
    for (const auto& path : files)
    {
        const auto file = juce::File(path);
        if (!isSupportedAudioFile(file)) continue;
        const auto result = session.insertAudioFileInSlot(file, hit.track, scene);
        if (result.failed())
        {
            if (status) status(result.getErrorMessage());
            break;
        }
        // Several files dropped at once fill consecutive scenes downwards.
        if (++scene >= session.sceneCount()) break;
    }
    repaint();
}

bool SessionView::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith("rhino-browser:");
}

void SessionView::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto next = hitTest(details.localPosition.toFloat());
    if (next == dropTarget) return;
    dropTarget = next.region == Region::slot ? next : Hit{};
    repaint();
}

void SessionView::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    if (dropTarget.region == Region::none) return;
    dropTarget = {};
    repaint();
}

void SessionView::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    dropTarget = {};
    const auto hit = hitTest(details.localPosition.toFloat());
    if (hit.region != Region::slot)
    {
        if (status) status("Drop browser items on a clip slot.");
        repaint();
        return;
    }
    selectTrack(hit.track);
    applyBrowserDrop(details.description.toString(), hit.track, hit.scene);
    repaint();
}

// Browser items mean the same thing here as on the timeline, except that a
// pattern or instrument lands in a clip slot rather than at a time position.
// Effects still go to the track, because a slot cannot hold one.
void SessionView::applyBrowserDrop(const juce::String& description, int track, int scene)
{
    const auto kind = browserDropKind(description);
    const auto id = browserDropId(description);
    juce::Result result = juce::Result::fail("Drop sounds, drums, instruments, MIDI FX, or audio effects on a clip slot.");
    juce::String message;
    if (kind == "preset")
    {
        if (const auto preset = patternPresetFromId(id))
        {
            result = session.insertPatternPresetInSlot(*preset, track, scene);
            message = "Added pattern clip to " + session.trackName(track) + " / " + session.sceneName(scene);
        }
    }
    else if (kind == "instrument")
    {
        if (const auto* instrument = deviceFromId(id, DeviceKind::Instrument))
        {
            result = session.insertDeviceClipInSlot(instrument->id, track, scene);
            message = "Added instrument clip to " + session.trackName(track) + " / " + session.sceneName(scene);
        }
    }
    else if (kind == "sample")
    {
        if (const auto sample = builtInSampleFromId(id))
        {
            result = session.insertBuiltInSampleInSlot(*sample, track, scene);
            message = "Added " + id + " to " + session.trackName(track) + " / " + session.sceneName(scene);
        }
    }
    else if (kind == "effect")
    {
        if (const auto* effect = deviceFromId(id, DeviceKind::AudioEffect))
        {
            result = session.addDevice(effect->id, track);
            message = "Added browser effect to " + session.trackName(track) + " Device View";
        }
    }
    else if (kind == "drumkit")
    {
        if (const auto kit = drumKitFromId(id))
        {
            result = session.addDrumKit(*kit, track);
            message = "Track " + juce::String(track + 1) + " now runs " + session.trackName(track);
        }
    }
    else if (kind == "midi-effect")
    {
        if (const auto* effect = deviceFromId(id, DeviceKind::MidiEffect))
        {
            result = session.addDevice(effect->id, track);
            message = "Added MIDI FX to " + session.trackName(track) + " Device View";
        }
    }
    else if (kind == "info")
    {
        result = juce::Result::ok();
        message = id + " is already available in this starter session.";
    }
    if (status) status(result.failed() ? result.getErrorMessage() : message);
}

}
