#include "AudioClipPanel.h"
#include "ui/shared/Theme.h"
#include <algorithm>
#include <cmath>

// The audio clip editor's third part: the clip's own envelopes, Live's clip
// envelopes, drawn over its waveform. One shows at a time, chosen from the
// Envelope button in the header or from a right-click on a knob, and it is
// edited where it is drawn: a press puts a node down or takes the one under
// it, a drag carries it - along in time, and up and down by how far the
// pointer travels, Shift for a tenth of that - and a double-click takes it
// away. Pitch lands on whole semitones unless Shift is held, as Live's
// Transposition envelope does.
//
// Every value is the model's (Session::setClipEnvelope); this file only
// draws and gestures. The line drawn is the line the engine plays: straight
// between nodes, level before the first and after the last, and the rules
// for both live in dsp/ClipEnvelope.h.
//
// AudioClipPanel is defined across this file, AudioClipPanel.cpp and
// AudioClipWarp.cpp. Nothing outside the class changed for the split.

namespace rhino
{
namespace
{
// The red track automation is drawn in, so a line that moves a knob reads the
// same on a clip as on a track.
const juce::Colour envelopeColour {0xffe2564f};
const juce::Colour restingEnvelope {0xffb9524d};

// The menu's item ids: chooseEnvelopeMenuItem reads them.
constexpr int showFirst = 1, hideItem = 4, clearFirst = 11;

void drawDashedLine(juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    const float dashes[] {5.0f, 4.0f};
    g.drawDashedLine({x1, y1, x2, y2}, dashes, 2, 1.4f);
}
}

void AudioClipPanel::configureEnvelopeControls()
{
    envelopeButton.setTooltip("Which of this clip's envelopes to draw over its waveform - "
                              "or right-click Gain, Pan or Pitch");
    envelopeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a30));
    envelopeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc2ccd4));
    envelopeButton.onClick = [this] { showEnvelopeMenu(); };
    addAndMakeVisible(envelopeButton);
    for (int control = 0; control < controlCount; ++control)
        if (envelopeOfControl(control).has_value())
            sliders[static_cast<size_t>(control)].addMouseListener(&knobMenu, false);
}

std::optional<Session::ClipEnvelope> AudioClipPanel::envelopeOfControl(int control)
{
    switch (control)
    {
        case gainControl:  return Session::ClipEnvelope::gain;
        case panControl:   return Session::ClipEnvelope::pan;
        case pitchControl: return Session::ClipEnvelope::pitch;
        default: break;
    }
    // A fade is a length, not a level, so it has nothing to move over time.
    return std::nullopt;
}

void AudioClipPanel::syncEnvelope()
{
    envelopesInUse = session.clipEnvelopesInUse(clip);
    envelopePoints = shownEnvelope.has_value() && mix.valid ? session.clipEnvelope(clip, *shownEnvelope)
                                                            : std::vector<Session::AutomationPoint>();
    envelopeButton.setEnabled(mix.valid);
    envelopeButton.setButtonText(juce::String("Envelope: ")
                                 + (shownEnvelope.has_value() ? clipEnvelopeName(*shownEnvelope) : "None"));
    // A knob whose envelope has points says so under its name, as Live's
    // envelope chooser lights its LED.
    for (int control = 0; control < controlCount; ++control)
        if (const auto kind = envelopeOfControl(control))
        {
            const auto inUse = mix.valid && envelopesInUse[static_cast<size_t>(*kind)];
            auto& name = names[static_cast<size_t>(control)];
            name.setText(juce::String(clipEnvelopeName(*kind)) + (inUse ? " \xe2\x80\xa2" : ""),
                         juce::dontSendNotification);
            name.setColour(juce::Label::textColourId, inUse ? envelopeColour.brighter(0.35f) : juce::Colour(0xffdfe6ea));
        }
}

void AudioClipPanel::layoutEnvelopeControls(juce::Rectangle<int>& header)
{
    envelopeButton.setBounds(header.removeFromRight(std::min(envelopeButtonWidth, header.getWidth())).reduced(0, 2));
    header.removeFromRight(std::min(6, header.getWidth()));
}

void AudioClipPanel::showEnvelope(std::optional<Session::ClipEnvelope> kind)
{
    shownEnvelope = kind;
    syncEnvelope();
    repaint();
}

void AudioClipPanel::showEnvelopeMenu()
{
    if (!mix.valid) return;
    juce::PopupMenu menu;
    menu.addSectionHeader("CLIP ENVELOPE");
    menu.addItem(juce::PopupMenu::Item("None").setID(hideItem).setTicked(!shownEnvelope.has_value()));
    for (int kind = 0; kind < Session::clipEnvelopeCount; ++kind)
    {
        const auto envelope = static_cast<Session::ClipEnvelope>(kind);
        juce::PopupMenu::Item item {clipEnvelopeName(envelope)};
        item.itemID = showFirst + kind;
        item.isTicked = shownEnvelope == envelope;
        // Drawn on this clip: the dot the knob's name wears too.
        if (envelopesInUse[static_cast<size_t>(kind)])
            item.shortcutKeyDescription = "\xe2\x80\xa2";
        menu.addItem(std::move(item));
    }
    if (shownEnvelope.has_value())
    {
        menu.addSeparator();
        menu.addItem(juce::PopupMenu::Item("Clear " + juce::String(clipEnvelopeName(*shownEnvelope)) + " envelope")
                         .setID(clearFirst + static_cast<int>(*shownEnvelope))
                         .setEnabled(envelopesInUse[static_cast<size_t>(*shownEnvelope)]));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(envelopeButton),
        [safe = juce::Component::SafePointer<AudioClipPanel>(this)](int result)
        {
            if (safe != nullptr && result > 0)
                safe->chooseEnvelopeMenuItem(result);
        });
}

void AudioClipPanel::showControlMenu(int control)
{
    const auto kind = envelopeOfControl(control);
    if (!kind.has_value() || !mix.valid) return;
    const auto name = juce::String(clipEnvelopeName(*kind));
    juce::PopupMenu menu;
    menu.addItem(juce::PopupMenu::Item("Show " + name + " envelope")
                     .setID(showFirst + static_cast<int>(*kind))
                     .setTicked(shownEnvelope == kind));
    menu.addItem(juce::PopupMenu::Item("Clear " + name + " envelope")
                     .setID(clearFirst + static_cast<int>(*kind))
                     .setEnabled(envelopesInUse[static_cast<size_t>(*kind)]));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(sliders[static_cast<size_t>(control)]),
        [safe = juce::Component::SafePointer<AudioClipPanel>(this)](int result)
        {
            if (safe != nullptr && result > 0)
                safe->chooseEnvelopeMenuItem(result);
        });
}

void AudioClipPanel::chooseEnvelopeMenuItem(int item)
{
    if (item == hideItem)
    {
        showEnvelope(std::nullopt);
        return;
    }
    if (item >= showFirst && item < showFirst + Session::clipEnvelopeCount)
    {
        showEnvelope(static_cast<Session::ClipEnvelope>(item - showFirst));
        return;
    }
    if (item >= clearFirst && item < clearFirst + Session::clipEnvelopeCount)
    {
        const auto kind = static_cast<Session::ClipEnvelope>(item - clearFirst);
        report(session.clearClipEnvelope(clip, kind));
        if (status && !session.clipEnvelopesInUse(clip)[static_cast<size_t>(kind)])
            status("Cleared the " + juce::String(clipEnvelopeName(kind)) + " envelope");
    }
}

void AudioClipPanel::KnobMenu::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu()) return;
    for (int control = 0; control < controlCount; ++control)
        if (event.eventComponent == &panel.sliders[static_cast<size_t>(control)])
            panel.showControlMenu(control);
}

juce::Rectangle<float> AudioClipPanel::envelopeArea() const
{
    if (waveArea.isEmpty() || !mix.valid || !(mix.lengthSeconds() > 0.0))
        return {};
    auto area = waveArea.reduced(4, 6).toFloat();
    if (warpMarkersVisible())
        area = area.withTrimmedTop(static_cast<float>(markerBar.getHeight() + 2));
    return area;
}

bool AudioClipPanel::envelopeShown() const
{
    return shownEnvelope.has_value() && !envelopeArea().isEmpty();
}

// Without markers the waveform is the span the clip plays, end to end; with
// them it is the whole file in played time, and the clip's span is the part
// of it AudioClipWarp.cpp leaves undimmed.
float AudioClipPanel::xForContentTime(double contentSeconds) const
{
    if (warpMarkersVisible())
        return xForWarpTime(contentSeconds * std::max(0.0001, mix.speedRatio));
    const auto area = envelopeArea();
    const auto length = mix.lengthSeconds();
    if (area.isEmpty() || !(length > 0.0))
        return area.getX();
    return area.getX() + static_cast<float>((contentSeconds - mix.offsetSeconds) / length) * area.getWidth();
}

double AudioClipPanel::contentTimeAtX(float x) const
{
    if (warpMarkersVisible())
        return warpTimeAtX(x) / std::max(0.0001, mix.speedRatio);
    const auto area = envelopeArea();
    if (area.isEmpty() || area.getWidth() <= 0.0f)
        return mix.offsetSeconds;
    return mix.offsetSeconds + (x - area.getX()) / area.getWidth() * mix.lengthSeconds();
}

float AudioClipPanel::yForEnvelopeValue(float value) const
{
    const auto area = envelopeArea();
    if (!shownEnvelope.has_value() || area.isEmpty())
        return area.getCentreY();
    const auto range = clipEnvelopeRange(*shownEnvelope);
    const auto amount = (clampClipEnvelopeValue(*shownEnvelope, value) - range.minimum) / (range.maximum - range.minimum);
    return area.getBottom() - amount * area.getHeight();
}

float AudioClipPanel::envelopeValueAtY(float y) const
{
    const auto area = envelopeArea();
    if (!shownEnvelope.has_value() || area.isEmpty() || area.getHeight() <= 0.0f)
        return 0.0f;
    const auto range = clipEnvelopeRange(*shownEnvelope);
    const auto amount = (area.getBottom() - y) / area.getHeight();
    return clampClipEnvelopeValue(*shownEnvelope, range.minimum + amount * (range.maximum - range.minimum));
}

int AudioClipPanel::envelopeNodeAt(juce::Point<float> point) const
{
    if (!envelopeShown())
        return -1;
    const auto from = mix.offsetSeconds, to = mix.offsetSeconds + mix.lengthSeconds();
    auto best = -1;
    auto bestDistance = envelopeGrabPixels;
    for (int i = 0; i < static_cast<int>(envelopePoints.size()); ++i)
    {
        const auto& node = envelopePoints[static_cast<size_t>(i)];
        if (node.timeSeconds < from || node.timeSeconds > to)
            continue;
        const auto distance = point.getDistanceFrom({xForContentTime(node.timeSeconds), yForEnvelopeValue(node.value)});
        // The later of two nodes at one spot: the one a drag would pull a
        // step's second side out from.
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

juce::String AudioClipPanel::envelopeValueText(float value) const
{
    if (!shownEnvelope.has_value())
        return {};
    switch (*shownEnvelope)
    {
        case Session::ClipEnvelope::gain:
        {
            const auto decibels = clipEnvelopeGainDecibels(value);
            return std::isfinite(decibels) ? juce::String(decibels, 1) + " dB" : juce::String("-inf dB");
        }
        case Session::ClipEnvelope::pan:
            if (std::abs(value) < 0.005f)
                return "C";
            return (value < 0.0f ? "L" : "R") + juce::String(std::abs(value) * 100.0f, 0);
        case Session::ClipEnvelope::pitch:
        {
            const auto whole = std::abs(value - std::round(value)) < 0.005f;
            return (value > 0.0f ? "+" : "") + juce::String(value, whole ? 0 : 2) + " st";
        }
    }
    return {};
}

void AudioClipPanel::paintEnvelope(juce::Graphics& g)
{
    if (!envelopeShown())
        return;
    const auto kind = *shownEnvelope;
    const auto range = clipEnvelopeRange(kind);
    const auto from = mix.offsetSeconds, to = mix.offsetSeconds + mix.lengthSeconds();
    const auto left = xForContentTime(from), right = xForContentTime(to);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(waveArea);

    // Where the envelope leaves the knob alone: the top for Gain, the middle
    // for Pan and Pitch.
    const auto neutralY = yForEnvelopeValue(range.neutral);
    g.setColour(restingEnvelope.withAlpha(0.22f));
    g.drawHorizontalLine(juce::roundToInt(neutralY), left, right);

    if (envelopePoints.empty())
    {
        // Never drawn: a dotted line where the knob is left alone, which says
        // "this is free" rather than "this is its curve".
        g.setColour(restingEnvelope.withAlpha(0.85f));
        drawDashedLine(g, left, neutralY, right, neutralY);
        return;
    }

    // The line the engine plays across the span the clip plays.
    juce::Path line;
    line.startNewSubPath(left, yForEnvelopeValue(clipEnvelopeValueAt(envelopePoints, from, range.neutral)));
    for (const auto& node : envelopePoints)
        if (node.timeSeconds > from && node.timeSeconds < to)
            line.lineTo(xForContentTime(node.timeSeconds), yForEnvelopeValue(node.value));
    line.lineTo(right, yForEnvelopeValue(clipEnvelopeValueBefore(envelopePoints, to, range.neutral)));
    g.setColour(envelopeColour);
    g.strokePath(line, juce::PathStrokeType(1.6f));

    for (int i = 0; i < static_cast<int>(envelopePoints.size()); ++i)
    {
        const auto& node = envelopePoints[static_cast<size_t>(i)];
        if (node.timeSeconds < from || node.timeSeconds > to)
            continue;
        const auto held = i == draggingNode;
        const auto box = juce::Rectangle<float>(held ? 9.0f : 7.0f, held ? 9.0f : 7.0f)
                             .withCentre({xForContentTime(node.timeSeconds), yForEnvelopeValue(node.value)});
        g.setColour(held ? envelopeColour : juce::Colour(0xff13181c));
        g.fillEllipse(box);
        g.setColour(held ? envelopeColour.brighter(0.5f) : envelopeColour);
        g.drawEllipse(box, 1.6f);
    }

    // The reading of the node being carried, beside it rather than under the
    // pointer, since a fine drag leaves the pointer some way off.
    if (juce::isPositiveAndBelow(draggingNode, static_cast<int>(envelopePoints.size())))
    {
        const auto& node = envelopePoints[static_cast<size_t>(draggingNode)];
        const juce::Point<float> at {xForContentTime(node.timeSeconds), yForEnvelopeValue(node.value)};
        const auto text = envelopeValueText(node.value);
        const auto font = uiFont(10.5f);
        const auto width = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text)) + 10.0f;
        auto box = juce::Rectangle<float>(width, 16.0f).withPosition(at.x + 8.0f, at.y - 20.0f);
        if (box.getRight() > static_cast<float>(waveArea.getRight()) - 2.0f)
            box.setX(at.x - 8.0f - width);
        if (box.getY() < static_cast<float>(waveArea.getY()) + 2.0f)
            box.setY(at.y + 6.0f);
        g.setColour(palette::appBackground.withAlpha(0.9f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(palette::text);
        g.setFont(font);
        drawSnappedText(g, text, box.toNearestInt(), juce::Justification::centred);
    }
}

bool AudioClipPanel::beginEnvelopeDrag(const juce::MouseEvent& event)
{
    if (!envelopeShown() || !event.mods.isLeftButtonDown())
        return false;
    const auto kind = *shownEnvelope;
    const auto position = event.position;
    auto node = envelopeNodeAt(position);
    if (node < 0 && !envelopeArea().contains(position))
        return false;
    auto points = envelopePoints;
    const auto added = node < 0;
    if (added)
    {
        // Put down where the press landed, or on the line when it landed on
        // the line, so clicking a ramp to bend it leaves it where it was.
        const auto from = mix.offsetSeconds, to = mix.offsetSeconds + mix.lengthSeconds();
        const auto time = juce::jlimit(from, to, contentTimeAtX(position.x));
        const auto onLine = clipEnvelopeValueAt(points, time, clipEnvelopeRange(kind).neutral);
        const auto value = std::abs(yForEnvelopeValue(onLine) - position.y) <= envelopeLineSnapPixels
            ? onLine : snapClipEnvelopeValue(kind, envelopeValueAtY(position.y), event.mods.isShiftDown());
        const auto at = std::upper_bound(points.begin(), points.end(), time,
                                         [](double t, const auto& point) { return t < point.timeSeconds; });
        node = static_cast<int>(std::distance(points.begin(), at));
        points.insert(at, {time, value});
    }
    session.beginAudioClipGesture(juce::String(clipEnvelopeName(kind)) + " envelope");
    envelopeGestureOpen = true;
    draggingNode = node;
    envelopeCarried = points[static_cast<size_t>(node)].value;
    envelopeLastY = position.y;
    // A press on a node that then lets go has changed nothing, and records
    // nothing; one that put a node down is already an edit.
    if (added)
        applyEnvelope(std::move(points));
    repaint(waveArea);
    return true;
}

void AudioClipPanel::dragEnvelope(const juce::MouseEvent& event)
{
    if (!shownEnvelope.has_value() || !juce::isPositiveAndBelow(draggingNode, static_cast<int>(envelopePoints.size())))
        return;
    const auto kind = *shownEnvelope;
    const auto range = clipEnvelopeRange(kind);
    const auto fine = event.mods.isShiftDown();
    // Up and down by travel, not position, carried unsnapped so a node pushed
    // past an end comes back as soon as the pointer turns.
    const auto travel = std::max(envelopeDragTravel, envelopeArea().getHeight());
    auto step = (envelopeLastY - event.position.y) / travel * (range.maximum - range.minimum);
    if (fine)
        step /= envelopeFineDivisor;
    envelopeLastY = event.position.y;
    envelopeCarried = juce::jlimit(range.minimum, range.maximum, envelopeCarried + step);

    // Along in time where the pointer is, held between its neighbours so the
    // order never changes, and inside the span the clip plays.
    auto points = envelopePoints;
    const auto index = static_cast<size_t>(draggingNode);
    auto lower = std::max(mix.offsetSeconds, index > 0 ? points[index - 1].timeSeconds : mix.offsetSeconds);
    auto upper = mix.offsetSeconds + mix.lengthSeconds();
    if (index + 1 < points.size())
        upper = std::min(upper, points[index + 1].timeSeconds);
    upper = std::max(lower, upper);
    const Session::AutomationPoint moved {juce::jlimit(lower, upper, contentTimeAtX(event.position.x)),
                                          snapClipEnvelopeValue(kind, envelopeCarried, fine)};
    if (moved.timeSeconds == points[index].timeSeconds && moved.value == points[index].value)
        return;
    points[index] = moved;
    applyEnvelope(std::move(points));
    repaint(waveArea);
}

void AudioClipPanel::endEnvelopeDrag()
{
    draggingNode = -1;
    if (envelopeGestureOpen)
    {
        envelopeGestureOpen = false;
        session.endAudioClipGesture();
    }
    repaint(waveArea);
}

bool AudioClipPanel::removeEnvelopeNodeAt(juce::Point<float> point)
{
    const auto node = envelopeNodeAt(point);
    if (node < 0)
        return false;
    auto points = envelopePoints;
    points.erase(points.begin() + node);
    applyEnvelope(std::move(points));
    return true;
}

void AudioClipPanel::applyEnvelope(std::vector<Session::AutomationPoint> points)
{
    if (!shownEnvelope.has_value())
        return;
    envelopePoints = points;
    const auto result = session.setClipEnvelope(clip, *shownEnvelope, std::move(points));
    if (result.failed())
    {
        report(result);
        envelopePoints = session.clipEnvelope(clip, *shownEnvelope);
    }
}
}
