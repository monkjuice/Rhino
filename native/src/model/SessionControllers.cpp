#include "SessionInternal.h"

// A hardware controller Rhino drives itself rather than leaving to the
// engine: the Ableton Push (app/PushController).
//
// The engine cannot be the one holding its ports. A Push's pads send fixed
// notes in rows a sixth apart that have to be translated into a scale before
// any track hears them, and its buttons and encoders send controller messages
// no track should hear at all - and the engine has no hook that rewrites a
// note on its way in. So the controller opens the ports itself, and since a
// Windows MIDI port has exactly one owner, the engine is told to leave them
// alone: they are disabled in its device list, which it remembers, All Ins
// never merges them and no card offers them.
//
// What the pads play comes back in here, translated, onto the same two
// devices a played note reaches: All Ins, and an input of the controller's
// own that a track can name the way it names a keyboard.

namespace rhino
{
namespace
{
const char* const pushDeviceName = "Push";
}

juce::String Session::midiInputPushToken() { return midiInputDeviceToken(pushDeviceName); }

bool Session::isReservedMidiPort(const juce::String& portName) const
{
    return reservedMidiPorts.contains(portName);
}

void Session::reserveMidiPorts(const juce::StringArray& portNames)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    for (const auto& name : portNames)
        reservedMidiPorts.addIfNotAlreadyThere(name);
    disableReservedMidiPorts();
}

// Run on every rebuild of the engine's device list as well as on reserving,
// because the engine builds its MIDI list on a timer and keeps each device's
// enabled flag from the last run: a Push taken before the first scan landed
// was found switched on by it and opened under the controller, and its raw
// pads reached All Ins. Idempotent, because disabling a device rescans the
// list: only a device still switched on is touched, so the two cannot chase
// each other. A keyboard Rhino reads for its velocity curve (SessionKeyboards.cpp)
// is kept from the engine the same way; only its output is left alone.
void Session::disableReservedMidiPorts()
{
    const auto heldByRhino = [this](const juce::String& name)
    { return isReservedMidiPort(name) || isCurvedMidiPort(name); };
    auto& deviceManager = engine.getDeviceManager();
    for (const auto& candidate : deviceManager.getMidiInDevices())
        if (candidate != nullptr && dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()) == nullptr
            && candidate->isEnabled() && heldByRhino(candidate->getName()))
            candidate->setEnabled(false);
    // Audio settings lists MIDI inputs through JUCE's own device manager, which
    // opens what is ticked there behind the engine's back.
    auto& juceDevices = deviceManager.deviceManager;
    for (const auto& input : juce::MidiInput::getAvailableDevices())
        if (heldByRhino(input.name) && juceDevices.isMidiInputDeviceEnabled(input.identifier))
            juceDevices.setMidiInputDeviceEnabled(input.identifier, false);
    // The engine gives every enabled output an all-notes-off at each stop,
    // which on a Push would blank the pads every time the song stopped.
    for (int index = 0; index < deviceManager.getNumMidiOutDevices(); ++index)
        if (auto* output = deviceManager.getMidiOutDevice(index);
            output != nullptr && output->isEnabled() && isReservedMidiPort(output->getName()))
            output->setEnabled(false);
}

void Session::setControllerConnected(bool connected)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (controllerConnected == connected)
        return;
    controllerConnected = connected;
    refreshControllerTargets();
    // The cards' choosers list the controller's input only while it is here.
    sendSynchronousChangeMessage();
}

te::MidiInputDevice* Session::pushInputDevice() const
{
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get());
            virtualDevice != nullptr && !virtualDevice->useAllInputs && virtualDevice->getName() == pushDeviceName)
            return virtualDevice;
    return nullptr;
}

// Made on demand, as the typing keyboard's is, so a machine that never names
// it carries nothing extra in its device list. The rescan that makes it is
// asynchronous: awaitingMidiDeviceScan keeps arming quiet until it lands, and
// midiDevicesChanged arms again and repoints sendControllerMidi when it has.
te::MidiInputDevice* Session::ensurePushInputDevice()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (auto* existing = pushInputDevice())
        return existing;
    engine.getDeviceManager().createVirtualMidiDevice(pushDeviceName);
    auto* created = pushInputDevice();
    awaitingMidiDeviceScan = created == nullptr;
    refreshControllerTargets();
    return created;
}

void Session::refreshControllerTargets()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    std::shared_ptr<te::MidiInputDevice> allIns, push;
    for (const auto& candidate : engine.getDeviceManager().getMidiInDevices())
        if (auto* virtualDevice = dynamic_cast<te::VirtualMidiInputDevice*>(candidate.get()))
        {
            if (virtualDevice->useAllInputs)
                allIns = candidate;
            else if (virtualDevice->getName() == pushDeviceName)
                push = candidate;
        }
    {
        const juce::SpinLock::ScopedLockType lock(controllerTargetLock);
        std::swap(controllerAllIns, allIns);
        std::swap(controllerPush, push);
    }
    // The ones replaced go here, outside the lock and on this thread.
}

void Session::sendControllerMidi(const juce::MidiMessage& message)
{
    const juce::SpinLock::ScopedLockType lock(controllerTargetLock);
    if (controllerAllIns != nullptr)
        controllerAllIns->handleIncomingMidiMessage(message, controllerAllIns->getMPESourceID());
    if (controllerPush != nullptr)
        controllerPush->handleIncomingMidiMessage(message, controllerPush->getMPESourceID());
}

juce::String Session::controllerSetting(const juce::String& key, const juce::String& fallback)
{
    if (isCommandLineTestMode())
        return fallback;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    return properties.getValue("push." + key, fallback);
}

void Session::setControllerSetting(const juce::String& key, const juce::String& value)
{
    if (isCommandLineTestMode())
        return;
    juce::PropertiesFile properties(rhinoSettingsOptions());
    properties.setValue("push." + key, value);
    properties.saveIfNeeded();
}

}
