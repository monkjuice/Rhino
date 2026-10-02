#include "SessionInternal.h"

// Which audio input a track records from.
//
// This is SessionMidiInput.cpp's question one signal across, and it is
// answered the same way for the same reasons: the choice is a property of the
// *track*, so it travels with the track when the stack is reordered and is
// saved with the project, and the common answer writes nothing at all.
//
// Until this existed there was one answer for the whole document - whatever
// Audio settings had made the default, or the first input there was - which is
// right for a laptop with one built-in microphone and wrong for every
// interface with more than one socket on it. A guitar on input 1 and a vocal
// on input 2 is the first thing you want the moment a second audio track
// exists, and there was no way to say it.
//
// The empty token still means that default rather than naming a device. That
// is what keeps a project opened on another machine pointed at an input which
// is actually plugged into it, and it is the same reason All Ins is the empty
// token for MIDI.

namespace rhino
{
namespace
{
const char* const devicePrefix = "device:";

juce::String deviceNameFromToken(const juce::String& token)
{
    return token.substring(static_cast<int>(std::string_view(devicePrefix).size()));
}
}

const juce::Identifier trackAudioInputID {"rhinoAudioInput"};

juce::String Session::audioInputDefaultToken() { return {}; }
juce::String Session::audioInputNoneToken() { return "none"; }

juce::String Session::audioInputDeviceToken(const juce::String& deviceName)
{
    return devicePrefix + deviceName;
}

// The input Audio settings chose, or the first the device offers when nothing
// has been. One rule, shared by the chooser's default entry and by arming, so
// what the card promises is what a take is captured from.
te::WaveInputDevice* Session::defaultWaveInputDevice() const
{
    auto& deviceManager = engine.getDeviceManager();
    if (auto* chosen = deviceManager.getDefaultWaveInDevice())
        return chosen;
    for (auto* candidate : deviceManager.getWaveInputDevices())
        if (candidate != nullptr)
            return candidate;
    return nullptr;
}

// Built when the menu opens rather than held anywhere, because it describes
// the machine: an interface plugged in while Rhino was running should be in
// it, and one unplugged should not. Every input the engine offers is listed,
// enabled or not - an input nobody had switched on is exactly the one someone
// is about to ask to record from, and choosing it is what switches it on.
std::vector<Session::InputChoice> Session::audioInputChoices() const
{
    std::vector<InputChoice> choices;
    const auto* fallback = defaultWaveInputDevice();
    choices.push_back({audioInputDefaultToken(),
                       fallback != nullptr ? "Default In" : "Default In (none)",
                       fallback != nullptr});
    for (auto* candidate : engine.getDeviceManager().getWaveInputDevices())
        if (candidate != nullptr)
            choices.push_back({audioInputDeviceToken(candidate->getName()), candidate->getName(), true});
    choices.push_back({audioInputNoneToken(), "None", true});
    return choices;
}

juce::String Session::trackAudioInput(int track) const
{
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return audioInputDefaultToken();
    return tracks[track]->state.getProperty(trackAudioInputID, juce::String()).toString();
}

juce::String Session::trackAudioInputName(int track) const
{
    const auto token = trackAudioInput(track);
    if (token == audioInputNoneToken())
        return "None";
    if (token.startsWith(devicePrefix))
        return deviceNameFromToken(token);
    return "Default In";
}

juce::Result Session::setTrackAudioInput(int track, const juce::String& token)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    const auto tracks = te::getAudioTracks(*edit);
    if (!juce::isPositiveAndBelow(track, tracks.size()))
        return juce::Result::fail("The main output has no audio input of its own.");
    if (trackRecordInput(track) != RecordInput::audio)
        return juce::Result::fail(trackName(track) + " is not an audio track, so it takes no audio input.");
    if (trackAudioInput(track) == token)
        return juce::Result::ok();

    // Not an edit of the music, so it opens no undo transaction - the same
    // treatment arming, the MIDI input and row height all get. It is still
    // written to the track, so it is saved and it follows the track when the
    // stack is reordered.
    if (token.isEmpty())
        tracks[track]->state.removeProperty(trackAudioInputID, nullptr);
    else
        tracks[track]->state.setProperty(trackAudioInputID, token, nullptr);
    const auto applied = applyRecordArming();
    markModified();
    sendSynchronousChangeMessage();
    return applied;
}

// What a track's setting resolves to. Null means nothing to record from, which
// is both what None asks for and what a device named by a project made on
// another machine comes to.
te::WaveInputDevice* Session::audioInputDeviceForTrack(int track) const
{
    const auto token = trackAudioInput(track);
    if (token == audioInputNoneToken())
        return nullptr;
    if (token.startsWith(devicePrefix))
    {
        const auto wanted = deviceNameFromToken(token);
        for (auto* candidate : engine.getDeviceManager().getWaveInputDevices())
            if (candidate != nullptr && candidate->getName() == wanted)
                return candidate;
        return nullptr;
    }
    return defaultWaveInputDevice();
}

}
