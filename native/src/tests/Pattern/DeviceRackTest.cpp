#include "DeviceRackTest.h"
#include "../../DeviceRack.h"
#include "../../Session.h"
#include "../../Theme.h"
#include "ContentLibrary.h"
#include "audio/AutoTuneDevice.h"
#include "audio/UtilityDevice.h"
#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace rhino
{
void runPatternDeviceRackTest()
{
    const auto require = [](bool valid, const char* message)
    {
        if (!valid) throw std::runtime_error(message);
    };
    Session session;
    const auto hasUtility = [&session](int track)
    {
        for (auto* plugin : te::getAudioTracks(*session.edit)[track]->pluginList)
            if (dynamic_cast<UtilityDevice*>(plugin) != nullptr)
                return true;
        return false;
    };
    require(hasUtility(0), "Session creates the Utility device on the starter track");
    require(session.trackCount() == 4 && !session.trackHasInstrument(0),
            "A new document opens with four empty tracks and nothing else");
    require(session.trackType(0) == Session::TrackType::midi
            && session.trackType(1) == Session::TrackType::audio
            && session.trackType(2) == Session::TrackType::midi
            && session.trackType(3) == Session::TrackType::audio,
            "The starter stack alternates MIDI and audio");
    require(hasUtility(1), "The starter audio track brings its own Utility device");
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
    // A MIDI effect added to a track with no instrument yet goes where it will
    // still be in front of one: an instrument added afterwards lands behind it.
    require(!session.trackHasInstrument(2)
                && session.addMidiEffect(Session::MidiEffect::RhinoArp, 2).wasOk()
                && session.addInstrument(Session::Instrument::FourOsc, 2).wasOk(),
            "A MIDI effect, then an instrument, on an empty MIDI track");
    const auto laterChain = session.deviceSlots(2);
    require(laterChain.size() == 2 && laterChain[0].kind == Session::DeviceKind::MidiEffect
                && laterChain[1].kind == Session::DeviceKind::Instrument,
            "The MIDI effect added first still runs before the instrument added after it");
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
    juce::TextButton* arpBeatRate = nullptr;
    juce::TextButton* arpMillisecondRate = nullptr;
    juce::Slider* arpInterval = nullptr;
    juce::Slider* arpRepeats = nullptr;
    juce::Slider* arpSteps = nullptr;
    juce::Slider* arpDistance = nullptr;
    juce::Slider* arpOffset = nullptr;
    juce::ComboBox* arpStyle = nullptr;
    juce::ComboBox* arpRetrigger = nullptr;
    for (auto* child : arpPanel->getChildren())
    {
        if (child->isVisible())
            require(arpPanel->getLocalBounds().contains(child->getBounds()),
                    "Every visible Rhino Arp control stays inside its face");
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
        {
            ++arpKnobCount;
            arpControls.push_back(child->getBounds());
            auto* slider = dynamic_cast<juce::Slider*>(child);
            if (slider->getTooltip().startsWith("Interval:")) arpInterval = slider;
            if (slider->getTooltip().startsWith("Repeats:")) arpRepeats = slider;
            if (slider->getTooltip().startsWith("Steps:")) arpSteps = slider;
            if (slider->getTooltip().startsWith("Distance:")) arpDistance = slider;
            if (slider->getTooltip().startsWith("Offset:")) arpOffset = slider;
        }
        if (child->isVisible())
            if (auto* choice = dynamic_cast<juce::ComboBox*>(child))
            {
                ++arpChoiceCount;
                arpControls.push_back(choice->getBounds());
                if (choice->getTooltip().startsWith("Root:")) arpRoot = choice;
                if (choice->getTooltip().startsWith("Scale:")) arpScale = choice;
                if (choice->getTooltip().startsWith("Style:")) arpStyle = choice;
                if (choice->getTooltip().startsWith("Retrigger:")) arpRetrigger = choice;
            }
        if (child->isVisible())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getName() == "Hold")
            {
                arpHold = button;
                arpControls.push_back(button->getBounds());
            }
        if (child->isVisible())
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr
                && (button->getName() == "Beat rate" || button->getName() == "Millisecond rate"))
            {
                if (button->getName() == "Beat rate") arpBeatRate = button;
                if (button->getName() == "Millisecond rate") arpMillisecondRate = button;
                arpControls.push_back(button->getBounds());
            }
    }
    require(arpKnobCount == 7 && arpChoiceCount == 5 && arpHold != nullptr
            && arpBeatRate != nullptr && arpMillisecondRate != nullptr
            && arpControls.size() == RhinoArpDevice::parameterCount,
            "Rhino Arp uses tactile rate, offset, interval, repeat, steps, distance, and gate controls");
    require(arpInterval != nullptr
            && arpInterval->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
            && arpRepeats != nullptr
            && arpRepeats->getSliderStyle() == juce::Slider::LinearBarVertical,
            "Rhino Arp presents Interval as a knob and Repeats as a vertically dragged value bar");
    require(arpSteps != nullptr && arpSteps->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
            "Rhino Arp presents Steps as a knob");
    // A LinearBarVertical is what gives Offset its up-and-down drag; the
    // property is what makes the theme fill it across instead of upwards.
    require(arpOffset != nullptr && arpOffset->getSliderStyle() == juce::Slider::LinearBarVertical
            && static_cast<bool>(arpOffset->getProperties()[barFillsAcross])
            && !arpOffset->getSliderSnapsToMousePosition()
            && !static_cast<bool>(arpRepeats->getProperties()[barFillsAcross]),
            "Rhino Arp's Offset is dragged up and down and fills left to right, unlike Repeats");
    require(arpStyle != nullptr && arpRetrigger != nullptr && arpRoot != nullptr
            && arpStyle->getRight() <= arpPanel->visualArea.getX()
            && arpHold->getRight() <= arpPanel->visualArea.getX()
            && arpMillisecondRate->getRight() <= arpPanel->visualArea.getX()
            && arpRetrigger->getX() >= arpPanel->visualArea.getRight()
            && arpRoot->getX() >= arpPanel->visualArea.getRight(),
            "Rhino Arp's Pattern and Rate stand left of its sequence display, Timing and Harmony right of it");
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
    arpMillisecondRate->onClick();
    const auto freeRateParameters = session.deviceParameters(0, session.deviceSlots(0).front().pluginIndex);
    require(freeRateParameters[RhinoArpDevice::rateModeParameter].valueText == "Milliseconds",
            "Rhino Arp's tiny rate buttons switch the scheduler between beats and milliseconds");
    arpBeatRate->onClick();
    arpRoot->setSelectedId(9, juce::sendNotificationSync);
    arpHold->onClick();
    // The knob is turned the way the hand turns it, so the refresh that
    // follows is the rack's own, not one the test calls for.
    require(arpDistance != nullptr && arpDistance->isEnabled(),
            "Rhino Arp's Distance is available while there are steps for it to move");
    arpSteps->setValue(0.0, juce::sendNotificationSync);
    require(session.deviceParameters(0, session.deviceSlots(0).front().pluginIndex)
                [RhinoArpDevice::stepsParameter].value == 0.0f
            && !arpDistance->isEnabled(),
            "Rhino Arp's Distance is unavailable while Steps is 0");
    arpSteps->setValue(2.0, juce::sendNotificationSync);
    require(arpDistance->isEnabled(), "Turning Steps back up makes Distance available again");
    // Painted in the app's own look, or the reference image would show JUCE's
    // stock knobs and an Offset bar with no fill and no number in it.
    Theme snapshotTheme;
    arpPanel->setLookAndFeel(&snapshotTheme);
    const auto arpSnapshot = arpPanel->createComponentSnapshot(arpPanel->getLocalBounds());
    arpPanel->setLookAndFeel(nullptr);
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
    // The shell's floor for the Device View: a chain too wide for the rack
    // grows a scrollbar, and a whole device face must still show above it.
    chainView.setSize(900, DeviceRack::minimumHeight);
    require(chainView.chainViewport.getHorizontalScrollBar().isVisible()
            && chainView.chainViewport.getViewHeight() >= DeviceEditorPanel::standardHeight,
            "A Device View at its minimum height shows a whole device above the chain's scrollbar");
    chainView.setSize(1400, 280);
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

    // The engine plays the lanes and announces nothing, so the rack reads a
    // knob a lane is moving back for itself, face by face.
    chainView.selectTrack(0);
    const auto synthSlot = session.deviceSlots(0)[1].pluginIndex;
    auto* synth = session.devicePlugin(0, synthSlot);
    require(dynamic_cast<te::FourOscPlugin*>(synth) != nullptr, "The 4OSC sits behind the arpeggiator");
    const auto attack = session.deviceParameters(0, synthSlot).front();
    require(session.setTrackAutomationPoints({0, synthSlot, 0}, {{0.0, attack.minimum}, {2.0, attack.maximum}}).wasOk(),
            "A lane is drawn on the 4OSC's first knob");
    auto* synthPanel = chainView.devicePanels[1];
    require(synthPanel->followsAutomation() && !chainView.devicePanels.getFirst()->followsAutomation(),
            "Only the face with a knob on a lane follows the engine");
    for (auto* parameter : synth->getAutomatableParameters())
        if (parameter->hasAutomationPoints())
            parameter->updateToFollowCurve(tracktion::core::TimePosition::fromSeconds(1.0));
    chainView.followAutomation();
    require(std::abs(synthPanel->parameters.front().value - (attack.minimum + attack.maximum) * 0.5f) < 0.02f,
            "The rack shows where the engine has moved a knob on a lane");
    require(session.clearTrackAutomationPoints({0, synthSlot, 0}).wasOk() && !chainView.devicePanels[1]->followsAutomation(),
            "A face stops following once its lane is cleared");

    // A device on the SDK without a face of its own gets one generated from
    // what its controls declare: sections, a chooser for a choice, a switch
    // for a toggle, and knobs that step and reset the way they declare.
    require(session.addDevice("RhinoFM", 2).wasOk(), "A MIDI track takes Rhino FM");
    chainView.selectTrack(2);
    chainView.setSize(2400, 280);
    DeviceEditorPanel* fmPanel = nullptr;
    for (auto* panel : chainView.devicePanels)
        if (panel->deviceId == "RhinoFM")
            fmPanel = panel;
    require(fmPanel != nullptr && fmPanel->face == DeviceEditorPanel::Face::Generated,
            "Rhino FM, on the SDK with no face of its own, gets a generated one");
    fmPanel->setSize(fmPanel->preferredWidth(), DeviceEditorPanel::standardHeight);
    const auto fmTitles = [&fmPanel]
    {
        std::vector<juce::String> titles;
        for (const auto& section : fmPanel->generatedSections)
            titles.push_back(section.title);
        return titles;
    };
    require(fmTitles() == std::vector<juce::String> {"Voice", "Op 1"},
            "A generated face groups controls by section, and a tab group shows its first section");
    const auto& operatorTabs = fmPanel->generatedSections.back().tabs;
    require(operatorTabs.size() == 4 && operatorTabs.front().first == "Op 1" && operatorTabs.back().first == "Op 4",
            "Rhino FM's four operators are tabs of one place");
    require(fmPanel->preferredWidth() < 900,
            ("Rhino FM's face is " + juce::String(fmPanel->preferredWidth()) + " px wide, still a wall of knobs").toRawUTF8());
    std::vector<juce::Rectangle<int>> fmControls;
    juce::ComboBox* algorithm = nullptr;
    juce::TextButton* mono = nullptr;
    juce::Slider* ratio = nullptr;
    juce::Slider* level = nullptr;
    for (auto* child : fmPanel->getChildren())
    {
        if (!child->isVisible())
            continue;
        require(fmPanel->getLocalBounds().contains(child->getBounds()), "Every generated control stays inside its face");
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
        {
            fmControls.push_back(slider->getBounds());
            if (slider->getTooltip().startsWith("Op 1 Ratio:")) ratio = slider;
            if (slider->getTooltip().startsWith("Op 1 Level:")) level = slider;
        }
        if (auto* choice = dynamic_cast<juce::ComboBox*>(child))
        {
            fmControls.push_back(choice->getBounds());
            if (choice->getTooltip().startsWith("Algorithm:")) algorithm = choice;
        }
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getTooltip().startsWith("Mono:"))
        {
            fmControls.push_back(button->getBounds());
            mono = button;
        }
    }
    require(fmControls.size() == 14 && algorithm != nullptr && mono != nullptr && ratio != nullptr && level != nullptr,
            ("Rhino FM's face shows the voice and the first operator's controls, "
                + juce::String(static_cast<int>(fmControls.size())) + " in all").toRawUTF8());
    for (size_t i = 0; i < fmControls.size(); ++i)
        for (auto j = i + 1; j < fmControls.size(); ++j)
            require(!fmControls[i].intersects(fmControls[j]), "Generated controls do not overlap");

    // The routing and the sound stand between the voice and the operators,
    // clear of every control. In 4>3 | 2>1 the carriers, 1 and 3, sit on the
    // bottom row wired to the output, each under the operator that feeds it.
    const auto& displayArea = fmPanel->displayArea;
    require(!displayArea.isEmpty() && fmPanel->getLocalBounds().contains(displayArea)
                && displayArea.getX() > fmPanel->generatedSections.front().area.getRight()
                && displayArea.getRight() < fmPanel->generatedSections.back().area.getX(),
            "Rhino FM's display stands between its voice and its operators");
    for (const auto& control : fmControls)
        require(!control.intersects(displayArea), "No control covers the display");
    const auto& boxes = fmPanel->displayBlocks;
    require(boxes.size() == 4 && fmPanel->display.traces.size() == 2, "The display draws four operators and two traces");
    require(boxes[0].getY() == boxes[2].getY() && boxes[1].getBottom() < boxes[0].getY() && boxes[3].getBottom() < boxes[2].getY()
                && std::abs(boxes[1].getCentreX() - boxes[0].getCentreX()) < 0.5f
                && std::abs(boxes[3].getCentreX() - boxes[2].getCentreX()) < 0.5f
                && fmPanel->display.blocks[0].output && fmPanel->display.blocks[2].output,
            "4>3 | 2>1 is drawn as two stacks over their carriers");
    for (const auto& box : boxes)
        require(fmPanel->diagramArea.toFloat().contains(box), "Every operator is drawn inside the diagram");
    require(fmPanel->displaySelected == 0, "The diagram marks the operator whose controls are showing");
    require(algorithm->getNumItems() == 8 && algorithm->getText() == "4>3 | 2>1" && mono->getButtonText() == "Off",
            "A choice is a chooser of its names, and a toggle a switch that reads Off");
    require(ratio->getInterval() == 0.5 && ratio->getValue() == 1.0,
            "A generated knob steps the way its control declares");
    require(level->isDoubleClickReturnEnabled() && std::abs(level->getDoubleClickReturnValue() - 0.85) < 1.0e-6,
            "Double-clicking a generated knob puts it back to its declared default");
    for (int i = 0; i < fmPanel->parameterLabels.size(); ++i)
        if (fmPanel->parameterLabels[i]->isVisible() && fmPanel->parameters[static_cast<size_t>(i)].name == "Op 1 Ratio")
            require(fmPanel->parameterLabels[i]->getText() == "Ratio",
                    "A caption leaves out the section it already sits under");

    const auto fmSlot = session.deviceSlots(2).back().pluginIndex;
    const auto fmValue = [&session, fmSlot](int parameter)
    {
        return session.deviceParameters(2, fmSlot)[static_cast<size_t>(parameter)].valueText;
    };
    algorithm->setSelectedId(1, juce::sendNotificationSync);
    require(fmValue(0) == "4>3>2>1", "A generated chooser writes its parameter");
    session.undo();
    require(fmValue(0) == "4>3 | 2>1", "and the write is one undo step");
    mono->onClick();
    require(fmValue(4) == "On", "A generated switch turns its toggle on");
    fmPanel->writeParameter(4, 0.0f);
    require(fmValue(4) == "Off", "and Reset writes a control back the same way");
    if (const auto path = juce::SystemStats::getEnvironmentVariable("RHINO_FM_SNAPSHOT", {}); path.isNotEmpty())
    {
        Theme faceTheme;
        fmPanel->setLookAndFeel(&faceTheme);
        const auto fmSnapshot = fmPanel->createComponentSnapshot(fmPanel->getLocalBounds());
        fmPanel->setLookAndFeel(nullptr);
        const juce::File file(path);
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            require(juce::PNGImageFormat().writeImageToStream(fmSnapshot, *stream), "Rhino FM snapshot is writable");
        else
            require(false, "Rhino FM snapshot path is writable");
    }

    // A tab shows its operator, and so does the operator's block in the
    // diagram. Every other operator's controls leave the face.
    const auto clickFm = [&fmPanel] (juce::Point<float> at)
    {
        fmPanel->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(),
                                            1.0f, 0, 0, 0, 0, fmPanel, fmPanel, juce::Time::getCurrentTime(), at,
                                            juce::Time::getCurrentTime(), 1, false));
    };
    const auto shows = [&fmPanel] (const juce::String& control)
    {
        for (auto* child : fmPanel->getChildren())
            if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->isVisible()
                && slider->getTooltip().startsWith(control + ":"))
                return true;
        return false;
    };
    clickFm(fmPanel->generatedSections.back().tabs[2].second.getCentre().toFloat());
    require(fmTitles().back() == "Op 3" && shows("Op 3 Ratio") && !shows("Op 1 Ratio") && fmPanel->displaySelected == 2,
            "Clicking the OP 3 tab shows operator 3's controls in place of operator 1's");
    clickFm(fmPanel->displayBlocks[3].getCentre());
    require(fmTitles().back() == "Op 4" && shows("Op 4 Level") && !shows("Op 3 Level"),
            "Clicking operator 4 in the diagram opens its tab");

    // What the face costs while a knob is dragged: every frame the device
    // draws a new picture and the panel is laid out and painted again.
    {
        const auto fmSlotState = session.deviceSlots(2).back();
        const auto levelIndex = 9;   // Op 1 Level
        std::vector<double> targets, paints;
        juce::Image canvas(juce::Image::ARGB, fmPanel->getWidth(), fmPanel->getHeight(), true, juce::SoftwareImageType());
        for (int frame = 0; frame < 60; ++frame)
        {
            require(session.setDeviceParameter(2, fmSlotState.pluginIndex, levelIndex, 0.3f + 0.01f * static_cast<float>(frame)).wasOk(),
                    "Op 1 Level can be set");
            auto start = juce::Time::getHighResolutionTicks();
            fmPanel->setTarget(2, fmSlotState, false);
            targets.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start));
            juce::Graphics g(canvas);
            start = juce::Time::getHighResolutionTicks();
            fmPanel->paint(g);
            paints.push_back(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start));
        }
        const auto median = [] (std::vector<double> values)
        {
            std::nth_element(values.begin(), values.begin() + static_cast<long>(values.size() / 2), values.end());
            return values[values.size() / 2] * 1.0e6;
        };
        juce::Logger::writeToLog("Rhino FM face: a knob frame refreshes in " + juce::String(median(targets), 1)
                                 + " us and paints in " + juce::String(median(paints), 1) + " us (medians)");
    }

    // The open tab belongs to the device, not to the panel, which the rack
    // builds again whenever the track changes.
    chainView.selectTrack(0);
    chainView.selectTrack(2);
    fmPanel = nullptr;
    for (auto* panel : chainView.devicePanels)
        if (panel->deviceId == "RhinoFM")
            fmPanel = panel;
    require(fmPanel != nullptr && fmTitles().back() == "Op 4", "Rhino FM's face reopens on the operator left open");

    // A preset dropped on the device it is for goes into that device, rather
    // than adding a second one.
    {
        const auto bells = ContentLibrary::file("Presets/RhinoFM/Glass Bells.rnd");
        const auto devicesBefore = session.deviceSlots(2).size();
        const auto at = chainView.getLocalPoint(&chainView.chainContent, fmPanel->getBounds().getCentre());
        chainView.itemDropped({"rhino-browser:device-preset:" + bells.getFullPathName(), nullptr, at});
        require(session.deviceSlots(2).size() == devicesBefore && fmValue(14) == "x3.5",
                "Glass Bells dropped on Rhino FM loads into it: operator 2 at x3.5");
        session.undo();
        require(fmValue(14) == "x1", "and undoing it puts the patch back");
    }

    // Rhino Space, with six controls and no sections, keeps a single row of
    // larger knobs and no titles. Adding it rebuilds every panel, so nothing
    // above may be used past this point.
    require(session.addDevice("RhinoSpace", 2).wasOk(), "Rhino Space follows the synth");
    DeviceEditorPanel* spacePanel = nullptr;
    for (auto* panel : chainView.devicePanels)
        if (panel->deviceId == "RhinoSpace")
            spacePanel = panel;
    require(spacePanel != nullptr && spacePanel->face == DeviceEditorPanel::Face::Generated
                && spacePanel->generatedSections.size() == 1 && spacePanel->generatedSections.front().title.isEmpty(),
            "Rhino Space's face is generated too, as one untitled group");
    spacePanel->setSize(spacePanel->preferredWidth(), DeviceEditorPanel::standardHeight);
    auto spaceKnobs = 0;
    auto knobTop = -1;
    for (auto* child : spacePanel->getChildren())
        if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->isVisible())
        {
            ++spaceKnobs;
            require(knobTop < 0 || slider->getY() == knobTop, "Rhino Space's six knobs stand in one row");
            knobTop = slider->getY();
        }
    require(spaceKnobs == 6, "Rhino Space shows its six knobs");
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
