#include "DeviceRack.h"
#include "Theme.h"
#include "BrowserIds.h"
#include "DrumKitFile.h"
#include "UiVisibility.h"
#include "instruments/DrumRackDevice.h"
#include <cmath>
#include <optional>

namespace rhino
{
namespace
{
// The rack takes any device the browser can offer, whatever its kind, so one
// lookup covers all three prefixes.
const DeviceDescriptor* deviceFromBrowserDrop(const juce::String& description)
{
    static constexpr const char* prefixes[] {
        "rhino-browser:effect:", "rhino-browser:instrument:", "rhino-browser:midi-effect:"
    };
    for (const auto* prefix : prefixes)
        if (description.startsWith(prefix))
            return DeviceCatalog::byId(browserDropId(description));
    return nullptr;
}

bool isSoundFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3");
}

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

}

// The line a dragged device will land on. It is a child of the chain content
// rather than something the rack paints, so it scrolls with the panels and
// needs no repaint plumbing of its own.
class DeviceRack::DropMarker final : public juce::Component
{
public:
    DropMarker() { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override { g.fillAll(palette::selection); }
};

class DeviceRack::FloatingDeviceWindow final : public juce::DocumentWindow
{
public:
    class Editor final : public juce::Component,
                         private juce::Timer
    {
    public:
        Editor(Session& s, int t, int sl) : session(s), track(t), slot(sl)
        {
            setOpaque(true);
            refresh();
            setSize(680, 430);
            startTimerHz(30);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(palette::appBackground);
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff1a1d21));
            g.fillRect(bounds.reduced(18.0f, 18.0f));
            g.setColour(juce::Colour(0xff313841));
            g.drawRect(bounds.reduced(18.0f, 18.0f), 1.0f);
            g.setColour(juce::Colour(0xffeef2f4));
            g.setFont(uiFont(20.0f));
            drawSnappedText(g, deviceName, {34, 28, getWidth() - 68, 34}, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff8cc5d2));
            g.setFont(uiFont(9.0f));
            drawSnappedText(g, deviceTypeLabel, {36, 62, 160, 18}, juce::Justification::centredLeft, true);

            const juce::Rectangle<float> scope(210.0f, 88.0f, 260.0f, 126.0f);
            g.setColour(juce::Colour(0xff171b20));
            g.fillRect(scope);
            g.setColour(juce::Colour(0xff333b45));
            g.drawRect(scope, 1.0f);
            g.setColour(juce::Colour(0x668cc5d2));
            for (int i = 0; i < 56; ++i)
            {
                const auto x = scope.getX() + 10.0f + i * (scope.getWidth() - 20.0f) / 55.0f;
                const auto h = 8.0f
                    + std::sin(animationPhase + i * 0.67f) * 13.0f
                    + std::sin(animationPhase * 0.41f + i * 0.21f) * 20.0f;
                g.drawVerticalLine(static_cast<int>(x), scope.getCentreY() - h, scope.getCentreY() + h);
            }
        }

        void resized() override
        {
            const auto count = static_cast<int>(sliders.size());
            const int top = 238;
            const int cellWidth = 182;
            const int cellHeight = 104;
            const int left = 68;
            for (int i = 0; i < count; ++i)
            {
                const int col = i % 3;
                const int row = i / 3;
                const int x = left + col * cellWidth;
                const int y = top + row * cellHeight;
                labels[i]->setBounds(x, y, cellWidth - 14, 18);
                sliders[i]->setBounds(x + (cellWidth - 78) / 2, y + 20, 78, 58);
                values[i]->setBounds(x, y + 78, cellWidth - 14, 18);
                automationButtons[i]->setBounds(sliders[i]->getRight() - 10, sliders[i]->getY() - 2, 20, 18);
            }
        }

        void timerCallback() override
        {
            animationPhase += 0.12f;
            refreshParameterValues();
            repaint(juce::Rectangle<int>(210, 88, 260, 126).expanded(2));
        }

        void refresh()
        {
            const auto deviceSlots = session.deviceSlots(track);
            const auto visibleSlot = std::find_if(deviceSlots.begin(), deviceSlots.end(),
                                                  [this](const auto& candidate) { return candidate.pluginIndex == slot; });
            deviceName = visibleSlot != deviceSlots.end() ? visibleSlot->name : "Device";
            const auto deviceType = visibleSlot != deviceSlots.end() ? visibleSlot->type : juce::String();
            const auto* device = DeviceCatalog::byTypeName(deviceType);
            deviceTypeLabel = device != nullptr && device->kind == DeviceKind::Instrument ? "RHINO INSTRUMENT"
                                                                                         : "RHINO FX";
            parameters = session.deviceParameters(track, slot);
            while (labels.size() < static_cast<int>(parameters.size()))
            {
                const auto index = labels.size();
                auto* label = labels.add(new juce::Label());
                auto* value = values.add(new juce::Label());
                auto* slider = sliders.add(new juce::Slider());
                auto* automation = automationButtons.add(new juce::TextButton());
                label->setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
                label->setFont(uiFont(10.0f));
                label->setJustificationType(juce::Justification::centred);
                value->setColour(juce::Label::textColourId, juce::Colour(0xff94a9b4));
                value->setFont(uiFont(10.0f));
                value->setJustificationType(juce::Justification::centred);
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff8cc5d2));
                slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff242b31));
                slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffc6d58c));
                slider->onDragStart = [this, index] { session.beginDeviceParameterGesture(track, slot, index); };
                slider->onValueChange = [this, index, slider]
                {
                    if (!syncing)
                    {
                        session.setDeviceParameter(track, slot, index, static_cast<float>(slider->getValue()));
                        if (juce::isPositiveAndBelow(index, parameters.size()))
                            parameters[static_cast<size_t>(index)].value = static_cast<float>(slider->getValue());
                        const auto next = session.deviceParameters(track, slot);
                        if (juce::isPositiveAndBelow(index, next.size()))
                        {
                            parameters[static_cast<size_t>(index)] = next[static_cast<size_t>(index)];
                            values[index]->setText(parameters[static_cast<size_t>(index)].valueText, juce::dontSendNotification);
                            slider->setTooltip(parameters[static_cast<size_t>(index)].name + ": "
                                               + parameters[static_cast<size_t>(index)].valueText);
                        }
                    }
                };
                slider->onDragEnd = [this, index]
                {
                    session.endDeviceParameterGesture(track, slot, index);
                    refresh();
                };
                automation->onClick = [this, index]
                {
                    const auto result = session.toggleParameterAutomationOverride(track, slot, index);
                    juce::ignoreUnused(result);
                    refresh();
                };
                addAndMakeVisible(label);
                addAndMakeVisible(value);
                addAndMakeVisible(slider);
                addAndMakeVisible(automation);
            }

            syncing = true;
            for (int i = 0; i < labels.size(); ++i)
            {
                const auto visible = i < static_cast<int>(parameters.size()) && i < 6;
                labels[i]->setVisible(visible);
                values[i]->setVisible(visible);
                sliders[i]->setVisible(visible);
                automationButtons[i]->setVisible(visible);
                if (!visible) continue;
                const auto& parameter = parameters[static_cast<size_t>(i)];
                labels[i]->setText(parameter.name, juce::dontSendNotification);
                values[i]->setText(parameter.valueText, juce::dontSendNotification);
                sliders[i]->setRange(parameter.minimum, parameter.maximum, parameter.discrete ? 1.0 : 0.0);
                sliders[i]->setValue(parameter.value, juce::dontSendNotification);
                sliders[i]->setColour(juce::Slider::trackColourId, juce::Colour(0xff8cc5d2));
                sliders[i]->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff8cc5d2));
                sliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff30414b));
                sliders[i]->setColour(juce::Slider::thumbColourId, juce::Colour(0xffc6d58c));
                sliders[i]->setTooltip(parameter.name + ": " + parameter.valueText);
                styleAutomationButton(*automationButtons[i], parameter);
            }
            syncing = false;
            resized();
            repaint();
        }

    private:
        void refreshParameterValues()
        {
            if (syncing)
                return;
            const auto next = session.deviceParameters(track, slot);
            const auto count = std::min(std::min(static_cast<int>(next.size()), static_cast<int>(parameters.size())),
                                        sliders.size());
            syncing = true;
            for (int i = 0; i < count; ++i)
            {
                parameters[static_cast<size_t>(i)] = next[static_cast<size_t>(i)];
                sliders[i]->setValue(parameters[static_cast<size_t>(i)].value, juce::dontSendNotification);
                values[i]->setText(parameters[static_cast<size_t>(i)].valueText, juce::dontSendNotification);
                sliders[i]->setTooltip(parameters[static_cast<size_t>(i)].name + ": "
                                       + parameters[static_cast<size_t>(i)].valueText);
                styleAutomationButton(*automationButtons[i], parameters[static_cast<size_t>(i)]);
            }
            syncing = false;
        }

        Session& session;
        int track = 0, slot = 0;
        bool syncing = false;
        float animationPhase = 0.0f;
        juce::String deviceName, deviceTypeLabel;
        std::vector<Session::DeviceParameter> parameters;
        juce::OwnedArray<juce::Label> labels, values;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::TextButton> automationButtons;
    };

    FloatingDeviceWindow(Session& session, int track, int slot)
        : DocumentWindow("Rhino Device", juce::Colour(0xff0f1114), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        // Through the session, so a device on the main track opens its own
        // editor too rather than the fallback face.
        plugin = session.devicePlugin(track, slot);

        if (plugin != nullptr)
        {
            if (auto* processor = plugin->getWrappedAudioProcessor(); processor != nullptr && processor->hasEditor())
                if (auto pluginEditor = processor->createEditorAndMakeActive())
                {
                    const auto editorSize = pluginEditor->getBounds();
                    const auto editorCanResize = pluginEditor->isResizable();
                    setName(plugin->getName());
                    setResizable(editorCanResize, editorCanResize);
                    setContentOwned(pluginEditor, true);
                    centreWithSize(std::max(320, editorSize.getWidth()), std::max(240, editorSize.getHeight()));
                    setVisible(true);
                    return;
                }

            if (auto pluginEditor = plugin->createEditor())
            {
                const auto editorSize = pluginEditor->getBounds();
                const auto editorCanResize = pluginEditor->allowWindowResizing();
                setName(plugin->getName());
                setResizable(editorCanResize, editorCanResize);
                setContentOwned(pluginEditor.release(), true);
                centreWithSize(std::max(320, editorSize.getWidth()), std::max(240, editorSize.getHeight()));
                setVisible(true);
                return;
            }
        }

        setResizable(false, false);
        auto* fallbackEditor = new Editor(session, track, slot);
        const auto editorSize = fallbackEditor->getBounds();
        setContentOwned(fallbackEditor, true);
        centreWithSize(editorSize.getWidth(), editorSize.getHeight());
        setVisible(true);
    }

    // Closing the window lets its owner destroy it, and with it the device's
    // editor: hidden, the editor stayed active for a device nobody could see.
    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClose) onClose();
    }

    // Whether the device shown is still in the edit. The window holds the
    // device alive, so one whose device was deleted or whose document was
    // closed has to go: a plugin must not outlive the edit it belongs to.
    bool showsDeviceIn(const te::Edit& edit) const
    {
        return plugin == nullptr || plugin->state.isAChildOf(edit.state);
    }

    std::function<void()> onClose;

private:
    te::Plugin::Ptr plugin;
};

DeviceRack::DeviceRack(Session& s) : session(s)
{
    setOpaque(true);
    title.setText("DEVICE VIEW", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffcbd6de));
    title.setFont(uiFont(10.5f));
    context.setColour(juce::Label::textColourId, juce::Colour(0xff89959f));
    context.setFont(uiFont(10.0f));
    outputLabel.setText("TRACK OUTPUT", juce::dontSendNotification);
    outputLabel.setColour(juce::Label::textColourId, juce::Colour(0xff82909a));
    outputLabel.setFont(uiFont(8.0f));
    outputLabel.setJustificationType(juce::Justification::centred);
    open.setButtonText("Edit");
    remove.setButtonText("Delete");
    add.setButtonText("+");
    open.setTooltip("Open the selected device's own editor in a window of its own.");
    remove.setTooltip("Delete the selected device from this track's chain.");
    add.setTooltip("Add a device at the end of this track's chain.");
    add.onClick = [this] { showAddMenu(); };
    open.onClick = [this] { openSelectedDevice(); };
    remove.onClick = [this]
    {
        const auto result = session.deleteDevice(selectedTrack, selectedPluginIndex());
        if (result.failed() && status) status(result.getErrorMessage());
    };
    chainViewport.setViewedComponent(&chainContent, false);
    chainViewport.setScrollBarsShown(false, true);
    chainViewport.setScrollBarThickness(chainScrollBarThickness);
    chainContent.addAndMakeVisible(add);
    chainContent.addAndMakeVisible(outputLabel);
    for (auto* component : std::initializer_list<juce::Component*>{&title, &context, &open, &remove, &chainViewport})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    session.deviceParameterValues.addChangeListener(this);
    session.listeners.add(this);
    selectTrack(0);
    // The rate the lanes used to be swept at, which is plenty for a knob.
    startTimerHz(30);
}

DeviceRack::~DeviceRack()
{
    stopTimer();
    session.listeners.remove(this);
    session.deviceParameterValues.removeChangeListener(this);
    session.removeChangeListener(this);
}

// The outgoing document's devices go with it, so a window open on one closes
// first: it holds its device alive, and a device must not outlive its edit.
void DeviceRack::editWillChange()
{
    floatingWindow.reset();
}

void DeviceRack::editDidChange() {}

void DeviceRack::timerCallback()
{
    if (isHiddenInShell(*this))
        return;
    const auto playing = session.edit->getTransport().isPlaying();
    // Once more as the transport stops, so the knobs settle where the curve
    // left them rather than a frame short of it.
    if (playing || wasPlaying)
        followAutomation();
    wasPlaying = playing;
}

void DeviceRack::followAutomation()
{
    for (int i = 0; i < devicePanels.size() && i < static_cast<int>(slots.size()); ++i)
        if (devicePanels[i]->followsAutomation())
            devicePanels[i]->setTarget(selectedTrack, slots[static_cast<size_t>(i)], i == selectedDevice);
}

void DeviceRack::paint(juce::Graphics& g)
{
    g.fillAll(palette::sideSurface);
    g.setColour(juce::Colour(0xff303840));
    g.drawRect(getLocalBounds());
}

void DeviceRack::resized()
{
    title.setBounds(12, 3, 92, 22);
    context.setBounds(106, 3, std::max(40, getWidth() - 282), 22);
    open.setBounds(getWidth() - 148, 3, 52, 22);
    remove.setBounds(getWidth() - 90, 3, 78, 22);
    chainViewport.setBounds(12, chainTop, getWidth() - 24,
                            std::max(0, getHeight() - chainTop - chainBottomMargin));

    constexpr auto panelHeight = DeviceEditorPanel::standardHeight;
    int x = 0;
    for (auto* panel : devicePanels)
    {
        const auto panelWidth = panel->preferredWidth();
        panel->setBounds(x, 0, panelWidth, panelHeight);
        x += panelWidth + 4;
    }
    add.setBounds(x, 0, 42, panelHeight);
    x += 46;
    outputLabel.setBounds(x, 0, 88, panelHeight);
    x += 88;
    chainContent.setSize(std::max(chainViewport.getWidth(), x), panelHeight);
}

void DeviceRack::openSelectedDevice()
{
    if (slots.empty())
        return;
    selectedDevice = juce::jlimit(0, static_cast<int>(slots.size()) - 1, selectedDevice);
    // Destroy any active AudioProcessorEditor before asking the wrapped
    // processor for its editor again. JUCE permits one active editor per
    // processor and createEditorIfNeeded may otherwise return the old pointer.
    floatingWindow.reset();
    floatingWindow = std::make_unique<FloatingDeviceWindow>(session, selectedTrack, selectedPluginIndex());
    // Destroyed after the click that closed it has finished with it, and only
    // if it is still the window open by then.
    floatingWindow->onClose = [this, window = floatingWindow.get()]
    {
        juce::MessageManager::callAsync([rack = juce::Component::SafePointer<DeviceRack>(this), window]
        {
            if (rack != nullptr && rack->floatingWindow.get() == window)
                rack->floatingWindow.reset();
        });
    };
    if (status) status("Opened " + slots[static_cast<size_t>(selectedDevice)].name + " device panel");
}

bool DeviceRack::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (deviceChainDragSlot(description, selectedTrack).has_value())
        return true;
    const auto kind = browserDropKind(description);
    // A kit or a drum preset brings a Drum Rack with it; a bare sample has
    // nowhere to go in a chain without one.
    if (kind == "drumkit" || kind == "drum-preset")
        return true;
    if (kind == "file")
        return chainHasDrumRack();
    return deviceFromBrowserDrop(description) != nullptr || deviceForPresetDrop(description) != nullptr;
}

void DeviceRack::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details)
{
    itemDragMove(details);
}

void DeviceRack::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (deviceChainDragSlot(description, selectedTrack).has_value())
    {
        showDropMarker(dropGapFor(details.localPosition));
        return;
    }
    const auto kind = browserDropKind(description);
    if (kind == "file" || kind == "drum-preset")
        showDrumDropTarget(details.localPosition);
}

void DeviceRack::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    hideDropMarker();
    clearDrumDropTargets();
}

bool DeviceRack::chainHasDrumRack() const
{
    return std::any_of(slots.begin(), slots.end(), [] (const auto& slot) { return slot.deviceId == "Drums"; });
}

DeviceEditorPanel* DeviceRack::drumPanelAt(juce::Point<int> rackPosition, int& pad) const
{
    pad = -1;
    // Asked in the chain's own coordinates, so the viewport's scroll offset
    // is already accounted for.
    const auto under = chainContent.getLocalPoint(this, rackPosition);
    for (auto* panel : devicePanels)
        if (panel->showsDrumRack() && panel->getBounds().contains(under))
        {
            pad = panel->drumPadAt(panel->getLocalPoint(&chainContent, under));
            return panel;
        }
    return nullptr;
}

void DeviceRack::showDrumDropTarget(juce::Point<int> rackPosition)
{
    auto pad = -1;
    auto* target = drumPanelAt(rackPosition, pad);
    for (auto* panel : devicePanels)
        panel->showDrumDropTarget(panel == target ? pad : -1);
}

void DeviceRack::clearDrumDropTargets()
{
    for (auto* panel : devicePanels)
        panel->showDrumDropTarget(-1);
}

void DeviceRack::dropOnDrumPads(DeviceEditorPanel& panel, int pad, const std::vector<juce::File>& sounds, bool presets)
{
    const auto slot = panel.devicePluginIndex();
    if (pad < 0)
        if (const auto* drums = dynamic_cast<DrumRackDevice*>(session.devicePlugin(selectedTrack, slot)))
            pad = drums->selectedPad();
    auto loaded = 0;
    juce::String failure;
    // Several files fill the pads one after another from the one dropped on;
    // the rack stops at its last pad.
    for (const auto& sound : sounds)
    {
        const auto target = pad + loaded;
        if (!juce::isPositiveAndBelow(target, DrumRackDevice::padCount))
            break;
        const auto result = presets ? session.loadDrumPadPreset(selectedTrack, slot, target, sound)
                                    : session.loadDrumPadSample(selectedTrack, slot, target, sound);
        if (result.failed())
        {
            failure = result.getErrorMessage();
            break;
        }
        ++loaded;
    }
    if (status == nullptr)
        return;
    if (failure.isNotEmpty())
        status(failure);
    else if (loaded == 1)
        status("Loaded " + sounds.front().getFileNameWithoutExtension() + " on " + DrumRackDevice::noteName(pad));
    else if (loaded > 1)
        status("Loaded " + juce::String(loaded) + " sounds on " + DrumRackDevice::noteName(pad) + " to "
               + DrumRackDevice::noteName(pad + loaded - 1));
}

bool DeviceRack::isInterestedInFileDrag(const juce::StringArray& files)
{
    return chainHasDrumRack()
        && std::any_of(files.begin(), files.end(), [] (const juce::String& path) { return isSoundFile(juce::File(path)); });
}

void DeviceRack::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void DeviceRack::fileDragMove(const juce::StringArray&, int x, int y)
{
    showDrumDropTarget({x, y});
}

void DeviceRack::fileDragExit(const juce::StringArray&)
{
    clearDrumDropTargets();
}

void DeviceRack::filesDropped(const juce::StringArray& files, int x, int y)
{
    clearDrumDropTargets();
    std::vector<juce::File> sounds;
    for (const auto& path : files)
        if (const juce::File file(path); isSoundFile(file))
            sounds.push_back(file);
    auto pad = -1;
    auto* panel = drumPanelAt({x, y}, pad);
    if (panel == nullptr || sounds.empty())
    {
        if (status) status("Drop samples on a pad of the Drum Rack.");
        return;
    }
    dropOnDrumPads(*panel, pad, sounds, false);
}

int DeviceRack::dropGapFor(juce::Point<int> rackPosition) const
{
    // Asked in the chain's own coordinates, so the viewport's scroll offset is
    // already accounted for.
    const auto x = chainContent.getLocalPoint(this, rackPosition).x;
    for (int i = 0; i < devicePanels.size(); ++i)
        if (x < devicePanels[i]->getBounds().getCentreX())
            return i;
    return devicePanels.size();
}

int DeviceRack::devicePositionFor(int pluginIndex) const
{
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
        if (slots[static_cast<size_t>(i)].pluginIndex == pluginIndex)
            return i;
    return -1;
}

void DeviceRack::showDropMarker(int gap)
{
    if (devicePanels.isEmpty())
        return;
    if (dropMarker == nullptr)
    {
        dropMarker = std::make_unique<DropMarker>();
        chainContent.addChildComponent(dropMarker.get());
    }
    gap = juce::jlimit(0, devicePanels.size(), gap);
    const auto x = gap < devicePanels.size() ? std::max(0, devicePanels[gap]->getX() - 3)
                                            : devicePanels.getLast()->getRight() + 1;
    dropMarker->setBounds(x, 0, 2, DeviceEditorPanel::standardHeight);
    dropMarker->setVisible(true);
    dropMarker->toFront(false);
}

void DeviceRack::hideDropMarker()
{
    if (dropMarker != nullptr)
        dropMarker->setVisible(false);
}

void DeviceRack::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    hideDropMarker();
    clearDrumDropTargets();
    const auto description = details.description.toString();
    const auto kind = browserDropKind(description);
    // A kit dropped on a Drum Rack loads into it. Anywhere else in the chain
    // it makes the track's instrument a rack playing it, as on the track.
    if (kind == "drumkit")
    {
        const auto kit = browserDropKitFile(description);
        auto pad = -1;
        auto* panel = drumPanelAt(details.localPosition, pad);
        const auto result = panel != nullptr ? session.loadDrumKit(selectedTrack, panel->devicePluginIndex(), kit)
                                             : session.addDrumKit(kit, selectedTrack);
        if (status)
            status(result.wasOk() ? "Loaded " + DrumFiles::nameOf(kit) + " on " + session.trackName(selectedTrack)
                                  : result.getErrorMessage());
        return;
    }
    // A sound dropped on a Drum Rack goes on a pad. A drum preset dropped
    // anywhere else fills the first empty pad, bringing a rack if need be.
    if (kind == "file" || kind == "drum-preset")
    {
        const auto presets = kind == "drum-preset";
        const auto sound = presets ? browserDropDrumPresetFile(description) : browserDropFile(description);
        auto pad = -1;
        if (auto* panel = drumPanelAt(details.localPosition, pad))
        {
            dropOnDrumPads(*panel, pad, {sound}, presets);
            return;
        }
        const auto result = presets ? session.addDrumSound(sound, selectedTrack)
                                    : juce::Result::fail("Drop samples on a pad of the Drum Rack.");
        if (status)
            status(result.wasOk() ? "Added " + sound.getFileNameWithoutExtension() + " to the Drum Rack on "
                                        + session.trackName(selectedTrack)
                                  : result.getErrorMessage());
        return;
    }
    // A device dragged out of this same chain is a reorder, not an add.
    if (const auto dragged = deviceChainDragSlot(description, selectedTrack))
    {
        const auto from = devicePositionFor(*dragged);
        if (from < 0)
            return;
        // The gap the cursor is in counts the dragged device itself while it
        // is still in place, so a gap behind it is one position further on
        // than where the device will end up.
        const auto gap = dropGapFor(details.localPosition);
        const auto name = slots[static_cast<size_t>(from)].name;
        const auto result = session.moveDevice(selectedTrack, from, gap > from ? gap - 1 : gap);
        if (status)
            status(result.wasOk() ? "Moved " + name + " in " + session.trackName(selectedTrack) + "'s chain"
                                  : result.getErrorMessage());
        return;
    }
    // A preset dropped on a device it is for goes into that device. Anywhere
    // else in the chain, it adds the device, already set.
    if (const auto preset = browserDropPresetFile(description); preset != juce::File())
    {
        const auto* presetDevice = deviceForPresetDrop(description);
        const auto under = chainContent.getLocalPoint(this, details.localPosition);
        for (int i = 0; i < devicePanels.size() && i < static_cast<int>(slots.size()); ++i)
            if (devicePanels[i]->getBounds().contains(under) && presetDevice != nullptr
                && slots[static_cast<size_t>(i)].deviceId == presetDevice->id)
            {
                const auto result = session.loadDevicePreset(selectedTrack, slots[static_cast<size_t>(i)].pluginIndex, preset);
                if (status)
                    status(result.wasOk() ? "Loaded " + preset.getFileNameWithoutExtension() + " into "
                                                + slots[static_cast<size_t>(i)].name
                                          : result.getErrorMessage());
                return;
            }
        const auto result = session.addDeviceFromPreset(preset, selectedTrack);
        if (status)
            status(result.wasOk() ? "Added " + preset.getFileNameWithoutExtension() + " to "
                                        + session.trackName(selectedTrack)
                                  : result.getErrorMessage());
        return;
    }
    const auto* device = deviceFromBrowserDrop(description);
    if (device == nullptr)
        return;
    const auto noun = device->kind == DeviceKind::Instrument ? "instrument"
        : device->kind == DeviceKind::MidiEffect ? "MIDI FX" : "effect";
    const auto result = session.addDevice(device->id, selectedTrack);
    if (status)
        status(result.wasOk() ? "Added " + juce::String(noun) + " to " + session.trackName(selectedTrack)
                              : result.getErrorMessage());
}

void DeviceRack::showAddMenu()
{
    // Built from the catalog, so a new device appears here the moment it has
    // an entry. Infrastructure is left out: the engine puts it in the chain.
    static const auto entries = []
    {
        std::vector<const DeviceDescriptor*> list;
        for (const auto& device : DeviceCatalog::all())
            if (!device.infrastructure)
                list.push_back(&device);
        return list;
    }();

    juce::PopupMenu audioEffects, instruments, midiEffects, menu;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const auto* device = entries[static_cast<size_t>(i)];
        // An external device can only be added once it has been found.
        const auto enabled = !device->external || session.isForgeAvailable();
        auto& target = device->kind == DeviceKind::Instrument ? instruments
            : device->kind == DeviceKind::MidiEffect ? midiEffects : audioEffects;
        target.addItem(i + 1, device->displayName, enabled);
    }
    menu.addSubMenu("Audio Effects", audioEffects);
    menu.addSubMenu("Instruments", instruments);
    menu.addSubMenu("MIDI Effects", midiEffects);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add),
        [safe = juce::Component::SafePointer<DeviceRack>(this)](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (!juce::isPositiveAndBelow(result - 1, static_cast<int>(entries.size()))) return;
            const auto* device = entries[static_cast<size_t>(result - 1)];
            const auto added = safe->session.addDevice(device->id, safe->selectedTrack);

            if (safe->status)
                safe->status(added.wasOk() ? "Added device to " + safe->session.trackName(safe->selectedTrack)
                                           : added.getErrorMessage());
            if (added.wasOk())
            {
                safe->sync();
                for (int i = 0; i < static_cast<int>(safe->slots.size()); ++i)
                    if (safe->slots[static_cast<size_t>(i)].kind == device->kind)
                        safe->selectedDevice = i;
                safe->sync();
                if (juce::isPositiveAndBelow(safe->selectedDevice, safe->devicePanels.size()))
                    safe->chainViewport.setViewPosition(safe->devicePanels[safe->selectedDevice]->getX(), 0);
            }
        });
}

int DeviceRack::selectedPluginIndex() const
{
    return juce::isPositiveAndBelow(selectedDevice, slots.size())
        ? slots[static_cast<size_t>(selectedDevice)].pluginIndex : -1;
}

void DeviceRack::selectDevice(int device)
{
    selectedDevice = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), device);
    sync();
}

void DeviceRack::rebuildDevicePanels()
{
    devicePanels.clear(true);
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
    {
        auto* panel = devicePanels.add(new DeviceEditorPanel(session));
        panel->status = [this](const juce::String& message) { if (status) status(message); };
        panel->selected = [this, i] { selectDevice(i); };
        panel->presetsChanged = [this] { if (presetsChanged) presetsChanged(); };
        panel->setTarget(selectedTrack, slots[static_cast<size_t>(i)], i == selectedDevice);
        chainContent.addAndMakeVisible(panel);
    }
    add.toFront(false);
    outputLabel.toFront(false);
    if (getWidth() > 24 && getHeight() > 0)
    {
        resized();
        repaint(chainViewport.getBounds());
    }
}

void DeviceRack::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Before anything else, hidden or not: a window left open on a deleted
    // device would keep it alive.
    if (floatingWindow != nullptr && !floatingWindow->showsDeviceIn(*session.edit))
        floatingWindow.reset();
    if (isHiddenInShell(*this))
    {
        staleWhileHidden = true;
        return;
    }
    if (source == &session.deviceParameterValues)
        refreshTouchedDevice();
    else
        sync();
}

void DeviceRack::visibilityChanged()
{
    if (staleWhileHidden && !isHiddenInShell(*this))
    {
        staleWhileHidden = false;
        sync();
    }
}

// One knob is being dragged, so one panel's readings move: that panel is
// refreshed and the chain, the other panels and their layout are left alone.
void DeviceRack::refreshTouchedDevice()
{
    const auto touched = session.lastTouchedDeviceParameter();
    if (touched.track != selectedTrack)
        return;
    for (int i = 0; i < devicePanels.size() && i < static_cast<int>(slots.size()); ++i)
        if (slots[static_cast<size_t>(i)].pluginIndex == touched.slot)
            devicePanels[i]->setTarget(selectedTrack, slots[static_cast<size_t>(i)], i == selectedDevice);
}

void DeviceRack::selectTrack(int track)
{
    selectedTrack = juce::jlimit(0, session.masterTrackIndex(), track);
    selectedDevice = 0;
    sync();
}

void DeviceRack::sync()
{
    selectedTrack = juce::jlimit(0, session.masterTrackIndex(), selectedTrack);
    const auto previousPluginIndex = selectedPluginIndex();
    auto nextSlots = session.deviceSlots(selectedTrack);
    auto chainChanged = nextSlots.size() != slots.size();
    for (int i = 0; !chainChanged && i < static_cast<int>(slots.size()); ++i)
    {
        const auto& before = slots[static_cast<size_t>(i)];
        const auto& after = nextSlots[static_cast<size_t>(i)];
        chainChanged = before.name != after.name || before.type != after.type || before.kind != after.kind
            || before.pluginIndex != after.pluginIndex || before.enabled != after.enabled
            || before.removable != after.removable;
    }
    slots = std::move(nextSlots);
    if (previousPluginIndex >= 0)
        for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            if (slots[static_cast<size_t>(i)].pluginIndex == previousPluginIndex)
                selectedDevice = i;
    selectedDevice = juce::jlimit(0, std::max(0, static_cast<int>(slots.size()) - 1), selectedDevice);
    context.setText(session.trackName(selectedTrack) + "  /  SIGNAL CHAIN", juce::dontSendNotification);
    if (chainChanged)
        rebuildDevicePanels();
    for (int i = 0; i < devicePanels.size() && i < static_cast<int>(slots.size()); ++i)
        devicePanels[i]->setTarget(selectedTrack, slots[static_cast<size_t>(i)], i == selectedDevice);
    open.setEnabled(!slots.empty());
    remove.setEnabled(!slots.empty() && slots[static_cast<size_t>(selectedDevice)].removable);
}
}
