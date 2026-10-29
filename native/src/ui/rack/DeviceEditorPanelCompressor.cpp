#include "DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "audio/CompressorDevice.h"
#include <algorithm>
#include <array>
#include <cmath>

// The Compressor's gain reduction meter: how far the compressor is turning the
// audio down, in decibels, its Output gain left out. A bar falls from 0 dB at
// the top of a column at the right of the plain grid, at once as the
// reduction deepens and at a track meter's 24 dB a second as it lets go, with
// the deepest of the last moment held as a line and printed under it. The
// figures are the gain the device applied (CompressorDevice), read once a
// frame.
//
// Drawn rather than made a child, as the Sidechain strip is, because the rack
// rebuilds its panels whenever the chain changes.
namespace rhino
{
namespace
{
constexpr float reductionScaleDb = 24.0f;
constexpr std::array<float, 5> reductionTicks {0.0f, 3.0f, 6.0f, 12.0f, 24.0f};

// How far down the bar a reduction reaches, from 0 at the top to 1 at 24 dB
// and beyond. On a square root, as a clip's Pitch envelope is drawn, so the few
// decibels a mix is usually compressed by take a third of the height rather
// than an eighth.
int reductionPixels(juce::Rectangle<int> bar, float decibels)
{
    const auto depth = std::sqrt(std::clamp(decibels, 0.0f, reductionScaleDb) / reductionScaleDb);
    return juce::roundToInt(depth * static_cast<float>(bar.getHeight()));
}

struct ReductionLayout
{
    juce::Rectangle<int> caption, well, bar, scale, reading;
};

ReductionLayout reductionLayout(juce::Rectangle<int> area)
{
    ReductionLayout layout;
    layout.caption = area.removeFromTop(DeviceEditorPanel::captionHeight);
    layout.reading = area.removeFromBottom(DeviceEditorPanel::readingHeight);
    area.reduce(0, 4);
    layout.well = {area.getX() + 6, area.getY(), 14, area.getHeight()};
    layout.bar = layout.well.reduced(3, 2);
    layout.scale = area.withLeft(layout.well.getRight() + 3);
    return layout;
}

// The reading, in tenths of a decibel so that what is printed and what decides
// a repaint are the same figure.
int reductionTenths(float decibels)
{
    return juce::roundToInt(std::max(0.0f, decibels) * 10.0f);
}
}

void DeviceEditorPanel::layoutReductionMeter()
{
    reductionArea = {};
    if (!reductionMetered)
        return;
    reductionArea = contentArea.removeFromRight(reductionMeterWidth);
    contentArea.removeFromRight(reductionMeterGap);
}

void DeviceEditorPanel::paintReductionMeter(juce::Graphics& g)
{
    const auto layout = reductionLayout(reductionArea);
    g.setColour(palette::text);
    g.setFont(captionFont());
    drawSnappedText(g, "GR", layout.caption, juce::Justification::centred);

    g.setColour(palette::displayInset);
    g.fillRoundedRectangle(layout.well.toFloat(), 2.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(layout.well.toFloat().reduced(0.5f), 2.0f, 1.0f);

    // The scale: a rule across the bar at each figure, under the bar, and the
    // figure beside it, kept inside the column at either end.
    g.setFont(uiFont(7.5f));
    for (const auto tick : reductionTicks)
    {
        const auto y = layout.bar.getY() + reductionPixels(layout.bar, tick);
        g.setColour(palette::border);
        g.fillRect(layout.bar.getX(), std::min(y, layout.bar.getBottom() - 1), layout.bar.getWidth(), 1);
        const auto label = juce::Rectangle<int>(layout.scale.getX(), y - 5, layout.scale.getWidth(), 10)
                               .constrainedWithin(layout.scale);
        g.setColour(palette::textDim);
        drawSnappedText(g, juce::String(juce::roundToInt(tick)), label, juce::Justification::centredLeft);
    }

    const auto level = reductionPixels(layout.bar, reduction.levelDb);
    const auto held = reductionPixels(layout.bar, reduction.heldDb);
    g.setColour(palette::gainReduction);
    g.fillRect(layout.bar.withHeight(level));
    if (held > 0)
        g.fillRect(layout.bar.getX(), layout.bar.getY() + held - 1, layout.bar.getWidth(), 1);

    const auto tenths = reductionTenths(reduction.heldDb);
    g.setColour(tenths > 0 ? palette::text : palette::textDim);
    g.setFont(uiFont(8.5f));
    drawSnappedText(g, (tenths > 0 ? "-" : "") + juce::String(static_cast<float>(tenths) / 10.0f, 1) + " dB",
                    layout.reading, juce::Justification::centred);
}

void DeviceEditorPanel::tickReductionMeter()
{
    auto* compressor = dynamic_cast<CompressorDevice*>(session.devicePlugin(track, pluginSlot));
    if (compressor == nullptr)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto seconds = now - reductionFedAt;
    auto deepest = compressor->takeGainReductionDb();
    // What gathered while nobody was looking, before this face's first frame
    // or behind a hidden pane, is not what the compressor is doing now.
    if (reductionFedAt <= 0.0 || seconds > 0.5)
        deepest = 0.0f;
    reductionFedAt = now;
    reduction.advance(deepest, seconds);

    const auto bar = reductionLayout(reductionArea).bar;
    const std::array<int, 3> drawn {reductionPixels(bar, reduction.levelDb), reductionPixels(bar, reduction.heldDb),
                                    reductionTenths(reduction.heldDb)};
    if (drawn == reductionDrawn)
        return;
    reductionDrawn = drawn;
    repaint(reductionArea);
}

juce::String DeviceEditorPanel::reductionMeterTooltip() const
{
    return "Gain reduction: how far " + deviceName + " is turning the audio down, its Output gain left out. "
           "The figure is the deepest of the last moment";
}
}
