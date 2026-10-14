#include "PushController.h"
#include "instruments/DrumRackDevice.h"

// What the Push's buttons and encoders do. All of it runs on the message
// thread, from the queue the MIDI thread fills.

namespace rhino
{
namespace
{
// An encoder sends many small steps a turn. A control that moves in whole
// steps - a chooser, a switch, a scale - takes this many for one of them.
constexpr int detentsPerStep = 6;
// A continuous control crosses its whole travel in about this many steps,
// and ten times as many with Shift held.
constexpr float stepsPerTravel = 140.0f;

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

void PushController::handleControl(const juce::MidiMessage& message)
{
    // Busy - a project opening, an export running - the document is in
    // flux: presses are dropped, but letting go of a held modifier is not.
    const auto allowed = !commandsAllowed || commandsAllowed();
    validateSelection();
    if (message.isController())
    {
        const auto controller = message.getControllerNumber(), value = message.getControllerValue();
        if (push::isTrackEncoder(controller) || controller == push::cc::masterEncoder
            || controller == push::cc::tempoEncoder || controller == push::cc::swingEncoder)
        {
            if (!allowed)
                return;
            const auto delta = push::encoderDelta(value);
            if (push::isTrackEncoder(controller)) turnEncoder(controller - push::cc::trackEncoderFirst, delta);
            else if (controller == push::cc::masterEncoder) turnMaster(delta);
            else if (controller == push::cc::tempoEncoder) turnTempo(delta);
            return;
        }
        if (value > 0)
        {
            if (allowed) pressButton(controller);
        }
        else
        {
            releaseButton(controller);
        }
        return;
    }
    if (message.isNoteOnOrOff() && push::isTouchNote(message.getNoteNumber()))
        touchEncoder(message.getNoteNumber(), message.isNoteOn());
}

void PushController::pressButton(int controller)
{
    namespace cc = push::cc;
    if (controller == cc::shift)
    {
        shift = true;
        return;
    }
    if (controller == cc::mute || controller == cc::solo)
    {
        (controller == cc::mute ? muteHeld : soloHeld) = true;
        rowUsedWhileHeld = false;
        return;
    }
    if (push::isLowerRow(controller))
    {
        const auto track = bankFirst() + controller - cc::lowerRowFirst;
        if (track >= session.trackCount())
            return;
        // Held Mute or Solo with a track's button is that track's switch, as
        // on Live's Push; on its own the button selects the track.
        if (muteHeld || soloHeld)
        {
            endGestures();
            if (muteHeld) session.toggleTrackMute(track);
            else session.toggleTrackSolo(track);
            rowUsedWhileHeld = true;
            return;
        }
        selectTrackAt(track);
        return;
    }
    if (push::isUpperRow(controller))
    {
        if (mode != EncoderMode::device || selectedTrack >= session.trackCount())
            return;
        const auto slots = session.deviceSlots(selectedTrack);
        const auto which = static_cast<size_t>(controller - cc::upperRowFirst);
        if (which >= slots.size() || slots[which].pluginIndex == selectedPlugin)
            return;
        endGestures();
        selectedPlugin = slots[which].pluginIndex;
        parameterPage = 0;
        if (selectDevice) selectDevice(selectedTrack, selectedPlugin);
        return;
    }
    switch (controller)
    {
        case cc::play:
            if (shift) { if (returnToStart) returnToStart(); }
            else if (play) play();
            break;
        case cc::record:
            if (record) record();
            break;
        case cc::metronome:
            session.setClickTrackEnabled(!session.clickTrackEnabled());
            break;
        case cc::tapTempo:
            tapTempo();
            break;
        case cc::undo:
            endGestures();
            if (shift) session.redo();
            else session.undo();
            break;
        case cc::accent:
            accent = !accent.load();
            break;
        case cc::octaveUp:
            moveOctave(1);
            break;
        case cc::octaveDown:
            moveOctave(-1);
            break;
        case cc::scales:
            setEncoderMode(mode == EncoderMode::scales ? modeBeforeScales : EncoderMode::scales);
            break;
        case cc::left:
            selectTrackAt(selectedTrack - 1);
            break;
        case cc::right:
            selectTrackAt(selectedTrack + 1);
            break;
        case cc::pageLeft:
        case cc::pageRight:
            if (mode == EncoderMode::device)
            {
                const auto page = juce::jlimit(0, pageCount() - 1, parameterPage + (controller == cc::pageRight ? 1 : -1));
                if (page != parameterPage)
                {
                    endGestures();
                    parameterPage = page;
                }
            }
            break;
        case cc::device:
            setEncoderMode(EncoderMode::device);
            break;
        case cc::volume:
            setEncoderMode(EncoderMode::volume);
            break;
        case cc::panAndSend:
            setEncoderMode(EncoderMode::pan);
            break;
        default:
            break;
    }
}

void PushController::releaseButton(int controller)
{
    namespace cc = push::cc;
    if (controller == cc::shift)
    {
        shift = false;
        return;
    }
    if ((controller == cc::mute && muteHeld) || (controller == cc::solo && soloHeld))
    {
        // Pressed and let go with no track's button between: the selected
        // track's switch.
        if (!rowUsedWhileHeld && selectedTrack < session.trackCount())
        {
            endGestures();
            if (controller == cc::mute) session.toggleTrackMute(selectedTrack);
            else session.toggleTrackSolo(selectedTrack);
        }
        (controller == cc::mute ? muteHeld : soloHeld) = false;
    }
}

void PushController::touchEncoder(int touch, bool isTouched)
{
    if (!juce::isPositiveAndBelow(touch, static_cast<int>(touched.size())))
        return;
    touched[static_cast<size_t>(touch)] = isTouched;
    // Letting go is the end of the turn. A touch alone opens nothing.
    if (!isTouched && touch < static_cast<int>(gestures.size()))
        endGesture(touch);
}

void PushController::turnEncoder(int encoder, int delta)
{
    switch (mode)
    {
        case EncoderMode::device: turnDeviceEncoder(encoder, delta); break;
        case EncoderMode::volume:
        case EncoderMode::pan: turnMixerEncoder(encoder, delta); break;
        case EncoderMode::scales: turnScalesEncoder(encoder, delta); break;
    }
}

void PushController::turnDeviceEncoder(int encoder, int delta)
{
    if (selectedPlugin < 0 || selectedTrack >= session.trackCount())
        return;
    const auto index = parameterPage * push::rowButtons + encoder;
    const auto parameter = session.deviceParameter(selectedTrack, selectedPlugin, index);
    if (!parameter || parameter->maximum <= parameter->minimum)
        return;
    float next = parameter->value;
    if (parameter->discrete || parameter->toggle || !parameter->choices.isEmpty())
    {
        auto& turned = detents[static_cast<size_t>(encoder)];
        turned += delta;
        const auto steps = turned / detentsPerStep;
        if (steps == 0)
            return;
        turned -= steps * detentsPerStep;
        const auto interval = parameter->interval > 0.0f ? parameter->interval : 1.0f;
        next = juce::jlimit(parameter->minimum, parameter->maximum, parameter->value + static_cast<float>(steps) * interval);
    }
    else
    {
        const juce::NormalisableRange<float> range(parameter->minimum, parameter->maximum, 0.0f,
                                                   static_cast<float>(parameter->skew > 0.0 ? parameter->skew : 1.0));
        const auto travel = (shift ? 10.0f : 1.0f) * stepsPerTravel;
        const auto proportion = juce::jlimit(0.0f, 1.0f, range.convertTo0to1(parameter->value) + static_cast<float>(delta) / travel);
        next = range.convertFrom0to1(proportion);
        if (parameter->interval > 0.0f)
            next = juce::jlimit(parameter->minimum, parameter->maximum,
                                parameter->minimum + std::round((next - parameter->minimum) / parameter->interval) * parameter->interval);
    }
    if (next == parameter->value)
        return;
    Gesture gesture;
    gesture.kind = Gesture::Kind::device;
    gesture.track = selectedTrack;
    gesture.plugin = selectedPlugin;
    gesture.parameter = index;
    beginGesture(encoder, gesture);
    if (gestures[static_cast<size_t>(encoder)].kind != Gesture::Kind::device)
        return;
    session.setDeviceParameter(selectedTrack, selectedPlugin, index, next);
}

void PushController::turnMixerEncoder(int encoder, int delta)
{
    const auto track = bankFirst() + encoder;
    if (track >= session.trackCount())
        return;
    const auto mixer = session.trackMixer(track);
    Gesture gesture;
    gesture.track = track;
    if (mode == EncoderMode::volume)
    {
        const auto next = juce::jlimit(Session::minimumVolumeDb, Session::maximumVolumeDb,
                                       mixer.volumeDb + static_cast<float>(delta) * (shift ? 0.1f : 0.5f));
        if (next == mixer.volumeDb)
            return;
        gesture.kind = Gesture::Kind::volume;
        beginGesture(encoder, gesture);
        session.setTrackVolumeDb(track, next);
    }
    else
    {
        const auto next = juce::jlimit(-1.0f, 1.0f, mixer.pan + static_cast<float>(delta) * (shift ? 0.002f : 0.01f));
        if (next == mixer.pan)
            return;
        gesture.kind = Gesture::Kind::pan;
        beginGesture(encoder, gesture);
        session.setTrackPan(track, next);
    }
}

// The scale page: the first encoder is the root, the second the scale, the
// third whether the pads are in key or chromatic.
void PushController::turnScalesEncoder(int encoder, int delta)
{
    if (encoder > 2)
        return;
    auto& turned = detents[static_cast<size_t>(encoder)];
    turned += delta;
    const auto steps = turned / detentsPerStep;
    if (steps == 0)
        return;
    turned -= steps * detentsPerStep;
    if (encoder == 0)
        layout.root = (((layout.root + steps) % 12) + 12) % 12;
    else if (encoder == 1)
        layout.scale = static_cast<MusicalScale>(juce::jlimit(0, musicalScaleCount - 1, static_cast<int>(layout.scale) + steps));
    else
        layout.inKey = steps < 0;
    saveLayout();
    updatePadMap();
}

void PushController::turnTempo(int delta)
{
    const auto next = juce::jlimit(Session::minimumTempo, Session::maximumTempo,
                                   session.tempo() + delta * (shift ? 0.1 : 1.0));
    if (next == session.tempo())
        return;
    // A tap's gesture and a turn's are one tempo either way.
    endGesture(tapGesture);
    Gesture gesture;
    gesture.kind = Gesture::Kind::tempo;
    beginGesture(push::touchTempoEncoder, gesture);
    session.setTempo(next);
}

void PushController::turnMaster(int delta)
{
    const auto current = session.masterVolumeDb();
    const auto next = juce::jlimit(Session::minimumVolumeDb, Session::maximumVolumeDb,
                                   current + static_cast<float>(delta) * (shift ? 0.1f : 0.5f));
    if (next == current)
        return;
    Gesture gesture;
    gesture.kind = Gesture::Kind::master;
    beginGesture(push::touchMasterEncoder, gesture);
    session.setMasterVolumeDb(next);
}

// A run of taps is one tempo change to undo, so the gesture stays open long
// enough for the next tap to land in it.
void PushController::tapTempo()
{
    const auto bpm = taps.tap(nowMs() * 0.001);
    if (!bpm)
        return;
    endGesture(push::touchTempoEncoder);
    Gesture gesture;
    gesture.kind = Gesture::Kind::tempo;
    gesture.idleMs = 2100.0;
    beginGesture(tapGesture, gesture);
    session.setTempo(std::round(*bpm * 10.0) / 10.0);
}

void PushController::beginGesture(int slot, Gesture gesture)
{
    auto& open = gestures[static_cast<size_t>(slot)];
    const auto same = open.kind == gesture.kind && open.track == gesture.track && open.plugin == gesture.plugin
                   && open.parameter == gesture.parameter;
    if (!same)
    {
        endGesture(slot);
        switch (gesture.kind)
        {
            case Gesture::Kind::device:
                if (session.beginDeviceParameterGesture(gesture.track, gesture.plugin, gesture.parameter).failed())
                    return;
                break;
            case Gesture::Kind::volume: session.beginTrackVolumeGesture(gesture.track); break;
            case Gesture::Kind::pan: session.beginTrackPanGesture(gesture.track); break;
            case Gesture::Kind::master: session.beginMasterVolumeGesture(); break;
            case Gesture::Kind::tempo: session.beginTempoGesture(); break;
            case Gesture::Kind::none: return;
        }
        open = gesture;
    }
    open.lastMoveMs = nowMs();
}

void PushController::endGesture(int slot)
{
    auto& open = gestures[static_cast<size_t>(slot)];
    const auto closing = open;
    open = {};
    switch (closing.kind)
    {
        case Gesture::Kind::device: session.endDeviceParameterGesture(closing.track, closing.plugin, closing.parameter); break;
        case Gesture::Kind::volume: session.endTrackVolumeGesture(closing.track); break;
        case Gesture::Kind::pan: session.endTrackPanGesture(closing.track); break;
        case Gesture::Kind::master: session.endMasterVolumeGesture(); break;
        case Gesture::Kind::tempo: session.endTempoGesture(); break;
        case Gesture::Kind::none: break;
    }
}

void PushController::endGestures()
{
    for (int slot = 0; slot < static_cast<int>(gestures.size()); ++slot)
        endGesture(slot);
    detents.fill(0);
}

// A turn with no finger resting on the encoder - or a tap - closes once it
// has been still for a moment. A touched encoder's gesture waits for the
// finger to lift.
void PushController::endIdleGestures(double now)
{
    for (int slot = 0; slot < static_cast<int>(gestures.size()); ++slot)
    {
        const auto& open = gestures[static_cast<size_t>(slot)];
        if (open.kind == Gesture::Kind::none)
            continue;
        const auto held = slot < static_cast<int>(touched.size()) && touched[static_cast<size_t>(slot)];
        if (!held && now - open.lastMoveMs > open.idleMs)
            endGesture(slot);
    }
}

void PushController::moveOctave(int direction)
{
    if (const auto slot = drumRackSlot(); slot >= 0)
    {
        if (auto* rack = dynamic_cast<DrumRackDevice*>(session.devicePlugin(selectedTrack, slot)))
        {
            const auto first = juce::jlimit(0, DrumRackDevice::lastFirstNote,
                                            rack->firstShownNote() + direction * DrumRackDevice::bankSize);
            session.showDrumBank(selectedTrack, slot, first);
        }
    }
    else
    {
        layout.octave = juce::jlimit(push::lowestOctave, push::highestOctave, layout.octave + direction);
        saveLayout();
    }
    updatePadMap();
}

void PushController::selectTrackAt(int track)
{
    if (session.trackCount() == 0)
        return;
    track = juce::jlimit(0, session.trackCount() - 1, track);
    if (track == selectedTrack)
        return;
    setSelectedTrack(track);
    if (selectTrack) selectTrack(track);
}

void PushController::setEncoderMode(EncoderMode next)
{
    if (next == mode)
        return;
    endGestures();
    if (next == EncoderMode::scales)
        modeBeforeScales = mode;
    mode = next;
}

void PushController::setSelectedTrack(int track)
{
    // The main row has no pads to play and no chain the encoders reach, so
    // the Push stays on the track it had.
    if (track == selectedTrack || !juce::isPositiveAndBelow(track, session.trackCount()))
        return;
    endGestures();
    selectedTrack = track;
    selectedPlugin = -1;
    parameterPage = 0;
    validateSelection();
    updatePadMap();
}

void PushController::setSelectedDevice(int track, int pluginIndex)
{
    if (track != selectedTrack || pluginIndex == selectedPlugin)
        return;
    endGestures();
    selectedPlugin = pluginIndex;
    parameterPage = 0;
}

void PushController::validateSelection()
{
    const auto count = session.trackCount();
    if (count == 0)
    {
        selectedPlugin = -1;
        return;
    }
    if (!juce::isPositiveAndBelow(selectedTrack, count))
    {
        endGestures();
        selectedTrack = juce::jlimit(0, count - 1, selectedTrack);
        selectedPlugin = -1;
    }
    const auto slots = session.deviceSlots(selectedTrack);
    const auto present = std::any_of(slots.begin(), slots.end(),
                                     [this](const auto& slot) { return slot.pluginIndex == selectedPlugin; });
    if (present)
        return;
    endGestures();
    parameterPage = 0;
    selectedPlugin = -1;
    for (const auto& slot : slots)
        if (slot.kind == DeviceKind::Instrument)
        {
            selectedPlugin = slot.pluginIndex;
            return;
        }
    if (!slots.empty())
        selectedPlugin = slots.front().pluginIndex;
}

int PushController::bankFirst() const
{
    return std::max(0, selectedTrack) / push::rowButtons * push::rowButtons;
}

int PushController::pageCount() const
{
    if (selectedPlugin < 0 || selectedTrack >= session.trackCount())
        return 1;
    const auto count = session.deviceParameterCount(selectedTrack, selectedPlugin);
    return std::max(1, (count + push::rowButtons - 1) / push::rowButtons);
}

int PushController::drumRackSlot() const
{
    if (!juce::isPositiveAndBelow(selectedTrack, session.trackCount()))
        return -1;
    for (const auto& slot : session.deviceSlots(selectedTrack))
        if (slot.deviceId == "Drums")
            return slot.pluginIndex;
    return -1;
}

void PushController::saveLayout() const
{
    Session::setControllerSetting("root", juce::String(layout.root));
    Session::setControllerSetting("scale", musicalScaleName(layout.scale));
    Session::setControllerSetting("inKey", layout.inKey ? "1" : "0");
    Session::setControllerSetting("octave", juce::String(layout.octave));
}

void PushController::loadLayout()
{
    layout.root = juce::jlimit(0, 11, Session::controllerSetting("root", "0").getIntValue());
    const auto scaleName = Session::controllerSetting("scale", musicalScaleName(MusicalScale::Major));
    for (int scale = 0; scale < musicalScaleCount; ++scale)
        if (scaleName == musicalScaleName(static_cast<MusicalScale>(scale)))
            layout.scale = static_cast<MusicalScale>(scale);
    layout.inKey = Session::controllerSetting("inKey", "1") != "0";
    layout.octave = juce::jlimit(push::lowestOctave, push::highestOctave,
                                 Session::controllerSetting("octave", "3").getIntValue());
}

}
