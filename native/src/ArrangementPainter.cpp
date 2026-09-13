#include "ArrangementInternal.h"
#include "Playhead.h"
#include <optional>
#include <set>

// Arrangement rendering.

namespace theta
{

void Arrangement::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1d2228));
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(juce::Colour(0xffbbc4cc));
    g.drawText("ARRANGEMENT", 10, 0, 138, 30, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff8a969f));
    g.drawText("Drop browser items or files / drag clips to move / trim edges",
               740, 0, getWidth() - 750, 30, juce::Justification::centredLeft);
    for (int track = 0; track < session.trackCount(); ++track)
    {
        const auto row = lane(track);
        if (row.getBottom() < lanesTop || row.getY() > getHeight() - 18.0f) continue;
        g.setColour(juce::Colour(track == 0 ? 0xff242b31 : 0xff20272e));
        g.fillRect(row.withX(0.0f).withWidth(static_cast<float>(getWidth()) - 14.0f));
        if (track == selectedTrack)
        {
            g.setColour(juce::Colour(0xff343f47));
            g.fillRect(row.withWidth(headerWidth));
            g.setColour(juce::Colour(0xffc6d58c));
            g.fillRect(row.withWidth(3.0f));
        }
        g.setColour(juce::Colour(0xffc4cbd1));
        g.drawText(juce::String(track + 1).paddedLeft('0', 2) + "  " + session.trackName(track),
                   10, static_cast<int>(row.getY()) + 8, 130, 22, juce::Justification::centredLeft);
    }

    const auto beatSeconds = 60.0 / session.tempo();
    auto rulerStep = beatSeconds;
    while (rulerStep / viewSpan * lane(0).getWidth() < 64.0) rulerStep *= 2.0;
    for (auto time = std::ceil(viewStart / rulerStep) * rulerStep; time <= viewStart + viewSpan; time += rulerStep)
    {
        const auto x = xFor(time);
        g.setColour(juce::Colour(0xff35404a));
        g.drawVerticalLine(static_cast<int>(x), static_cast<int>(lanesTop), getHeight() - 18.0f);
        const int beat = juce::roundToInt(time / beatSeconds);
        g.setColour(juce::Colour(0xff8c99a4));
        g.drawText(juce::String(beat / 4 + 1) + "." + juce::String(beat % 4 + 1),
                   static_cast<int>(x) + 4, static_cast<int>(rulerTop), 64, 24, juce::Justification::centredLeft);
    }
    {
        const auto loopRange = session.edit->getTransport().getLoopRange();
        auto start = loopGesture != LoopGesture::none ? loopPreviewStart : loopRange.getStart().inSeconds();
        auto end = loopGesture != LoopGesture::none ? loopPreviewEnd : loopRange.getEnd().inSeconds();
        if (end < start) std::swap(start, end);
        if ((loopGesture != LoopGesture::none || session.hasManualLoopRange()) && end - start > 0.02)
        {
            const auto x1 = xFor(start);
            const auto x2 = xFor(end);
            juce::Rectangle<float> loopBounds {std::max(headerWidth, std::min(x1, x2)), rulerTop,
                                               std::max(0.0f, std::min(std::max(x1, x2), static_cast<float>(getWidth() - 14)) - std::max(headerWidth, std::min(x1, x2))),
                                               getHeight() - rulerTop - 18.0f};
            if (!loopBounds.isEmpty())
            {
                g.setColour(juce::Colour(0x245ab9d6));
                g.fillRect(loopBounds);
                g.setColour(juce::Colour(0xff5ab9d6));
                g.fillRect(loopBounds.withHeight(3.0f));
                g.drawVerticalLine(static_cast<int>(loopBounds.getX()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
                g.drawVerticalLine(static_cast<int>(loopBounds.getRight()), static_cast<int>(rulerTop), static_cast<int>(lanesTop));
            }
        }
    }
    const auto dirty = g.getClipBounds().toFloat();
    bool hasAudio = false;
    for (const auto& clip : clips)
    {
        hasAudio |= clip.track == 1;
        const auto paintTrack = dragging && clip.id == selected ? previewTrack : clip.track;
        const auto box = bounds(clip);
        const auto visible = box.getIntersection(lane(paintTrack))
            .getIntersection({0.0f, lanesTop, static_cast<float>(getWidth() - 14), laneContentHeight()});
        if (visible.isEmpty() || !visible.intersects(dirty)) continue;
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(juce::Rectangle<int>(0, static_cast<int>(lanesTop), getWidth() - 14,
                                                std::max(1, getHeight() - static_cast<int>(lanesTop) - 18)));
        const auto fallback = juce::Colour(clip.track == 0 ? 0xff414c34 : 0xff284b59);
        const auto label = clip.colour.isTransparent() ? fallback : clip.colour;
        g.setColour(label.withAlpha(isSelected(clip.id) ? 0.82f : 0.68f));
        g.fillRect(box);
        g.setColour(label.brighter(0.55f));
        g.fillRect(box.withHeight(4.0f));
        g.setColour(juce::Colour(isSelected(clip.id) ? 0xffdce9b1 : 0xff617985));
        g.drawRect(box.reduced(0.5f), isSelected(clip.id) ? 2.0f : 1.0f);
        g.setColour(juce::Colour(0xffe0e7ec));
        if (visible.getWidth() >= 24.0f)
            g.drawText(clip.name, visible.reduced(6.0f, 0).withHeight(23.0f), juce::Justification::centredLeft, true);
        if (clip.clipPlugins > 0)
        {
            const auto badge = visible.withSizeKeepingCentre(28.0f, 16.0f).withRightX(visible.getRight() - 5.0f).withY(visible.getY() + 5.0f);
            g.setColour(juce::Colour(0xcc15191d));
            g.fillRect(badge);
            g.setColour(label.brighter(0.75f));
            g.drawRect(badge.reduced(0.5f), 1.0f);
            g.setColour(juce::Colour(0xffeaf0f3));
            g.drawText("FX" + juce::String(clip.clipPlugins), badge, juce::Justification::centred, true);
        }
        const auto position = dragging && clip.id == selected ? preview : clip.position;
        const auto automationStack = automationBounds(clip);
        const auto activeLane = displayedAutomationIndex(clip);
        const auto automationAreaFor = [&] (int laneIndex, int laneCount)
        {
            juce::ignoreUnused(laneIndex, laneCount);
            return automationStack.reduced(0.0f, 1.0f);
        };
        const auto drawAutomation = [&] (const Session::ClipAutomation& automation, double startTime,
                                         double endTime, float startValue, float endValue, bool previewLine,
                                         int laneIndex, int laneCount)
        {
            if (!automation.active && !previewLine) return;
            const auto minValue = automation.maximum > automation.minimum ? automation.minimum : 0.0f;
            const auto maxValue = automation.maximum > automation.minimum ? automation.maximum : 1.0f;
            const auto autoArea = automationAreaFor(laneIndex, laneCount);
            const auto yFor = [autoArea, minValue, maxValue](float value)
            {
                const auto amount = std::clamp((value - minValue) / std::max(0.0001f, maxValue - minValue), 0.0f, 1.0f);
                return autoArea.getBottom() - amount * autoArea.getHeight();
            };
            const auto x1 = xFor(startTime);
            const auto x2 = xFor(endTime);
            const auto y1 = yFor(startValue);
            const auto y2 = yFor(endValue);
            const auto laneActive = laneIndex == activeLane || previewLine;
            const auto laneColour = previewLine ? juce::Colour(0xffffbf7a)
                : laneActive ? juce::Colour(0xffff8eea)
                : juce::Colour(0xffd9a5ff);
            const auto startColour = previewLine ? juce::Colour(0xffffd08a) : juce::Colour(0xff75d3e6);
            const auto endColour = previewLine ? juce::Colour(0xffffa45d) : juce::Colour(0xffffbf7a);
            if (laneActive && activeLane >= 0 && !previewLine)
            {
                g.setColour(juce::Colour(0x552f151f));
                g.fillRect(autoArea);
                g.setColour(juce::Colour(0xffffbf7a).withAlpha(0.9f));
                g.drawRect(autoArea.reduced(0.5f), 2.0f);
            }
            if (laneActive)
            {
                g.setColour(laneColour.withAlpha(0.34f));
                g.fillRect(juce::Rectangle<float>(std::min(x1, x2), autoArea.getY(), std::abs(x2 - x1), autoArea.getHeight()));
            }
            g.setColour(juce::Colour(0x5511191f));
            g.drawRect(autoArea.reduced(0.5f), 1.0f);
            g.setColour(laneColour.withAlpha(laneActive ? 1.0f : 0.38f));
            g.drawLine(x1, y1, x2, y2, laneActive ? 2.6f : 1.5f);
            if (laneActive)
            {
                const auto startHandle = juce::Rectangle<float>(9.0f, 9.0f).withCentre({x1, y1});
                const auto endHandle = juce::Rectangle<float>(9.0f, 9.0f).withCentre({x2, y2});
                g.setColour(startColour);
                g.fillRect(startHandle);
                g.setColour(juce::Colour(0xff11161b));
                g.drawRect(startHandle.reduced(0.5f), 1.0f);
                g.setColour(endColour);
                g.fillRect(endHandle);
                g.setColour(juce::Colour(0xff11161b));
                g.drawRect(endHandle.reduced(0.5f), 1.0f);
            }
            if (!automation.parameterName.isEmpty())
            {
                g.setFont(juce::FontOptions(11.0f));
                const auto labelArea = laneActive && activeLane >= 0 && !previewLine
                    ? autoArea.withHeight(18.0f).reduced(5.0f, 1.0f)
                    : !laneActive && activeLane >= 0
                        ? autoArea.withY(autoArea.getY() + laneIndex * 13.0f).withHeight(12.0f).reduced(4.0f, 0.0f)
                        : autoArea.withHeight(std::min(16.0f, autoArea.getHeight())).reduced(4.0f, 0.0f);
                if (laneActive && activeLane >= 0 && !previewLine)
                {
                    const auto textWidth = static_cast<float>(automation.parameterName.length()) * 6.5f;
                    const auto badge = labelArea.withWidth(std::min(96.0f, std::max(46.0f, textWidth + 18.0f)));
                    g.setColour(juce::Colour(0xee11161b));
                    g.fillRect(badge);
                    g.setColour(juce::Colour(0xffffbf7a));
                    g.drawRect(badge.reduced(0.5f), 1.0f);
                    g.setColour(juce::Colour(0xfff4e0bb));
                    g.drawText(automation.parameterName, badge.reduced(5.0f, 0.0f), juce::Justification::centredLeft, true);
                }
                else
                {
                    g.setColour(laneActive ? juce::Colour(0xffe7c5ff) : juce::Colour(0xffd0a4e8));
                    g.drawText(automation.parameterName, labelArea, juce::Justification::centredRight, true);
                }
            }
        };
        auto previewLaneIndex = static_cast<int>(clip.automations.size());
        auto displayLaneCount = static_cast<int>(clip.automations.size());
        for (int i = 0; i < static_cast<int>(clip.automations.size()); ++i)
        {
            const auto& automation = clip.automations[static_cast<size_t>(i)];
            if (automationTarget.isValid() && automation.target.track == automationTarget.track
                && automation.target.slot == automationTarget.slot && automation.target.parameter == automationTarget.parameter)
            {
                previewLaneIndex = i;
                break;
            }
        }
        if (automationDragging && clip.id == selected)
            displayLaneCount = std::max(displayLaneCount, previewLaneIndex + 1);
        if (activeLane >= 0 && juce::isPositiveAndBelow(activeLane, clip.automations.size()))
        {
            const auto& automation = clip.automations[static_cast<size_t>(activeLane)];
            drawAutomation(automation,
                           position.start + automation.startSeconds,
                           position.start + automation.endSeconds,
                           automation.startValue,
                           automation.endValue,
                           false,
                           activeLane,
                           displayLaneCount);
        }
        for (int i = 0; i < static_cast<int>(clip.automations.size()); ++i)
        {
            if (i == activeLane)
                continue;
            const auto& automation = clip.automations[static_cast<size_t>(i)];
            drawAutomation(automation,
                           position.start + automation.startSeconds,
                           position.start + automation.endSeconds,
                           automation.startValue,
                           automation.endValue,
                           false,
                           i,
                           displayLaneCount);
        }
        if (automationDragging && clip.id == selected)
        {
            Session::ClipAutomation previewAutomation;
            previewAutomation.active = true;
            previewAutomation.target = automationTarget;
            previewAutomation.startSeconds = 0.0;
            previewAutomation.endSeconds = 1.0;
            const auto parameters = session.deviceParameters(automationTarget.track, automationTarget.slot);
            if (juce::isPositiveAndBelow(automationTarget.parameter, parameters.size()))
            {
                previewAutomation.parameterName = parameters[static_cast<size_t>(automationTarget.parameter)].name;
                previewAutomation.minimum = parameters[static_cast<size_t>(automationTarget.parameter)].minimum;
                previewAutomation.maximum = parameters[static_cast<size_t>(automationTarget.parameter)].maximum;
            }
            drawAutomation(previewAutomation, automationStartTime, automationEndTime,
                           automationStartValue, automationEndValue, true,
                           previewLaneIndex,
                           displayLaneCount);
        }
        if (clip.waveform)
        {
            auto waveArea = visible.withTop(box.getY() + 26.0f).reduced(0, 5).getSmallestIntegerContainer();
            if (clip.waveform->thumbnail.getTotalLength() > 0.0)
            {
                const auto start = (position.offset + std::max(0.0, timeAt(visible.getX()) - position.start)) * clip.speed;
                const auto end = start + visible.getWidth() / lane(0).getWidth() * viewSpan * clip.speed;
                g.setColour(juce::Colour(0xff8cc5d2));
                clip.waveform->thumbnail.drawChannels(g, waveArea, start, end, 0.85f);
            }
            else
            {
                g.setColour(juce::Colour(0xffa1b1b9));
                g.drawText(clip.waveform->readable ? "Reading waveform..." : "Missing or unreadable audio",
                           waveArea.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            juce::Graphics::ScopedSaveState clipContentScope(g);
            g.reduceClipRegion(visible.getSmallestIntegerContainer());
            const auto noteArea = box.withTop(box.getY() + 28.0f).reduced(6.0f, 5.0f);
            g.setColour(juce::Colour(0x553f4837));
            for (int step = 1; step < Session::steps; ++step)
            {
                const auto x = xFor(position.start + step * (position.end - position.start) / Session::steps);
                if (x > noteArea.getX() && x < noteArea.getRight())
                    g.drawVerticalLine(static_cast<int>(x), noteArea.getY(), noteArea.getBottom());
            }
            auto lowPitch = Session::lowestNote;
            auto highPitch = Session::lowestNote + Session::pitches - 1;
            for (const auto& note : clip.midiNotes)
            {
                lowPitch = std::min(lowPitch, note.pitch);
                highPitch = std::max(highPitch, note.pitch);
            }
            for (const auto& note : clip.midiNotes)
            {
                const auto x1 = xFor(position.start + note.start - clip.position.start);
                const auto x2 = xFor(position.start + note.end - clip.position.start);
                const auto w = std::max(3.0f, x2 - x1);
                const auto pitchScale = static_cast<float>(note.pitch - lowPitch)
                    / static_cast<float>(std::max(1, highPitch - lowPitch));
                const auto h = std::max(4.0f, noteArea.getHeight() / Session::pitches - 1.0f);
                const auto y = noteArea.getBottom() - h - pitchScale * (noteArea.getHeight() - h);
                const juce::Rectangle<float> noteBox {x1, y, w, h};
                if (!noteBox.intersects(visible)) continue;
                g.setColour(juce::Colour(0xffc6d58c));
                g.fillRect(noteBox);
                g.setColour(juce::Colour(0xffe8f1bd));
                g.drawRect(noteBox.reduced(0.5f), 1.0f);
            }
            if (clip.midiNotes.empty())
            {
                g.setColour(juce::Colour(0xff9daa7e));
                g.drawText("Edit notes below", noteArea, juce::Justification::centredLeft, true);
            }
        }
    }
    if (!hasAudio && session.trackCount() > 1)
    {
        g.setColour(juce::Colour(0xff75828e));
        g.drawText("Drop audio here, or use Add audio", lane(1).reduced(16, 0), juce::Justification::centredLeft);
    }
    if (marqueeSelecting)
    {
        g.setColour(juce::Colour(0x285ab9d6));
        g.fillRect(marqueeBounds);
        g.setColour(juce::Colour(0xff5ab9d6));
        g.drawRect(marqueeBounds, 1.0f);
    }
    if (playhead >= headerWidth)
    {
        g.setColour(playheadColour);
        g.fillRect(playhead, rulerTop, 2.0f, getHeight() - rulerTop - 18.0f);
    }
}

}
