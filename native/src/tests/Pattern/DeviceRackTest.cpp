#include "DeviceRackTest.h"
#include "../../Session.h"
#include <stdexcept>

namespace theta
{
void runPatternDeviceRackTest()
{
    const auto require = [](bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    };
    Session session;
    require(session.utility != nullptr && session.audioUtility != nullptr && session.synth != nullptr
            && session.thetaWave != nullptr && session.drums != nullptr,
            "Session creates synth, wavetable, drum, and Utility devices");
    auto* effectTrack = te::getAudioTracks(*session.edit)[1];
    const auto initialAudioPluginCount = effectTrack->pluginList.size();
    const auto patternDevices = session.deviceSlots(0);
    require(patternDevices.size() == 1 && patternDevices.front().kind == Session::DeviceKind::Instrument,
            "Device View shows the active pattern instrument, not dormant alternatives");
    require(session.deviceSlots(1).empty(), "Device View hides permanent audio-track channel infrastructure");
    require(session.addAudioEffect(Session::AudioEffect::Compressor, 0).wasOk()
            && session.addMidiEffect(Session::MidiEffect::ThetaArp, 0).wasOk(),
            "Pattern track accepts MIDI and audio processors around its instrument");
    const auto signalChain = session.deviceSlots(0);
    require(signalChain.size() == 3
            && signalChain[0].kind == Session::DeviceKind::MidiEffect
            && signalChain[1].kind == Session::DeviceKind::Instrument
            && signalChain[2].kind == Session::DeviceKind::AudioEffect,
            "Device View follows MIDI effect to instrument to audio effect signal order");
    require(session.addAudioEffect(Session::AudioEffect::Equaliser).wasOk(), "Audio FX browser action inserts EQ");
    require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Audio FX insert grows the audio track chain");
    auto audioDevices = session.deviceSlots(1);
    require(audioDevices.size() == 1 && audioDevices.front().kind == Session::DeviceKind::AudioEffect,
            "Device View exposes the inserted audio effect without channel-strip nodes");
    require(audioDevices.back().removable && juce::isPositiveAndBelow(audioDevices.back().pluginIndex, effectTrack->pluginList.size())
            && effectTrack->pluginList[audioDevices.back().pluginIndex]->getPluginType() == audioDevices.back().type,
            "Visible devices retain their real processing-graph positions");
    const auto effectPluginIndex = audioDevices.back().pluginIndex;
    require(session.toggleDeviceEnabled(1, effectPluginIndex).wasOk(), "Device View bypasses selected effect");
    require(!effectTrack->pluginList[effectPluginIndex]->isEnabled(), "Bypass disables the effect plugin");
    session.undo();
    require(effectTrack->pluginList[effectPluginIndex]->isEnabled(), "Undo restores effect enabled state");
    require(session.deleteDevice(1, effectPluginIndex).wasOk(), "Device View deletes inserted effect");
    require(effectTrack->pluginList.size() == initialAudioPluginCount, "Delete removes inserted audio effect");
    session.undo();
    require(effectTrack->pluginList.size() == initialAudioPluginCount + 1, "Undo restores deleted audio effect");
    session.undo();
    require(effectTrack->pluginList.size() == initialAudioPluginCount, "Undo removes inserted audio effect");
}
}
