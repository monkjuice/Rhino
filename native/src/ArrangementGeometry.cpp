#include "Arrangement.h"
#include <limits>

namespace theta
{
float Arrangement::laneContentHeight() const
{
    return std::max(1.0f, getHeight() - lanesTop - 18.0f);
}

float Arrangement::laneHeight() const
{
    return std::max(48.0f, std::min(82.0f, laneContentHeight() / 2.0f));
}

juce::Rectangle<float> Arrangement::lane(int track) const
{
    const auto height = laneHeight();
    return {headerWidth, lanesTop + track * height - static_cast<float>(trackScroll),
            std::max(1.0f, getWidth() - headerWidth - 14.0f), height};
}

float Arrangement::xFor(double seconds) const
{
    return headerWidth + static_cast<float>((seconds - viewStart) / viewSpan * lane(0).getWidth());
}

double Arrangement::timeAt(float x) const
{
    return viewStart + (x - headerWidth) / lane(0).getWidth() * viewSpan;
}

juce::Rectangle<float> Arrangement::bounds(const ClipView& clip) const
{
    const auto p = dragging && clip.id == selected ? preview : clip.position;
    const auto track = dragging && clip.id == selected ? previewTrack : clip.track;
    return {xFor(p.start), lane(track).getY() + 5.0f,
            std::max(1.0f, xFor(p.end) - xFor(p.start)), lane(track).getHeight() - 10.0f};
}

double Arrangement::snapped(double seconds, bool bypass) const
{
    const auto unit = snapUnitSeconds();
    return snap.getToggleState() && !bypass ? std::round(seconds / unit) * unit : seconds;
}

double Arrangement::snappedClipMoveStart(double desiredStart, double length, int targetTrack, bool bypass) const
{
    if (bypass || !snap.getToggleState())
        return desiredStart;

    const auto desiredEnd = desiredStart + length;
    auto bestStart = desiredStart;
    auto bestPixels = std::numeric_limits<float>::max();
    constexpr auto edgeSnapPixels = 14.0f;

    for (const auto& clip : clips)
    {
        if (isSelected(clip.id) || clip.track != targetTrack)
            continue;
        for (const auto edge : {clip.position.start, clip.position.end})
        {
            const auto startPixels = std::abs(xFor(edge) - xFor(desiredStart));
            if (startPixels <= edgeSnapPixels && startPixels < bestPixels)
            {
                bestPixels = startPixels;
                bestStart = edge;
            }

            const auto endPixels = std::abs(xFor(edge) - xFor(desiredEnd));
            if (endPixels <= edgeSnapPixels && endPixels < bestPixels)
            {
                bestPixels = endPixels;
                bestStart = edge - length;
            }
        }
    }

    return std::max(0.0, bestPixels < std::numeric_limits<float>::max() ? bestStart : snapped(desiredStart, false));
}

juce::Rectangle<float> Arrangement::automationBounds(const ClipView& clip) const
{
    // Automation is an overlay on the clip. Give every curve the complete
    // usable clip height, including while its first drag is still a preview.
    const auto clipBounds = bounds(clip);
    return clipBounds.reduced(0.0f, 4.0f);
}

float Arrangement::automationValueForY(const ClipView& clip, float y, Session::DeviceTarget target) const
{
    const auto parameters = session.deviceParameters(target.track, target.slot);
    if (!juce::isPositiveAndBelow(target.parameter, parameters.size()))
        return 0.0f;
    const auto& parameter = parameters[static_cast<size_t>(target.parameter)];
    const auto stack = automationBounds(clip);
    // Curves share one editor. Adding a parameter must never reduce the
    // physical throw used to reach its minimum and maximum.
    const auto area = stack.reduced(0.0f, 1.0f);
    const auto amount = 1.0f - std::clamp((y - area.getY()) / std::max(1.0f, area.getHeight()), 0.0f, 1.0f);
    return parameter.minimum + (parameter.maximum - parameter.minimum) * amount;
}

int Arrangement::activeAutomationIndex(const ClipView& clip) const
{
    if (clip.id != activeAutomationClip || !activeAutomationTarget.isValid())
        return -1;
    for (int i = 0; i < static_cast<int>(clip.automations.size()); ++i)
    {
        const auto& automation = clip.automations[static_cast<size_t>(i)];
        if (automation.target.track == activeAutomationTarget.track && automation.target.slot == activeAutomationTarget.slot
            && automation.target.parameter == activeAutomationTarget.parameter)
            return i;
    }
    return -1;
}

int Arrangement::displayedAutomationIndex(const ClipView& clip) const
{
    // A new parameter has no persisted lane yet. Promote its live preview to
    // the full editor instead of squeezing it into the overview strip.
    if (automationDragging && clip.id == selected && automationTarget.isValid())
    {
        for (int i = 0; i < static_cast<int>(clip.automations.size()); ++i)
        {
            const auto& automation = clip.automations[static_cast<size_t>(i)];
            if (automation.target.track == automationTarget.track && automation.target.slot == automationTarget.slot
                && automation.target.parameter == automationTarget.parameter)
                return i;
        }
        return static_cast<int>(clip.automations.size());
    }

    return activeAutomationIndex(clip);
}

int Arrangement::automationLaneAt(const ClipView& clip, juce::Point<float> point) const
{
    if (clip.automations.empty() || !automationBounds(clip).contains(point))
        return -1;
    return std::max(0, activeAutomationIndex(clip));
}

int Arrangement::hit(juce::Point<float> point) const
{
    if (point.x < headerWidth) return -1;
    for (int i = static_cast<int>(clips.size()); --i >= 0;)
        if (bounds(clips[static_cast<size_t>(i)]).contains(point)) return i;
    return -1;
}

Arrangement::LoopGesture Arrangement::loopGestureAt(juce::Point<float> point) const
{
    if (!session.hasManualLoopRange() || point.y < rulerTop || point.y >= lanesTop || point.x < headerWidth)
        return LoopGesture::none;

    const auto loopRange = session.edit->getTransport().getLoopRange();
    const auto x1 = xFor(loopRange.getStart().inSeconds());
    const auto x2 = xFor(loopRange.getEnd().inSeconds());
    const auto left = std::min(x1, x2);
    const auto right = std::max(x1, x2);
    if (point.x < left || point.x > right)
        return LoopGesture::none;

    const auto handle = std::min(10.0f, std::max(4.0f, (right - left) * 0.3f));
    if (point.x - left <= handle)
        return LoopGesture::trimStart;
    if (right - point.x <= handle)
        return LoopGesture::trimEnd;
    return LoopGesture::move;
}

double Arrangement::snapUnitSeconds() const
{
    const auto beatSeconds = 60.0 / session.tempo();
    switch (snapSize.getSelectedId())
    {
        case 2: return beatSeconds * 0.5;
        case 3: return beatSeconds;
        case 4: return beatSeconds * 4.0;
        default: return beatSeconds * 0.25;
    }
}

int Arrangement::trackAt(float y) const
{
    for (int track = 0; track < session.trackCount(); ++track)
        if (lane(track).contains(juce::Point<float>(headerWidth, y)))
            return track;
    return -1;
}
}
