#include "DeviceEditorPanel.h"
#include "Theme.h"
#include "audio/AutoTuneDevice.h"
#include <algorithm>
#include <cmath>

// Rhino Tune's face, read left to right: what came in, what is being done to
// it, and what is being done with it.
//
// The parts that are not knobs are the parts a knob is the wrong shape for --
// a meter that has to move, a key that is either in the scale or not, a
// chooser with thirteen entries.  They are drawn here and hit-tested by
// rectangle rather than made into a dozen child components, because the rack
// destroys and rebuilds every panel whenever anything about a track changes.
namespace rhino
{
namespace
{
const juce::Colour accentInk {0xffb2739c};
const juce::Colour wellInk {0xff141a1f};
const juce::Colour frameInk {0xff36414a};
const juce::Colour brightText {0xffdfe6ea};
const juce::Colour dimText {0xff85929c};
const juce::Colour deadText {0xff4a555e};

// The two halves of the correction meter. Flat is warm and sharp is cold,
// which is the one colour convention a tuner can count on being read right.
const juce::Colour flatInk {0xffd8a25a};
const juce::Colour sharpInk {0xff6fb7d8};

// Which grid cell each parameter is drawn in. The order on the face is not
// the order the device declares them in: the device leads with the twelve the
// generic fallback panel should show, and this puts input gain back at the
// front, where the signal actually arrives.
constexpr int cellForParameter[] {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0};
constexpr int gridColumns = 7;
constexpr int tuneParameterCount = 13;

const int whitePitchClasses[] {0, 2, 4, 5, 7, 9, 11};
const int blackPitchClasses[] {1, 3, 6, 8, 10};
// Which white key each black key sits after, so the keyboard is drawn from
// the same numbers it is clicked with.
const int blackKeyGaps[] {1, 2, 4, 5, 6};

struct TuneLayout
{
    juce::Rectangle<int> title, live, natural, latency;
    juce::Rectangle<int> inputWell, inputCaption, note, cents, range[3], clarity;
    juce::Rectangle<int> centreWell, meterScale, meter, keyboard, root, scale, shiftText, shiftDown, shiftUp;
    juce::Rectangle<int> knob[tuneParameterCount];
};

TuneLayout tuneLayout(juce::Rectangle<int> panel)
{
    TuneLayout layout;
    const auto width = panel.getWidth();
    layout.latency = {width - 94, 3, 88, 18};
    layout.natural = {width - 138, 3, 40, 18};
    layout.live = {width - 182, 3, 40, 18};
    layout.title = {27, 1, std::max(40, width - 213), 21};

    const auto content = panel.withTrimmedTop(24).reduced(4);
    const auto left = content.getX();
    const auto top = content.getY();

    layout.inputWell = {left, top, 78, content.getHeight()};
    layout.inputCaption = {left, top + 3, 78, 12};
    layout.note = {left, top + 16, 78, 22};
    layout.cents = {left, top + 38, 78, 13};
    for (int i = 0; i < 3; ++i)
        layout.range[i] = {left + 5, top + 56 + i * 20, 68, 18};
    layout.clarity = {left + 5, content.getBottom() - 13, 68, 7};

    const auto centre = left + 86;
    layout.centreWell = {centre, top, 236, content.getHeight()};
    layout.meterScale = {centre + 4, top + 2, 228, 11};
    layout.meter = {centre + 4, top + 13, 228, 18};
    layout.keyboard = {centre + 4, top + 36, 228, 56};
    layout.root = {centre + 4, top + 97, 98, 21};
    layout.scale = {centre + 106, top + 97, 126, 21};
    layout.shiftText = {centre + 4, top + 121, 144, 21};
    layout.shiftDown = {centre + 150, top + 121, 40, 21};
    layout.shiftUp = {centre + 192, top + 121, 40, 21};

    const auto gridLeft = layout.centreWell.getRight() + 8;
    const auto cellWidth = std::max(30, (content.getRight() - gridLeft) / gridColumns);
    const auto rowHeight = (content.getHeight() - 2) / 2;
    for (int parameter = 0; parameter < tuneParameterCount; ++parameter)
    {
        const auto cell = cellForParameter[parameter];
        layout.knob[parameter] = {gridLeft + (cell % gridColumns) * cellWidth,
                                  top + (cell / gridColumns) * (rowHeight + 2),
                                  cellWidth, rowHeight};
    }
    return layout;
}

juce::Rectangle<float> whiteKeyBounds(juce::Rectangle<int> area, int index)
{
    const auto width = static_cast<float>(area.getWidth()) / 7.0f;
    return {static_cast<float>(area.getX()) + index * width, static_cast<float>(area.getY()),
            width, static_cast<float>(area.getHeight())};
}

juce::Rectangle<float> blackKeyBounds(juce::Rectangle<int> area, int index)
{
    const auto width = static_cast<float>(area.getWidth()) / 7.0f;
    const auto keyWidth = width * 0.62f;
    return {static_cast<float>(area.getX()) + blackKeyGaps[index] * width - keyWidth * 0.5f,
            static_cast<float>(area.getY()), keyWidth, static_cast<float>(area.getHeight()) * 0.62f};
}

// Black keys first: they are drawn on top, so they are clicked first too.
int pitchClassAt(juce::Rectangle<int> area, juce::Point<int> position)
{
    const auto point = position.toFloat();
    for (int i = 0; i < 5; ++i)
        if (blackKeyBounds(area, i).contains(point))
            return blackPitchClasses[i];
    for (int i = 0; i < 7; ++i)
        if (whiteKeyBounds(area, i).contains(point))
            return whitePitchClasses[i];
    return -1;
}

juce::String noteNameFor(float midiNote)
{
    const auto rounded = juce::roundToInt(midiNote);
    return juce::String(pitchClassName(rounded)) + juce::String(rounded / 12 - 1);
}

int rangeToIndex(PitchTracker::Range range)
{
    return range == PitchTracker::Range::High ? 0 : range == PitchTracker::Range::Mid ? 1 : 2;
}
}

void DeviceEditorPanel::layoutAutoTune()
{
    visualArea = {};
    const auto layout = tuneLayout(getLocalBounds());
    title.setBounds(layout.title);

    const auto count = visibleParameterCount();
    for (int i = 0; i < count; ++i)
    {
        const auto cell = layout.knob[i];
        parameterLabels[i]->setFont(uiFont(10.0f));
        parameterValues[i]->setFont(uiFont(9.5f));
        parameterLabels[i]->setBounds(cell.getX() + 1, cell.getY(), cell.getWidth() - 2, 13);
        const auto knobSize = std::min({40, std::max(24, cell.getWidth() - 12), std::max(24, cell.getHeight() - 30)});
        parameterSliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize).translated(0, 1));
        parameterValues[i]->setBounds(cell.getX() + 1, cell.getBottom() - 14, cell.getWidth() - 2, 13);
        parameterAutomation[i]->setBounds(cell.getRight() - 17, cell.getY() + 13, 16, 14);
    }
}

void DeviceEditorPanel::paintAutoTune(juce::Graphics& g)
{
    auto* tune = dynamic_cast<AutoTuneDevice*>(session.devicePlugin(track, pluginSlot));
    if (tune == nullptr)
        return;
    const auto layout = tuneLayout(getLocalBounds());
    const auto readout = tune->readout();
    const auto mask = tune->scaleMask();

    const auto well = [&g] (juce::Rectangle<int> bounds)
    {
        g.setColour(wellInk);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
    };
    well(layout.inputWell);
    well(layout.centreWell);

    const auto switchBox = [&g] (juce::Rectangle<int> bounds, const juce::String& text, bool on)
    {
        g.setColour(on ? accentInk.withAlpha(0.88f) : juce::Colour(0xff232b32));
        g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
        g.setColour(on ? accentInk.brighter(0.4f) : frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(on ? juce::Colour(0xff15191d) : dimText);
        g.setFont(uiFontBold(8.5f));
        drawSnappedText(g, text, bounds, juce::Justification::centred);
    };
    switchBox(layout.live, "LIVE", tune->liveMode());
    switchBox(layout.natural, "NAT", tune->naturalVibrato());
    g.setColour(dimText);
    g.setFont(uiFont(9.0f));
    drawSnappedText(g, juce::String(readout.latencyMs, 1) + " ms latency", layout.latency,
                    juce::Justification::right);

    // ---- what came in ------------------------------------------------------
    g.setColour(dimText);
    g.setFont(uiFont(8.5f));
    drawSnappedText(g, "INPUT", layout.inputCaption, juce::Justification::centred);
    if (readout.voiced)
    {
        const auto deviation = juce::roundToInt((readout.detectedNote - std::round(readout.detectedNote)) * 100.0f);
        g.setColour(brightText);
        g.setFont(uiFontBold(15.0f));
        drawSnappedText(g, noteNameFor(readout.detectedNote), layout.note, juce::Justification::centred);
        g.setColour(deviation == 0 ? dimText : deviation < 0 ? flatInk : sharpInk);
        g.setFont(uiFont(9.5f));
        drawSnappedText(g, (deviation > 0 ? "+" : "") + juce::String(deviation) + " ct", layout.cents,
                        juce::Justification::centred);
    }
    else
    {
        g.setColour(deadText);
        g.setFont(uiFontBold(15.0f));
        drawSnappedText(g, "--", layout.note, juce::Justification::centred);
        g.setFont(uiFont(9.5f));
        drawSnappedText(g, "no pitch", layout.cents, juce::Justification::centred);
    }

    static const char* const rangeNames[] {"High", "Mid", "Bass"};
    const auto selectedRange = rangeToIndex(tune->trackingRange());
    for (int i = 0; i < 3; ++i)
    {
        const auto bounds = layout.range[i];
        const auto on = i == selectedRange;
        g.setColour(on ? juce::Colour(0xff2c343b) : juce::Colour(0xff1a2126));
        g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
        g.setColour(on ? accentInk : frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
        // Ableton's idea, and a good one: the lamp says which band the voice
        // is actually in, so a range set wrong announces itself.
        const auto lit = readout.voiced && readout.band == i;
        g.setColour(lit ? flatInk : juce::Colour(0xff39434b));
        g.fillEllipse(static_cast<float>(bounds.getX() + 6), static_cast<float>(bounds.getCentreY() - 3), 6.0f, 6.0f);
        g.setColour(on ? brightText : dimText);
        g.setFont(uiFont(10.0f));
        drawSnappedText(g, rangeNames[i], bounds.withTrimmedLeft(18), juce::Justification::centredLeft);
    }

    g.setColour(juce::Colour(0xff20272d));
    g.fillRect(layout.clarity);
    g.setColour(readout.voiced ? accentInk : juce::Colour(0xff404b53));
    g.fillRect(layout.clarity.withWidth(juce::roundToInt(
        static_cast<float>(layout.clarity.getWidth()) * std::clamp(readout.clarity, 0.0f, 1.0f))));

    // ---- how far it is being moved ----------------------------------------
    g.setColour(dimText);
    g.setFont(uiFont(8.0f));
    drawSnappedText(g, "-100 ct", layout.meterScale, juce::Justification::centredLeft);
    drawSnappedText(g, "0", layout.meterScale, juce::Justification::centred);
    drawSnappedText(g, "+100 ct", layout.meterScale, juce::Justification::centredRight);

    constexpr int segments = 45;   // odd, so one of them is the centre
    const auto segmentWidth = static_cast<float>(layout.meter.getWidth()) / segments;
    const auto correction = std::clamp(readout.correctionCents / 100.0f, -1.0f, 1.0f);
    const auto reach = juce::roundToInt(std::abs(correction) * (segments / 2));
    for (int segment = 0; segment < segments; ++segment)
    {
        const auto offset = segment - segments / 2;
        const auto colour = offset == 0 ? brightText : offset < 0 ? flatInk : sharpInk;
        const auto on = offset == 0
            || (readout.voiced && std::abs(offset) <= reach
                && ((correction < 0.0f) == (offset < 0)));
        g.setColour(on ? colour : colour.withAlpha(0.13f));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(layout.meter.getX()) + segment * segmentWidth + 1.0f,
                                          static_cast<float>(layout.meter.getY()) + 2.0f,
                                          segmentWidth - 2.0f,
                                          static_cast<float>(layout.meter.getHeight()) - 4.0f));
    }

    // ---- which notes it is allowed to land on ------------------------------
    const auto targetClass = readout.voiced ? ((juce::roundToInt(readout.targetNote) % 12) + 12) % 12 : -1;
    // A black key lit by the same accent as a white one is not a black key.
    // Measured off a render, 0.35 of a darken put the two within three counts
    // of each other and the keyboard read as a solid block of colour.
    const auto drawKey = [&g, targetClass] (juce::Rectangle<float> bounds, int pitchClass, bool allowed, bool black)
    {
        g.setColour(allowed ? (black ? accentInk.darker(0.95f) : accentInk.withAlpha(0.85f))
                            : (black ? juce::Colour(0xff0e1317) : juce::Colour(0xff283037)));
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(juce::Colour(0xff0f1418));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);
        g.setColour(black ? (allowed ? juce::Colour(0xfff0e3ea) : juce::Colour(0xff5c676f))
                          : (allowed ? juce::Colour(0xff1a1216) : juce::Colour(0xff69757e)));
        g.setFont(uiFont(8.5f));
        drawSnappedText(g, pitchClassName(pitchClass),
                        bounds.toNearestInt().removeFromBottom(13), juce::Justification::centred);
        if (pitchClass == targetClass)
        {
            g.setColour(juce::Colour(0xfff2f7f4));
            g.fillEllipse(bounds.getCentreX() - 2.5f, bounds.getY() + 4.0f, 5.0f, 5.0f);
        }
    };
    for (int i = 0; i < 7; ++i)
        drawKey(whiteKeyBounds(layout.keyboard, i), whitePitchClasses[i],
                mask[static_cast<size_t>(whitePitchClasses[i])], false);
    for (int i = 0; i < 5; ++i)
        drawKey(blackKeyBounds(layout.keyboard, i), blackPitchClasses[i],
                mask[static_cast<size_t>(blackPitchClasses[i])], true);

    const auto chooser = [&g] (juce::Rectangle<int> bounds, const juce::String& caption, const juce::String& value)
    {
        g.setColour(juce::Colour(0xff1f262c));
        g.fillRoundedRectangle(bounds.toFloat(), 2.5f);
        g.setColour(frameInk);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(dimText);
        g.setFont(uiFont(8.5f));
        drawSnappedText(g, caption, bounds.withTrimmedLeft(7), juce::Justification::centredLeft);
        g.setColour(brightText);
        g.setFont(uiFontBold(10.0f));
        drawSnappedText(g, value, bounds.withTrimmedRight(7), juce::Justification::centredRight);
    };
    chooser(layout.root, "ROOT", pitchClassName(tune->scaleRoot()));
    // A key switched off by hand no longer spells the scale it was built
    // from, and saying so is the only honest thing the chooser can show.
    chooser(layout.scale, "SCALE",
            tune->maskMatchesNamedScale() ? musicalScaleName(tune->scale()) : juce::String("Custom"));
    const auto degrees = tune->scaleDegreeShift();
    chooser(layout.shiftText, "SHIFT", (degrees > 0 ? "+" : "") + juce::String(degrees) + " sd");
    for (const auto& step : {std::pair<juce::Rectangle<int>, const char*> {layout.shiftDown, "-"},
                             std::pair<juce::Rectangle<int>, const char*> {layout.shiftUp, "+"}})
    {
        g.setColour(juce::Colour(0xff232b32));
        g.fillRoundedRectangle(step.first.toFloat(), 2.5f);
        g.setColour(frameInk);
        g.drawRoundedRectangle(step.first.toFloat().reduced(0.5f), 2.5f, 1.0f);
        g.setColour(brightText);
        g.setFont(uiFontBold(12.0f));
        drawSnappedText(g, step.second, step.first, juce::Justification::centred);
    }
}

bool DeviceEditorPanel::handleAutoTuneClick(const juce::MouseEvent& event)
{
    auto* tune = dynamic_cast<AutoTuneDevice*>(session.devicePlugin(track, pluginSlot));
    if (tune == nullptr)
        return false;
    const auto layout = tuneLayout(getLocalBounds());
    const auto position = event.getEventRelativeTo(this).getPosition();

    // Every one of these edits the device's own state tree, so undo comes for
    // free; what the tree cannot tell Session is that the project is now
    // different from the file on disk.
    const auto changed = [this] (const juce::String& message)
    {
        session.markModified();
        repaint();
        if (status) status(message);
    };
    // The range and Live Mode both move the reported latency, and the graph
    // only reads that when it is built. Without this the delay compensation
    // stays on the old figure until the next time playback starts, which puts
    // the corrected voice out of time with everything else.
    const auto rebuildGraph = [this]
    {
        if (session.edit != nullptr && session.edit->getTransport().isPlaying())
            session.edit->restartPlayback();
    };

    if (layout.live.contains(position))
    {
        tune->setLiveMode(!tune->liveMode());
        rebuildGraph();
        changed(tune->liveMode() ? "Rhino Tune: live mode, lower latency and rougher onsets"
                                 : "Rhino Tune: studio mode");
        return true;
    }
    if (layout.natural.contains(position))
    {
        tune->setNaturalVibrato(!tune->naturalVibrato());
        changed(tune->naturalVibrato() ? "Rhino Tune: vibrato wanders" : "Rhino Tune: vibrato is steady");
        return true;
    }
    for (int i = 0; i < 3; ++i)
        if (layout.range[i].contains(position))
        {
            static const PitchTracker::Range ranges[] {PitchTracker::Range::High, PitchTracker::Range::Mid,
                                                       PitchTracker::Range::Bass};
            static const char* const names[] {"high", "mid", "bass"};
            tune->setTrackingRange(ranges[i]);
            rebuildGraph();
            changed(juce::String("Rhino Tune: tracking the ") + names[i] + " range");
            return true;
        }
    if (layout.keyboard.contains(position))
    {
        const auto pitchClass = pitchClassAt(layout.keyboard, position);
        if (pitchClass < 0)
            return false;
        auto mask = tune->scaleMask();
        mask[static_cast<size_t>(pitchClass)] = !mask[static_cast<size_t>(pitchClass)];
        tune->setScaleMask(mask);
        changed(juce::String(pitchClassName(pitchClass))
                + (mask[static_cast<size_t>(pitchClass)] ? " is in the scale" : " is out of the scale"));
        return true;
    }
    if (layout.shiftDown.contains(position) || layout.shiftUp.contains(position))
    {
        tune->setScaleDegreeShift(tune->scaleDegreeShift() + (layout.shiftUp.contains(position) ? 1 : -1));
        changed("Rhino Tune: shifted " + juce::String(tune->scaleDegreeShift()) + " scale degrees");
        return true;
    }

    // The menus are asynchronous, so neither the panel nor the device can be
    // assumed to still be there when one closes. The panel is guarded by a
    // SafePointer and the device is looked up again through it rather than
    // captured: a device deleted from the rack while its menu is open would
    // otherwise be written to after it was freed.
    const auto safe = juce::Component::SafePointer<DeviceEditorPanel>(this);
    if (layout.root.contains(position))
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("ROOT");
        for (int i = 0; i < 12; ++i)
            menu.addItem(i + 1, pitchClassName(i), true, i == tune->scaleRoot());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                               .withTargetScreenArea(localAreaToGlobal(layout.root)),
            [safe] (int result)
            {
                if (safe == nullptr || result == 0) return;
                auto* device = dynamic_cast<AutoTuneDevice*>(
                    safe->session.devicePlugin(safe->track, safe->pluginSlot));
                if (device == nullptr) return;
                device->applyScale(result - 1, device->scale());
                safe->session.markModified();
                safe->repaint();
                if (safe->status) safe->status(juce::String("Rhino Tune: root is ") + pitchClassName(result - 1));
            });
        return true;
    }
    if (layout.scale.contains(position))
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("SCALE");
        for (int i = 0; i < musicalScaleCount; ++i)
            menu.addItem(i + 1, musicalScaleName(static_cast<MusicalScale>(i)), true,
                         tune->maskMatchesNamedScale() && i == static_cast<int>(tune->scale()));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                               .withTargetScreenArea(localAreaToGlobal(layout.scale)),
            [safe] (int result)
            {
                if (safe == nullptr || result == 0) return;
                auto* device = dynamic_cast<AutoTuneDevice*>(
                    safe->session.devicePlugin(safe->track, safe->pluginSlot));
                if (device == nullptr) return;
                const auto chosen = static_cast<MusicalScale>(result - 1);
                device->applyScale(device->scaleRoot(), chosen);
                safe->session.markModified();
                safe->repaint();
                if (safe->status) safe->status(juce::String("Rhino Tune: ") + musicalScaleName(chosen));
            });
        return true;
    }
    return false;
}

void DeviceEditorPanel::timerCallback()
{
    if (face == Face::Eq)
    {
        tickEqSpectrum();
        return;
    }
    if (face != Face::AutoTune)
    {
        stopTimer();
        return;
    }
    auto* tune = dynamic_cast<AutoTuneDevice*>(session.devicePlugin(track, pluginSlot));
    if (tune == nullptr)
        return;
    const auto readout = tune->readout();
    const auto note = readout.voiced ? readout.detectedNote : 0.0f;
    const auto band = readout.voiced ? readout.band : -1;
    // A device with nothing going through it asks for no frames at all, which
    // matters because the rack can hold several of these at once.
    if (std::abs(readout.correctionCents - lastDrawnCents) < 0.5f
        && std::abs(note - lastDrawnNote) < 0.01f && band == lastDrawnBand)
        return;

    const auto keyMoved = std::round(note) != std::round(lastDrawnNote);
    lastDrawnCents = readout.correctionCents;
    lastDrawnNote = note;
    lastDrawnBand = band;

    const auto layout = tuneLayout(getLocalBounds());
    repaint(layout.note.getUnion(layout.cents));
    repaint(layout.range[0].getUnion(layout.range[2]));
    repaint(layout.clarity);
    repaint(layout.meter);
    // The keyboard only changes when the target does, and the target can only
    // change when the detected note crosses a semitone.
    if (keyMoved)
        repaint(layout.keyboard);
}
}
