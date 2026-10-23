#include "PushController.h"
#include "instruments/DrumRackDevice.h"

// What the Push shows: its pads, its button lights and its four lines of
// text. Each redraw works out the whole picture from the model and sends only
// what differs from the last one sent, so a still surface costs nothing on
// the port.

namespace rhino
{
namespace
{
namespace cc = push::cc;

// Every button with a light, so a fresh picture can put out whatever Live
// or a previous run left lit.
constexpr int lightedButtons[] {
    cc::tapTempo, cc::metronome, cc::master, cc::stop, cc::left, cc::right, cc::up, cc::down, cc::select,
    cc::shift, cc::note, cc::session, cc::addEffect, cc::addTrack, cc::octaveDown, cc::octaveUp, cc::repeat,
    cc::accent, cc::scales, cc::user, cc::mute, cc::solo, cc::pageLeft, cc::pageRight, cc::play, cc::record,
    cc::newClip, cc::duplicate, cc::automation, cc::fixedLength, cc::device, cc::browse, cc::track, cc::clip,
    cc::volume, cc::panAndSend, cc::quantize, cc::doubleLoop, cc::deleteClip, cc::undo};

juce::String volumeText(float decibels)
{
    return decibels <= Session::minimumVolumeDb ? juce::String("-inf") : juce::String(decibels, 1) + "dB";
}

juce::String panText(float pan)
{
    const auto amount = juce::roundToInt(std::abs(pan) * 100.0f);
    return amount == 0 ? juce::String("C") : juce::String(amount) + (pan < 0.0f ? "L" : "R");
}

std::string cellText(const juce::String& text)
{
    return push::fitCell(text.toStdString());
}

// Free text on the left of a line and more on its right, as the third line
// carries a track and its device on one side and the page on the other.
std::string splitLine(const juce::String& left, const juce::String& right)
{
    auto line = push::displayText(left.toStdString());
    const auto tail = push::toDisplayText(right.toStdString());
    if (tail.size() < line.size())
        line.replace(line.size() - tail.size(), tail.size(), tail);
    return line;
}
}

int PushController::trackColourIndex(int track, int shade) const
{
    if (!juce::isPositiveAndBelow(track, session.trackCount()))
        return push::colour::black;
    const auto colour = session.trackColour(track);
    return push::paletteIndexFor(colour.getRed(), colour.getGreen(), colour.getBlue(), shade);
}

void PushController::updatePadMap()
{
    std::array<int, push::padCount> map {};
    map.fill(-1);
    if (const auto slot = drumRackSlot(); slot >= 0)
    {
        const auto* rack = dynamic_cast<DrumRackDevice*>(session.devicePlugin(selectedTrack, slot));
        const auto first = rack != nullptr ? rack->firstShownNote() : 36;
        for (int pad = 0; pad < push::padCount; ++pad)
        {
            map[static_cast<size_t>(pad)] = push::drumPadNote(first, pad);
            padRoles[static_cast<size_t>(pad)] = map[static_cast<size_t>(pad)] >= 0 ? push::PadRole::inScale : push::PadRole::none;
        }
    }
    else
    {
        for (int pad = 0; pad < push::padCount; ++pad)
        {
            const auto played = push::padNote(layout, pad);
            map[static_cast<size_t>(pad)] = played.note;
            padRoles[static_cast<size_t>(pad)] = played.role;
        }
    }
    const juce::SpinLock::ScopedLockType lock(padLock);
    padMap = map;
}

PushController::Surface PushController::wantedSurface()
{
    validateSelection();
    // The Drum Rack's bank can move from its own face, so the map follows it
    // here rather than only when the Push moves it.
    updatePadMap();
    Surface surface;
    wantPads(surface);
    wantButtons(surface);
    wantDisplay(surface);
    return surface;
}

// In key, the root wears the track's colour and the rest of the scale is
// white; chromatic, the notes outside it are dark. On a Drum Rack, a pad
// with a sound wears the track's colour and an empty one is grey. Whatever is
// sounding is green - on every pad that plays that note.
void PushController::wantPads(Surface& surface)
{
    std::array<int, push::padCount> map {};
    std::array<bool, 128> held {};
    {
        const juce::SpinLock::ScopedLockType lock(padLock);
        map = padMap;
        for (int note = 0; note < 128; ++note)
            held[static_cast<size_t>(note)] = padsHolding[static_cast<size_t>(note)] > 0;
    }
    auto rootColour = trackColourIndex(selectedTrack, push::colour::full);
    // A grey track's root would vanish among the white pads.
    if (rootColour <= push::colour::white)
        rootColour = push::colour::hue(push::colour::sky);
    std::array<bool, DrumRackDevice::padCount> filled {};
    const auto drumSlot = drumRackSlot();
    if (drumSlot >= 0)
        if (const auto* rack = dynamic_cast<DrumRackDevice*>(session.devicePlugin(selectedTrack, drumSlot)))
            filled = rack->filledPads();

    for (int pad = 0; pad < push::padCount; ++pad)
    {
        const auto note = map[static_cast<size_t>(pad)];
        auto& light = surface.pads[static_cast<size_t>(pad)];
        if (note < 0)
            light = push::colour::black;
        else if (held[static_cast<size_t>(note)])
            light = push::colour::hue(push::colour::green);
        else if (drumSlot >= 0)
            light = filled[static_cast<size_t>(note)] ? rootColour : push::colour::darkGrey;
        else
            switch (padRoles[static_cast<size_t>(pad)])
            {
                case push::PadRole::root: light = rootColour; break;
                case push::PadRole::inScale: light = push::colour::white; break;
                case push::PadRole::outOfScale:
                case push::PadRole::none: light = push::colour::black; break;
            }
    }
}

void PushController::wantButtons(Surface& surface)
{
    namespace led = push::led;
    auto& lights = surface.buttons;
    const auto set = [&lights](int controller, int value) { lights[static_cast<size_t>(controller)] = value; };
    const auto lit = [](bool on) { return on ? led::lit : led::dim; };
    const auto trackCount = session.trackCount();
    const auto hasTrack = juce::isPositiveAndBelow(selectedTrack, trackCount);

    set(cc::play, lit(session.isPlaying()));
    set(cc::record, session.isRecording() ? led::lit : session.isCountingIn() ? led::litBlink : led::dim);
    set(cc::metronome, lit(session.clickTrackEnabled()));
    set(cc::tapTempo, led::dim);
    set(cc::undo, led::dim);
    set(cc::shift, lit(shift));
    set(cc::accent, lit(accent));
    set(cc::scales, lit(mode == EncoderMode::scales));
    set(cc::device, lit(mode == EncoderMode::device));
    set(cc::volume, lit(mode == EncoderMode::volume));
    set(cc::panAndSend, lit(mode == EncoderMode::pan));
    set(cc::left, selectedTrack > 0 ? led::dim : led::off);
    set(cc::right, selectedTrack < trackCount - 1 ? led::dim : led::off);
    if (const auto drumSlot = drumRackSlot(); drumSlot >= 0)
    {
        const auto* rack = dynamic_cast<DrumRackDevice*>(session.devicePlugin(selectedTrack, drumSlot));
        const auto first = rack != nullptr ? rack->firstShownNote() : 0;
        set(cc::octaveDown, first > 0 ? led::dim : led::off);
        set(cc::octaveUp, first < DrumRackDevice::lastFirstNote ? led::dim : led::off);
    }
    else
    {
        set(cc::octaveDown, layout.octave > push::lowestOctave ? led::dim : led::off);
        set(cc::octaveUp, layout.octave < push::highestOctave ? led::dim : led::off);
    }
    if (mode == EncoderMode::device)
    {
        set(cc::pageLeft, parameterPage > 0 ? led::dim : led::off);
        set(cc::pageRight, parameterPage < pageCount() - 1 ? led::dim : led::off);
    }
    if (hasTrack)
    {
        const auto mixer = session.trackMixer(selectedTrack);
        set(cc::mute, lit(mixer.muted || muteHeld));
        set(cc::solo, lit(mixer.soloed || soloHeld));
    }

    // The upper row picks a device, the lower row a track.
    if (mode == EncoderMode::device && hasTrack)
    {
        const auto slots = session.deviceSlots(selectedTrack);
        for (int i = 0; i < push::rowButtons && i < static_cast<int>(slots.size()); ++i)
            set(cc::upperRowFirst + i, slots[static_cast<size_t>(i)].pluginIndex == selectedPlugin ? led::orangeLit : led::orangeDim);
    }
    for (int i = 0; i < push::rowButtons; ++i)
    {
        const auto track = bankFirst() + i;
        if (track < trackCount)
            set(cc::lowerRowFirst + i, trackColourIndex(track, track == selectedTrack ? push::colour::full : push::colour::veryDim));
    }
}

void PushController::wantDisplay(Surface& surface)
{
    auto& lines = surface.lines;
    std::array<std::string, push::cellCount> cells;
    const auto clearCells = [&cells] { cells.fill({}); };
    const auto trackCount = session.trackCount();
    const auto hasTrack = juce::isPositiveAndBelow(selectedTrack, trackCount);
    const auto trackName = hasTrack ? session.trackName(selectedTrack) : juce::String();

    if (mode == EncoderMode::scales)
    {
        clearCells();
        cells[0] = cellText("Root");
        cells[1] = cellText("Scale");
        cells[2] = cellText("Layout");
        lines[0] = push::displayLine(cells);
        clearCells();
        cells[0] = cellText(pitchClassName(layout.root));
        cells[1] = cellText(musicalScaleName(layout.scale));
        cells[2] = cellText(layout.inKey ? "In Key" : "Chromatic");
        lines[1] = push::displayLine(cells);
        lines[2] = push::displayText("");
        lines[3] = splitLine("Scales", "Octave " + juce::String(layout.octave));
        return;
    }

    if (mode == EncoderMode::volume || mode == EncoderMode::pan)
    {
        const auto volume = mode == EncoderMode::volume;
        std::array<std::string, push::cellCount> names, values, bars;
        for (int i = 0; i < push::cellCount; ++i)
        {
            const auto track = bankFirst() + i;
            if (track >= trackCount)
                continue;
            const auto mixer = session.trackMixer(track);
            names[static_cast<size_t>(i)] = cellText(session.trackName(track));
            values[static_cast<size_t>(i)] = cellText(volume ? volumeText(mixer.volumeDb) : panText(mixer.pan));
            bars[static_cast<size_t>(i)] = push::valueBar(
                volume ? (mixer.volumeDb - Session::minimumVolumeDb) / (Session::maximumVolumeDb - Session::minimumVolumeDb)
                       : (mixer.pan + 1.0f) * 0.5f);
        }
        lines[0] = push::displayLine(names);
        lines[1] = push::displayLine(values);
        lines[2] = push::displayLine(bars);
        lines[3] = splitLine(volume ? "Volume" : "Pan", "Main " + volumeText(session.masterVolumeDb()));
        return;
    }

    // The selected device: its controls a page at a time, and the chain
    // under them, one name over each button of the upper row.
    const auto slots = hasTrack ? session.deviceSlots(selectedTrack) : std::vector<Session::DeviceSlot> {};
    juce::String deviceName;
    for (const auto& slot : slots)
        if (slot.pluginIndex == selectedPlugin)
            deviceName = slot.name;
    std::array<std::string, push::cellCount> names, values;
    if (selectedPlugin >= 0)
        for (int i = 0; i < push::cellCount; ++i)
            if (const auto parameter = session.deviceParameter(selectedTrack, selectedPlugin, parameterPage * push::rowButtons + i))
            {
                names[static_cast<size_t>(i)] = cellText(parameter->name);
                values[static_cast<size_t>(i)] = cellText(parameter->valueText);
            }
    lines[0] = push::displayLine(names);
    lines[1] = push::displayLine(values);
    if (!hasTrack)
        lines[2] = push::displayText("Rhino");
    else if (selectedPlugin < 0)
        lines[2] = splitLine(trackName, "No devices");
    else
        lines[2] = splitLine(trackName + "  >  " + deviceName,
                             "Page " + juce::String(parameterPage + 1) + "/" + juce::String(pageCount()));
    clearCells();
    for (int i = 0; i < push::cellCount && i < static_cast<int>(slots.size()); ++i)
    {
        const auto& slot = slots[static_cast<size_t>(i)];
        auto text = push::fitCell(slot.name.toStdString(), push::cellWidth - 1);
        // The selected device is marked with the display's own arrow, which
        // sits in the place a leading space would.
        cells[static_cast<size_t>(i)] = (slot.pluginIndex == selectedPlugin ? std::string(1, push::glyph::selected) : std::string(" ")) + text;
    }
    lines[3] = push::displayLine(cells);
}

void PushController::redraw()
{
    if (!connected && !sendToPush)
        return;
    const auto wanted = wantedSurface();
    for (int pad = 0; pad < push::padCount; ++pad)
        if (!shownValid || wanted.pads[static_cast<size_t>(pad)] != shown.pads[static_cast<size_t>(pad)])
            send(juce::MidiMessage::noteOn(1, push::padNoteFirst + pad,
                                           static_cast<juce::uint8>(wanted.pads[static_cast<size_t>(pad)])));
    const auto sendButton = [this, &wanted](int controller)
    {
        const auto index = static_cast<size_t>(controller);
        if (!shownValid || wanted.buttons[index] != shown.buttons[index])
            send(juce::MidiMessage::controllerEvent(1, controller, wanted.buttons[index]));
    };
    for (const auto controller : lightedButtons)
        sendButton(controller);
    for (int i = 0; i < push::rowButtons; ++i)
    {
        sendButton(cc::upperRowFirst + i);
        sendButton(cc::lowerRowFirst + i);
        sendButton(cc::sceneFirst + i);
    }
    for (int line = 0; line < push::displayLineCount; ++line)
        if (!shownValid || wanted.lines[static_cast<size_t>(line)] != shown.lines[static_cast<size_t>(line)])
            sendBytes(push::displayLineSysex(line, wanted.lines[static_cast<size_t>(line)]));
    shown = wanted;
    shownValid = true;
    flushOutput();
}

}
