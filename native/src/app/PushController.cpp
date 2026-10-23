#include "PushController.h"

// The Push's ports, its MIDI thread and its lifecycle. What the buttons and
// encoders do is PushControllerControls.cpp; what it shows is
// PushControllerSurface.cpp.

namespace rhino
{

PushController::PushController(Session& s) : session(s)
{
    padMap.fill(-1);
    sounding.fill(-1);
    playNote = [this](const juce::MidiMessage& message) { session.sendControllerMidi(message); };
    loadLayout();
    session.listeners.add(this);
    updatePadMap();
}

PushController::~PushController()
{
    stop();
    session.listeners.remove(this);
}

void PushController::start()
{
    if (started)
        return;
    started = true;
    ticksToLook = 0;
    startTimerHz(30);
}

void PushController::stop()
{
    stopTimer();
    cancelPendingUpdate();
    endGestures();
    closePorts(true);
    started = false;
}

void PushController::timerCallback()
{
    endIdleGestures(juce::Time::getMillisecondCounterHiRes());
    // Once a second: plugging a Push in, or unplugging it, is noticed within
    // that, and nothing is enumerated on every frame.
    if (--ticksToLook <= 0)
    {
        ticksToLook = 30;
        lookForPush();
    }
    // The model announces most changes, but not a knob an automation lane is
    // moving or the transport starting from the keyboard, so the surface is
    // worked out again each tick and only what differs is sent.
    if (connected)
        redraw();
}

void PushController::lookForPush()
{
    juce::StringArray pushPorts;
    std::optional<juce::MidiDeviceInfo> userInput, userOutput;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (const auto role = push::portRole(device.name.toStdString()); role != push::PortRole::none)
        {
            pushPorts.add(device.name);
            if (role == push::PortRole::user && !userInput)
                userInput = device;
        }
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        if (const auto role = push::portRole(device.name.toStdString()); role != push::PortRole::none)
        {
            pushPorts.add(device.name);
            if (role == push::PortRole::user && !userOutput)
                userOutput = device;
        }

    if (connected)
    {
        if (!userInput || userInput->identifier != inputIdentifier)
        {
            closePorts(false);
            juce::Logger::writeToLog("Push disconnected");
            if (status) status("The Push was unplugged.");
        }
        return;
    }
    if (!userInput || !userOutput)
    {
        attemptsFailed = 0;
        reportedBusy = false;
        return;
    }
    // The engine has to let go first. Reserving disables its devices, and it
    // closes them a moment later, so the first try can fail and the next one,
    // a second on, finds the port free.
    session.reserveMidiPorts(pushPorts);
    if (openPorts(*userInput, *userOutput))
    {
        connected = true;
        attemptsFailed = 0;
        reportedBusy = false;
        juce::Logger::writeToLog("Push connected on " + userInput->name + " / " + userOutput->name);
        session.setControllerConnected(true);
        greet();
        if (status) status("Ableton Push connected. Arm a MIDI track to play it from the pads.");
        return;
    }
    if (++attemptsFailed >= 3 && !reportedBusy)
    {
        reportedBusy = true;
        juce::Logger::writeToLog("Push found but its port would not open: " + userInput->name);
        if (status)
            status("The Push is plugged in, but another program has it open. Close Ableton Live, or whatever "
                   "else is using it, and Rhino takes it over.");
    }
}

bool PushController::openPorts(const juce::MidiDeviceInfo& inputInfo, const juce::MidiDeviceInfo& outputInfo)
{
    auto opened = juce::MidiInput::openDevice(inputInfo.identifier, this);
    if (opened == nullptr)
        return false;
    auto sending = juce::MidiOutput::openDevice(outputInfo.identifier);
    if (sending == nullptr)
        return false;
    {
        const juce::SpinLock::ScopedLockType lock(padLock);
        sounding.fill(-1);
        padsHolding.fill(0);
    }
    output = std::move(sending);
    output->startBackgroundThread();
    input = std::move(opened);
    inputIdentifier = inputInfo.identifier;
    input->start();
    return true;
}

void PushController::closePorts(bool sayGoodbye)
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
    inputIdentifier = {};
    // A Push unplugged under a finger never sends the touch's release, so
    // nothing it held is held any more: an encoder left touched would keep
    // its gesture open for good.
    touched.fill(false);
    shift = muteHeld = soloHeld = false;
    endGestures();
    // Whatever a finger was holding when the port went, so no note is left
    // hanging on a track.
    std::vector<int> held;
    {
        const juce::SpinLock::ScopedLockType lock(padLock);
        for (int note = 0; note < 128; ++note)
            if (padsHolding[static_cast<size_t>(note)] > 0)
                held.push_back(note);
        sounding.fill(-1);
        padsHolding.fill(0);
    }
    for (const auto note : held)
        if (playNote) playNote(juce::MidiMessage::noteOff(1, note));
    // Whatever the port sent and nobody read yet is for a Push that is gone.
    controlRead.store(controlWritten.load(std::memory_order_acquire), std::memory_order_release);
    if (output != nullptr)
    {
        output->stopBackgroundThread();
        output->clearAllPendingMessages();
        // Dark pads and a line that says who had it, rather than a frozen
        // picture of the last thing Rhino showed.
        if (sayGoodbye)
        {
            for (int pad = 0; pad < push::padCount; ++pad)
                output->sendMessageNow(juce::MidiMessage::noteOn(1, push::padNoteFirst + pad, static_cast<juce::uint8>(0)));
            for (int controller = 0; controller < 128; ++controller)
                if (shown.buttons[static_cast<size_t>(controller)] != 0)
                    output->sendMessageNow(juce::MidiMessage::controllerEvent(1, controller, 0));
            for (int line = 0; line < push::displayLineCount; ++line)
            {
                const auto bytes = line == 1 ? push::displayLineSysex(line, push::displayText("                    Rhino has closed"))
                                             : push::clearLineSysex(line);
                output->sendMessageNow(juce::MidiMessage(bytes.data(), static_cast<int>(bytes.size())));
            }
        }
        output.reset();
    }
    pendingOutput.clear();
    shownValid = false;
    if (connected)
    {
        connected = false;
        session.setControllerConnected(false);
    }
}

// The Push starts in User mode, but one Live crashed out of is left in Live
// mode, which routes everything to the other port. Then Live's pad
// sensitivity, without which the pads need hitting harder for full velocity,
// pressure per pad, the strip as a pitch bend, and a full picture.
void PushController::greet()
{
    sendBytes(push::userModeSysex());
    sendBytes(push::padSensitivitySysex());
    sendBytes(push::polyAftertouchSysex());
    sendBytes(push::touchStripPitchBendSysex());
    invalidateSurface();
    redraw();
}

void PushController::send(const juce::MidiMessage& message)
{
    if (sendToPush)
        sendToPush(message);
    else
        pendingOutput.addEvent(message, 0);
}

void PushController::sendBytes(const push::Bytes& bytes)
{
    send(juce::MidiMessage(bytes.data(), static_cast<int>(bytes.size())));
}

// One block per redraw, handed to the output's own thread, which sends it in
// order: a sysex line can take the port a few milliseconds.
void PushController::flushOutput()
{
    if (output != nullptr && !pendingOutput.isEmpty())
        output->sendBlockOfMessages(pendingOutput, juce::Time::getMillisecondCounterHiRes(), 44100.0);
    pendingOutput.clear();
}

void PushController::invalidateSurface()
{
    shownValid = false;
}

void PushController::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    receive(message);
}

// The MIDI thread. Pads are played from here, translated through the pad
// map, with no hop to the message thread to add its jitter; the strip and
// channel pressure pass straight through. Everything else - buttons, encoders
// and their touches, sysex replies - is queued for the message thread and
// never reaches a track: an upper-row button is CC 20, and Forge learns CC
// 21 to 28 as its macros.
void PushController::receive(const juce::MidiMessage& message)
{
    if (message.isActiveSense() || message.isMidiClock())
        return;
    const auto isPad = (message.isNoteOnOrOff() || message.isAftertouch()) && push::isPadNote(message.getNoteNumber());
    if (isPad)
    {
        playPad(message);
        return;
    }
    if (message.isPitchWheel() || message.isChannelPressure())
    {
        if (playNote) playNote(message);
        return;
    }
    const auto written = controlWritten.load(std::memory_order_relaxed);
    if (written - controlRead.load(std::memory_order_acquire) < controlQueueSize)
    {
        controlQueue[static_cast<size_t>(written % controlQueueSize)] = message;
        controlWritten.store(written + 1, std::memory_order_release);
    }
    triggerAsyncUpdate();
}

void PushController::playPad(const juce::MidiMessage& message)
{
    const auto pad = message.getNoteNumber() - push::padNoteFirst;
    std::array<juce::MidiMessage, 2> played;
    auto count = 0;
    {
        const juce::SpinLock::ScopedLockType lock(padLock);
        auto& note = sounding[static_cast<size_t>(pad)];
        if (message.isNoteOn())
        {
            // A pad still down from before is let go first, which only a
            // missed release could cause.
            if (note >= 0 && --padsHolding[static_cast<size_t>(note)] == 0)
                played[static_cast<size_t>(count++)] = juce::MidiMessage::noteOff(1, note);
            note = padMap[static_cast<size_t>(pad)];
            if (note >= 0 && padsHolding[static_cast<size_t>(note)]++ == 0)
                played[static_cast<size_t>(count++)] = juce::MidiMessage::noteOn(
                    1, note, accent ? static_cast<juce::uint8>(127) : message.getVelocity());
        }
        else if (message.isNoteOff())
        {
            if (note >= 0 && --padsHolding[static_cast<size_t>(note)] == 0)
                played[static_cast<size_t>(count++)] = juce::MidiMessage::noteOff(1, note, message.getVelocity());
            note = -1;
        }
        else if (note >= 0)
        {
            played[static_cast<size_t>(count++)] = juce::MidiMessage::aftertouchChange(1, note, message.getAfterTouchValue());
        }
    }
    for (int i = 0; i < count; ++i)
    {
        auto& out = played[static_cast<size_t>(i)];
        out.setTimeStamp(message.getTimeStamp());
        if (playNote) playNote(out);
    }
    // A pad lights while it is held.
    if (!message.isAftertouch())
        triggerAsyncUpdate();
}

void PushController::handleAsyncUpdate()
{
    auto read = controlRead.load(std::memory_order_relaxed);
    const auto written = controlWritten.load(std::memory_order_acquire);
    for (; read != written; ++read)
    {
        // Copied out before the slot is released, so the writer cannot
        // overwrite a message while it is being handled.
        const auto message = controlQueue[static_cast<size_t>(read % controlQueueSize)];
        controlRead.store(read + 1, std::memory_order_release);
        handleControl(message);
    }
    if (connected || sendToPush)
        redraw();
}

void PushController::flush()
{
    cancelPendingUpdate();
    handleAsyncUpdate();
}

void PushController::editWillChange()
{
    endGestures();
}

void PushController::editDidChange()
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
    selectedPlugin = -1;
    parameterPage = 0;
    updatePadMap();
    invalidateSurface();
}

}
