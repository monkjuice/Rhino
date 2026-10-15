#include "SessionInternal.h"
#include <algorithm>
#include <bitset>

// A keyboard's velocity curve, and the port Rhino reads to apply it.
//
// The engine has no hook that rewrites a note on its way in - the gap
// SessionControllers.cpp meets with the Push - so a keyboard with a curve is
// read by Rhino itself, through a juce::MidiInput of its own, and kept from
// the engine the way a controller's ports are. Each note-on is bent by the
// curve on the MIDI thread and delivered where the engine would have put it:
// onto All Ins, and onto a virtual device standing in for the port, which a
// track that names the port listens to instead. Arming, monitoring and
// recording see an ordinary MIDI input, so a take records what was heard.
//
// A Linear port is left to the engine untouched, which keeps a machine that
// never sets a curve on exactly the path it always had. The price of a curve
// is that Rhino holds the port for as long as it runs, as it holds a Push:
// Windows gives a MIDI port one owner, so nothing else can have it meanwhile.
//
// The curves are a preference of the machine, kept in the settings file by
// port name, and never read or written under the test runners.

namespace rhino
{
namespace
{
const juce::String curveSettingPrefix {"keyboard.velocityCurve."};
const juce::String curvedDeviceSuffix {" (curved)"};

// The device a curved port's notes arrive on. The engine keeps the virtual
// devices it has made between runs, so the next launch finds it waiting.
juce::String curvedDeviceName(const juce::String& port)
{
    return port + curvedDeviceSuffix;
}

juce::String curveName(VelocityCurve curve)
{
    const auto name = velocityCurveInfo(curve).name;
    return juce::String(name.data(), name.size());
}

// The last note a port played, in one word, so a panel never reads one note's
// struck velocity beside another note's played one.
struct VelocityMeter
{
    void record(int struck, int played) noexcept
    {
        auto notes = ((packed.load(std::memory_order_relaxed) >> 16) + 1) & 0xffffu;
        if (notes == 0)
            notes = 1;
        packed.store((notes << 16) | (static_cast<juce::uint32>(struck & 0x7f) << 8)
                         | static_cast<juce::uint32>(played & 0x7f),
                     std::memory_order_relaxed);
    }

    Session::KeyboardNote read() const noexcept
    {
        const auto value = packed.load(std::memory_order_relaxed);
        return {static_cast<int>((value >> 8) & 0x7f), static_cast<int>(value & 0x7f), value >> 16};
    }

    std::atomic<juce::uint32> packed {0};
};

// The same reading for a port the engine reads: what is struck is played.
// Called on the MIDI thread, under the device's own listener lock, which
// removeListener takes too, so taking it off from here is safe.
struct VelocityListener final : te::PhysicalMidiInputDevice::Listener
{
    VelocityListener(std::shared_ptr<VelocityMeter> reading, std::weak_ptr<te::MidiInputDevice> source)
        : meter(std::move(reading)), device(std::move(source))
    {
    }

    void handleIncomingMidiMessage(const juce::MidiMessage& message) override
    {
        if (message.isNoteOn())
            meter->record(message.getVelocity(), message.getVelocity());
    }

    void detach()
    {
        if (auto source = device.lock())
            if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*>(source.get()))
                physical->removeListener(this);
        device.reset();
    }

    const std::shared_ptr<VelocityMeter> meter;
    std::weak_ptr<te::MidiInputDevice> device;
};

// A port Rhino reads for its curve. The MIDI thread delivers through it; the
// message thread opens, closes and repoints it.
struct KeyboardPort final : juce::MidiInputCallback
{
    KeyboardPort(juce::String portName, std::shared_ptr<VelocityMeter> reading)
        : name(std::move(portName)), meter(std::move(reading))
    {
    }

    ~KeyboardPort() override
    {
        close();
        point({}, {});
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        deliver(message);
    }

    // The curve, then the two devices a played note reaches. Any thread.
    void deliver(const juce::MidiMessage& incoming)
    {
        auto message = incoming;
        if (incoming.isNoteOn())
        {
            const auto struck = incoming.getVelocity();
            const auto played = applyVelocityCurve(static_cast<VelocityCurve>(curve.load(std::memory_order_relaxed)),
                                                   struck);
            message = juce::MidiMessage::noteOn(incoming.getChannel(), incoming.getNoteNumber(),
                                                static_cast<juce::uint8>(played));
            message.setTimeStamp(incoming.getTimeStamp());
            meter->record(struck, played);
        }
        const juce::SpinLock::ScopedLockType lock(targetLock);
        if (message.isNoteOn())
            held[static_cast<size_t>(message.getChannel() - 1)].set(static_cast<size_t>(message.getNoteNumber()));
        else if (message.isNoteOff())
            held[static_cast<size_t>(message.getChannel() - 1)].reset(static_cast<size_t>(message.getNoteNumber()));
        send(message);
    }

    // Lets go of whatever the keys were holding, so a port that goes -
    // unplugged, or handed back to the engine - leaves no note hanging.
    void releaseHeld()
    {
        const juce::SpinLock::ScopedLockType lock(targetLock);
        for (size_t channel = 0; channel < held.size(); ++channel)
            for (size_t note = 0; note < held[channel].size(); ++note)
                if (held[channel][note])
                    send(juce::MidiMessage::noteOff(static_cast<int>(channel) + 1, static_cast<int>(note)));
        for (auto& notes : held)
            notes.reset();
    }

    void close()
    {
        if (input != nullptr)
        {
            input->stop();
            input.reset();
        }
        identifier = {};
        releaseHeld();
    }

    // The devices the list holds now. Message thread.
    void point(std::shared_ptr<te::MidiInputDevice> merge, std::shared_ptr<te::MidiInputDevice> device)
    {
        {
            const juce::SpinLock::ScopedLockType lock(targetLock);
            std::swap(allIns, merge);
            std::swap(own, device);
        }
        // The ones replaced go here, outside the lock and on this thread.
    }

    const juce::String name;
    const std::shared_ptr<VelocityMeter> meter;
    std::atomic<int> curve {static_cast<int>(VelocityCurve::linear)};
    std::unique_ptr<juce::MidiInput> input;
    juce::String identifier;
    // Opens tried and refused since it was last open or last gone. The first
    // can fail as the engine lets go of the port, so one alone means nothing.
    int refusals = 0;

private:
    void send(const juce::MidiMessage& message)
    {
        if (allIns != nullptr)
            allIns->handleIncomingMidiMessage(message, allIns->getMPESourceID());
        if (own != nullptr)
            own->handleIncomingMidiMessage(message, own->getMPESourceID());
    }

    juce::SpinLock targetLock;
    std::shared_ptr<te::MidiInputDevice> allIns, own;
    std::array<std::bitset<128>, 16> held;
};
}

struct Session::Keyboards
{
    explicit Keyboards(Session& owner) : retry(owner) {}

    // Looks again once a second while a port with a curve is not open: one
    // another program held may have been let go, and one unplugged may be back
    // before the engine's own rescan notices.
    struct Retry final : juce::Timer
    {
        explicit Retry(Session& owner) : session(owner) {}
        void timerCallback() override
        {
            if (session.syncKeyboardPorts())
                session.rearmListeningMidiTracks();
        }
        Session& session;
    } retry;

    KeyboardPort* port(const juce::String& name) const
    {
        for (const auto& candidate : ports)
            if (candidate->name == name)
                return candidate.get();
        return nullptr;
    }

    std::shared_ptr<VelocityMeter> meter(const juce::String& name)
    {
        auto& reading = meters[name];
        if (reading == nullptr)
            reading = std::make_shared<VelocityMeter>();
        return reading;
    }

    void detachListeners()
    {
        for (auto& listener : listeners)
            listener->detach();
        listeners.clear();
    }

    // What each port is set to; absent is Linear.
    std::map<juce::String, VelocityCurve> curves;
    // Every port with a curve, open or not.
    std::vector<std::unique_ptr<KeyboardPort>> ports;
    std::map<juce::String, std::shared_ptr<VelocityMeter>> meters;
    std::vector<std::unique_ptr<VelocityListener>> listeners;
    bool started = false;
    // Taking or handing back a port rescans the device list, whose handler
    // syncs again; that is noted and run once the sync in hand is done.
    bool syncing = false, syncAgain = false;
};

void Session::KeyboardsDeleter::operator()(Keyboards* keyboards) const
{
    delete keyboards;
}

void Session::initialiseKeyboards()
{
    keyboards.reset(new Keyboards(*this));
    if (isCommandLineTestMode())
        return;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    const auto& settings = properties.getAllProperties();
    for (const auto& key : settings.getAllKeys())
        if (key.startsWith(curveSettingPrefix))
            if (const auto curve = velocityCurveFromId(settings[key].toStdString()); curve != VelocityCurve::linear)
                keyboards->curves[key.substring(curveSettingPrefix.length())] = curve;
    // Rhino's from the start, though not opened until the shell is up: a
    // project opened from the command line arms its tracks before then, and a
    // track that names the keyboard is armed on its stand-in rather than on a
    // port the engine would open and then have to give up.
    for (const auto& [name, curve] : keyboards->curves)
    {
        keyboards->ports.push_back(std::make_unique<KeyboardPort>(name, keyboards->meter(name)));
        keyboards->ports.back()->curve = static_cast<int>(curve);
    }
}

void Session::startKeyboardPorts()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    keyboards->started = true;
    if (syncKeyboardPorts())
        rearmListeningMidiTracks();
}

void Session::closeKeyboardPorts()
{
    if (keyboards == nullptr)
        return;
    keyboards->retry.stopTimer();
    keyboards->started = false;
    for (auto& port : keyboards->ports)
    {
        port->close();
        port->point({}, {});
    }
    keyboards->detachListeners();
}

bool Session::isCurvedMidiPort(const juce::String& portName) const
{
    return keyboards != nullptr && keyboards->port(portName) != nullptr;
}

te::MidiInputDevice* Session::curvedInputDevice(const juce::String& portName) const
{
    const auto wanted = curvedDeviceName(portName);
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get());
            virtualDevice != nullptr && !virtualDevice->useAllInputs && virtualDevice->getName() == wanted)
            return virtualDevice;
    return nullptr;
}

juce::StringArray Session::keyboardPortNames() const
{
    juce::StringArray names;
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (candidate != nullptr && dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()) == nullptr
            && !isReservedMidiPort(candidate->getName()))
            names.addIfNotAlreadyThere(candidate->getName());
    // One with a curve that is not plugged in, so its setting can still be
    // seen and taken off.
    for (const auto& entry : keyboards->curves)
        names.addIfNotAlreadyThere(entry.first);
    return names;
}

VelocityCurve Session::velocityCurve(const juce::String& portName) const
{
    const auto found = keyboards->curves.find(portName);
    return found == keyboards->curves.end() ? VelocityCurve::linear : found->second;
}

void Session::setVelocityCurve(const juce::String& portName, VelocityCurve curve)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    // A controller's ports are its own, and what it plays is already shaped.
    if (portName.isEmpty() || isReservedMidiPort(portName) || velocityCurve(portName) == curve)
        return;
    if (curve == VelocityCurve::linear)
        keyboards->curves.erase(portName);
    else
        keyboards->curves[portName] = curve;
    if (!isCommandLineTestMode())
    {
        juce::PropertiesFile properties(rhinoSettingsOptions());
        const auto id = velocityCurveInfo(curve).id;
        if (curve == VelocityCurve::linear)
            properties.removeValue(curveSettingPrefix + portName);
        else
            properties.setValue(curveSettingPrefix + portName, juce::String(id.data(), id.size()));
        properties.saveIfNeeded();
    }
    juce::Logger::writeToLog("Rhino: " + portName + " plays through the " + curveName(curve) + " velocity curve");
    if (syncKeyboardPorts())
        rearmListeningMidiTracks();
}

Session::KeyboardNote Session::lastKeyboardNote(const juce::String& portName) const
{
    const auto found = keyboards->meters.find(portName);
    return found == keyboards->meters.end() ? KeyboardNote {} : found->second->read();
}

Session::KeyboardPortState Session::keyboardPortState(const juce::String& portName) const
{
    const auto* port = keyboards->port(portName);
    if (port == nullptr)
        return KeyboardPortState::engine;
    if (port->input != nullptr)
        return KeyboardPortState::open;
    return port->refusals >= 2 ? KeyboardPortState::busy : KeyboardPortState::absent;
}

void Session::receiveKeyboardMidi(const juce::String& portName, const juce::MidiMessage& message)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (auto* port = keyboards->port(portName))
        port->deliver(message);
}

bool Session::syncKeyboardPorts()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (keyboards == nullptr)
        return false;
    auto& state = *keyboards;
    if (state.syncing)
    {
        state.syncAgain = true;
        return false;
    }
    const juce::ScopedValueSetter<bool> guard(state.syncing, true);
    auto& deviceManager = engine.getDeviceManager();
    auto changedHands = false;
    do
    {
        state.syncAgain = false;

        // A different curve on a port Rhino already reads takes effect at once,
        // take or no take: no track moves for it.
        for (auto& port : state.ports)
            port->curve = static_cast<int>(velocityCurve(port->name));

        // Taking a port, or handing one back, moves the tracks that play from
        // it onto another device, which under a take would cut it.
        // finishRecording syncs again once the take is done.
        if (!isRecording())
        {
            for (auto it = state.ports.begin(); it != state.ports.end();)
            {
                if (velocityCurve((*it)->name) != VelocityCurve::linear)
                {
                    ++it;
                    continue;
                }
                const auto name = (*it)->name;
                it = state.ports.erase(it);
                juce::Logger::writeToLog("Rhino: " + name + " is the engine's again");
                changedHands = true;
            }
            for (const auto& [name, curve] : state.curves)
            {
                if (curve == VelocityCurve::linear || state.port(name) != nullptr)
                    continue;
                auto port = std::make_unique<KeyboardPort>(name, state.meter(name));
                port->curve = static_cast<int>(curve);
                state.ports.push_back(std::move(port));
                changedHands = true;
            }
            // Every curved port has its device; any the engine still keeps for
            // a port with no curve goes, so the list holds no stale ones.
            std::vector<te::VirtualMidiInputDevice*> stale;
            for (const auto& candidate : deviceManager.getMidiInDevices())
                if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get());
                    virtualDevice != nullptr && !virtualDevice->useAllInputs
                    && virtualDevice->getName().endsWith(curvedDeviceSuffix)
                    && !isCurvedMidiPort(virtualDevice->getName().dropLastCharacters(curvedDeviceSuffix.length())))
                    stale.push_back(virtualDevice);
            for (auto* device : stale)
                deviceManager.deleteVirtualMidiDevice(*device);
            for (const auto& port : state.ports)
                if (curvedInputDevice(port->name) == nullptr)
                {
                    deviceManager.createVirtualMidiDevice(curvedDeviceName(port->name));
                    if (curvedInputDevice(port->name) == nullptr)
                        awaitingMidiDeviceScan = true;
                }
        }
        // The engine lets go of every curved port, and keeps letting go of it,
        // because it remembers each device's switch from the last run. Only a
        // device still switched on is touched.
        disableReservedMidiPorts();

        // Open what can be opened, and let go of what has gone.
        const auto available = juce::MidiInput::getAvailableDevices();
        auto waiting = false;
        for (auto& port : state.ports)
        {
            const auto info = std::find_if(available.begin(), available.end(),
                                           [&port](const juce::MidiDeviceInfo& device) { return device.name == port->name; });
            const auto present = info != available.end();
            if (port->input != nullptr && (!present || info->identifier != port->identifier))
            {
                port->close();
                juce::Logger::writeToLog("Rhino: " + port->name + " was unplugged");
            }
            if (!present)
                port->refusals = 0;
            if (port->input == nullptr && present && state.started)
            {
                if (auto opened = juce::MidiInput::openDevice(info->identifier, port.get()))
                {
                    port->identifier = info->identifier;
                    port->input = std::move(opened);
                    port->input->start();
                    port->refusals = 0;
                    juce::Logger::writeToLog("Rhino: reading " + port->name + " through the "
                                             + curveName(velocityCurve(port->name)) + " velocity curve");
                }
                else if (++port->refusals == 2)
                {
                    juce::Logger::writeToLog("Rhino: " + port->name + " would not open; another program has it");
                }
            }
            waiting = waiting || (state.started && port->input == nullptr);
        }
        if (!waiting)
            state.retry.stopTimer();
        else if (!state.retry.isTimerRunning())
            state.retry.startTimer(1000);

        // Each port delivers to the devices the list holds now.
        std::shared_ptr<te::MidiInputDevice> allIns;
        std::map<juce::String, std::shared_ptr<te::MidiInputDevice>> standIns;
        for (const auto& candidate : deviceManager.getMidiInDevices())
            if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()))
            {
                if (virtualDevice->useAllInputs)
                    allIns = candidate;
                else
                    standIns[virtualDevice->getName()] = candidate;
            }
        for (auto& port : state.ports)
        {
            const auto own = standIns.find(curvedDeviceName(port->name));
            port->point(allIns, own == standIns.end() ? nullptr : own->second);
        }

        // The panel's reading of the ports the engine reads.
        state.detachListeners();
        for (const auto& candidate : deviceManager.getMidiInDevices())
            if (auto* physical = dynamic_cast<te::PhysicalMidiInputDevice*>(candidate.get());
                physical != nullptr && !isReservedMidiPort(physical->getName()) && !isCurvedMidiPort(physical->getName()))
            {
                auto listener = std::make_unique<VelocityListener>(state.meter(physical->getName()), candidate);
                physical->addListener(listener.get());
                state.listeners.push_back(std::move(listener));
            }
    }
    while (state.syncAgain);
    return changedHands;
}

}
