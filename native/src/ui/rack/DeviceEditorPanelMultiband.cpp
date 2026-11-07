#include "DeviceEditorPanelMultibandInternal.h"
#include "ui/shared/Theme.h"
#include <algorithm>
#include <cmath>

// Multiband Dynamics' face, after Live's: the band switches and the split down
// the left, each band's Input, then the display, each band's Output, and the
// device's Output, Time and Amount on the right.
//
// The display has a row per band, High at the top. The Below block runs from
// the left end to the lower threshold and the Above block from the upper
// threshold to the right end, across Live's -80 to 0 dB. Dragging a block's
// edge sideways moves its threshold, and dragging the block up or down makes
// what is in it louder or quieter, which is its ratio (MultibandDisplayGeometry.h).
// Ctrl does the same to every band, Alt to both of a band's thresholds or both
// of its blocks, and Shift makes it finer; a double-click on a block puts it
// back to 1:1. Through each row run the band's level coming in, after its
// Input, as a thin bar, and going out, as a thick one.
//
// The numbers beside the blocks and the crossovers are readouts, dragged up
// and down and double-clicked back to their defaults. Every drag is one undo
// step, written through the session's parameter gestures as a knob's turn is,
// so a lane, a preset and an undo all see the same controls.
//
// Everything but the knobs is drawn and hit-tested by rectangle: the rack
// rebuilds its panels whenever the chain changes.
//
// Pressing, dragging and pointing are DeviceEditorPanelMultibandGestures.cpp.
namespace rhino
{
using namespace multibandface;

DeviceEditorPanel::MultibandLayout DeviceEditorPanel::multibandLayout(juce::Rectangle<int> bounds)
{
    MultibandLayout layout;
    layout.content = bounds.withTrimmedTop(headerHeight + 1).reduced(4);
    const auto& content = layout.content;
    const auto rowsTop = content.getY() + titleHeight;
    const auto rowHeight = (content.getHeight() - titleHeight - footerHeight) / 3;
    auto x = content.getX();
    const auto column = [&x] (int width, int gap)
    {
        const juce::Range<int> taken(x, x + width);
        x += width + gap;
        return taken;
    };
    const auto split = column(92, 6), input = column(44, 6), below = column(50, 4), meters = column(176, 4),
               above = column(50, 6), times = column(46, 6), output = column(44, 10), global = column(56, 0);
    layout.well = { below.getStart() - 4, content.getY(), times.getEnd() + 4 - (below.getStart() - 4),
                    content.getHeight() };
    layout.meters = { meters.getStart(), rowsTop, meters.getLength(), rowHeight * 3 };
    const auto footer = content.getBottom() - lineHeight;
    layout.scale = { meters.getStart(), footer, meters.getLength(), lineHeight };
    const auto box = [] (juce::Range<int> across, int y)
    {
        return juce::Rectangle<int>(across.getStart(), y, across.getLength(), lineHeight);
    };
    const auto switchesLeft = split.getStart() + 2;
    for (int b = 0; b < 3; ++b)
    {
        auto& row = layout.bands[static_cast<size_t>(b)];
        const auto y = rowsTop + (2 - b) * rowHeight;
        const auto top = y + 3, bottom = top + lineHeight + 2;
        // The mid band is never split off, so it has no switch and no
        // crossover of its own.
        if (b != 1)
        {
            row.splitSwitch = { switchesLeft, top, 52, lineHeight };
            row.crossover = { switchesLeft, bottom, 52, lineHeight };
        }
        row.active = { switchesLeft + 55, top, 18, lineHeight };
        row.solo = { switchesLeft + 55, bottom, 18, lineHeight };
        row.input = { input.getStart(), y, input.getLength(), rowHeight };
        row.belowThreshold = box(below, top);
        row.belowRatio = box(below, bottom);
        row.meter = { meters.getStart(), top, meters.getLength(), rowHeight - 6 };
        row.aboveThreshold = box(above, top);
        row.aboveRatio = box(above, bottom);
        row.attack = box(times, top);
        row.release = box(times, bottom);
        row.output = { output.getStart(), y, output.getLength(), rowHeight };
    }
    layout.softKnee = { switchesLeft, footer, 52, lineHeight };
    layout.rms = { switchesLeft + 55, footer, split.getEnd() - switchesLeft - 55, lineHeight };
    const auto cell = content.getHeight() / 3;
    layout.output = { global.getStart(), content.getY(), global.getLength(), cell };
    layout.time = layout.output.translated(0, cell);
    layout.amount = layout.time.translated(0, cell);
    return layout;
}

bool DeviceEditorPanel::isMultibandKnob(int parameter)
{
    if (isMultibandCaption(parameter))
        return true;
    for (int b = 0; b < 3; ++b)
        if (parameter == parameterOf(b, Control::input) || parameter == parameterOf(b, Control::output))
            return true;
    return false;
}

// The device's own knobs are captioned; a band's Input and Output stand under
// their column's title instead.
bool DeviceEditorPanel::isMultibandCaption(int parameter)
{
    return parameter == Device::outputParameter || parameter == Device::timeParameter
        || parameter == Device::amountParameter;
}

void DeviceEditorPanel::layoutMultiband()
{
    visualArea = {};
    const auto layout = multibandLayout(getLocalBounds());
    const auto shown = [this] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, parameterSliders.size());
    };
    for (int b = 0; b < 3; ++b)
    {
        const auto& row = layout.bands[static_cast<size_t>(b)];
        for (const auto& [parameter, cell] : { std::pair(parameterOf(b, Control::input), row.input),
                                               std::pair(parameterOf(b, Control::output), row.output) })
        {
            if (!shown(parameter))
                continue;
            const juce::Rectangle<int> knob(cell.getCentreX() - bandKnobSize / 2, cell.getY() + 2, bandKnobSize,
                                            bandKnobSize);
            parameterSliders[parameter]->setBounds(knob);
            parameterValues[parameter]->setBounds(cell.getX(), knob.getBottom(), cell.getWidth(), readingHeight - 1);
            placeAutomationBadge(parameter, knob);
        }
    }
    for (const auto& [parameter, cell] : { std::pair(Device::outputParameter, layout.output),
                                           std::pair(Device::timeParameter, layout.time),
                                           std::pair(Device::amountParameter, layout.amount) })
        if (shown(parameter))
            placeKnob(parameter, cell, 26);

    // A drawn control carries its A on its own corner, as a switch does.
    const auto badge = [this, &shown] (int parameter, juce::Rectangle<int> control)
    {
        if (shown(parameter) && !control.isEmpty())
            placeAutomationBadge(parameter, control);
    };
    badge(Device::lowBandParameter, layout.bands[0].splitSwitch);
    badge(Device::lowCrossoverParameter, layout.bands[0].crossover);
    badge(Device::highBandParameter, layout.bands[2].splitSwitch);
    badge(Device::highCrossoverParameter, layout.bands[2].crossover);
    badge(Device::softKneeParameter, layout.softKnee);
    badge(Device::rmsParameter, layout.rms);
    for (int b = 0; b < 3; ++b)
    {
        const auto& row = layout.bands[static_cast<size_t>(b)];
        badge(parameterOf(b, Control::active), row.active);
        badge(parameterOf(b, Control::solo), row.solo);
        badge(parameterOf(b, Control::belowThreshold), row.belowThreshold);
        badge(parameterOf(b, Control::belowRatio), row.belowRatio);
        badge(parameterOf(b, Control::aboveThreshold), row.aboveThreshold);
        badge(parameterOf(b, Control::aboveRatio), row.aboveRatio);
        badge(parameterOf(b, Control::attack), row.attack);
        badge(parameterOf(b, Control::release), row.release);
    }
}

void DeviceEditorPanel::readMultibandParameter(int parameter)
{
    if (!juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size())))
        return;
    if (auto fresh = session.deviceParameter(track, pluginSlot, parameter))
        parameters[static_cast<size_t>(parameter)] = *fresh;
}

void DeviceEditorPanel::paintMultiband(juce::Graphics& g)
{
    const auto layout = multibandLayout(getLocalBounds());
    const auto accent = faceAccent();
    const auto count = static_cast<int>(parameters.size());
    const auto valueOf = [this, count] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, count) ? parameters[static_cast<size_t>(parameter)].value : 0.0f;
    };
    const auto textOf = [this, count] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, count) ? parameters[static_cast<size_t>(parameter)].valueText
                                                          : juce::String();
    };
    const auto on = [&valueOf] (int parameter) { return valueOf(parameter) >= 0.5f; };
    const std::array<bool, 3> split { on(Device::lowBandParameter), true, on(Device::highBandParameter) };
    const auto held = multibandDrag.kind == MultibandDrag::Kind::none ? std::vector<int>() : multibandDrag.controls;
    const auto isHeld = [&held] (int parameter) { return std::find(held.begin(), held.end(), parameter) != held.end(); };

    const auto switchBox = [&g, &accent] (juce::Rectangle<int> area, const juce::String& text, bool lit, bool live)
    {
        g.setColour(lit ? accent.withAlpha(live ? 0.9f : 0.4f) : palette::control);
        g.fillRoundedRectangle(area.toFloat(), 2.5f);
        g.setColour(lit ? accent.brighter(0.3f).withAlpha(live ? 1.0f : 0.45f) : palette::border);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(lit ? palette::displayInset : live ? palette::textDim : palette::disabled);
        g.setFont(uiFontBold(7.5f));
        drawSnappedText(g, text, area, juce::Justification::centred);
    };
    const auto readout = [&] (juce::Rectangle<int> area, int parameter, bool live)
    {
        const auto hand = isHeld(parameter);
        g.setColour(hand ? accent.withAlpha(0.22f) : palette::control);
        g.fillRoundedRectangle(area.toFloat(), 2.0f);
        g.setColour(hand ? accent : palette::border);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.0f, 1.0f);
        g.setColour(live ? palette::text : palette::disabled);
        g.setFont(uiFont(8.5f));
        drawSnappedText(g, textOf(parameter), area, juce::Justification::centred);
    };

    // ---- the band switches, the split and the detector ---------------------
    if (g.clipRegionIntersects(layout.content.withRight(layout.well.getX())))
    {
        for (int b = 0; b < 3; ++b)
        {
            const auto& row = layout.bands[static_cast<size_t>(b)];
            const auto active = on(parameterOf(b, Control::active));
            if (b == 1)
            {
                g.setColour(palette::textDim);
                g.setFont(uiFontBold(7.5f));
                drawSnappedText(g, "MID", row.active.withX(row.active.getX() - 55).withWidth(52)
                                              .withBottom(row.solo.getBottom()), juce::Justification::centred);
            }
            else
            {
                const auto splitParameter = b == 0 ? Device::lowBandParameter : Device::highBandParameter;
                switchBox(row.splitSwitch, b == 0 ? "LOW" : "HIGH", on(splitParameter), true);
                readout(row.crossover, b == 0 ? Device::lowCrossoverParameter : Device::highCrossoverParameter,
                        split[static_cast<size_t>(b)]);
            }
            // The band's switch, a power symbol, lit while its thresholds and
            // gains act.
            switchBox(row.active, {}, active, split[static_cast<size_t>(b)]);
            const auto centre = row.active.toFloat().getCentre();
            juce::Path symbol;
            symbol.addCentredArc(centre.x, centre.y + 0.5f, 3.5f, 3.5f, 0.0f, juce::degreesToRadians(40.0f),
                                 juce::degreesToRadians(320.0f), true);
            symbol.startNewSubPath(centre.x, centre.y - 3.5f);
            symbol.lineTo(centre.x, centre.y + 0.5f);
            g.setColour(active ? palette::displayInset : palette::textDim);
            g.strokePath(symbol, juce::PathStrokeType(1.2f));
            switchBox(row.solo, "S", on(parameterOf(b, Control::solo)), split[static_cast<size_t>(b)]);
        }
        switchBox(layout.softKnee, "SOFT KNEE", on(Device::softKneeParameter), true);
        switchBox(layout.rms, "RMS", on(Device::rmsParameter), true);
    }

    // ---- the display ---------------------------------------------------------
    if (g.clipRegionIntersects(layout.well))
    {
        const auto well = layout.well.toFloat();
        g.setColour(palette::displayInset);
        g.fillRoundedRectangle(well, 3.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(well.reduced(0.5f), 3.0f, 1.0f);

        g.setFont(uiFontBold(7.5f));
        g.setColour(palette::textDim);
        const auto titleRow = [&layout] (const juce::Rectangle<int>& under)
        {
            return juce::Rectangle<int>(under.getX(), layout.content.getY(), under.getWidth(), titleHeight);
        };
        const auto& top = layout.bands[2];
        drawSnappedText(g, "BELOW", titleRow(top.belowThreshold), juce::Justification::centred);
        drawSnappedText(g, "ABOVE", titleRow(top.aboveThreshold), juce::Justification::centred);
        drawSnappedText(g, "ATT / REL", titleRow(top.attack), juce::Justification::centred);

        // The decibel grid, every ten, and its figures under the rows.
        const auto meters = layout.meters.toFloat();
        g.setFont(uiFont(7.5f));
        for (int decibels = static_cast<int>(md::floorDb); decibels <= static_cast<int>(md::ceilingDb); decibels += 10)
        {
            const auto x = juce::roundToInt(md::xForDb(static_cast<float>(decibels), meters.getX(), meters.getWidth()));
            g.setColour(palette::border.withAlpha(0.45f));
            g.fillRect(std::min(x, layout.meters.getRight() - 1), layout.meters.getY(), 1, layout.meters.getHeight());
            // The end figures stand inside the row's ends, the rest centred
            // under their lines.
            const auto first = decibels == static_cast<int>(md::floorDb);
            const auto last = decibels == static_cast<int>(md::ceilingDb);
            const auto figure = first ? juce::Rectangle<int>(x, layout.scale.getY(), 18, layout.scale.getHeight())
                              : last ? juce::Rectangle<int>(x - 18, layout.scale.getY(), 18, layout.scale.getHeight())
                                     : juce::Rectangle<int>(x - 9, layout.scale.getY(), 18, layout.scale.getHeight());
            g.setColour(palette::textDim);
            drawSnappedText(g, juce::String(-decibels), figure,
                            first ? juce::Justification::centredLeft
                                  : last ? juce::Justification::centredRight : juce::Justification::centred);
        }

        for (int b = 0; b < 3; ++b)
        {
            const auto& row = layout.bands[static_cast<size_t>(b)];
            const auto live = split[static_cast<size_t>(b)] && on(parameterOf(b, Control::active));
            readout(row.belowThreshold, thresholdOf(b, false), live);
            readout(row.belowRatio, ratioOf(b, false), live);
            readout(row.aboveThreshold, thresholdOf(b, true), live);
            readout(row.aboveRatio, ratioOf(b, true), live);
            readout(row.attack, parameterOf(b, Control::attack), live);
            readout(row.release, parameterOf(b, Control::release), live);

            const auto meter = row.meter.toFloat();
            const auto above = valueOf(thresholdOf(b, true));
            const auto below = std::min(valueOf(thresholdOf(b, false)), above);
            const auto belowX = md::xForDb(below, meter.getX(), meter.getWidth());
            const auto aboveX = md::xForDb(above, meter.getX(), meter.getWidth());
            const auto centre = meter.getCentreY();
            const auto reach = meter.getHeight() * 0.5f - 2.0f;
            // Each block's volume: a line above the middle for louder, below
            // for quieter, the ground between shaded green or amber, and at
            // 1:1 a faint line through the middle.
            struct Block { float left, right, volume; };
            const std::array<Block, 2> blocks {
                Block { meter.getX(), belowX, md::volumeForRatio(valueOf(ratioOf(b, false)), false) },
                Block { aboveX, meter.getRight(), md::volumeForRatio(valueOf(ratioOf(b, true)), true) } };
            const auto inkOf = [live] (float volume)
            {
                return (volume > 0.0f ? palette::meterSafe : palette::gainReduction).withAlpha(live ? 1.0f : 0.4f);
            };
            for (const auto& block : blocks)
            {
                if (block.right - block.left < 0.5f)
                    continue;
                g.setColour(palette::control.withAlpha(live ? 1.0f : 0.45f));
                g.fillRect(juce::Rectangle<float>(block.left, meter.getY(), block.right - block.left, meter.getHeight()));
                const auto level = centre - block.volume * reach;
                if (std::abs(block.volume) < 0.005f)
                {
                    g.setColour(palette::border);
                    g.fillRect(juce::Rectangle<float>(block.left, centre - 0.5f, block.right - block.left, 1.0f));
                    continue;
                }
                g.setColour(inkOf(block.volume).withMultipliedAlpha(0.25f));
                g.fillRect(juce::Rectangle<float>(block.left, std::min(centre, level), block.right - block.left,
                                                  std::abs(centre - level)));
            }

            // The band coming in and going out. Only what is louder than the
            // meters' floor is drawn, so silence is an empty row.
            const auto bar = [&] (float decibels, float y, float height, juce::Colour ink)
            {
                if (decibels <= MeterBallistics::floorDb + 0.5f)
                    return;
                const auto end = md::xForDb(decibels, meter.getX(), meter.getWidth());
                g.setColour(ink);
                g.fillRect(juce::Rectangle<float>(meter.getX(), y, end - meter.getX(), height));
            };
            bar(multibandOut[static_cast<size_t>(b)].levelDb, centre - 3.0f, 6.0f, palette::volume);
            bar(multibandIn[static_cast<size_t>(b)].levelDb, centre - 5.5f, 2.0f, palette::text);

            // Over the bars, so a level never hides what a block is set to:
            // the volume lines, then the edges, which are the thresholds.
            for (const auto& block : blocks)
            {
                if (block.right - block.left < 0.5f || std::abs(block.volume) < 0.005f)
                    continue;
                g.setColour(inkOf(block.volume));
                g.fillRect(juce::Rectangle<float>(block.left, centre - block.volume * reach - 0.75f,
                                                  block.right - block.left, 1.5f));
            }
            for (const auto& [x, upper] : { std::pair(belowX, false), std::pair(aboveX, true) })
            {
                g.setColour(isHeld(thresholdOf(b, upper)) ? accent : live ? palette::textDim : palette::disabled);
                g.fillRect(juce::Rectangle<float>(x - 1.0f, meter.getY(), 2.0f, meter.getHeight()));
            }
        }
    }

    // ---- titles over the knobs, and the line before the device's own --------
    g.setFont(uiFontBold(7.5f));
    g.setColour(palette::textDim);
    const auto& top = layout.bands[2];
    drawSnappedText(g, "INPUT", top.input.withY(layout.content.getY()).withHeight(titleHeight),
                    juce::Justification::centred);
    drawSnappedText(g, "OUTPUT", top.output.withY(layout.content.getY()).withHeight(titleHeight),
                    juce::Justification::centred);
    g.setColour(palette::border);
    g.fillRect(layout.output.getX() - 5, layout.content.getY() + 3, 1, layout.content.getHeight() - 6);
}

void DeviceEditorPanel::tickMultiband()
{
    auto* device = dynamic_cast<MultibandDynamicsDevice*>(session.devicePlugin(track, pluginSlot));
    if (device == nullptr)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto seconds = now - multibandFedAt;
    // What gathered while nobody was looking, before this face's first frame
    // or behind a hidden pane, is not what the bands are doing now.
    const auto stale = multibandFedAt <= 0.0 || seconds > 0.5;
    multibandFedAt = now;
    const auto meters = multibandLayout(getLocalBounds()).meters.toFloat();
    std::array<int, 6> drawn {};
    for (int b = 0; b < 3; ++b)
    {
        const auto levels = device->takeBandLevels(b);
        const auto decibels = [stale] (float level)
        {
            return stale ? MeterBallistics::floorDb : juce::Decibels::gainToDecibels(level, MeterBallistics::floorDb);
        };
        auto& in = multibandIn[static_cast<size_t>(b)];
        auto& out = multibandOut[static_cast<size_t>(b)];
        in.advance(decibels(levels.input), seconds);
        out.advance(decibels(levels.output), seconds);
        const auto pixel = [&meters] (float level)
        {
            return level <= MeterBallistics::floorDb + 0.5f
                ? -1 : juce::roundToInt(md::xForDb(level, meters.getX(), meters.getWidth()));
        };
        drawn[static_cast<size_t>(b * 2)] = pixel(in.levelDb);
        drawn[static_cast<size_t>(b * 2 + 1)] = pixel(out.levelDb);
    }
    if (drawn == multibandDrawn)
        return;
    multibandDrawn = drawn;
    repaint(meters.toNearestInt());
}
}
