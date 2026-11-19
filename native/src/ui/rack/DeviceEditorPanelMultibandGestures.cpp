#include "DeviceEditorPanelMultibandInternal.h"
#include <algorithm>
#include <cmath>
#include <limits>

// What a point on Multiband Dynamics' face is, and what a press, a drag, a
// double-click or a pause there does (DeviceEditorPanelMultiband.cpp has the
// rest). An edge or a block moves its threshold or ratio, with Alt the band's
// other one too and with Ctrl on every band; a readout moves itself; a
// switch is a click. Each is one undo step, through the session's parameter
// gestures, as a knob's turn is.
namespace rhino
{
using namespace multibandface;

DeviceEditorPanel::MultibandHit DeviceEditorPanel::multibandHitAt(juce::Point<int> position) const
{
    const auto layout = multibandLayout(getLocalBounds());
    const auto valueOf = [this] (int parameter)
    {
        return juce::isPositiveAndBelow(parameter, static_cast<int>(parameters.size()))
            ? parameters[static_cast<size_t>(parameter)].value : 0.0f;
    };
    MultibandHit hit;
    const auto take = [&hit, &position] (juce::Rectangle<int> area, MultibandHit::Kind kind, int parameter)
    {
        if (hit.kind != MultibandHit::Kind::none || area.isEmpty() || !area.contains(position))
            return;
        hit.kind = kind;
        hit.parameter = parameter;
    };
    using Kind = MultibandHit::Kind;
    take(layout.bands[0].splitSwitch, Kind::toggle, Device::lowBandParameter);
    take(layout.bands[0].crossover, Kind::readout, Device::lowCrossoverParameter);
    take(layout.bands[2].splitSwitch, Kind::toggle, Device::highBandParameter);
    take(layout.bands[2].crossover, Kind::readout, Device::highCrossoverParameter);
    take(layout.softKnee, Kind::toggle, Device::softKneeParameter);
    take(layout.rms, Kind::toggle, Device::rmsParameter);
    for (int b = 0; b < 3 && hit.kind == Kind::none; ++b)
    {
        const auto& row = layout.bands[static_cast<size_t>(b)];
        take(row.active, Kind::toggle, parameterOf(b, Control::active));
        take(row.solo, Kind::toggle, parameterOf(b, Control::solo));
        take(row.belowThreshold, Kind::readout, parameterOf(b, Control::belowThreshold));
        take(row.belowRatio, Kind::readout, parameterOf(b, Control::belowRatio));
        take(row.aboveThreshold, Kind::readout, parameterOf(b, Control::aboveThreshold));
        take(row.aboveRatio, Kind::readout, parameterOf(b, Control::aboveRatio));
        take(row.attack, Kind::readout, parameterOf(b, Control::attack));
        take(row.release, Kind::readout, parameterOf(b, Control::release));
        if (hit.kind != Kind::none)
        {
            hit.band = b;
            continue;
        }
        if (!row.meter.contains(position))
            continue;
        const auto meter = row.meter.toFloat();
        const auto above = valueOf(thresholdOf(b, true));
        // The lower threshold is held at or below the upper, as the engine
        // holds it, so the blocks never cross.
        const auto below = std::min(valueOf(thresholdOf(b, false)), above);
        const auto x = static_cast<float>(position.x);
        hit.band = b;
        switch (md::gripAt(x, md::xForDb(below, meter.getX(), meter.getWidth()),
                           md::xForDb(above, meter.getX(), meter.getWidth())))
        {
            case md::Grip::belowEdge:  hit.kind = Kind::edge;  hit.above = false; break;
            case md::Grip::aboveEdge:  hit.kind = Kind::edge;  hit.above = true;  break;
            case md::Grip::belowBlock: hit.kind = Kind::block; hit.above = false; break;
            case md::Grip::aboveBlock: hit.kind = Kind::block; hit.above = true;  break;
            case md::Grip::none:       hit.kind = Kind::meters; break;
        }
        hit.parameter = hit.kind == Kind::edge ? thresholdOf(b, hit.above)
                      : hit.kind == Kind::block ? ratioOf(b, hit.above) : -1;
    }
    return hit;
}

bool DeviceEditorPanel::handleMultibandMouseDown(const juce::MouseEvent& event)
{
    const auto position = event.getEventRelativeTo(this).getPosition();
    const auto hit = multibandHitAt(position);
    using Kind = MultibandHit::Kind;
    if (hit.kind == Kind::none || hit.kind == Kind::meters)
        return false;
    if (event.mods.isPopupMenu())
    {
        showParameterMenu(hit.parameter);
        return true;
    }
    if (!juce::isPositiveAndBelow(hit.parameter, static_cast<int>(parameters.size())))
        return false;
    if (hit.kind == Kind::toggle)
    {
        // Each switch is one of the device's own controls, written as one
        // undo step the way a generated switch is. Read before the write: the
        // rack hears of it at once and may give this face fresh parameters.
        const auto name = parameters[static_cast<size_t>(hit.parameter)].name;
        const auto next = parameters[static_cast<size_t>(hit.parameter)].value >= 0.5f ? 0.0f : 1.0f;
        writeParameter(hit.parameter, next);
        readMultibandParameter(hit.parameter);
        repaint();
        if (status) status(name + (next >= 0.5f ? " on" : " off"));
        return true;
    }

    multibandDrag = {};
    multibandDrag.kind = hit.kind == Kind::edge ? MultibandDrag::Kind::edge
                       : hit.kind == Kind::block ? MultibandDrag::Kind::block : MultibandDrag::Kind::readout;
    multibandDrag.last = event.getEventRelativeTo(this).position;
    // A readout moves itself. An edge or a block moves its own threshold or
    // ratio, and the band's other one too with Alt, on every band with Ctrl.
    std::vector<int> controls;
    if (multibandDrag.kind == MultibandDrag::Kind::readout)
    {
        controls.push_back(hit.parameter);
    }
    else
    {
        const auto edge = multibandDrag.kind == MultibandDrag::Kind::edge;
        const auto own = [edge] (int band, bool above) { return edge ? thresholdOf(band, above) : ratioOf(band, above); };
        for (int b = 0; b < 3; ++b)
        {
            if (b != hit.band && !event.mods.isCtrlDown())
                continue;
            controls.push_back(own(b, hit.above));
            if (event.mods.isAltDown())
                controls.push_back(own(b, !hit.above));
        }
    }
    for (const auto control : controls)
    {
        if (!juce::isPositiveAndBelow(control, static_cast<int>(parameters.size())))
            continue;
        const auto started = session.beginDeviceParameterGesture(track, pluginSlot, control);
        if (started.failed())
        {
            if (status) status(started.getErrorMessage());
            continue;
        }
        multibandDrag.controls.push_back(control);
        multibandDrag.from.push_back(parameters[static_cast<size_t>(control)].value);
    }
    if (multibandDrag.controls.empty())
        multibandDrag = {};
    repaint();
    return true;
}

void DeviceEditorPanel::handleMultibandDrag(const juce::MouseEvent& event)
{
    if (multibandDrag.kind == MultibandDrag::Kind::none)
        return;
    const auto position = event.getEventRelativeTo(this).position;
    const auto moved = position - multibandDrag.last;
    multibandDrag.last = position;
    const auto fine = event.mods.isShiftDown() ? fineFactor : 1.0f;
    const auto layout = multibandLayout(getLocalBounds());
    const auto valueOf = [this] (int parameter) { return parameters[static_cast<size_t>(parameter)].value; };
    const auto& controls = multibandDrag.controls;
    const auto& from = multibandDrag.from;
    const auto moving = [&controls] (int parameter)
    {
        return std::find(controls.begin(), controls.end(), parameter) != controls.end();
    };

    // How far the drag may go before one of the controls it moves would leave
    // its range, worked out each time, so turning back answers at once rather
    // than after the pointer has come back from beyond the end.
    auto lowest = -std::numeric_limits<float>::max(), highest = std::numeric_limits<float>::max();
    const auto allow = [&lowest, &highest] (float least, float most)
    {
        lowest = std::max(lowest, least);
        highest = std::min(highest, most);
    };
    std::vector<float> to(controls.size());
    switch (multibandDrag.kind)
    {
        case MultibandDrag::Kind::edge:
        {
            multibandDrag.travel += moved.x * fine * (md::ceilingDb - md::floorDb)
                                  / static_cast<float>(std::max(1, layout.meters.getWidth()));
            for (size_t k = 0; k < controls.size(); ++k)
            {
                allow(md::floorDb - from[k], md::ceilingDb - from[k]);
                // A threshold moved without its partner stops at it: the lower
                // never passes the upper.
                for (int b = 0; b < 3; ++b)
                    for (const auto upper : { false, true })
                        if (controls[k] == thresholdOf(b, upper) && !moving(thresholdOf(b, !upper)))
                        {
                            const auto partner = valueOf(thresholdOf(b, !upper)) - from[k];
                            if (upper)
                                allow(partner, highest);
                            else
                                allow(lowest, partner);
                        }
            }
            break;
        }
        case MultibandDrag::Kind::block:
            multibandDrag.travel -= moved.y * fine / volumePixels;
            for (size_t k = 0; k < controls.size(); ++k)
            {
                const auto volume = md::volumeForRatio(from[k], isAboveRatio(controls[k]));
                allow(-1.0f - volume, 1.0f - volume);
            }
            break;
        case MultibandDrag::Kind::readout:
        {
            multibandDrag.travel -= moved.y * fine / readoutPixels;
            const auto range = rangeOf(parameters[static_cast<size_t>(controls.front())]);
            const auto start = range.convertTo0to1(from.front());
            allow(-start, 1.0f - start);
            break;
        }
        case MultibandDrag::Kind::none:
            return;
    }
    if (highest < lowest)
        highest = lowest;
    multibandDrag.travel = std::clamp(multibandDrag.travel, lowest, highest);
    const auto travel = multibandDrag.travel;

    for (size_t k = 0; k < controls.size(); ++k)
    {
        auto value = from[k] + travel;
        if (multibandDrag.kind == MultibandDrag::Kind::block)
        {
            const auto upper = isAboveRatio(controls[k]);
            value = md::ratioForVolume(md::volumeForRatio(from[k], upper) + travel, upper);
        }
        else if (multibandDrag.kind == MultibandDrag::Kind::readout)
        {
            const auto range = rangeOf(parameters[static_cast<size_t>(controls[k])]);
            value = range.convertFrom0to1(std::clamp(range.convertTo0to1(from[k]) + travel, 0.0f, 1.0f));
        }
        const auto result = session.setDeviceParameter(track, pluginSlot, controls[k], value);
        if (result.failed() && status)
            status(result.getErrorMessage());
        readMultibandParameter(controls[k]);
    }
    repaint();
}

void DeviceEditorPanel::endMultibandDrag(bool report)
{
    if (multibandDrag.kind == MultibandDrag::Kind::none)
        return;
    for (const auto control : multibandDrag.controls)
        session.endDeviceParameterGesture(track, pluginSlot, control);
    if (report && status && !multibandDrag.controls.empty()
        && juce::isPositiveAndBelow(multibandDrag.controls.front(), static_cast<int>(parameters.size())))
    {
        const auto& first = parameters[static_cast<size_t>(multibandDrag.controls.front())];
        const auto others = static_cast<int>(multibandDrag.controls.size()) - 1;
        status(first.name + " " + first.valueText
               + (others > 0 ? ", and " + juce::String(others) + " more with it" : juce::String()));
    }
    multibandDrag = {};
    repaint();
}

bool DeviceEditorPanel::handleMultibandDoubleClick(const juce::MouseEvent& event)
{
    const auto hit = multibandHitAt(event.getEventRelativeTo(this).getPosition());
    using Kind = MultibandHit::Kind;
    if (hit.kind != Kind::block && hit.kind != Kind::readout)
        return false;
    // The second press of the double-click took hold of what was under it and
    // never moved, so there is nothing to keep: let go first, or the release
    // that follows finds nothing held and the gestures stay open.
    endMultibandDrag(false);
    if (!juce::isPositiveAndBelow(hit.parameter, static_cast<int>(parameters.size())))
        return false;
    const auto fallback = parameters[static_cast<size_t>(hit.parameter)].defaultValue;
    if (!fallback.has_value())
        return false;
    writeParameter(hit.parameter, *fallback);
    readMultibandParameter(hit.parameter);
    repaint();
    if (status) status(parameters[static_cast<size_t>(hit.parameter)].name + " back to "
                       + parameters[static_cast<size_t>(hit.parameter)].valueText);
    return true;
}

juce::String DeviceEditorPanel::multibandTooltip(juce::Point<int> position) const
{
    const auto hit = multibandHitAt(position);
    using Kind = MultibandHit::Kind;
    if (hit.kind == Kind::meters && juce::isPositiveAndBelow(hit.band, 3))
        return juce::String(bandNames[hit.band]) + " band: the thin bar is its level coming in, after Input, "
               "and the thick bar its level going out";
    if (!juce::isPositiveAndBelow(hit.parameter, static_cast<int>(parameters.size())))
        return {};
    const auto& parameter = parameters[static_cast<size_t>(hit.parameter)];
    const auto reading = parameter.name + ", " + parameter.valueText;
    switch (hit.kind)
    {
        case Kind::toggle:
            return reading + ". Click to switch, right-click for automation";
        case Kind::readout:
            return reading + ". Drag up or down, Shift for finer; double-click for its default; right-click for automation";
        case Kind::edge:
            return reading + ". Drag sideways. Ctrl moves every band, Alt both of the band's thresholds, Shift finer";
        case Kind::block:
            return reading + (hit.above ? ". Drag down to compress what is above it, up to expand it"
                                        : ". Drag up to lift what is below it, down to push it further down")
                 + ". Double-click for 1:1. Ctrl moves every band, Alt both blocks, Shift finer";
        case Kind::none:
        case Kind::meters:
            break;
    }
    return {};
}
}
