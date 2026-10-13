#include "DeviceEditorPanelDrumsInternal.h"
#include <algorithm>

// The Drum Rack face's pointer handling: a click on the map, a pad or one of
// its buttons, a pad dragged to another note, the wheel over the pads, and the
// tooltips that say what each spot does. The selected pad's side has its own,
// in DeviceEditorPanelDrumSample.cpp.
namespace rhino
{
using namespace drumface;

// ---- the hand ------------------------------------------------------------------

bool DeviceEditorPanel::handleDrumMouseDown(const juce::MouseEvent& event)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;
    // A right-click on a knob is the automation menu for whatever the knob
    // stands for right now.
    if (event.mods.isPopupMenu())
        for (int i = 0; i < drumSliders.size(); ++i)
            if (event.eventComponent == drumSliders[i])
            {
                showParameterMenu(DrumRackDevice::parameterIndex(device->selectedPad(), i));
                return true;
            }
    if (event.eventComponent != this)
        return handleDrumSampleMouseDown(event);
    const auto position = event.getPosition();
    // The name bar's window button. The rest of the name bar is the panel's:
    // it drags the device along the chain.
    if (openInWindow != nullptr && windowButtonIn(getLocalBounds()).contains(position))
    {
        openInWindow();
        return true;
    }
    if (event.y < headerHeight)
        return false;

    const auto layout = layoutFor(getLocalBounds());
    const auto chosen = device->selectedPad();
    for (int i = 0; i < controlCells; ++i)
        if (automationIn(layout.cell[static_cast<size_t>(i)]).contains(position))
        {
            const auto control = DrumRackDevice::parameterIndex(chosen, i);
            if (juce::isPositiveAndBelow(control, static_cast<int>(parameters.size()))
                && parameters[static_cast<size_t>(control)].automated)
            {
                const auto result = session.toggleParameterAutomationOverride(track, pluginSlot, control);
                if (status) status(result.wasOk() ? "Toggled parameter automation" : result.getErrorMessage());
                return true;
            }
        }

    if (layout.autoSelect.contains(position))
    {
        const auto on = !device->autoSelect();
        device->setAutoSelect(on);
        repaint(layout.autoSelect.expanded(2));
        if (status) status(on ? "Auto Select on: a pad is selected as it is played"
                              : "Auto Select off: the selected pad stays while others play");
        return true;
    }

    // The map: the bank shown follows the pointer, the row under it the
    // second of the four.
    if (const auto note = mapNoteAt(layout, position); note >= 0)
    {
        drumDrag = DrumDrag::map;
        showDrumBank((note / padColumns - 1) * padColumns);
        return true;
    }

    const auto first = device->firstShownNote();
    for (int index = 0; index < padsShown; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        const auto note = first + index;
        // Picking a pad is a view of the rack, not an edit to it.
        if (note != chosen)
        {
            device->setSelectedPad(note);
            readDrumParameters();
            styleDrumControls();
            repaint();
        }
        if (event.mods.isPopupMenu())
        {
            showDrumPadMenu(note);
            return true;
        }
        const auto view = device->pad(note);
        const auto label = view.sound.has_value() ? view.sound->displayName() : padTitle(note);
        if (layout.play[i].contains(position))
        {
            device->previewPad(note);
            return true;
        }
        if (view.sound.has_value() && layout.mute[i].contains(position))
        {
            const auto muted = !view.muted;
            session.editDeviceSettings(track, pluginSlot, muted ? "Mute pad" : "Unmute pad",
                                       [device, note, muted] { device->setPadMuted(note, muted); });
            if (status) status(label + (muted ? " muted" : " unmuted"));
            return true;
        }
        if (view.sound.has_value() && layout.solo[i].contains(position))
        {
            const auto soloed = !view.soloed;
            session.editDeviceSettings(track, pluginSlot, soloed ? "Solo pad" : "Unsolo pad",
                                       [device, note, soloed] { device->setPadSoloed(note, soloed); });
            if (status) status(label + (soloed ? " soloed" : " no longer soloed"));
            return true;
        }
        // A press on a filled pad's body may become a drag to another pad.
        if (view.sound.has_value())
        {
            drumDrag = DrumDrag::pad;
            drumDragPad = note;
            drumPadDragging = false;
        }
        if (status) status(label + " on " + DrumRackDevice::noteName(note));
        return true;
    }
    return handleDrumSampleMouseDown(event);
}

int DeviceEditorPanel::drumNoteAt(juce::Point<int> position) const
{
    if (const auto pad = drumPadAt(position); pad >= 0)
        return pad;
    return mapNoteAt(layoutFor(getLocalBounds()), position);
}

// The pad in hand, drawn beside the pointer rather than under it, so the pad
// it would land on stays in sight.
juce::Rectangle<int> DeviceEditorPanel::drumGhostArea() const
{
    return {drumDragPoint.x + 10, drumDragPoint.y + 8, padWidth, 18};
}

void DeviceEditorPanel::handleDrumDrag(const juce::MouseEvent& event)
{
    if (drumDrag == DrumDrag::pad)
    {
        // A few pixels of slack, so a click that wavers selects the pad
        // rather than moving it.
        if (!drumPadDragging && event.getDistanceFromDragStart() < 4)
            return;
        drumPadDragging = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        const auto target = drumNoteAt(event.getPosition());
        showDrumDropTarget(target != drumDragPad ? target : -1);
        repaint(drumGhostArea().expanded(2));
        drumDragPoint = event.getPosition();
        repaint(drumGhostArea().expanded(2));
        return;
    }
    if (drumDrag == DrumDrag::map)
    {
        showDrumBank((mapRowAt(layoutFor(getLocalBounds()), event.y) - 1) * padColumns);
        return;
    }
    if (drumDrag != DrumDrag::start && drumDrag != DrumDrag::end)
        return;
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(device->selectedPad());
    if (!view.sound.has_value())
        return;
    const auto wave = layoutFor(getLocalBounds()).picture.reduced(3);
    const auto place = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.x - wave.getX()) / static_cast<float>(wave.getWidth()));
    auto playback = view.sound->playback;
    if (drumDrag == DrumDrag::start)
        playback.start = std::min(place, playback.end - 0.001f);
    else
        playback.end = std::max(place, playback.start + 0.001f);
    previewDrumPlayback(playback);
}

void DeviceEditorPanel::handleDrumMouseUp(const juce::MouseEvent& event)
{
    const auto dragged = drumDrag;
    drumDrag = DrumDrag::none;
    if (dragged == DrumDrag::pad)
    {
        const auto from = drumDragPad;
        const auto dropped = drumPadDragging;
        repaint(drumGhostArea().expanded(2));
        drumDragPad = -1;
        drumPadDragging = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        showDrumDropTarget(-1);
        const auto to = dropped ? drumNoteAt(event.getPosition()) : -1;
        auto* device = drumsIn(session, track, pluginSlot);
        if (device == nullptr || to < 0 || to == from)
            return;
        const auto moving = device->pad(from).sound;
        const auto displaced = device->pad(to).sound;
        const auto report = status;
        const auto result = session.moveDrumPad(track, pluginSlot, from, to);
        if (report == nullptr || !moving.has_value())
            return;
        if (result.failed())
            report(result.getErrorMessage());
        else if (displaced.has_value())
            report("Swapped " + moving->displayName() + " and " + displaced->displayName());
        else
            report("Moved " + moving->displayName() + " to " + DrumRackDevice::noteName(to));
        return;
    }
    if (dragged == DrumDrag::start)
        endDrumPlaybackDrag("Move the sample's start");
    else if (dragged == DrumDrag::end)
        endDrumPlaybackDrag("Move the sample's end");
}

bool DeviceEditorPanel::handleDrumWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr || wheel.deltaY == 0.0f)
        return false;
    // The wheel over the map or the pads moves a row of four at a time, up
    // the notes as it turns away.
    const auto layout = layoutFor(getLocalBounds());
    const auto grid = layout.pad[0].getUnion(layout.pad[padsShown - 1]);
    if (!layout.map.expanded(4).contains(event.getPosition()) && !grid.contains(event.getPosition()))
        return false;
    showDrumBank(device->firstShownNote() + (wheel.deltaY > 0.0f ? padColumns : -padColumns));
    return true;
}

juce::String DeviceEditorPanel::drumTooltip(juce::Point<int> position) const
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return {};
    const auto layout = layoutFor(getLocalBounds());
    const auto first = device->firstShownNote();
    if (openInWindow != nullptr && windowButtonIn(getLocalBounds()).contains(position))
        return "Open the Drum Rack in a window of its own, the pads above and the sample editor below the whole "
               "width, to play and edit it bigger";
    if (layout.autoSelect.contains(position))
        return "Auto Select: a pad that holds a sound is selected as its note arrives, from a keyboard, a controller "
               "or a clip. Switch it off to keep one pad selected while others play.";
    if (const auto note = mapNoteAt(layout, position); note >= 0)
        return "All 128 notes, four to a row. The framed ones, " + DrumRackDevice::noteName(first) + " to "
               + DrumRackDevice::noteName(first + padsShown - 1)
               + ", are on the pads; click or drag to show others. Lit notes hold a sound, and a key flashes orange "
                 "as it is played.";
    for (int index = 0; index < padsShown; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        const auto note = first + index;
        const auto view = device->pad(note);
        if (layout.play[i].contains(position))
            return "Play this pad";
        if (layout.mute[i].contains(position))
            return "Mute this pad";
        if (layout.solo[i].contains(position))
            return "Solo this pad: while any pad is soloed, only soloed pads sound";
        if (!view.sound.has_value())
            return padTitle(note) + " is empty. Drop a sample or a drum preset on it, or right-click for a synth.";
        return view.sound->displayName() + " on " + DrumRackDevice::noteName(note)
               + ". Drag it to another pad, or a note on the map, to move it there or swap it with what is there; "
                 "drop a sound here to replace it; right-click for its synth, choke group and more.";
    }
    return drumSampleTooltip(position);
}
}
