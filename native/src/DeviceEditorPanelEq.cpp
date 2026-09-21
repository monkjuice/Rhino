#include "DeviceEditorPanel.h"
#include "Theme.h"
#include "audio/RhinoEqDevice.h"
#include <algorithm>
#include <cmath>

// Rhino EQ's face: the curve, what is going through it, and the band in hand.
//
// The display is the control. A band is dragged where it is wanted rather
// than dialled in, and the three knobs beside it are the same three numbers
// written down -- they follow whichever band is selected instead of standing
// for one each, which is why they are not the generic grid's sliders.
//
// Everything that is not a knob is drawn and hit-tested by rectangle, for the
// reason the tuner's face is: the rack destroys and rebuilds every panel when
// anything about a track changes, and eight band rows plus two choosers is a
// lot of components to build for a repaint.
namespace rhino
{
namespace
{
const juce::Colour accentInk {0xff6f9ec4};
const juce::Colour wellInk {0xff10161a};
const juce::Colour frameInk {0xff36414a};
const juce::Colour gridInk {0xff232c33};
const juce::Colour brightText {0xffdfe6ea};
const juce::Colour dimText {0xff85929c};
const juce::Colour deadText {0xff4a555e};
const juce::Colour curveInk {0xffc6d58c};
const juce::Colour spectrumInk {0xff54707f};

// Eight bands want eight tellable colours. They run cool to warm across the
// spectrum, so a handle says roughly where it belongs even after it has been
// dragged somewhere else.
const juce::Colour bandInk[RhinoEqDevice::bandCount]
{
    juce::Colour(0xff7f8ed8), juce::Colour(0xff5fa8d8), juce::Colour(0xff54bdb4),
    juce::Colour(0xff86c46a), juce::Colour(0xffd2c25c), juce::Colour(0xffdf9d55),
    juce::Colour(0xffd97070), juce::Colour(0xffbd76c4)
};

// What the display spans. The frequency ends match the parameter range, so a
// band can always be dragged to either edge and read the number it shows.
constexpr float displayLowHz = 10.0f;
constexpr float displayHighHz = 22000.0f;
// Three dB of headroom past the gain range, so a band pushed to the limit
// still has its handle inside the box rather than half off the top.
constexpr float displayDbRange = 18.0f;
// The analyser gets its own scale. A single full-scale sine lands one bin at
// 0 dBFS; broadband material spread over a thousand bins sits far below that,
// so the floor has to be a long way down for anything to be visible at all.
constexpr float spectrumTopDb = 6.0f;
constexpr float spectrumBottomDb = -90.0f;
constexpr float displayInset = 5.0f;
constexpr float handleRadius = 5.5f;
constexpr float handleGrabDistance = 13.0f;

constexpr int knobCount = 5;
constexpr int freqKnob = 0, gainKnob = 1, qKnob = 2, outputKnob = 3, scaleKnob = 4;

RhinoEqDevice* eqDeviceIn(Session& session, int track, int slot)
{
    return dynamic_cast<RhinoEqDevice*>(session.devicePlugin(track, slot));
}

int parameterForKnob(const RhinoEqDevice& device, int knob)
{
    const auto band = device.selectedBand();
    switch (knob)
    {
        case freqKnob:   return RhinoEqDevice::frequencyParameter(band);
        case gainKnob:   return RhinoEqDevice::gainParameter(band);
        case qKnob:      return RhinoEqDevice::qParameter(band);
        case outputKnob: return RhinoEqDevice::outputGainParameter;
        default:         return RhinoEqDevice::scaleParameter;
    }
}

float xForFrequency(juce::Rectangle<float> area, float hertz)
{
    const auto position = std::log(std::clamp(hertz, displayLowHz, displayHighHz) / displayLowHz)
                        / std::log(displayHighHz / displayLowHz);
    return area.getX() + position * area.getWidth();
}

float frequencyForX(juce::Rectangle<float> area, float x)
{
    const auto position = std::clamp((x - area.getX()) / std::max(1.0f, area.getWidth()), 0.0f, 1.0f);
    return displayLowHz * std::pow(displayHighHz / displayLowHz, position);
}

float yForDb(juce::Rectangle<float> area, float decibels)
{
    const auto position = std::clamp(decibels, -displayDbRange, displayDbRange) / displayDbRange;
    return area.getCentreY() - position * area.getHeight() * 0.5f;
}

float dbForY(juce::Rectangle<float> area, float y)
{
    const auto position = (area.getCentreY() - y) / std::max(1.0f, area.getHeight() * 0.5f);
    return std::clamp(position, -1.0f, 1.0f) * displayDbRange;
}

float yForSpectrumDb(juce::Rectangle<float> area, float decibels)
{
    const auto position = std::clamp((decibels - spectrumBottomDb) / (spectrumTopDb - spectrumBottomDb),
                                     0.0f, 1.0f);
    return area.getBottom() - position * area.getHeight();
}

// A cut has no gain to sit at, so its handle sits on its own curve, which at
// the corner of a Butterworth is the three dB point -- exactly where the eye
// looks for a cutoff. A notch is minus infinity at its centre and would drag
// its handle off the bottom of the display, so that one sits on the line.
float handleDbFor(const EqBandSettings& band, double sampleRate)
{
    if (eqTypeUsesGain(band.type))
        return band.gainDb;
    if (band.type == EqFilterType::Notch)
        return 0.0f;
    auto sounding = band;
    sounding.enabled = true;
    return static_cast<float>(eqBandMagnitudeDbAt(sounding, band.frequency, sampleRate));
}

struct EqLayout
{
    juce::Rectangle<int> display, bandColumn, typeChooser, analyserButton;
    juce::Rectangle<int> bandCell[RhinoEqDevice::bandCount];
    juce::Rectangle<int> bandLed[RhinoEqDevice::bandCount];
    juce::Rectangle<int> knob[knobCount], knobLabel[knobCount], knobValue[knobCount], knobAutomation[knobCount];
};

EqLayout eqLayoutFor(juce::Rectangle<int> panel)
{
    EqLayout layout;
    auto area = panel.withTrimmedTop(24).reduced(4);

    layout.display = area.removeFromLeft(std::max(220, area.getWidth() - 312));
    area.removeFromLeft(6);
    layout.bandColumn = area.removeFromLeft(30);
    area.removeFromLeft(6);

    const auto cellHeight = std::max(12, layout.bandColumn.getHeight() / RhinoEqDevice::bandCount);
    for (int i = 0; i < RhinoEqDevice::bandCount; ++i)
    {
        layout.bandCell[i] = {layout.bandColumn.getX(), layout.bandColumn.getY() + i * cellHeight,
                              layout.bandColumn.getWidth(), cellHeight - 1};
        layout.bandLed[i] = layout.bandCell[i].withWidth(13);
    }

    auto top = area.removeFromTop(20);
    layout.typeChooser = top.removeFromLeft(std::max(90, top.getWidth() - 110));
    top.removeFromLeft(6);
    layout.analyserButton = top;
    area.removeFromTop(4);

    const auto cellWidth = std::max(34, area.getWidth() / knobCount);
    for (int i = 0; i < knobCount; ++i)
    {
        const juce::Rectangle<int> cell(area.getX() + i * cellWidth, area.getY(), cellWidth, area.getHeight());
        layout.knobLabel[i] = cell.withHeight(13);
        layout.knobValue[i] = {cell.getX(), cell.getBottom() - 13, cell.getWidth(), 13};
        const auto size = std::min({54, cell.getWidth() - 6, cell.getHeight() - 30});
        layout.knob[i] = cell.withSizeKeepingCentre(size, size);
        layout.knobAutomation[i] = {cell.getRight() - 16, cell.getY() + 12, 14, 12};
    }
    return layout;
}

void drawChooser(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& caption,
                 const juce::String& value, juce::Colour valueInk)
{
    g.setColour(juce::Colour(0xff1f262c));
    g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
    g.setColour(frameInk);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
    g.setColour(dimText);
    g.setFont(uiFont(8.5f));
    drawSnappedText(g, caption, bounds.withTrimmedLeft(7), juce::Justification::centredLeft);
    g.setColour(valueInk);
    g.setFont(uiFontBold(10.0f));
    drawSnappedText(g, value, bounds.withTrimmedRight(7), juce::Justification::centredRight);
}

const char* const knobCaptions[knobCount] {"FREQ", "GAIN", "Q", "OUT", "SCALE"};

// Which vertical grid lines get a number under them. Every decade is drawn,
// but labelling all of them at this width turns the axis into a smear.
struct GridLine { float hertz; const char* label; };
const GridLine gridLines[]
{
    {20.0f, nullptr}, {30.0f, nullptr}, {50.0f, nullptr}, {100.0f, "100"},
    {200.0f, nullptr}, {300.0f, nullptr}, {500.0f, nullptr}, {1000.0f, "1k"},
    {2000.0f, nullptr}, {3000.0f, nullptr}, {5000.0f, nullptr}, {10000.0f, "10k"},
    {20000.0f, nullptr}
};
}

void DeviceEditorPanel::ensureEqControls()
{
    if (!eqSliders.isEmpty())
        return;
    for (int i = 0; i < knobCount; ++i)
    {
        auto* slider = eqSliders.add(new juce::Slider());
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        // The parameter behind a knob is decided when the knob is touched,
        // not when it is made: selecting another band re-points all three.
        slider->onDragStart = [this, i]
        {
            if (auto* device = eqDeviceIn(session, track, pluginSlot))
                session.beginDeviceParameterGesture(track, pluginSlot, parameterForKnob(*device, i));
        };
        slider->onValueChange = [this, i, slider]
        {
            if (syncing) return;
            auto* device = eqDeviceIn(session, track, pluginSlot);
            if (device == nullptr) return;
            const auto result = session.setDeviceParameter(track, pluginSlot,
                                                           parameterForKnob(*device, i),
                                                           static_cast<float>(slider->getValue()));
            if (result.failed() && status) status(result.getErrorMessage());
        };
        slider->onDragEnd = [this, i]
        {
            if (auto* device = eqDeviceIn(session, track, pluginSlot))
                session.endDeviceParameterGesture(track, pluginSlot, parameterForKnob(*device, i));
        };
        addAndMakeVisible(slider);
    }
}

void DeviceEditorPanel::styleEqControls()
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto type = device->bandType(device->selectedBand());
    syncing = true;
    for (int i = 0; i < eqSliders.size(); ++i)
    {
        const auto index = parameterForKnob(*device, i);
        if (!juce::isPositiveAndBelow(index, static_cast<int>(parameters.size())))
            continue;
        const auto& parameter = parameters[static_cast<size_t>(index)];
        const auto live = (i != gainKnob || eqTypeUsesGain(type)) && (i != qKnob || eqTypeUsesQ(type));
        const auto ink = i < 3 ? bandInk[device->selectedBand()] : accentInk;
        auto* slider = eqSliders[i];
        slider->setRange(parameter.minimum, parameter.maximum, 0.0);
        // Frequency and Q read logarithmically, so the middle of the knob is
        // the geometric middle of the range and a sweep feels even.
        if (i == freqKnob || i == qKnob)
            slider->setSkewFactorFromMidPoint(
                std::sqrt(std::max(0.001, static_cast<double>(parameter.minimum * parameter.maximum))));
        else
            slider->setSkewFactor(1.0);
        slider->setValue(parameter.value, juce::dontSendNotification);
        slider->setEnabled(live);
        slider->setTooltip(parameter.name + ": " + parameter.valueText);
        slider->setColour(juce::Slider::rotarySliderFillColourId, live ? ink : juce::Colour(0xff39434b));
        slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff20282e));
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2d3940));
        slider->setColour(juce::Slider::thumbColourId, live ? ink.brighter(0.3f) : juce::Colour(0xff4a555e));
    }
    syncing = false;
}

void DeviceEditorPanel::layoutEq()
{
    visualArea = {};
    ensureEqControls();
    // The generic grid is built by the shared code before the face gets a
    // say. An EQ never shows it, and a hidden slider costs nothing but the
    // object, so it is simply put away rather than not made. Its bounds are
    // cleared as well as hidden: the rack sizes a panel after targeting it,
    // so those controls have already been laid out once against a panel of no
    // width, and stale bounds from that pass sit outside the finished panel.
    for (int i = 0; i < parameterLabels.size(); ++i)
        for (auto* control : std::initializer_list<juce::Component*>
                 {parameterLabels[i], parameterValues[i], parameterSliders[i], parameterAutomation[i]})
        {
            control->setVisible(false);
            control->setBounds({});
        }

    const auto layout = eqLayoutFor(getLocalBounds());
    eqDisplay = layout.display;
    for (int i = 0; i < eqSliders.size(); ++i)
    {
        eqSliders[i]->setBounds(layout.knob[i]);
        eqSliders[i]->setVisible(true);
    }
    styleEqControls();
    rebuildEqCurve();
}

void DeviceEditorPanel::rebuildEqCurve()
{
    eqCurve.clear();
    eqBandCurve.clear();
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr || eqDisplay.getWidth() < 8)
        return;

    const auto area = eqDisplay.toFloat().reduced(displayInset);
    const auto settings = device->currentSettings();
    const auto rate = device->spectrumRate();
    const auto chosen = device->selectedBand();

    // The coefficients are made once per band and then evaluated across the
    // sweep. Remaking them at every pixel is several thousand sines a frame
    // for an answer that does not change along the way.
    EqBandStages stages[RhinoEqDevice::bandCount];
    bool sounding[RhinoEqDevice::bandCount];
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        const auto scaled = EqEngine::scaledBand(settings.band[static_cast<size_t>(band)],
                                                 settings.scalePercent);
        sounding[band] = scaled.enabled;
        stages[band] = eqStagesFor(scaled, rate);
    }

    const auto steps = std::max(2, static_cast<int>(area.getWidth()));
    for (int i = 0; i <= steps; ++i)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(steps);
        const auto z = eqUnitCircle(frequencyForX(area, x), rate);
        auto total = static_cast<double>(settings.outputGainDb);
        for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
            if (sounding[band])
                total += eqStagesMagnitudeDb(stages[band], z);
        const auto y = yForDb(area, static_cast<float>(total));
        if (i == 0) eqCurve.startNewSubPath(x, y); else eqCurve.lineTo(x, y);

        if (sounding[chosen])
        {
            const auto own = yForDb(area, static_cast<float>(eqStagesMagnitudeDb(stages[chosen], z)));
            if (i == 0) eqBandCurve.startNewSubPath(x, own); else eqBandCurve.lineTo(x, own);
        }
    }
}

void DeviceEditorPanel::tickEqSpectrum()
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr || spectrum == nullptr
        || device->analyserMode() == EqEngine::AnalyserMode::Off)
    {
        stopTimer();
        return;
    }
    // Instant attack and 1.2 dB a frame back down, which at 24 Hz is about
    // 29 dB a second: fast enough to follow a part, slow enough that the
    // shape stays still long enough to read a decision off it.
    if (spectrum->update(device->spectrumTap(), 1.2f))
        repaint(eqDisplay);
}

void DeviceEditorPanel::paintEq(juce::Graphics& g)
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto layout = eqLayoutFor(getLocalBounds());
    const auto settings = device->currentSettings();
    const auto rate = device->spectrumRate();
    const auto chosen = device->selectedBand();
    const auto area = layout.display.toFloat().reduced(displayInset);

    // ---- the well and its grid --------------------------------------------
    g.setColour(wellInk);
    g.fillRoundedRectangle(layout.display.toFloat(), 3.0f);
    g.setColour(frameInk);
    g.drawRoundedRectangle(layout.display.toFloat().reduced(0.5f), 3.0f, 1.0f);

    g.setFont(uiFont(8.0f));
    for (const auto& line : gridLines)
    {
        const auto x = xForFrequency(area, line.hertz);
        g.setColour(gridInk);
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        if (line.label != nullptr)
        {
            g.setColour(deadText);
            drawSnappedText(g, line.label,
                            {juce::roundToInt(x) + 2, layout.display.getBottom() - 12, 26, 11},
                            juce::Justification::centredLeft);
        }
    }
    g.setColour(gridInk);
    for (const auto decibels : {-12.0f, -6.0f, 6.0f, 12.0f})
        g.drawHorizontalLine(juce::roundToInt(yForDb(area, decibels)), area.getX(), area.getRight());
    g.setColour(juce::Colour(0xff313c44));
    g.drawHorizontalLine(juce::roundToInt(yForDb(area, 0.0f)), area.getX(), area.getRight());

    // ---- what is going through it -----------------------------------------
    if (spectrum != nullptr && settings.analyser != EqEngine::AnalyserMode::Off)
    {
        const auto& bins = spectrum->decibels();
        juce::Path shape;
        for (int i = 0; i < SpectrumReader::binCount; ++i)
        {
            const auto x = xForFrequency(area, SpectrumReader::binFrequency(i));
            const auto y = yForSpectrumDb(area, bins[static_cast<size_t>(i)]);
            if (i == 0)
            {
                shape.startNewSubPath(x, area.getBottom());
                shape.lineTo(x, y);
            }
            else
            {
                shape.lineTo(x, y);
            }
        }
        shape.lineTo(xForFrequency(area, SpectrumReader::highestFrequency), area.getBottom());
        shape.closeSubPath();
        g.setColour(spectrumInk.withAlpha(0.30f));
        g.fillPath(shape);
        g.setColour(spectrumInk.withAlpha(0.75f));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

    // ---- the response ------------------------------------------------------
    if (!eqBandCurve.isEmpty())
    {
        g.setColour(bandInk[chosen].withAlpha(0.55f));
        g.strokePath(eqBandCurve, juce::PathStrokeType(1.0f));
    }
    if (!eqCurve.isEmpty())
    {
        g.setColour(curveInk);
        g.strokePath(eqCurve, juce::PathStrokeType(1.8f));
    }

    // ---- the handles -------------------------------------------------------
    g.setFont(uiFontBold(8.5f));
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        const auto& settingsBand = settings.band[static_cast<size_t>(band)];
        const auto scaled = EqEngine::scaledBand(settingsBand, settings.scalePercent);
        const auto x = xForFrequency(area, scaled.frequency);
        const auto y = yForDb(area, handleDbFor(scaled, rate));
        const auto ink = bandInk[band];
        const auto radius = band == chosen ? handleRadius + 1.5f : handleRadius;
        if (band == chosen)
        {
            g.setColour(ink.withAlpha(0.3f));
            g.fillEllipse(x - radius - 3.0f, y - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f);
        }
        g.setColour(settingsBand.enabled ? ink : wellInk);
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(settingsBand.enabled ? ink.brighter(0.4f) : ink.withAlpha(0.55f));
        g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
        g.setColour(settingsBand.enabled ? juce::Colour(0xff10161a) : ink.withAlpha(0.8f));
        drawSnappedText(g, juce::String(band + 1),
                        juce::Rectangle<float>(x - radius, y - radius, radius * 2.0f, radius * 2.0f).toNearestInt(),
                        juce::Justification::centred);
    }

    // ---- the band column ---------------------------------------------------
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        const auto cell = layout.bandCell[band];
        const auto on = settings.band[static_cast<size_t>(band)].enabled;
        g.setColour(band == chosen ? juce::Colour(0xff2b343b) : juce::Colour(0xff1a2126));
        g.fillRoundedRectangle(cell.toFloat(), 2.0f);
        if (band == chosen)
        {
            g.setColour(bandInk[band].withAlpha(0.7f));
            g.drawRoundedRectangle(cell.toFloat().reduced(0.5f), 2.0f, 1.0f);
        }
        const auto led = layout.bandLed[band].toFloat().withSizeKeepingCentre(7.0f, 7.0f);
        g.setColour(on ? bandInk[band] : juce::Colour(0xff2f3941));
        g.fillEllipse(led);
        g.setColour(brightText.withAlpha(on ? 1.0f : 0.45f));
        g.setFont(uiFontBold(9.5f));
        drawSnappedText(g, juce::String(band + 1), cell.withTrimmedRight(5),
                        juce::Justification::centredRight);
    }

    // ---- the choosers and the knob captions --------------------------------
    drawChooser(g, layout.typeChooser, "TYPE",
                eqFilterTypeName(device->bandType(chosen)), brightText);
    const auto analyser = settings.analyser;
    drawChooser(g, layout.analyserButton, "ANALYSE",
                analyser == EqEngine::AnalyserMode::Off ? "Off"
                : analyser == EqEngine::AnalyserMode::Pre ? "Pre" : "Post",
                analyser == EqEngine::AnalyserMode::Off ? deadText : accentInk);

    const auto type = device->bandType(chosen);
    for (int i = 0; i < knobCount; ++i)
    {
        const auto index = parameterForKnob(*device, i);
        if (!juce::isPositiveAndBelow(index, static_cast<int>(parameters.size())))
            continue;
        const auto& parameter = parameters[static_cast<size_t>(index)];
        const auto live = (i != gainKnob || eqTypeUsesGain(type)) && (i != qKnob || eqTypeUsesQ(type));
        g.setColour(live ? dimText : deadText);
        g.setFont(uiFont(8.5f));
        drawSnappedText(g, knobCaptions[i], layout.knobLabel[i], juce::Justification::centred);
        g.setColour(live ? brightText : deadText);
        g.setFont(uiFont(9.5f));
        drawSnappedText(g, live ? parameter.valueText : juce::String("--"), layout.knobValue[i],
                        juce::Justification::centred);
        // The badge only appears on a parameter that has a lane, and it is
        // the one way back to manual control once that lane is moving.
        if (!parameter.automated)
            continue;
        const auto badge = layout.knobAutomation[i];
        g.setColour(parameter.automationOverridden ? juce::Colour(0xff8a6a2e) : juce::Colour(0xff2f7d55));
        g.fillRoundedRectangle(badge.toFloat(), 2.0f);
        g.setColour(juce::Colour(0xffeef5ef));
        g.setFont(uiFontBold(8.5f));
        drawSnappedText(g, "A", badge, juce::Justification::centred);
    }
}

bool DeviceEditorPanel::handleEqMouseDown(const juce::MouseEvent& event)
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;

    // A right-click on one of the five knobs is an automation menu for
    // whatever that knob is pointing at right now.
    if (event.mods.isPopupMenu())
        for (int i = 0; i < eqSliders.size(); ++i)
            if (event.eventComponent == eqSliders[i])
            {
                showParameterMenu(parameterForKnob(*device, i));
                return true;
            }
    if (event.eventComponent != this)
        return false;

    const auto layout = eqLayoutFor(getLocalBounds());
    const auto position = event.getEventRelativeTo(this).getPosition();
    const auto settings = device->currentSettings();

    const auto changed = [this] (const juce::String& message)
    {
        session.markModified();
        resized();
        repaint();
        if (status) status(message);
    };

    for (int i = 0; i < knobCount; ++i)
        if (layout.knobAutomation[i].contains(position))
        {
            const auto index = parameterForKnob(*device, i);
            if (juce::isPositiveAndBelow(index, static_cast<int>(parameters.size()))
                && parameters[static_cast<size_t>(index)].automated)
            {
                const auto result = session.toggleParameterAutomationOverride(track, pluginSlot, index);
                if (status) status(result.wasOk() ? "Toggled parameter automation" : result.getErrorMessage());
                return true;
            }
        }

    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        if (!layout.bandCell[band].contains(position))
            continue;
        if (layout.bandLed[band].contains(position))
        {
            device->setBandEnabled(band, !device->bandEnabled(band));
            device->setSelectedBand(band);
            changed("Rhino EQ: band " + juce::String(band + 1)
                    + (device->bandEnabled(band) ? " on" : " off"));
        }
        else if (event.mods.isPopupMenu())
        {
            device->setSelectedBand(band);
            showEqTypeMenu(band);
        }
        else
        {
            device->setSelectedBand(band);
            changed("Rhino EQ: band " + juce::String(band + 1) + ", "
                    + eqFilterTypeName(device->bandType(band)));
        }
        return true;
    }

    if (layout.typeChooser.contains(position))
    {
        showEqTypeMenu(device->selectedBand());
        return true;
    }
    if (layout.analyserButton.contains(position))
    {
        const auto next = settings.analyser == EqEngine::AnalyserMode::Off ? EqEngine::AnalyserMode::Pre
            : settings.analyser == EqEngine::AnalyserMode::Pre ? EqEngine::AnalyserMode::Post
            : EqEngine::AnalyserMode::Off;
        device->setAnalyserMode(next);
        if (next == EqEngine::AnalyserMode::Off)
        {
            stopTimer();
            if (spectrum != nullptr) spectrum->clear();
        }
        else
        {
            startTimerHz(24);
        }
        changed(next == EqEngine::AnalyserMode::Off ? "Rhino EQ: analyser off"
            : next == EqEngine::AnalyserMode::Pre ? "Rhino EQ: analysing the input"
                                                  : "Rhino EQ: analysing the output");
        return true;
    }

    if (!layout.display.contains(position))
        return false;

    const auto area = layout.display.toFloat().reduced(displayInset);
    const auto rate = device->spectrumRate();
    auto nearest = -1;
    auto nearestDistance = handleGrabDistance;
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        const auto scaled = EqEngine::scaledBand(settings.band[static_cast<size_t>(band)],
                                                 settings.scalePercent);
        const juce::Point<float> handle {xForFrequency(area, scaled.frequency),
                                         yForDb(area, handleDbFor(scaled, rate))};
        const auto distance = handle.getDistanceFrom(position.toFloat());
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = band;
        }
    }
    if (nearest < 0)
        return false;

    device->setSelectedBand(nearest);
    if (event.mods.isPopupMenu())
    {
        showEqTypeMenu(nearest);
        return true;
    }

    eqDragBand = nearest;
    eqDragging = true;
    session.beginDeviceParameterGesture(track, pluginSlot, RhinoEqDevice::frequencyParameter(nearest));
    session.beginDeviceParameterGesture(track, pluginSlot, RhinoEqDevice::gainParameter(nearest));
    changed("Rhino EQ: band " + juce::String(nearest + 1) + ", "
            + eqFilterTypeName(device->bandType(nearest)));
    return true;
}

bool DeviceEditorPanel::handleEqDrag(const juce::MouseEvent& event)
{
    if (!eqDragging || eqDragBand < 0)
        return false;
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;
    const auto area = eqLayoutFor(getLocalBounds()).display.toFloat().reduced(displayInset);
    const auto position = event.getEventRelativeTo(this).position;
    session.setDeviceParameter(track, pluginSlot, RhinoEqDevice::frequencyParameter(eqDragBand),
                               frequencyForX(area, position.x));
    // A cut has nowhere to go vertically, so dragging one only sweeps it.
    if (eqTypeUsesGain(device->bandType(eqDragBand)))
        session.setDeviceParameter(track, pluginSlot, RhinoEqDevice::gainParameter(eqDragBand),
                                   dbForY(area, position.y));
    return true;
}

bool DeviceEditorPanel::handleEqDoubleClick(const juce::MouseEvent& event)
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;
    const auto layout = eqLayoutFor(getLocalBounds());
    const auto position = event.getEventRelativeTo(this).getPosition();
    if (!layout.display.contains(position))
        return false;

    const auto area = layout.display.toFloat().reduced(displayInset);
    const auto settings = device->currentSettings();
    const auto rate = device->spectrumRate();
    for (int band = 0; band < RhinoEqDevice::bandCount; ++band)
    {
        const auto scaled = EqEngine::scaledBand(settings.band[static_cast<size_t>(band)],
                                                 settings.scalePercent);
        const juce::Point<float> handle {xForFrequency(area, scaled.frequency),
                                         yForDb(area, handleDbFor(scaled, rate))};
        if (handle.getDistanceFrom(position.toFloat()) >= handleGrabDistance)
            continue;
        // The drag that the first click of this double-click started never
        // moved, so there is nothing to undo -- just stop holding the band.
        eqDragging = false;
        eqDragBand = -1;
        device->setBandEnabled(band, !device->bandEnabled(band));
        session.markModified();
        resized();
        repaint();
        if (status)
            status("Rhino EQ: band " + juce::String(band + 1)
                   + (device->bandEnabled(band) ? " on" : " off"));
        return true;
    }
    return false;
}

bool DeviceEditorPanel::handleEqWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;
    const auto layout = eqLayoutFor(getLocalBounds());
    if (!layout.display.contains(event.getEventRelativeTo(this).getPosition()))
        return false;

    const auto band = device->selectedBand();
    if (!eqTypeUsesQ(device->bandType(band)))
        return true;
    const auto index = RhinoEqDevice::qParameter(band);
    if (!juce::isPositiveAndBelow(index, static_cast<int>(parameters.size())))
        return false;
    // Q is multiplied rather than added: a tenth of a turn should widen a
    // narrow notch as much as it widens a broad bell.
    const auto current = parameters[static_cast<size_t>(index)].value;
    const auto next = std::clamp(current * std::pow(2.0f, wheel.deltaY * 2.0f),
                                 eqMinimumQ, eqMaximumQ);
    session.setDeviceParameter(track, pluginSlot, index, next);
    if (status) status("Rhino EQ: band " + juce::String(band + 1) + " Q " + juce::String(next, 2));
    return true;
}

void DeviceEditorPanel::showEqTypeMenu(int band)
{
    auto* device = eqDeviceIn(session, track, pluginSlot);
    if (device == nullptr || !juce::isPositiveAndBelow(band, RhinoEqDevice::bandCount))
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader("BAND " + juce::String(band + 1));
    menu.addItem(1, device->bandEnabled(band) ? "Switch off" : "Switch on");
    menu.addSeparator();
    for (int i = 0; i < eqFilterTypeCount; ++i)
        menu.addItem(i + 2, eqFilterTypeName(static_cast<EqFilterType>(i)), true,
                     static_cast<EqFilterType>(i) == device->bandType(band));

    // The menu outlives the click, so neither the panel nor the device can be
    // assumed to still be there when it closes: the panel is guarded by a
    // SafePointer and the device is looked up again through it.
    const auto safe = juce::Component::SafePointer<DeviceEditorPanel>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe, band] (int result)
        {
            if (safe == nullptr || result == 0) return;
            auto* target = eqDeviceIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr) return;
            juce::String message;
            if (result == 1)
            {
                target->setBandEnabled(band, !target->bandEnabled(band));
                message = "Rhino EQ: band " + juce::String(band + 1)
                        + (target->bandEnabled(band) ? " on" : " off");
            }
            else
            {
                const auto type = static_cast<EqFilterType>(result - 2);
                target->setBandType(band, type);
                // A band nobody can hear that has just been given a shape was
                // almost certainly meant to be heard.
                target->setBandEnabled(band, true);
                message = "Rhino EQ: band " + juce::String(band + 1) + " is a "
                        + juce::String(eqFilterTypeName(type)).toLowerCase();
            }
            safe->session.markModified();
            safe->resized();
            safe->repaint();
            if (safe->status) safe->status(message);
        });
}
}
