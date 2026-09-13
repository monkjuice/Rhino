#include "DeviceRack.h"
#include "BrowserIds.h"
#include <cmath>
#include <optional>

namespace theta
{
namespace
{
std::optional<Session::AudioEffect> effectFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:effect:")) return std::nullopt;
    return audioEffectFromId(browserDropId(description));
}

std::optional<Session::Instrument> instrumentFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:instrument:")) return std::nullopt;
    return instrumentFromId(browserDropId(description));
}

std::optional<Session::MidiEffect> midiEffectFromBrowserDrop(const juce::String& description)
{
    if (!description.startsWith("theta-browser:midi-effect:")) return std::nullopt;
    return midiEffectFromId(browserDropId(description));
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

juce::String deviceKindName(Session::DeviceKind kind)
{
    switch (kind)
    {
        case Session::DeviceKind::MidiEffect:  return "MIDI EFFECT";
        case Session::DeviceKind::Instrument:  return "INSTRUMENT";
        case Session::DeviceKind::AudioEffect: return "AUDIO EFFECT";
    }
    return "DEVICE";
}
}

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
            setSize(isThetaWave ? 920 : 680, isThetaWave ? 800 : 430);
            startTimerHz(30);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff111316));
            if (isThetaWave)
            {
                paintThetaWave(g);
                return;
            }
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff1a1d21));
            g.fillRect(bounds.reduced(18.0f, 18.0f));
            g.setColour(juce::Colour(0xff313841));
            g.drawRect(bounds.reduced(18.0f, 18.0f), 1.0f);
            g.setColour(juce::Colour(0xffeef2f4));
            g.setFont(juce::FontOptions(24.0f));
            g.drawText(deviceName, 34, 28, getWidth() - 68, 34, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff8cc5d2));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(deviceTypeLabel, 36, 62, 160, 18, juce::Justification::centredLeft, true);

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
            if (isThetaWave)
            {
                layoutThetaWave();
                return;
            }
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
            repaint(isThetaWave ? getLocalBounds().reduced(28, 74).withHeight(154)
                                : juce::Rectangle<int>(210, 88, 260, 126).expanded(2));
        }

        void refresh()
        {
            const auto deviceSlots = session.deviceSlots(track);
            const auto visibleSlot = std::find_if(deviceSlots.begin(), deviceSlots.end(),
                                                  [this](const auto& candidate) { return candidate.pluginIndex == slot; });
            deviceName = visibleSlot != deviceSlots.end() ? visibleSlot->name : "Device";
            const auto deviceType = visibleSlot != deviceSlots.end() ? visibleSlot->type : juce::String();
            isThetaWave = deviceType == ThetaWaveDevice::xmlTypeName;
            deviceTypeLabel = deviceType == ThetaWaveDevice::xmlTypeName ? "THETA SYNTH"
                : deviceType == DrumDevice::xmlTypeName || deviceType == te::FourOscPlugin::xmlTypeName ? "THETA INSTRUMENT"
                : "THETA FX";
            parameters = session.deviceParameters(track, slot);
            while (labels.size() < static_cast<int>(parameters.size()))
            {
                const auto index = labels.size();
                auto* label = labels.add(new juce::Label());
                auto* value = values.add(new juce::Label());
                auto* slider = sliders.add(new juce::Slider());
                auto* automation = automationButtons.add(new juce::TextButton());
                label->setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
                label->setFont(juce::FontOptions(12.0f));
                label->setJustificationType(juce::Justification::centred);
                value->setColour(juce::Label::textColourId, juce::Colour(0xff94a9b4));
                value->setFont(juce::FontOptions(12.0f));
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
                            if (isThetaWave)
                                repaint(oscillatorArea.getUnion(envelopeArea).expanded(2));
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
                const auto visible = i < static_cast<int>(parameters.size()) && (isThetaWave || i < 6);
                labels[i]->setVisible(visible);
                values[i]->setVisible(visible);
                sliders[i]->setVisible(visible);
                automationButtons[i]->setVisible(visible);
                if (!visible) continue;
                const auto& parameter = parameters[static_cast<size_t>(i)];
                const auto accent = thetaWaveAccent(i);
                labels[i]->setText(parameter.name, juce::dontSendNotification);
                values[i]->setText(parameter.valueText, juce::dontSendNotification);
                sliders[i]->setRange(parameter.minimum, parameter.maximum, parameter.discrete ? 1.0 : 0.0);
                sliders[i]->setValue(parameter.value, juce::dontSendNotification);
                sliders[i]->setColour(juce::Slider::trackColourId, isThetaWave ? accent : juce::Colour(0xff8cc5d2));
                sliders[i]->setColour(juce::Slider::rotarySliderFillColourId, isThetaWave ? accent : juce::Colour(0xff8cc5d2));
                sliders[i]->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff30414b));
                sliders[i]->setColour(juce::Slider::thumbColourId, isThetaWave ? accent.brighter(0.25f) : juce::Colour(0xffc6d58c));
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

        float normalisedValue(int index) const
        {
            if (!juce::isPositiveAndBelow(index, parameters.size())) return 0.0f;
            const auto& parameter = parameters[static_cast<size_t>(index)];
            const auto length = parameter.maximum - parameter.minimum;
            if (length <= 0.0f) return 0.0f;
            return std::clamp((parameter.value - parameter.minimum) / length, 0.0f, 1.0f);
        }

        juce::Colour thetaWaveAccent(int index) const
        {
            if (index <= 4) return juce::Colour(0xff75d3e6);
            if (index <= 9) return juce::Colour(0xffc8de8f);
            if (index <= 13) return juce::Colour(0xffd9a5ff);
            return juce::Colour(0xffffbf7a);
        }

        void paintSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                          juce::Colour accent) const
        {
            const auto box = area.toFloat();
            g.setColour(juce::Colour(0xff171b20));
            g.fillRoundedRectangle(box, 5.0f);
            g.setColour(juce::Colour(0xff323b45));
            g.drawRoundedRectangle(box, 5.0f, 1.0f);
            g.setColour(accent.withAlpha(0.2f));
            g.fillRect(area.getX(), area.getY(), area.getWidth(), 3);
            g.setColour(juce::Colour(0xffdce5ea));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(title.toUpperCase(), area.reduced(14, 8).withHeight(18), juce::Justification::centredLeft, true);
        }

        void paintWaveScope(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            const auto scope = area.toFloat().reduced(18.0f, 38.0f).withTrimmedBottom(96.0f);
            g.setColour(juce::Colour(0xff101419));
            g.fillRect(scope);
            g.setColour(juce::Colour(0xff2c3740));
            g.drawRect(scope, 1.0f);

            const auto positionValue = normalisedValue(0);
            const auto shapeValue = normalisedValue(1);
            const auto motionValue = normalisedValue(2);
            juce::Path wavePath;
            for (int i = 0; i < 128; ++i)
            {
                const auto phase = static_cast<float>(i) / 127.0f;
                const auto motionWarp = std::sin((phase * 2.0f + animationPhase * (0.1f + motionValue * 0.9f))
                                                 * juce::MathConstants<float>::twoPi)
                    * motionValue * 0.085f;
                const auto animatedPhase = phase + positionValue * 0.18f + motionWarp;
                const auto sine = std::sin(animatedPhase * juce::MathConstants<float>::twoPi);
                const auto fold = std::sin((phase * (2.0f + shapeValue * 5.0f + motionValue * 2.4f)
                                            + animationPhase * (0.015f + motionValue * 0.028f))
                                           * juce::MathConstants<float>::twoPi);
                const auto shimmer = std::sin((phase * (9.0f + motionValue * 8.0f)
                                               + animationPhase * (0.22f + motionValue * 1.8f))
                                              * juce::MathConstants<float>::twoPi);
                const auto y = sine * (0.46f - shapeValue * 0.16f)
                    + fold * (0.18f + shapeValue * 0.2f)
                    + shimmer * motionValue * 0.16f;
                const auto point = juce::Point<float>(scope.getX() + phase * scope.getWidth(),
                                                      scope.getCentreY() - y * scope.getHeight() * 0.38f);
                if (i == 0) wavePath.startNewSubPath(point);
                else wavePath.lineTo(point);
            }
            g.setColour(juce::Colour(0xff75d3e6).withAlpha(0.18f));
            for (int i = 0; i < 5; ++i)
                g.drawVerticalLine(static_cast<int>(scope.getX() + scope.getWidth() * i / 4.0f),
                                   scope.getY(), scope.getBottom());
            g.setColour(juce::Colour(0xff75d3e6));
            g.strokePath(wavePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colour(0xffd9a5ff).withAlpha(0.55f));
            g.strokePath(wavePath, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void paintEnvelope(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            const auto graph = area.toFloat().reduced(18.0f, 42.0f).withTrimmedBottom(108.0f);
            g.setColour(juce::Colour(0xff101419));
            g.fillRect(graph);
            g.setColour(juce::Colour(0xff2c3740));
            g.drawRect(graph, 1.0f);
            const auto attackValue = normalisedValue(10);
            const auto decayValue = normalisedValue(11);
            const auto sustainValue = normalisedValue(12);
            const auto releaseValue = normalisedValue(13);
            const auto aX = graph.getX() + graph.getWidth() * (0.12f + attackValue * 0.18f);
            const auto dX = aX + graph.getWidth() * (0.12f + decayValue * 0.16f);
            const auto sX = graph.getRight() - graph.getWidth() * (0.18f + releaseValue * 0.2f);
            const auto top = graph.getY() + 12.0f;
            const auto sustainY = graph.getBottom() - 12.0f - sustainValue * (graph.getHeight() - 24.0f);
            juce::Path envelope;
            envelope.startNewSubPath(graph.getX() + 8.0f, graph.getBottom() - 10.0f);
            envelope.lineTo(aX, top);
            envelope.lineTo(dX, sustainY);
            envelope.lineTo(sX, sustainY);
            envelope.lineTo(graph.getRight() - 8.0f, graph.getBottom() - 10.0f);
            g.setColour(juce::Colour(0xffd9a5ff));
            g.strokePath(envelope, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void paintThetaWave(juce::Graphics& g)
        {
            const auto bounds = getLocalBounds();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11161b), 0.0f, 0.0f,
                                                   juce::Colour(0xff0e1115), 0.0f, static_cast<float>(bounds.getBottom()), false));
            g.fillAll();
            g.setColour(juce::Colour(0xff27313a));
            g.drawRect(bounds.reduced(16), 1);

            g.setColour(juce::Colour(0xfff3f7fa));
            g.setFont(juce::FontOptions(30.0f));
            g.drawText("Theta Wave", 30, 24, 240, 36, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xff75d3e6));
            g.setFont(juce::FontOptions(11.0f));
            g.drawText("MORPHING WAVETABLE SYNTH", 33, 58, 220, 18, juce::Justification::centredLeft, true);
            g.setColour(juce::Colour(0xffc8de8f));
            g.drawText(deviceName, bounds.getWidth() - 250, 34, 210, 18, juce::Justification::centredRight, true);

            paintSection(g, oscillatorArea, "Oscillators", juce::Colour(0xff75d3e6));
            paintSection(g, filterArea, "Filter + Tone", juce::Colour(0xffc8de8f));
            paintSection(g, envelopeArea, "Amp Envelope", juce::Colour(0xffd9a5ff));
            paintSection(g, voiceArea, "Modulation + Output", juce::Colour(0xffffbf7a));
            paintWaveScope(g, oscillatorArea);
            paintEnvelope(g, envelopeArea);
        }

        void placeControl(int index, juce::Rectangle<int> area, int column, int row, int columns, int rows,
                          int knobSize = 66, int yOffset = 0)
        {
            if (!juce::isPositiveAndBelow(index, sliders.size())) return;
            const auto cellW = area.getWidth() / columns;
            const auto cellH = area.getHeight() / rows;
            const juce::Rectangle<int> cell(area.getX() + column * cellW, area.getY() + row * cellH + yOffset, cellW, cellH);
            const auto labelHeight = 18;
            const auto valueHeight = 18;
            const auto gap = 4;
            const auto availableKnobHeight = std::max(34, cell.getHeight() - labelHeight - valueHeight - gap * 2);
            const auto fittedKnob = std::min({knobSize, std::max(34, cell.getWidth() - 22), availableKnobHeight});
            labels[index]->setBounds(cell.getX() + 4, cell.getY(), cell.getWidth() - 8, labelHeight);
            values[index]->setBounds(cell.getX() + 4, cell.getBottom() - valueHeight, cell.getWidth() - 8, valueHeight);
            const auto knobArea = cell.withTrimmedTop(labelHeight + gap).withTrimmedBottom(valueHeight + gap);
            sliders[index]->setBounds(knobArea.withSizeKeepingCentre(fittedKnob, fittedKnob));
            automationButtons[index]->setBounds(sliders[index]->getRight() - 12, sliders[index]->getY() - 2, 20, 18);
        }

        void layoutThetaWave()
        {
            const auto bounds = getLocalBounds().reduced(28);
            const auto top = bounds.getY() + 60;
            const auto gap = 14;
            const auto topHeight = 300;
            const auto bottomHeight = bounds.getBottom() - top - topHeight - gap;
            oscillatorArea = {bounds.getX(), top, 548, topHeight};
            filterArea = {oscillatorArea.getRight() + 14, oscillatorArea.getY(), bounds.getRight() - oscillatorArea.getRight() - 14, oscillatorArea.getHeight()};
            envelopeArea = {bounds.getX(), oscillatorArea.getBottom() + gap, 432, bottomHeight};
            voiceArea = {envelopeArea.getRight() + 14, envelopeArea.getY(), bounds.getRight() - envelopeArea.getRight() - 14, envelopeArea.getHeight()};

            for (int i = 0; i < labels.size(); ++i)
            {
                const auto visible = i < static_cast<int>(parameters.size());
                labels[i]->setVisible(visible);
                values[i]->setVisible(visible);
                sliders[i]->setVisible(visible);
                automationButtons[i]->setVisible(visible);
            }

            const auto oscControls = oscillatorArea.reduced(18).removeFromBottom(96);
            placeControl(0, oscControls, 0, 0, 4, 1, 60);
            placeControl(1, oscControls, 1, 0, 4, 1, 60);
            placeControl(3, oscControls, 2, 0, 4, 1, 60);
            placeControl(4, oscControls, 3, 0, 4, 1, 60);

            const auto filterControls = filterArea.reduced(18, 42);
            placeControl(5, filterControls, 0, 0, 2, 2);
            placeControl(9, filterControls, 1, 0, 2, 2);
            placeControl(6, filterControls, 0, 1, 2, 2);
            placeControl(7, filterControls, 1, 1, 2, 2);

            const auto envControls = envelopeArea.reduced(18).removeFromBottom(96);
            placeControl(10, envControls, 0, 0, 4, 1, 60);
            placeControl(11, envControls, 1, 0, 4, 1, 60);
            placeControl(12, envControls, 2, 0, 4, 1, 60);
            placeControl(13, envControls, 3, 0, 4, 1, 60);

            const auto voiceControls = voiceArea.reduced(18, 42);
            placeControl(2, voiceControls, 0, 0, 3, 4, 52);
            placeControl(8, voiceControls, 1, 0, 3, 4, 52);
            placeControl(14, voiceControls, 2, 0, 3, 4, 52);
            placeControl(15, voiceControls, 0, 1, 3, 4, 52);
            placeControl(16, voiceControls, 1, 1, 3, 4, 52);
            placeControl(17, voiceControls, 2, 1, 3, 4, 52);
            placeControl(18, voiceControls, 0, 2, 3, 4, 52);
            placeControl(19, voiceControls, 1, 2, 3, 4, 52);
            placeControl(20, voiceControls, 2, 2, 3, 4, 52);
            placeControl(21, voiceControls, 0, 3, 3, 4, 52);
            placeControl(22, voiceControls, 1, 3, 3, 4, 52);
        }

        Session& session;
        int track = 0, slot = 0;
        bool syncing = false;
        bool isThetaWave = false;
        float animationPhase = 0.0f;
        juce::String deviceName, deviceTypeLabel;
        juce::Rectangle<int> oscillatorArea, filterArea, envelopeArea, voiceArea;
        std::vector<Session::DeviceParameter> parameters;
        juce::OwnedArray<juce::Label> labels, values;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::TextButton> automationButtons;
    };

    FloatingDeviceWindow(Session& session, int track, int slot)
        : DocumentWindow("Theta Device", juce::Colour(0xff0f1114), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        const auto tracks = te::getAudioTracks(*session.edit);
        if (juce::isPositiveAndBelow(track, tracks.size())
            && juce::isPositiveAndBelow(slot, tracks[track]->pluginList.size()))
            plugin = tracks[track]->pluginList[slot];

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

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    te::Plugin::Ptr plugin;
};

DeviceRack::DeviceRack(Session& s) : session(s), editor(s)
{
    setOpaque(true);
    title.setText("DEVICE VIEW", juce::dontSendNotification);
    title.setColour(juce::Label::textColourId, juce::Colour(0xffcbd6de));
    title.setFont(juce::FontOptions(13.0f));
    context.setColour(juce::Label::textColourId, juce::Colour(0xff89959f));
    context.setFont(juce::FontOptions(12.0f));
    selectedDeviceLabel.setColour(juce::Label::textColourId, juce::Colour(0xffdce5ea));
    selectedDeviceLabel.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
    outputLabel.setText("TRACK OUTPUT", juce::dontSendNotification);
    outputLabel.setColour(juce::Label::textColourId, juce::Colour(0xff82909a));
    outputLabel.setFont(juce::FontOptions(10.0f));
    outputLabel.setJustificationType(juce::Justification::centred);
    open.setButtonText("Edit");
    bypass.setButtonText("Enabled");
    remove.setButtonText("Delete");
    add.setButtonText("+");
    open.setTooltip("Open selected device editor");
    bypass.setTooltip("Bypass or enable selected device");
    remove.setTooltip("Delete selected device");
    add.setTooltip("Add a device at the end of this track's chain");
    editor.status = [this](const juce::String& message) { if (status) status(message); };
    add.onClick = [this] { showAddMenu(); };
    open.onClick = [this] { openSelectedDevice(); };
    bypass.onClick = [this]
    {
        const auto result = session.toggleDeviceEnabled(selectedTrack, selectedPluginIndex());
        if (result.failed() && status) status(result.getErrorMessage());
    };
    remove.onClick = [this]
    {
        const auto result = session.deleteDevice(selectedTrack, selectedPluginIndex());
        if (result.failed() && status) status(result.getErrorMessage());
    };
    chainViewport.setViewedComponent(&chainContent, false);
    chainViewport.setScrollBarsShown(false, true);
    chainViewport.setScrollBarThickness(8);
    chainContent.addAndMakeVisible(add);
    chainContent.addAndMakeVisible(outputLabel);
    for (auto* component : std::initializer_list<juce::Component*>{&title, &context, &open, &bypass, &remove,
                                                                    &chainViewport, &selectedDeviceLabel, &editor})
        addAndMakeVisible(component);
    session.addChangeListener(this);
    selectTrack(0);
}

DeviceRack::~DeviceRack()
{
    session.removeChangeListener(this);
}

void DeviceRack::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b2025));
    g.setColour(juce::Colour(0xff303840));
    g.drawRect(getLocalBounds());
    g.drawHorizontalLine(chainViewport.getBottom() + 7, 12.0f, static_cast<float>(getWidth() - 12));
}

void DeviceRack::resized()
{
    title.setBounds(12, 5, 92, 24);
    context.setBounds(106, 5, std::max(40, getWidth() - 342), 24);
    open.setBounds(getWidth() - 224, 5, 52, 24);
    bypass.setBounds(getWidth() - 166, 5, 72, 24);
    remove.setBounds(getWidth() - 88, 5, 76, 24);
    chainViewport.setBounds(12, 36, getWidth() - 24, 84);

    constexpr int cardWidth = 148;
    constexpr int cardHeight = 58;
    constexpr int arrowWidth = 28;
    int x = 0;
    for (int i = 0; i < deviceCards.size(); ++i)
    {
        deviceCards[i]->setBounds(x, 1, cardWidth, cardHeight);
        x += cardWidth;
        if (juce::isPositiveAndBelow(i, signalArrows.size()))
        {
            signalArrows[i]->setBounds(x, 1, arrowWidth, cardHeight);
            x += arrowWidth;
        }
    }
    add.setBounds(x, 6, 46, 48);
    x += 46;
    if (auto* finalArrow = signalArrows.getLast())
    {
        finalArrow->setBounds(x, 1, arrowWidth, cardHeight);
        x += arrowWidth;
    }
    outputLabel.setBounds(x, 1, 92, cardHeight);
    x += 92;
    chainContent.setSize(std::max(chainViewport.getWidth(), x), cardHeight + 10);

    selectedDeviceLabel.setBounds(14, 130, getWidth() - 28, 22);
    editor.setBounds(12, 154, getWidth() - 24, std::max(0, getHeight() - 160));
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
    if (status) status("Opened " + slots[static_cast<size_t>(selectedDevice)].name + " device panel");
}

bool DeviceRack::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    return effectFromBrowserDrop(description).has_value()
        || instrumentFromBrowserDrop(description).has_value()
        || midiEffectFromBrowserDrop(description).has_value();
}

void DeviceRack::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (const auto effect = effectFromBrowserDrop(description))
    {
        const auto result = session.addAudioEffect(*effect, selectedTrack);
        if (status) status(result.wasOk() ? "Added effect to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
    else if (const auto instrument = instrumentFromBrowserDrop(description))
    {
        const auto result = session.addInstrument(*instrument, selectedTrack);
        if (status) status(result.wasOk() ? "Added instrument to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
    else if (const auto midiEffect = midiEffectFromBrowserDrop(description))
    {
        const auto result = session.addMidiEffect(*midiEffect, selectedTrack);
        if (status) status(result.wasOk() ? "Added MIDI FX to " + session.trackName(selectedTrack) : result.getErrorMessage());
    }
}

void DeviceRack::showAddMenu()
{
    juce::PopupMenu audioEffects, instruments, midiEffects, menu;
    audioEffects.addItem(101, "EQ");
    audioEffects.addItem(102, "Reverb");
    audioEffects.addItem(103, "Delay");
    audioEffects.addItem(104, "Compressor");
    audioEffects.addItem(105, "Theta Space");
    audioEffects.addItem(106, "Theta Bloom");
    instruments.addItem(201, "4OSC");
    instruments.addItem(202, "Theta Wave");
    instruments.addItem(203, "Theta Forge", session.isForgeAvailable());
    instruments.addItem(204, "Theta Drums");
    midiEffects.addItem(301, "Theta Arp");
    menu.addSubMenu("Audio Effects", audioEffects);
    menu.addSubMenu("Instruments", instruments);
    menu.addSubMenu("MIDI Effects", midiEffects);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add),
        [safe = juce::Component::SafePointer<DeviceRack>(this)](int result)
        {
            if (safe == nullptr || result == 0) return;
            juce::Result added = juce::Result::fail("That device could not be created.");
            if (result >= 101 && result <= 106)
                added = safe->session.addAudioEffect(static_cast<Session::AudioEffect>(result - 101), safe->selectedTrack);
            else if (result >= 201 && result <= 204)
                added = safe->session.addInstrument(static_cast<Session::Instrument>(result - 201), safe->selectedTrack);
            else if (result == 301)
                added = safe->session.addMidiEffect(Session::MidiEffect::ThetaArp, safe->selectedTrack);

            if (safe->status)
                safe->status(added.wasOk() ? "Added device to " + safe->session.trackName(safe->selectedTrack)
                                           : added.getErrorMessage());
            if (added.wasOk())
            {
                safe->sync();
                const auto wantedKind = result >= 101 && result <= 106 ? Session::DeviceKind::AudioEffect
                    : result >= 201 && result <= 204 ? Session::DeviceKind::Instrument
                    : Session::DeviceKind::MidiEffect;
                for (int i = 0; i < static_cast<int>(safe->slots.size()); ++i)
                    if (safe->slots[static_cast<size_t>(i)].kind == wantedKind)
                        safe->selectedDevice = i;
                safe->sync();
                if (juce::isPositiveAndBelow(safe->selectedDevice, safe->deviceCards.size()))
                    safe->chainViewport.setViewPosition(safe->deviceCards[safe->selectedDevice]->getX(), 0);
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

void DeviceRack::rebuildDeviceCards()
{
    deviceCards.clear(true);
    signalArrows.clear(true);
    for (int i = 0; i < static_cast<int>(slots.size()); ++i)
    {
        const auto& slot = slots[static_cast<size_t>(i)];
        auto* card = deviceCards.add(new juce::TextButton());
        card->setButtonText(slot.name + "\n" + deviceKindName(slot.kind) + (slot.enabled ? "  /  ON" : "  /  BYPASSED"));
        card->setTooltip(deviceKindName(slot.kind) + " · processing position " + juce::String(i + 1));
        card->setToggleState(i == selectedDevice, juce::dontSendNotification);
        card->setColour(juce::TextButton::buttonColourId,
                        slot.enabled ? juce::Colour(0xff252d33) : juce::Colour(0xff202429));
        card->setColour(juce::TextButton::buttonOnColourId,
                        slot.enabled ? juce::Colour(0xff38505b) : juce::Colour(0xff303a40));
        card->setColour(juce::TextButton::textColourOffId,
                        slot.enabled ? juce::Colour(0xffdce5ea) : juce::Colour(0xff77818a));
        card->setColour(juce::TextButton::textColourOnId,
                        slot.enabled ? juce::Colour(0xffedf4f7) : juce::Colour(0xff98a2aa));
        card->onClick = [this, i] { selectDevice(i); };
        chainContent.addAndMakeVisible(card);
    }
    const auto arrowCount = static_cast<int>(slots.size()) + 1;
    for (int i = 0; i < arrowCount; ++i)
    {
        auto* arrow = signalArrows.add(new juce::Label());
        arrow->setText(juce::String::fromUTF8("\xe2\x86\x92"), juce::dontSendNotification);
        arrow->setColour(juce::Label::textColourId, juce::Colour(0xff6f7d86));
        arrow->setFont(juce::FontOptions(18.0f));
        arrow->setJustificationType(juce::Justification::centred);
        chainContent.addAndMakeVisible(arrow);
    }
    add.toFront(false);
    outputLabel.toFront(false);
    renderedSelectedDevice = selectedDevice;
    if (getWidth() > 24 && getHeight() > 0)
    {
        resized();
        repaint(chainViewport.getBounds());
    }
}

void DeviceRack::changeListenerCallback(juce::ChangeBroadcaster*)
{
    sync();
}

void DeviceRack::selectTrack(int track)
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), track);
    selectedDevice = 0;
    sync();
}

void DeviceRack::sync()
{
    selectedTrack = juce::jlimit(0, std::max(0, session.trackCount() - 1), selectedTrack);
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
    selectedDeviceLabel.setText(slots.empty() ? "No devices — use + or drag from the Browser"
        : slots[static_cast<size_t>(selectedDevice)].name + "  /  CONTROLS", juce::dontSendNotification);
    if (chainChanged || renderedSelectedDevice != selectedDevice)
        rebuildDeviceCards();
    bypass.setEnabled(!slots.empty());
    open.setEnabled(!slots.empty());
    remove.setEnabled(!slots.empty() && slots[static_cast<size_t>(selectedDevice)].removable);
    bypass.setButtonText(!slots.empty() && !slots[static_cast<size_t>(selectedDevice)].enabled ? "Bypassed" : "Enabled");
    editor.setTarget(selectedTrack, selectedPluginIndex(), slots.empty() ? nullptr : &slots[static_cast<size_t>(selectedDevice)]);
}
}
