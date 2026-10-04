#include "DeviceRackTest.h"
#include "../../DeviceRack.h"
#include "../../Session.h"
#include "audio/AutoTuneDevice.h"
#include "audio/UtilityDevice.h"
#include "midi/RhinoArpDevice.h"
#include <stdexcept>

namespace rhino
{
void runPatternDeviceRackTest()
{
    const auto require = [](bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    };
    Session session;
    require(session.utility != nullptr, "Session creates the Utility device on the starter track");
    require(session.trackCount() == 4 && !session.trackHasInstrument(0),
            "A new document opens with four empty tracks and nothing else");
    require(session.trackType(0) == Session::TrackType::midi
            && session.trackType(1) == Session::TrackType::audio
            && session.trackType(2) == Session::TrackType::midi
            && session.trackType(3) == Session::TrackType::audio,
            "The starter stack alternates MIDI and audio");
    require(session.audioUtility != nullptr,
            "The starter audio track brings its own Utility device");
    require(session.patternInstrument() == nullptr,
            "The starter track runs no instrument until one is dropped on it");
    require(session.addInstrument(Session::Instrument::FourOsc, 0).wasOk(),
            "The starter track takes an instrument");
    {
        const auto added = session.trackCount();
        require(session.addAudioTrack().wasOk(), "A track can be added to the starter stack");
        auto* addedTrack = te::getAudioTracks(*session.edit)[added];
        auto carriesUtility = false;
        for (auto* plugin : addedTrack->pluginList)
            carriesUtility = carriesUtility || dynamic_cast<UtilityDevice*>(plugin) != nullptr;
        require(carriesUtility, "An added track brings its own Utility device");
    }
    require(session.patternInstrument() != nullptr
            && session.patternInstrumentKind() == Session::Instrument::FourOsc,
            "The pattern track carries one instrument, 4OSC");
    auto* effectTrack = te::getAudioTracks(*session.edit)[1];
    const auto initialAudioPluginCount = effectTrack->pluginList.size();
    const auto patternDevices = session.deviceSlots(0);
    require(patternDevices.size() == 1 && patternDevices.front().kind == Session::DeviceKind::Instrument,
            "Device View shows the active pattern instrument, not dormant alternatives");
    require(session.deviceSlots(1).empty(), "Device View hides permanent audio-track channel infrastructure");
    require(session.addAudioEffect(Session::AudioEffect::Compressor, 0).wasOk()
            && session.addMidiEffect(Session::MidiEffect::RhinoArp, 0).wasOk(),
            "Pattern track accepts MIDI and audio processors around its instrument");
    const auto signalChain = session.deviceSlots(0);
    require(signalChain.size() == 3
            && signalChain[0].kind == Session::DeviceKind::MidiEffect
            && signalChain[1].kind == Session::DeviceKind::Instrument
            && signalChain[2].kind == Session::DeviceKind::AudioEffect,
            "Device View follows MIDI effect to instrument to audio effect signal order");
    DeviceRack deviceView(session);
    deviceView.setSize(900, 280);
    const auto panelsBeforeInsert = deviceView.devicePanels.size();
    require(session.addAudioEffect(Session::AudioEffect::Reverb, 0).wasOk(),
            "Device View test can append an effect after layout");
    require(deviceView.devicePanels.size() == panelsBeforeInsert + 1,
            "Adding an effect rebuilds the inline device panels");
    for (auto* panel : deviceView.devicePanels)
        require(panel != nullptr && panel->isVisible() && !panel->getBounds().isEmpty(),
                "Rebuilt device panels are visible and laid out immediately");
    require(deviceView.devicePanels.getFirst()->getHeight() == DeviceEditorPanel::standardHeight,
            "Device panels use the standard fixed control height");
    deviceView.setSize(900, 520);
    require(deviceView.devicePanels.getFirst()->getHeight() == DeviceEditorPanel::standardHeight,
            "Expanding Device View does not stretch device controls");
    deviceView.setSize(900, 120);
    require(deviceView.devicePanels.getFirst()->getHeight() == DeviceEditorPanel::standardHeight,
            "Shrinking Device View does not compress device controls");
    require(session.addAudioEffect(Session::AudioEffect::Equaliser, 1).wasOk(), "Audio FX browser action inserts EQ");
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

    // The pattern track now runs arp, instrument, compressor, reverb, which is
    // every ordering rule a drag can break.
    auto* patternTrack = te::getAudioTracks(*session.edit)[0];
    const auto chainBefore = session.deviceSlots(0);
    const auto patternPluginCount = patternTrack->pluginList.size();
    require(chainBefore.size() == 4, "The pattern chain carries four visible devices");
    const auto reverbPluginIndex = chainBefore[3].pluginIndex;
    require(session.moveDevice(0, 3, 2).wasOk(), "An audio effect can be dragged ahead of another");
    const auto chainAfter = session.deviceSlots(0);
    require(chainAfter.size() == 4 && chainAfter[2].name == chainBefore[3].name
            && chainAfter[3].name == chainBefore[2].name,
            "Moving a device reorders the chain and keeps every device in it");
    require(patternTrack->pluginList.size() == patternPluginCount,
            "A move adds and removes nothing, and leaves the hidden channel strip alone");
    require(chainAfter[2].pluginIndex < chainAfter[3].pluginIndex
            && patternTrack->pluginList[chainAfter[2].pluginIndex]->getPluginType() == chainBefore[3].type,
            "The visible order is the real processing order");
    session.undo();
    require(session.deviceSlots(0)[3].pluginIndex == reverbPluginIndex
            && session.deviceSlots(0)[3].name == chainBefore[3].name,
            "Undo puts a moved device back where it was");
    require(session.moveDevice(0, 3, 0).failed(), "An audio effect cannot be dragged in front of the instrument");
    require(session.moveDevice(0, 0, 3).failed(), "A MIDI effect cannot be dragged behind the instrument");
    require(session.moveDevice(0, 1, 0).failed(), "The instrument cannot be dragged in front of its MIDI FX");
    require(session.deviceSlots(0)[3].name == chainBefore[3].name,
            "A refused move leaves the chain exactly as it was");

    // The rack turns a cursor position into the gap it is nearest, which is
    // the whole of what a drop has to get right.
    DeviceRack chainView(session);
    chainView.selectTrack(0);
    chainView.setSize(1400, 280);
    require(chainView.devicePanels.size() == 4, "The rack shows a panel per visible device");
    auto* arpPanel = chainView.devicePanels.getFirst();
    require(arpPanel->preferredWidth() >= 800, "Rhino Arp has room for its full dedicated face");
    arpPanel->setSize(arpPanel->preferredWidth(), DeviceEditorPanel::standardHeight);
    std::vector<juce::Rectangle<int>> arpControls;
    int arpKnobCount = 0, arpChoiceCount = 0;
    juce::ComboBox* arpRoot = nullptr;
    juce::ComboBox* arpScale = nullptr;
    juce::TextButton* arpHold = nullptr;
    for (auto* child : arpPanel->getChildren())
    {
        if (child->isVisible())
            require(arpPanel->getLocalBounds().contains(child->getBounds()),
                    "Every visible Rhino Arp control stays inside its face");
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
        {
            ++arpKnobCount;
            arpControls.push_back(child->getBounds());
        }
        if (child->isVisible())
            if (auto* choice = dynamic_cast<juce::ComboBox*>(child))
            {
                ++arpChoiceCount;
                arpControls.push_back(choice->getBounds());
                if (choice->getTooltip().startsWith("Root:")) arpRoot = choice;
                if (choice->getTooltip().startsWith("Scale:")) arpScale = choice;
            }
        if (child->isVisible())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getName() == "Hold")
            {
                arpHold = button;
                arpControls.push_back(button->getBounds());
            }
    }
    require(arpKnobCount == 2 && arpChoiceCount == 10 && arpHold != nullptr
            && arpControls.size() == RhinoArpDevice::parameterCount,
            "Rhino Arp uses two continuous knobs, choice menus, and one Hold switch");
    require(arpRoot != nullptr && arpScale != nullptr && arpRoot->getNumItems() == 12
            && arpScale->getNumItems() == 3 && arpRoot->getText() == "G#" && arpScale->getText() == "Minor",
            "Rhino Arp exposes its root and scale as named dropdowns");
    for (size_t i = 0; i < arpControls.size(); ++i)
        for (auto j = i + 1; j < arpControls.size(); ++j)
            require(!arpControls[i].intersects(arpControls[j]), "Rhino Arp controls do not overlap");
    arpRoot->setSelectedId(1, juce::sendNotificationSync);
    arpHold->onClick();
    const auto arpParameters = session.deviceParameters(0, session.deviceSlots(0).front().pluginIndex);
    require(arpParameters[RhinoArpDevice::rootParameter].valueText == "C"
            && arpParameters[RhinoArpDevice::holdParameter].valueText == "Off",
            "Rhino Arp's dropdown and Hold switch write ordinary automatable parameters");
    arpRoot->setSelectedId(9, juce::sendNotificationSync);
    arpHold->onClick();
    const auto arpSnapshot = arpPanel->createComponentSnapshot(arpPanel->getLocalBounds());
    require(arpSnapshot.getWidth() == arpPanel->getWidth()
            && arpSnapshot.getHeight() == DeviceEditorPanel::standardHeight,
            "Rhino Arp's dedicated face paints at its promised size");
    if (const auto path = juce::SystemStats::getEnvironmentVariable("RHINO_ARP_SNAPSHOT", {});
        path.isNotEmpty())
    {
        const juce::File file(path);
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            require(juce::PNGImageFormat().writeImageToStream(arpSnapshot, *stream),
                    "Rhino Arp snapshot is writable");
        else
            require(false, "Rhino Arp snapshot path is writable");
    }
    const auto gapAt = [&chainView](juce::Point<int> point) { return chainView.dropGapFor(point); };
    const auto* second = chainView.devicePanels[1];
    require(gapAt({0, 60}) == 0, "A drop at the far left lands in front of the first device");
    require(gapAt({second->getX() + 12 + 2, 60}) == 1, "A drop left of a panel's centre lands in front of it");
    require(gapAt({second->getRight() + 12 - 2, 60}) == 2, "A drop right of a panel's centre lands behind it");
    require(gapAt({5000, 60}) == 4, "A drop past the last device lands at the end of the chain");

    // A face may put a control on the same row as the device's name, and the
    // name bar being the handle a chain is dragged by must not swallow it.
    require(session.addDevice("RhinoTune", 1).wasOk(), "An audio track takes Rhino Tune");
    chainView.selectTrack(1);
    chainView.setSize(1400, 280);
    const auto tuneSlots = session.deviceSlots(1);
    require(tuneSlots.size() == 1 && chainView.devicePanels.size() == 1, "The audio track shows Rhino Tune alone");
    auto* tune = dynamic_cast<AutoTuneDevice*>(session.devicePlugin(1, tuneSlots.front().pluginIndex));
    require(tune != nullptr, "Rhino Tune is the device the rack is showing");
    auto* tunePanel = chainView.devicePanels.getFirst();
    const auto press = [](juce::Component& target, juce::Point<float> point)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                &target, &target, juce::Time::getCurrentTime(), point,
                                juce::Time::getCurrentTime(), 1, false);
    };
    const auto liveBefore = tune->liveMode();
    tunePanel->mouseDown(press(*tunePanel, {static_cast<float>(tunePanel->getWidth() - 162), 12.0f}));
    require(tune->liveMode() != liveBefore, "Rhino Tune's LIVE toggle still answers a click on the name row");
}

int runArpSnapshotTest()
{
    try
    {
        runPatternDeviceRackTest();
        return 0;
    }
    catch (const std::exception& error)
    {
        juce::Logger::writeToLog("Rhino Arp snapshot: " + juce::String(error.what()));
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
}
