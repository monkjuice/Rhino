#include "DeviceEditorPanel.h"
#include "Theme.h"
#include "audio/AutoTuneDevice.h"
#include "audio/RhinoEqDevice.h"
#include "audio/VocoderDevice.h"
#include "midi/RhinoArpDevice.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
void styleAutomationButton(juce::TextButton& button, const Session::DeviceParameter& parameter)
{
    button.setButtonText("A");
    button.setEnabled(parameter.automated);
    const auto fill = !parameter.automated ? juce::Colour(0xff242a30)
        : parameter.automationOverridden ? juce::Colour(0xff8a6a2e)
        : juce::Colour(0xff2f7d55);
    const auto text = parameter.automated ? juce::Colour(0xffeef5ef) : juce::Colour(0xff63707a);
    button.setColour(juce::TextButton::buttonColourId, fill);
    button.setColour(juce::TextButton::buttonOnColourId, fill);
    button.setColour(juce::TextButton::textColourOffId, text);
    button.setColour(juce::TextButton::textColourOnId, text);
    button.setTooltip(!parameter.automated ? "No automation for this parameter"
        : parameter.automationOverridden ? "Manual override. Click to follow automation"
        : "Following automation. Click to hold manual value");
}

// A generated face groups controls under their section's title, so a caption
// need not repeat it: "Op 1 Ratio" under "OP 1" reads "Ratio". The full name
// stays on the tooltip and on the automation lane, which are read alone.
juce::String captionFor(const Session::DeviceParameter& parameter)
{
    const auto prefix = parameter.section + " ";
    return parameter.section.isNotEmpty() && parameter.name.startsWith(prefix)
        ? parameter.name.substring(prefix.length()) : parameter.name;
}
}

DeviceEditorPanel::DeviceEditorPanel(Session& s) : session(s)
{
    setOpaque(false);
    title.setFont(uiFontBold(9.0f));
    title.setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
    title.setInterceptsMouseClicks(false, false);
    power.setTooltip("Enable or bypass this device");
    power.onClick = [this]
    {
        const auto result = session.toggleDeviceEnabled(track, pluginSlot);
        if (result.failed() && status) status(result.getErrorMessage());
    };
    // A drag has no affordance to draw, so the Info View is where the gesture
    // is told. The knobs carry their own readings and win it where they are.
    setTooltip("Drag this device by its name to move it along the chain");
    addAndMakeVisible(title);
    addAndMakeVisible(power);
    addMouseListener(this, true);
}

void DeviceEditorPanel::setTarget(int nextTrack, const Session::DeviceSlot& device, bool nextSelected)
{
    track = nextTrack;
    pluginSlot = device.pluginIndex;
    deviceName = device.name;
    isSelected = nextSelected;
    deviceId = device.deviceId;
    // A face is chosen by what the device is. A device on the SDK with no
    // face of its own gets one generated from its declarations; anything
    // else, a VST3 or a Tracktion built-in, gets the plain grid.
    face = deviceId == "RhinoArp" ? Face::Arp
         : deviceId == "RhinoTune" ? Face::AutoTune
         : deviceId == "Equaliser" ? Face::Eq
         : deviceId == "RhinoVocoder" ? Face::Vocoder
         : deviceId == "Drums" ? Face::DrumRack
         : device.native ? Face::Generated
         : Face::Generic;
    // A freshly rebuilt built-in plugin can briefly have no display name while
    // Tracktion refreshes its state. The face still has a stable catalog name.
    if (face == Face::Arp)
        deviceName = RhinoArpDevice::getPluginName();
    // The tuner's meter, the vocoder's bank, the EQ's spectrum and the drum
    // pads lighting as they are struck are the only things on any face that
    // move on their own, so they are the only devices that ask for frames.
    // The EQ stops asking when its analyser is switched off.
    const auto* eqDevice = face == Face::Eq
        ? dynamic_cast<RhinoEqDevice*>(session.devicePlugin(track, pluginSlot)) : nullptr;
    const auto eqAnalysing = eqDevice != nullptr && device.enabled
        && eqDevice->analyserMode() != EqEngine::AnalyserMode::Off;
    if (face == Face::AutoTune || face == Face::Vocoder || face == Face::DrumRack || eqAnalysing)
        startTimerHz(24);
    else
        stopTimer();
    if (face == Face::Eq && spectrum == nullptr)
        spectrum = std::make_unique<SpectrumReader>();
    if (face == Face::DrumRack)
    {
        readDrumParameters();
    }
    else
    {
        parameters = session.deviceParameters(track, pluginSlot);
        drumParametersFrom = -1;
    }
    const auto* plugin = session.devicePlugin(track, pluginSlot);
    deviceKey = plugin != nullptr ? plugin->itemID.toString() : juce::String();
    display = face == Face::Generated ? session.deviceDisplay(track, pluginSlot) : DeviceDisplay {};
    title.setText(deviceName, juce::dontSendNotification);
    power.setButtonText(device.enabled ? juce::String::fromUTF8("\xe2\x97\x8f") : juce::String::fromUTF8("\xe2\x97\x8b"));
    power.setColour(juce::TextButton::textColourOffId,
                    device.enabled ? juce::Colour(0xffc6d58c) : juce::Colour(0xff6f7982));
    ensureControls();
    styleControls();
    if (face == Face::Generated)
    {
        ensureGeneratedControls();
        styleGeneratedControls();
    }
    else
    {
        for (auto* choice : generatedChoices)
            choice->setVisible(false);
        for (auto* toggle : generatedToggles)
            toggle->setVisible(false);
    }
    if (face == Face::Arp)
    {
        ensureArpControls();
        styleArpControls();
    }
    else
    {
        arpHold.setVisible(false);
        arpRateBeats.setVisible(false);
        arpRateMilliseconds.setVisible(false);
        for (auto* choice : arpChoiceControls)
            choice->setVisible(false);
    }
    if (face == Face::DrumRack)
    {
        ensureDrumControls();
        styleDrumControls();
    }
    else
    {
        for (auto* slider : drumSliders)
            slider->setVisible(false);
        for (auto* slider : drumSampleSliders)
            slider->setVisible(false);
    }
    resized();
    if (face == Face::DrumRack)
        repaintDrums();
    else
        repaint();
}

bool DeviceEditorPanel::followsAutomation() const
{
    return std::any_of(parameters.begin(), parameters.end(), [](const auto& parameter)
    {
        return parameter.automated && !parameter.automationOverridden;
    });
}

int DeviceEditorPanel::preferredWidth() const
{
    if (face == Face::AutoTune)
        return 712;
    if (face == Face::Vocoder)
        return 752;
    if (face == Face::Eq)
        return 660;
    if (face == Face::DrumRack)
        return drumFaceWidth;
    if (face == Face::Generated)
        return generatedWidth();
    if (face == Face::Arp)
        return 820;
    const auto columns = std::max(2, std::min(6, visibleParameterCount()));
    return juce::jlimit(190, 470, 46 + columns * 66);
}

void DeviceEditorPanel::mouseDown(const juce::MouseEvent& event)
{
    headerPressed = false;
    // Right-clicking a knob is how automation is revealed, so the panel takes
    // the event back from the slider rather than letting it fall through.
    if (event.mods.isPopupMenu())
    {
        if (event.eventComponent == this && event.y < headerHeight)
        {
            if (selected) selected();
            showPresetMenu();
            return;
        }
        if (face == Face::Arp)
            if (const auto parameter = arpParameterForComponent(event.eventComponent); parameter >= 0)
            {
                if (selected) selected();
                showParameterMenu(parameter);
                return;
            }
        if (face == Face::Generated)
            if (const auto parameter = generatedParameterForComponent(event.eventComponent); parameter >= 0)
            {
                if (selected) selected();
                showParameterMenu(parameter);
                return;
            }
        for (int i = 0; i < parameterSliders.size(); ++i)
            if (parameterSliders[i]->isVisible()
                && (event.eventComponent == parameterSliders[i] || event.eventComponent == parameterLabels[i]
                    || event.eventComponent == parameterValues[i]))
            {
                if (selected) selected();
                showParameterMenu(i);
                return;
            }
    }
    if (selected) selected();
    // The EQ face hit-tests its own children as well as itself: its knobs
    // follow the selected band, so which parameter a right-click means is
    // not known until the click happens. The Drum Rack's follow its pad.
    if (face == Face::DrumRack)
    {
        if (handleDrumMouseDown(event))
            return;
    }
    else if (face == Face::Eq)
    {
        if (handleEqMouseDown(event))
            return;
    }
    else if (event.eventComponent == this)
    {
        // A face is asked before the name bar is, because a face may put a
        // control up there: Rhino Tune's LIVE and NAT toggles share that row
        // with the device's own name.
        if (face == Face::AutoTune && handleAutoTuneClick(event))
            return;
        if (face == Face::Vocoder && handleVocoderClick(event))
            return;
        if (face == Face::Generated && handleGeneratedClick(event))
            return;
    }
    if (event.eventComponent != this)
        return;
    // Whatever is left of the name bar is the handle the chain is reordered by.
    headerPressed = event.y < headerHeight && !event.mods.isPopupMenu();
}

// The EQ display is dragged, wheeled and double-clicked; nothing else on any
// face is. Each of these asks the face first and does nothing otherwise, so a
// generic panel behaves exactly as it did.
void DeviceEditorPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (headerPressed)
    {
        // A few pixels of slack, so clicking the name bar to select the device
        // does not start a drag the hand did not mean.
        if (event.getDistanceFromDragStart() < 4)
            return;
        headerPressed = false;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        {
            // The name bar alone, not the whole face: a tab under the cursor
            // reads as the thing being carried, and an EQ is 660 pixels wide.
            const auto tab = createComponentSnapshot(getLocalBounds().withHeight(headerHeight), true, 1.0f);
            container->startDragging(deviceChainDragDescription(track, pluginSlot), this,
                                     juce::ScaledImage(tab), true);
        }
        return;
    }
    if (face == Face::Eq && event.eventComponent == this)
        handleEqDrag(event);
    if (face == Face::DrumRack && event.eventComponent == this)
        handleDrumDrag(event);
}

void DeviceEditorPanel::mouseUp(const juce::MouseEvent& event)
{
    headerPressed = false;
    if (face == Face::DrumRack)
    {
        handleDrumMouseUp(event);
        return;
    }
    if (face != Face::Eq || !eqDragging)
        return;
    juce::ignoreUnused(event);
    if (eqDragBand >= 0)
    {
        session.endDeviceParameterGesture(track, pluginSlot, RhinoEqDevice::frequencyParameter(eqDragBand));
        session.endDeviceParameterGesture(track, pluginSlot, RhinoEqDevice::gainParameter(eqDragBand));
    }
    eqDragging = false;
    eqDragBand = -1;
}

void DeviceEditorPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (face == Face::Eq && event.eventComponent == this)
        handleEqDoubleClick(event);
}

void DeviceEditorPanel::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel)
{
    if (face == Face::Eq && event.eventComponent == this && handleEqWheel(event, wheel))
        return;
    if (face == Face::DrumRack && event.eventComponent == this && handleDrumWheel(event, wheel))
        return;
    Component::mouseWheelMove(event, wheel);
}

// "Show automation" reveals the lane over the track itself; "on new lane"
// stacks a ghost copy of the track under it so one curve can be read alone.
void DeviceEditorPanel::showParameterMenu(int index)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(parameters.size())))
        return;
    const Session::DeviceTarget target {track, pluginSlot, index};
    const auto lane = session.trackAutomationState(target);
    const auto& parameter = parameters[static_cast<size_t>(index)];
    const auto name = parameter.name;

    juce::PopupMenu menu;
    menu.addSectionHeader(name.toUpperCase());
    menu.addItem(1, "Show automation", true, lane.visible && !lane.ownLane);
    menu.addItem(2, "Show automation on new lane", true, lane.visible && lane.ownLane);
    menu.addSeparator();
    menu.addItem(3, "Hide automation", lane.visible);
    menu.addItem(4, "Delete automation", lane.active);
    // Only a control that says what its default is can be put back to it.
    if (parameter.defaultValue.has_value())
    {
        menu.addSeparator();
        menu.addItem(5, "Reset to default", parameter.value != *parameter.defaultValue);
    }
    // The EQ face has more parameters than the generic grid has knobs, so the
    // menu falls back to the panel when there is no slider to point at. A
    // generated chooser or switch has no slider showing, so it points at
    // itself.
    auto* anchor = index < parameterSliders.size()
        ? static_cast<juce::Component*>(parameterSliders[index]) : this;
    if (face == Face::Generated && !isKnob(index))
    {
        if (index < generatedChoices.size() && generatedChoices[index]->isVisible())
            anchor = generatedChoices[index];
        else if (index < generatedToggles.size() && generatedToggles[index]->isVisible())
            anchor = generatedToggles[index];
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), target, name,
         reset = parameter.defaultValue] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 5)
            {
                if (reset.has_value())
                    safe->writeParameter(target.parameter, *reset);
                return;
            }
            const auto outcome = result == 1 ? safe->session.showTrackAutomation(target, false)
                : result == 2 ? safe->session.showTrackAutomation(target, true)
                : result == 3 ? safe->session.hideTrackAutomation(target)
                : safe->session.clearTrackAutomationPoints(target);
            if (safe->status == nullptr) return;
            if (outcome.failed())
                safe->status(outcome.getErrorMessage());
            else
                safe->status(result == 3 ? name + ": automation hidden"
                    : result == 4 ? name + ": automation deleted"
                    : name + ": automation shown in the arrangement");
        });
}

int DeviceEditorPanel::visibleParameterCount() const
{
    // Twelve is what the generic grid can lay out and stay readable. Dedicated
    // faces can expose more because they give each control an intentional home,
    // and a generated face lays out every control a device declares, in
    // sections, up to a limit no device comes near.
    // A Drum Rack's 768 controls are reached through its six knobs that
    // follow the selected pad, so it builds none of the generic ones.
    const auto limit = face == Face::Arp ? RhinoArpDevice::parameterCount
        : face == Face::Generated ? 48
        : face == Face::DrumRack ? 0
        : face == Face::AutoTune || face == Face::Vocoder ? 13 : 12;
    return std::min(limit, static_cast<int>(parameters.size()));
}

void DeviceEditorPanel::ensureControls()
{
    while (parameterLabels.size() < visibleParameterCount())
    {
        const auto index = parameterLabels.size();
        auto* name = parameterLabels.add(new juce::Label());
        auto* value = parameterValues.add(new juce::Label());
        auto* slider = parameterSliders.add(new juce::Slider());
        auto* automation = parameterAutomation.add(new juce::TextButton());
        name->setFont(uiFont(9.5f));
        name->setJustificationType(juce::Justification::centred);
        value->setFont(uiFont(8.5f));
        value->setJustificationType(juce::Justification::centred);
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->onDragStart = [this, index]
        {
            const auto result = session.beginDeviceParameterGesture(track, pluginSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onValueChange = [this, index, slider]
        {
            if (syncing) return;
            const auto result = session.setDeviceParameter(track, pluginSlot, index, static_cast<float>(slider->getValue()));
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onDragEnd = [this, index]
        {
            const auto result = session.endDeviceParameterGesture(track, pluginSlot, index);
            if (result.failed() && status) status(result.getErrorMessage());
        };
        automation->onClick = [this, index]
        {
            const auto result = session.toggleParameterAutomationOverride(track, pluginSlot, index);
            if (status) status(result.wasOk() ? "Toggled parameter automation" : result.getErrorMessage());
        };
        addAndMakeVisible(name);
        addAndMakeVisible(value);
        addAndMakeVisible(slider);
        addAndMakeVisible(automation);
    }
}

void DeviceEditorPanel::styleControls()
{
    const auto count = visibleParameterCount();
    const auto beatRate = !juce::isPositiveAndBelow(RhinoArpDevice::rateModeParameter,
                                                     static_cast<int>(parameters.size()))
        || parameters[RhinoArpDevice::rateModeParameter].value < 0.5f;
    const auto noArpSteps = juce::isPositiveAndBelow(RhinoArpDevice::stepsParameter,
                                                     static_cast<int>(parameters.size()))
        && parameters[RhinoArpDevice::stepsParameter].value < 0.5f;
    syncing = true;
    for (int i = 0; i < parameterLabels.size(); ++i)
    {
        // Arp mixes selectors with tactile fields. Both rate values exist for
        // automation, but only the value chosen by the tiny mode buttons is
        // put under the hand.
        const auto arpSlider = i == RhinoArpDevice::gateParameter
            || i == RhinoArpDevice::distanceParameter
            || i == RhinoArpDevice::stepsParameter
            || i == RhinoArpDevice::offsetParameter
            || i == RhinoArpDevice::intervalParameter
            || i == RhinoArpDevice::repeatsParameter
            || (i == RhinoArpDevice::rateParameter && beatRate)
            || (i == RhinoArpDevice::freeRateParameter && !beatRate);
        const auto visible = i < count && (face != Face::Arp || arpSlider)
            && !(face == Face::Generated && onHiddenTab(i));
        // A generated face puts a chooser or a switch where a control is one;
        // only a continuous control gets a knob and a reading under it.
        const auto knob = face != Face::Generated || isKnob(i);
        // The Arp face draws the two knob captions itself, alongside the
        // compact grouped controls. Leaving the generic captions or A buttons
        // live would retain their previous layout rectangles.
        const auto genericChromeVisible = visible && face != Face::Arp;
        parameterLabels[i]->setVisible(genericChromeVisible);
        const auto arpRepeatReadout = face == Face::Arp && visible
            && i == RhinoArpDevice::repeatsParameter;
        parameterValues[i]->setVisible((genericChromeVisible && knob) || arpRepeatReadout);
        parameterSliders[i]->setVisible(visible && knob);
        parameterAutomation[i]->setVisible(genericChromeVisible);
        // Offset reads left to right, as a place in the pattern does, but is
        // dragged up and down like everything else on the face. It does not
        // snap to the pointer: on a bar that fills across, the height a press
        // lands at would mean nothing.
        const auto arpOffset = face == Face::Arp && i == RhinoArpDevice::offsetParameter;
        parameterSliders[i]->setSliderStyle(arpOffset || (face == Face::Arp && i == RhinoArpDevice::repeatsParameter)
            ? juce::Slider::LinearBarVertical : juce::Slider::RotaryHorizontalVerticalDrag);
        parameterSliders[i]->setSliderSnapsToMousePosition(!arpOffset);
        if (arpOffset)
            parameterSliders[i]->getProperties().set(barFillsAcross, true);
        else
            parameterSliders[i]->getProperties().remove(barFillsAcross);
        parameterSliders[i]->setSkewFactor(1.0);
        parameterSliders[i]->textFromValueFunction = {};
        // Distance is how far each step moves, so with no steps it moves
        // nothing. It stays on the face, shown as unavailable.
        const auto idle = face == Face::Arp && i == RhinoArpDevice::distanceParameter && noArpSteps;
        parameterSliders[i]->setEnabled(!idle);
        if (!visible) continue;

        const auto& parameter = parameters[static_cast<size_t>(i)];
        const auto accent = idle ? palette::disabled
            : face == Face::Generated ? faceAccent()
            : face == Face::AutoTune ? juce::Colour(0xffb2739c)
            : face == Face::Vocoder ? juce::Colour(0xff7d8fc4)
            : face == Face::Arp ? palette::midiEffect
            : juce::Colour(0xffc6d58c);
        parameterLabels[i]->setText(face == Face::Generated ? captionFor(parameter) : parameter.name,
                                    juce::dontSendNotification);
        parameterLabels[i]->setColour(juce::Label::textColourId, juce::Colour(0xffdfe6ea));
        parameterValues[i]->setText(parameter.valueText, juce::dontSendNotification);
        parameterValues[i]->setColour(juce::Label::textColourId, juce::Colour(0xffaebbc3));
        parameterValues[i]->setInterceptsMouseClicks(!arpRepeatReadout, false);
        if (arpRepeatReadout)
        {
            parameterValues[i]->setText(parameter.value < 0.5f ? "All" : parameter.valueText,
                                        juce::dontSendNotification);
            parameterValues[i]->toFront(false);
        }
        // A generated knob steps the way its control declares, and travels
        // with the control's own skew; double-clicking puts it back to its
        // default.
        const auto generated = face == Face::Generated;
        const auto step = generated && parameter.interval > 0.0f ? static_cast<double>(parameter.interval)
            : parameter.discrete ? 1.0 : 0.0;
        parameterSliders[i]->setRange(parameter.minimum, parameter.maximum, step);
        if (generated)
            parameterSliders[i]->setSkewFactor(parameter.skew);
        parameterSliders[i]->setDoubleClickReturnValue(generated && parameter.defaultValue.has_value(),
                                                       parameter.defaultValue.value_or(0.0f));
        if (face == Face::Arp && i == RhinoArpDevice::freeRateParameter)
            parameterSliders[i]->setSkewFactorFromMidPoint(250.0);
        if (face == Face::Arp && (i == RhinoArpDevice::rateParameter
                                 || i == RhinoArpDevice::offsetParameter
                                 || i == RhinoArpDevice::intervalParameter
                                 || i == RhinoArpDevice::repeatsParameter))
            parameterSliders[i]->textFromValueFunction = [i] (double value)
            {
                if (i == RhinoArpDevice::repeatsParameter && value < 0.5)
                    return juce::String("All");
                return RhinoArpDevice::parameterChoiceName(i, juce::roundToInt(value));
            };
        else if (face == Face::Arp && i == RhinoArpDevice::freeRateParameter)
            parameterSliders[i]->textFromValueFunction = [] (double value)
            {
                return juce::String(juce::roundToInt(value)) + " ms";
            };
        parameterSliders[i]->setValue(parameter.value, juce::dontSendNotification);
        parameterSliders[i]->setTooltip(parameter.name + ": " + parameter.valueText
                                        + (idle ? ". Does nothing while Steps is 0." : ""));
        parameterSliders[i]->setColour(juce::Slider::trackColourId, accent);
        parameterSliders[i]->setColour(juce::Slider::rotarySliderFillColourId, accent);
        parameterSliders[i]->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff20282e));
        parameterSliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2d3940));
        parameterSliders[i]->setColour(juce::Slider::thumbColourId, accent.brighter(0.28f));
        styleAutomationButton(*parameterAutomation[i], parameter);
    }
    syncing = false;
}

void DeviceEditorPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff171d22));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour(0xff222a30));
    g.fillRoundedRectangle(bounds.withHeight(static_cast<float>(headerHeight)), 3.0f);
    g.setColour(isSelected ? juce::Colour(0xff75b9cc) : juce::Colour(0xff3a454d));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, isSelected ? 1.5f : 1.0f);

    if (parameters.empty())
    {
        g.setColour(juce::Colour(0xff75818a));
        g.setFont(uiFont(9.0f));
        drawSnappedText(g, "No exposed parameters", contentArea, juce::Justification::centred, true);
        return;
    }

    if (face == Face::AutoTune)
    {
        paintAutoTune(g);
        return;
    }
    if (face == Face::Eq)
    {
        paintEq(g);
        return;
    }
    if (face == Face::Vocoder)
    {
        paintVocoder(g);
        return;
    }
    if (face == Face::Arp)
    {
        paintArp(g);
        return;
    }
    if (face == Face::DrumRack)
    {
        paintDrums(g);
        return;
    }
    if (face == Face::Generated)
        paintGenerated(g);
}

void DeviceEditorPanel::resized()
{
    power.setBounds(3, 2, 20, 19);
    title.setBounds(27, 1, getWidth() - 32, 21);
    contentArea = getLocalBounds().withTrimmedTop(24).reduced(4);
    // A panel can be retargeted from one device to another without being
    // rebuilt, so the EQ's own knobs go away when the face does.
    if (face != Face::Eq)
        for (auto* slider : eqSliders)
            slider->setVisible(false);
    if (face != Face::DrumRack)
    {
        for (auto* slider : drumSliders)
            slider->setVisible(false);
        for (auto* slider : drumSampleSliders)
            slider->setVisible(false);
    }
    if (face == Face::DrumRack)
        layoutDrums();
    else if (face == Face::AutoTune && contentArea.getWidth() >= 620 && contentArea.getHeight() >= 120)
        layoutAutoTune();
    else if (face == Face::Eq && contentArea.getWidth() >= 560 && contentArea.getHeight() >= 110)
        layoutEq();
    else if (face == Face::Vocoder && contentArea.getWidth() >= 660 && contentArea.getHeight() >= 120)
        layoutVocoder();
    else if (face == Face::Arp && contentArea.getWidth() >= 650 && contentArea.getHeight() >= 120)
        layoutArp();
    else if (face == Face::Generated)
        layoutGenerated();
    else
        layoutGeneric();
}

void DeviceEditorPanel::layoutGeneric()
{
    visualArea = {};
    const auto count = visibleParameterCount();
    const auto columns = std::max(1, std::min(count, 6));
    const auto rows = count > 0 ? (count + columns - 1) / columns : 1;
    const auto cellWidth = contentArea.getWidth() / columns;
    const auto cellHeight = contentArea.getHeight() / rows;
    for (int i = 0; i < count; ++i)
    {
        const juce::Rectangle<int> cell(contentArea.getX() + (i % columns) * cellWidth,
                                        contentArea.getY() + (i / columns) * cellHeight,
                                        cellWidth, cellHeight);
        const auto knobSize = std::min({64, std::max(34, cell.getWidth() - 28), std::max(34, cell.getHeight() - 36)});
        parameterLabels[i]->setBounds(cell.getX() + 4, cell.getY(), cell.getWidth() - 8, 18);
        parameterSliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 4));
        parameterValues[i]->setBounds(cell.getX() + 4, cell.getBottom() - 20, cell.getWidth() - 8, 18);
        parameterAutomation[i]->setBounds(parameterSliders[i]->getRight() - 10, parameterSliders[i]->getY() - 2, 20, 18);
    }
}

void DeviceEditorPanel::writeParameter(int parameter, float value)
{
    const auto started = session.beginDeviceParameterGesture(track, pluginSlot, parameter);
    if (started.failed())
    {
        if (status) status(started.getErrorMessage());
        return;
    }
    const auto changed = session.setDeviceParameter(track, pluginSlot, parameter, value);
    const auto ended = session.endDeviceParameterGesture(track, pluginSlot, parameter);
    if (changed.failed() && status) status(changed.getErrorMessage());
    else if (ended.failed() && status) status(ended.getErrorMessage());
}
}
