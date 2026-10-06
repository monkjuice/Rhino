#include "DeviceEditorPanel.h"
#include "ContentLibrary.h"
#include "DrumKitFile.h"
#include "Theme.h"
#include "instruments/DrumRackDevice.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// The Drum Rack's face: sixteen pads in four rows of four, the lowest note at
// the bottom left as on a pad controller and in Live, and beside them the
// selected pad -- what it holds, a picture of the sound it makes, and its six
// knobs.
//
// The pads are the rack's whole surface for the hand. A click picks a pad and
// its three buttons mute, play and solo it; a right-click offers everything
// else that can happen to one; a sample or a drum preset dragged from the
// browser lands on the pad under the pointer (DeviceRack hands the drop over).
// Nothing here is a child component except the six knobs, which stand for
// whichever pad is selected: the rack rebuilds its panels whenever the chain
// changes, and sixteen pads of buttons would be a lot to build each time.
namespace rhino
{
namespace
{
constexpr int padCount = DrumRackDevice::padCount;
constexpr int padColumns = 4;
constexpr int padRows = padCount / padColumns;
constexpr int padWidth = 66, padHeight = 34, padGap = 2;
constexpr int gridWidth = padColumns * padWidth + (padColumns - 1) * padGap;
constexpr int gridHeight = padRows * padHeight + (padRows - 1) * padGap;
constexpr int gapAfterGrid = 10;
constexpr int knobCount = DrumRackDevice::controlCount;
constexpr int headerRow = 16;
constexpr int displayHeight = 60;
constexpr int padButtonWidth = 18, padButtonHeight = 11;
// How much of a pad's light goes out each frame after it is struck: at 24
// frames a second, a strike glows for a fifth of a second.
constexpr float flashFade = 0.2f;

// A pad's ground, empty and filled, and the inks that say what fills it: warm
// for a sample, as the browser draws a sample's row, cool for a synth.
const juce::Colour emptyPadInk {0xff222222};
const juce::Colour filledPadInk {0xff2f3236};
const juce::Colour padButtonInk {0xff1b1d20};
const juce::Colour sampleInk {0xffe09a70};
const juce::Colour synthInk {0xff8cc5d2};
const juce::Colour muteInk {0xffe0c14a};
const juce::Colour soloInk {0xff5f9ee0};
const juce::Colour wellInk {0xff121416};
const juce::Colour missingInk {0xffd4564e};
const juce::Colour lanedInk {0xff2f7d55};
const juce::Colour overriddenInk {0xff8a6a2e};

const char* const knobCaptions[knobCount] {"TUNE", "DECAY", "TONE", "VEL", "LEVEL", "PAN"};

struct DrumLayout
{
    std::array<juce::Rectangle<int>, padCount> pad, mute, play, solo;
    juce::Rectangle<int> chain, nameArea, soundChooser, chokeChooser, display;
    std::array<juce::Rectangle<int>, knobCount> knob, caption, value, automation;
};

DrumLayout drumLayoutFor(juce::Rectangle<int> panel)
{
    DrumLayout layout;
    const auto area = panel.withTrimmedTop(DeviceEditorPanel::headerHeight + 1).reduced(4);
    const auto gridTop = area.getY() + std::max(0, (area.getHeight() - gridHeight) / 2);
    for (int index = 0; index < padCount; ++index)
    {
        // The lowest note bottom left, rising along a row and then up a row.
        const auto i = static_cast<size_t>(index);
        const auto column = index % padColumns;
        const auto row = padRows - 1 - index / padColumns;
        const juce::Rectangle<int> cell(area.getX() + column * (padWidth + padGap),
                                        gridTop + row * (padHeight + padGap), padWidth, padHeight);
        layout.pad[i] = cell;
        const auto strip = cell.getBottom() - padButtonHeight - 2;
        layout.mute[i] = {cell.getX() + 3, strip, padButtonWidth, padButtonHeight};
        layout.play[i] = {cell.getX() + 5 + padButtonWidth, strip, padButtonWidth, padButtonHeight};
        layout.solo[i] = {cell.getX() + 7 + 2 * padButtonWidth, strip, padButtonWidth, padButtonHeight};
    }

    auto chain = area.withTrimmedLeft(gridWidth + gapAfterGrid);
    layout.chain = chain;
    auto header = chain.removeFromTop(headerRow);
    layout.chokeChooser = header.removeFromRight(70);
    header.removeFromRight(4);
    layout.soundChooser = header.removeFromRight(106);
    header.removeFromRight(6);
    layout.nameArea = header;
    chain.removeFromTop(2);
    layout.display = chain.removeFromTop(displayHeight);
    chain.removeFromTop(2);
    const auto cellWidth = chain.getWidth() / knobCount;
    for (int index = 0; index < knobCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        const juce::Rectangle<int> cell(chain.getX() + index * cellWidth, chain.getY(), cellWidth, chain.getHeight());
        layout.caption[i] = cell.withHeight(12);
        layout.value[i] = {cell.getX(), cell.getBottom() - 12, cell.getWidth(), 12};
        const auto size = std::min({40, cell.getWidth() - 10, cell.getHeight() - 26});
        layout.knob[i] = juce::Rectangle<int>(cell.getX(), cell.getY() + 12, cell.getWidth(), cell.getHeight() - 24)
                             .withSizeKeepingCentre(size, size);
        layout.automation[i] = {cell.getRight() - 15, cell.getY() + 12, 14, 12};
    }
    return layout;
}

DrumRackDevice* drumsIn(Session& session, int track, int slot)
{
    return dynamic_cast<DrumRackDevice*>(session.devicePlugin(track, slot));
}

juce::String padTitle(int pad)
{
    return "Pad " + juce::String(pad + 1);
}

// The kind of drum a sound is, for the folder its preset is saved in: the
// synth's own, or what the sound's names say, or Percussion when they say
// nothing.
juce::String drumTypeOfSound(const DrumSound& sound)
{
    if (sound.source == DrumRackEngine::Source::synth)
        return drumModelInfo(sound.model).type;
    auto unnamed = sound;
    unnamed.name.clear();
    for (const auto& words : {sound.displayName(), unnamed.displayName()})
        if (const auto type = ContentLibrary::drumTypeOf({}, words); type.isNotEmpty())
            return type;
    return "Percussion";
}

// drawSnappedText, with each line kept once it is shaped. Shaping a line is
// most of what this face costs to paint -- sixteen names, thirty-two letters,
// a dozen captions and readings -- and nearly all of them are the same frame
// after frame, so each is laid out once, where it stands, and drawn from
// then on. The baseline and justification are drawSnappedText's own, so a
// line comes out exactly as it would have. Painting is the message thread's
// alone, so one cache serves every face.
void drawLine(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
              juce::Justification justification = juce::Justification::centredLeft, bool elide = false)
{
    static std::unordered_map<juce::String, juce::GlyphArrangement> lines;
    const auto font = g.getCurrentFont();
    const auto key = text + "\n" + area.toString() + "\n" + juce::String(justification.getFlags())
                   + (elide ? "\ne" : "\n") + font.toString();
    auto found = lines.find(key);
    if (found == lines.end())
    {
        // Readings change as knobs turn, so the cache is let go of rather
        // than allowed to grow without end.
        if (lines.size() > 2048)
            lines.clear();
        juce::GlyphArrangement glyphs;
        const auto line = elide ? elidedToWidth(font, text, area.getWidth()) : text;
        if (line.isNotEmpty())
        {
            const auto baseline = area.getY()
                + juce::roundToInt((static_cast<float>(area.getHeight()) + font.getAscent() - font.getDescent()) * 0.5f);
            auto x = area.getX();
            if (justification.testFlags(juce::Justification::horizontallyCentred))
                x = area.getCentreX() - juce::roundToInt(juce::GlyphArrangement::getStringWidth(font, line) * 0.5f);
            else if (justification.testFlags(juce::Justification::right))
                x = area.getRight() - juce::GlyphArrangement::getStringWidthInt(font, line);
            glyphs.addLineOfText(font, line, static_cast<float>(x), static_cast<float>(baseline));
        }
        found = lines.emplace(key, std::move(glyphs)).first;
    }
    found->second.draw(g);
}

// A pad's M, play and S. Each is drawn rather than made a button, so a face of
// sixteen pads is one component and a strike repaints one rectangle.
void drawPadButton(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& letter, juce::Colour onInk,
                   bool lit, bool usable)
{
    const auto box = area.toFloat();
    g.setColour(lit ? onInk : padButtonInk);
    g.fillRoundedRectangle(box, 1.5f);
    g.setColour(lit ? palette::appBackground : usable ? palette::textDim : palette::disabled);
    if (letter.isEmpty())
    {
        // Play is drawn rather than set as a glyph: Inter has no triangle,
        // and the fallback face's sits off the button's centre.
        const auto centre = box.getCentre();
        juce::Path arrow;
        arrow.addTriangle(centre.x - 2.5f, centre.y - 3.0f, centre.x - 2.5f, centre.y + 3.0f, centre.x + 3.0f, centre.y);
        g.fillPath(arrow);
        return;
    }
    g.setFont(uiFontBold(7.5f));
    drawLine(g, letter, area, juce::Justification::centred);
}

void drawChooser(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool usable)
{
    g.setColour(palette::control);
    g.fillRoundedRectangle(area.toFloat(), 2.0f);
    g.setColour(palette::border);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.0f, 1.0f);
    g.setColour(usable ? palette::text : palette::disabled);
    g.setFont(uiFont(8.5f));
    drawLine(g, text, area.withTrimmedLeft(6).withTrimmedRight(14), juce::Justification::centredLeft, true);
    // The arrow that says it opens.
    const auto tip = area.toFloat().removeFromRight(13.0f).withSizeKeepingCentre(6.0f, 4.0f);
    juce::Path arrow;
    arrow.addTriangle(tip.getX(), tip.getY(), tip.getRight(), tip.getY(), tip.getCentreX(), tip.getBottom());
    g.setColour(usable ? palette::textDim : palette::disabled);
    g.fillPath(arrow);
}

juce::String soundChooserText(const DrumRackDevice::Pad& pad)
{
    if (!pad.sound.has_value())
        return "Empty";
    if (pad.sound->source == DrumRackEngine::Source::synth)
        return juce::String("Synth: ") + drumModelInfo(pad.sound->model).name;
    return pad.unreadable ? "Sample missing" : "Sample";
}

juce::String chokeChooserText(const DrumRackDevice::Pad& pad)
{
    const auto group = pad.sound.has_value() ? pad.sound->choke : 0;
    return group > 0 ? "Choke " + juce::String(group) : juce::String("No choke");
}

juce::String secondsText(double seconds)
{
    return seconds < 1.0 ? juce::String(juce::roundToInt(seconds * 1000.0)) + " ms" : juce::String(seconds, 2) + " s";
}

}

// ---- for the rack --------------------------------------------------------------

int DeviceEditorPanel::drumPadAt(juce::Point<int> position) const
{
    if (face != Face::DrumRack)
        return -1;
    const auto layout = drumLayoutFor(getLocalBounds());
    for (int index = 0; index < padCount; ++index)
        if (layout.pad[static_cast<size_t>(index)].contains(position))
            return index;
    return -1;
}

void DeviceEditorPanel::showDrumDropTarget(int pad)
{
    pad = face == Face::DrumRack && juce::isPositiveAndBelow(pad, padCount) ? pad : -1;
    if (pad == drumDropTarget)
        return;
    const auto layout = drumLayoutFor(getLocalBounds());
    for (const auto lit : {drumDropTarget, pad})
        if (juce::isPositiveAndBelow(lit, padCount))
            repaint(layout.pad[static_cast<size_t>(lit)].expanded(2));
    drumDropTarget = pad;
}

// ---- the knobs -------------------------------------------------------------------

void DeviceEditorPanel::ensureDrumControls()
{
    if (!drumSliders.isEmpty())
        return;
    for (int i = 0; i < knobCount; ++i)
    {
        auto* slider = drumSliders.add(new juce::Slider());
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        // The control behind a knob is decided when the knob is touched:
        // selecting another pad points all six at that pad's controls.
        const auto controlFor = [this, i]
        {
            const auto* device = drumsIn(session, track, pluginSlot);
            return device != nullptr ? DrumRackDevice::parameterIndex(device->selectedPad(), i) : -1;
        };
        slider->onDragStart = [this, controlFor]
        {
            if (const auto index = controlFor(); index >= 0)
                session.beginDeviceParameterGesture(track, pluginSlot, index);
        };
        slider->onValueChange = [this, controlFor, slider]
        {
            if (syncing)
                return;
            if (const auto index = controlFor(); index >= 0)
            {
                const auto result = session.setDeviceParameter(track, pluginSlot, index,
                                                               static_cast<float>(slider->getValue()));
                if (result.failed() && status) status(result.getErrorMessage());
            }
        };
        slider->onDragEnd = [this, controlFor]
        {
            if (const auto index = controlFor(); index >= 0)
                session.endDeviceParameterGesture(track, pluginSlot, index);
        };
        addAndMakeVisible(slider);
    }
}

void DeviceEditorPanel::styleDrumControls()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    // A face retargeted at another rack takes that rack's strike counts as
    // where it stands, rather than flashing every pad struck before it came.
    if (drumStrikesDevice != deviceKey)
    {
        drumStrikesDevice = deviceKey;
        for (int index = 0; index < padCount; ++index)
            drumStrikesSeen[static_cast<size_t>(index)] = device->padStrikes(index);
        drumFlash.fill(0.0f);
    }
    const auto chosen = device->selectedPad();
    const auto filled = device->pad(chosen).sound.has_value();
    const auto accent = faceAccent();
    syncing = true;
    for (int i = 0; i < drumSliders.size(); ++i)
    {
        const auto index = DrumRackDevice::parameterIndex(chosen, i);
        if (!juce::isPositiveAndBelow(index, static_cast<int>(parameters.size())))
            continue;
        const auto& parameter = parameters[static_cast<size_t>(index)];
        auto* slider = drumSliders[i];
        slider->setRange(parameter.minimum, parameter.maximum,
                         parameter.interval > 0.0f ? static_cast<double>(parameter.interval) : 0.0);
        slider->setSkewFactor(parameter.skew);
        slider->setDoubleClickReturnValue(parameter.defaultValue.has_value(), parameter.defaultValue.value_or(0.0f));
        slider->setValue(parameter.value, juce::dontSendNotification);
        // An empty pad has nothing for its knobs to shape.
        slider->setEnabled(filled);
        slider->setTooltip(parameter.name + ": " + parameter.valueText);
        const auto ink = filled ? accent : palette::disabled;
        slider->setColour(juce::Slider::trackColourId, ink);
        slider->setColour(juce::Slider::rotarySliderFillColourId, ink);
        slider->setColour(juce::Slider::backgroundColourId, palette::control);
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, palette::border);
        slider->setColour(juce::Slider::thumbColourId, filled ? accent.brighter(0.28f) : palette::disabled);
    }
    syncing = false;
}

void DeviceEditorPanel::layoutDrums()
{
    visualArea = {};
    ensureDrumControls();
    // A panel is reused for whatever device it is shown next, so a generic
    // grid built for an earlier one is put away, its bounds with it.
    for (int i = 0; i < parameterLabels.size(); ++i)
        for (auto* control : std::initializer_list<juce::Component*>
                 {parameterLabels[i], parameterValues[i], parameterSliders[i], parameterAutomation[i]})
        {
            control->setVisible(false);
            control->setBounds({});
        }
    const auto layout = drumLayoutFor(getLocalBounds());
    for (int i = 0; i < drumSliders.size(); ++i)
    {
        drumSliders[i]->setBounds(layout.knob[static_cast<size_t>(i)]);
        drumSliders[i]->setVisible(true);
    }
    styleDrumControls();
}

// ---- painting --------------------------------------------------------------------

void DeviceEditorPanel::repaintDrums()
{
    juce::String drawn;
    if (auto* device = drumsIn(session, track, pluginSlot))
    {
        drawn << device->selectedPad() << '|' << drumDropTarget << '|' << (isSelected ? "selected" : "");
        for (int index = 0; index < padCount; ++index)
        {
            const auto view = device->pad(index);
            drawn << '|';
            if (view.sound.has_value())
                drawn << view.sound->displayName() << (view.sound->source == DrumRackEngine::Source::synth ? ":synth" : ":sample");
            drawn << (view.muted ? ":muted" : "") << (view.soloed ? ":soloed" : "") << (view.unreadable ? ":unreadable" : "");
        }
    }
    if (drawn == drumPadsDrawn)
    {
        repaint(drumLayoutFor(getLocalBounds()).chain.expanded(gapAfterGrid / 2 + 1, 2));
        return;
    }
    drumPadsDrawn = drawn;
    repaint();
}

void DeviceEditorPanel::paintDrums(juce::Graphics& g)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto layout = drumLayoutFor(getLocalBounds());
    const auto accent = faceAccent();
    const auto chosen = device->selectedPad();

    // ---- the pads ------------------------------------------------------------
    for (int index = 0; index < padCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        const auto cell = layout.pad[i];
        // A flash repaints one pad, and the others are not worth drawing again.
        if (!g.clipRegionIntersects(cell.expanded(2)))
            continue;
        const auto view = device->pad(index);
        const auto filled = view.sound.has_value();
        const auto box = cell.toFloat();
        g.setColour(filled ? filledPadInk : emptyPadInk);
        g.fillRoundedRectangle(box, 2.5f);
        if (drumFlash[i] > 0.0f)
        {
            g.setColour(accent.withAlpha(0.5f * drumFlash[i]));
            g.fillRoundedRectangle(box, 2.5f);
        }
        if (index == drumDropTarget)
        {
            g.setColour(palette::selection);
            g.drawRoundedRectangle(box.reduced(1.0f), 2.5f, 2.0f);
        }
        else
        {
            g.setColour(index == chosen ? accent : palette::border);
            g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, index == chosen ? 1.5f : 1.0f);
        }

        const juce::Rectangle<int> nameBox(cell.getX() + 4, cell.getY() + 3, cell.getWidth() - 8, 13);
        if (filled)
        {
            const auto synth = view.sound->source == DrumRackEngine::Source::synth;
            g.setColour(view.unreadable ? missingInk : synth ? synthInk : sampleInk);
            g.fillEllipse(static_cast<float>(cell.getRight()) - 8.0f, static_cast<float>(cell.getY()) + 6.0f, 4.0f, 4.0f);
            g.setColour(view.unreadable ? palette::disabled : palette::text);
            g.setFont(uiFontBold(8.5f));
            drawLine(g, view.sound->displayName(), nameBox.withTrimmedRight(6), juce::Justification::centredLeft,
                            true);
        }
        else
        {
            g.setColour(palette::disabled);
            g.setFont(uiFont(8.5f));
            drawLine(g, DrumRackDevice::noteName(index), nameBox, juce::Justification::centredLeft);
        }
        drawPadButton(g, layout.mute[i], "M", muteInk, view.muted, filled);
        drawPadButton(g, layout.play[i], {}, accent, false, filled);
        drawPadButton(g, layout.solo[i], "S", soloInk, view.soloed, filled);
    }

    // ---- the selected pad ------------------------------------------------------
    if (!g.clipRegionIntersects(layout.chain.expanded(gapAfterGrid / 2, 0)))
        return;
    g.setColour(palette::border);
    g.fillRect(layout.chain.getX() - gapAfterGrid / 2, layout.chain.getY() + 3, 1, layout.chain.getHeight() - 6);

    const auto view = device->pad(chosen);
    const auto filled = view.sound.has_value();
    g.setFont(uiFontBold(10.0f));
    const auto heading = filled ? view.sound->displayName() : padTitle(chosen);
    const auto titleWidth = juce::jmin(layout.nameArea.getWidth() - 28,
                                       juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), heading));
    g.setColour(filled ? palette::text : palette::textDim);
    drawLine(g, heading, layout.nameArea.withWidth(titleWidth), juce::Justification::centredLeft, true);
    g.setColour(palette::textDim);
    g.setFont(uiFont(8.5f));
    drawLine(g, DrumRackDevice::noteName(chosen), layout.nameArea.withTrimmedLeft(titleWidth + 6),
                    juce::Justification::centredLeft, true);
    drawChooser(g, layout.soundChooser, soundChooserText(view), true);
    drawChooser(g, layout.chokeChooser, chokeChooserText(view), filled);

    // One strike of the pad as it plays now, from the engine's own voice, so
    // the picture moves with Tune, Decay and Tone exactly as the sound does.
    const auto well = layout.display.toFloat();
    g.setColour(wellInk);
    g.fillRoundedRectangle(well, 3.0f);
    g.setColour(chosen == drumDropTarget ? palette::selection : palette::border);
    g.drawRoundedRectangle(well.reduced(0.5f), 3.0f, 1.0f);
    const auto inner = layout.display.reduced(4, 4);
    if (!filled || view.unreadable)
    {
        g.setColour(view.unreadable ? missingInk : palette::textDim);
        g.setFont(uiFont(9.0f));
        const auto missing = filled ? ContentLibrary::resolveStoredPath(view.sound->sample).getFileName() : juce::String();
        drawLine(g, view.unreadable ? missing + " is missing or cannot be read"
                                           : "Drop a sample or a drum preset here, or click to choose a sample",
                        inner, juce::Justification::centred, true);
    }
    else
    {
        const auto& picture = device->padPicture(chosen, inner.getWidth());
        const auto middle = static_cast<float>(inner.getCentreY());
        const auto reach = static_cast<float>(inner.getHeight()) * 0.5f;
        // Each picture is scaled to its own peak, as a sampler draws its
        // file: the shape is what the picture is for, and Level reads below.
        const auto scale = picture.peak > 1.0e-6f ? reach / picture.peak : 0.0f;
        if (!picture.highs.empty() && scale > 0.0f)
        {
            juce::Path wave;
            const auto columns = static_cast<int>(picture.highs.size());
            for (int column = 0; column < columns; ++column)
            {
                const auto x = static_cast<float>(inner.getX() + column);
                const auto y = middle - picture.highs[static_cast<size_t>(column)] * scale;
                if (column == 0)
                    wave.startNewSubPath(x, y);
                else
                    wave.lineTo(x, y);
            }
            for (int column = columns - 1; column >= 0; --column)
                wave.lineTo(static_cast<float>(inner.getX() + column),
                            middle - picture.lows[static_cast<size_t>(column)] * scale);
            wave.closeSubPath();
            g.setColour(accent.withAlpha(0.6f));
            g.fillPath(wave);
        }
        g.setColour(accent.withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(middle), static_cast<float>(inner.getX()), static_cast<float>(inner.getRight()));
        g.setColour(palette::textDim);
        g.setFont(uiFont(8.0f));
        drawLine(g, secondsText(picture.seconds), inner.withTrimmedTop(inner.getHeight() - 11),
                        juce::Justification::centredRight);
    }

    // ---- the knobs' captions, readings and lanes --------------------------------
    for (int index = 0; index < knobCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        const auto control = DrumRackDevice::parameterIndex(chosen, index);
        if (!juce::isPositiveAndBelow(control, static_cast<int>(parameters.size())))
            continue;
        const auto& parameter = parameters[static_cast<size_t>(control)];
        g.setColour(filled ? palette::textDim : palette::disabled);
        g.setFont(uiFont(8.0f));
        drawLine(g, knobCaptions[i], layout.caption[i], juce::Justification::centred);
        g.setColour(filled ? palette::text : palette::disabled);
        g.setFont(uiFont(8.5f));
        drawLine(g, filled ? parameter.valueText : juce::String("--"), layout.value[i], juce::Justification::centred);
        // The badge shows only on a control with a lane, and it is the one
        // way back to the lane once the knob has been taken over by hand.
        if (!parameter.automated)
            continue;
        g.setColour(parameter.automationOverridden ? overriddenInk : lanedInk);
        g.fillRoundedRectangle(layout.automation[i].toFloat(), 2.0f);
        g.setColour(palette::text);
        g.setFont(uiFontBold(8.0f));
        drawLine(g, "A", layout.automation[i], juce::Justification::centred);
    }
}

// ---- the hand ------------------------------------------------------------------

bool DeviceEditorPanel::handleDrumMouseDown(const juce::MouseEvent& event)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return false;
    // A right-click on a knob is the automation menu for whatever the knob
    // stands for right now.
    if (event.mods.isPopupMenu())
        for (int i = 0; i < drumSliders.size(); ++i)
            if (event.eventComponent == drumSliders[i])
            {
                showParameterMenu(DrumRackDevice::parameterIndex(device->selectedPad(), i));
                return true;
            }
    if (event.eventComponent != this || event.y < headerHeight)
        return false;

    const auto position = event.getPosition();
    const auto layout = drumLayoutFor(getLocalBounds());
    const auto chosen = device->selectedPad();
    for (int i = 0; i < knobCount; ++i)
        if (layout.automation[static_cast<size_t>(i)].contains(position))
        {
            const auto control = DrumRackDevice::parameterIndex(chosen, i);
            if (juce::isPositiveAndBelow(control, static_cast<int>(parameters.size()))
                && parameters[static_cast<size_t>(control)].automated)
            {
                const auto result = session.toggleParameterAutomationOverride(track, pluginSlot, control);
                if (status) status(result.wasOk() ? "Toggled parameter automation" : result.getErrorMessage());
                return true;
            }
        }

    for (int index = 0; index < padCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        // Picking a pad is a view of the rack, not an edit to it.
        if (index != chosen)
        {
            device->setSelectedPad(index);
            styleDrumControls();
            repaint();
        }
        if (event.mods.isPopupMenu())
        {
            showDrumPadMenu(index);
            return true;
        }
        const auto view = device->pad(index);
        const auto label = view.sound.has_value() ? view.sound->displayName() : padTitle(index);
        if (layout.play[i].contains(position))
        {
            device->previewPad(index);
            return true;
        }
        if (view.sound.has_value() && layout.mute[i].contains(position))
        {
            const auto muted = !view.muted;
            session.editDeviceSettings(track, pluginSlot, muted ? "Mute pad" : "Unmute pad",
                                       [device, index, muted] { device->setPadMuted(index, muted); });
            if (status) status(label + (muted ? " muted" : " unmuted"));
            return true;
        }
        if (view.sound.has_value() && layout.solo[i].contains(position))
        {
            const auto soloed = !view.soloed;
            session.editDeviceSettings(track, pluginSlot, soloed ? "Solo pad" : "Unsolo pad",
                                       [device, index, soloed] { device->setPadSoloed(index, soloed); });
            if (status) status(label + (soloed ? " soloed" : " no longer soloed"));
            return true;
        }
        if (status) status(label + " on " + DrumRackDevice::noteName(index));
        return true;
    }

    const auto filled = device->pad(chosen).sound.has_value();
    if (layout.soundChooser.contains(position))
    {
        showDrumPadMenu(chosen);
        return true;
    }
    if (layout.chokeChooser.contains(position))
    {
        if (filled)
            showDrumChokeMenu(chosen);
        return true;
    }
    if (layout.display.contains(position) && !filled)
    {
        chooseDrumSample(chosen);
        return true;
    }
    return false;
}

juce::String DeviceEditorPanel::drumTooltip(juce::Point<int> position) const
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return {};
    const auto layout = drumLayoutFor(getLocalBounds());
    for (int index = 0; index < padCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        const auto view = device->pad(index);
        if (layout.play[i].contains(position))
            return "Play this pad";
        if (layout.mute[i].contains(position))
            return "Mute this pad";
        if (layout.solo[i].contains(position))
            return "Solo this pad: while any pad is soloed, only soloed pads sound";
        if (!view.sound.has_value())
            return padTitle(index) + " on " + DrumRackDevice::noteName(index)
                   + " is empty. Drop a sample or a drum preset on it, or right-click for a synth.";
        return view.sound->displayName() + " on " + DrumRackDevice::noteName(index)
               + ". Drop a sound here to replace it; right-click for its synth, choke group and more.";
    }
    if (layout.soundChooser.contains(position))
        return "What this pad plays: a sample, or one of Rhino's synthesised drums";
    if (layout.chokeChooser.contains(position))
        return "Pads in one choke group cut each other off, the way a closed hat stops an open one";
    if (layout.display.contains(position))
        return "One strike of this pad, as it plays now";
    return {};
}

// ---- menus and dialogs ------------------------------------------------------------

void DeviceEditorPanel::showDrumPadMenu(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr || !juce::isPositiveAndBelow(pad, padCount))
        return;
    const auto view = device->pad(pad);
    const auto filled = view.sound.has_value();
    const auto playsSynth = filled && view.sound->source == DrumRackEngine::Source::synth;
    constexpr int playItem = 1, sampleItem = 2, renameItem = 3, saveItem = 4, clearItem = 5;
    constexpr int synthItems = 100, chokeItems = 200;

    juce::PopupMenu synths;
    for (int model = 0; model < drumModelCount; ++model)
        synths.addItem(synthItems + model, drumModelInfo(static_cast<DrumModel>(model)).name, true,
                       playsSynth && static_cast<int>(view.sound->model) == model);
    juce::PopupMenu choke;
    for (int group = 0; group <= DrumRackEngine::chokeGroupCount; ++group)
        choke.addItem(chokeItems + group, group == 0 ? juce::String("None") : "Group " + juce::String(group), true,
                      filled && view.sound->choke == group);

    juce::PopupMenu menu;
    menu.addSectionHeader((padTitle(pad) + "  " + DrumRackDevice::noteName(pad)
                           + (filled ? "  " + view.sound->displayName() : juce::String())).toUpperCase());
    menu.addItem(playItem, "Play", filled);
    menu.addSeparator();
    menu.addItem(sampleItem, "Sample...");
    menu.addSubMenu("Synth", synths);
    menu.addSubMenu("Choke group", choke, filled);
    menu.addSeparator();
    menu.addItem(renameItem, "Rename...", filled);
    menu.addItem(saveItem, "Save as drum preset...", filled);
    menu.addItem(clearItem, "Clear pad", filled);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            // The menu outlives the click, so the device is looked up again.
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            const auto name = padTitle(pad);
            const auto report = safe->status;
            if (result == playItem)
                target->previewPad(pad);
            else if (result == sampleItem)
                safe->chooseDrumSample(pad);
            else if (result == renameItem)
                safe->askToRenameDrumPad(pad);
            else if (result == saveItem)
                safe->askToSaveDrumPreset(pad);
            else if (result == clearItem)
            {
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Clear " + name.toLowerCase(),
                                                 [target, pad] { target->clearPad(pad); });
                if (report) report(name + " cleared");
            }
            else if (result >= synthItems && result < synthItems + drumModelCount)
            {
                const auto model = static_cast<DrumModel>(result - synthItems);
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot,
                                                 "Make " + name.toLowerCase() + " a " + drumModelInfo(model).name,
                                                 [target, pad, model] { target->setPadSynth(pad, model); });
                if (report) report(name + " plays a synthesised " + juce::String(drumModelInfo(model).name).toLowerCase());
            }
            else if (result >= chokeItems && result <= chokeItems + DrumRackEngine::chokeGroupCount)
            {
                const auto group = result - chokeItems;
                safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Choke " + name.toLowerCase(),
                                                 [target, pad, group] { target->setPadChoke(pad, group); });
                if (report) report(group == 0 ? name + " chokes nothing" : name + " is in choke group " + juce::String(group));
            }
        });
}

void DeviceEditorPanel::showDrumChokeMenu(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(pad);
    juce::PopupMenu menu;
    menu.addSectionHeader("CHOKE GROUP");
    for (int group = 0; group <= DrumRackEngine::chokeGroupCount; ++group)
        menu.addItem(group + 1, group == 0 ? juce::String("None") : "Group " + juce::String(group), true,
                     view.sound.has_value() && view.sound->choke == group);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            const auto group = result - 1;
            safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Choke " + padTitle(pad).toLowerCase(),
                                             [target, pad, group] { target->setPadChoke(pad, group); });
        });
}

void DeviceEditorPanel::chooseDrumSample(int pad)
{
    const auto start = ContentLibrary::file("Samples");
    drumFileChooser = std::make_unique<juce::FileChooser>("A sample for " + padTitle(pad).toLowerCase(),
                                                          start.isDirectory() ? start : juce::File(),
                                                          "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    drumFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), pad] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (safe == nullptr || file == juce::File())
                return;
            const auto report = safe->status;
            const auto result = safe->session.loadDrumPadSample(safe->track, safe->pluginSlot, pad, file);
            if (report)
                report(result.wasOk() ? "Loaded " + file.getFileNameWithoutExtension() + " on " + padTitle(pad).toLowerCase()
                                      : result.getErrorMessage());
        });
}

void DeviceEditorPanel::askToRenameDrumPad(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    auto* window = new juce::AlertWindow("Rename " + padTitle(pad).toLowerCase(), "A name for this pad:",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", device->padName(pad));
    window->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), window, pad] (int result)
        {
            if (result != 1 || safe == nullptr)
                return;
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            // An empty name gives the pad back its sound's own.
            const auto name = window->getTextEditorContents("name").trim();
            safe->session.editDeviceSettings(safe->track, safe->pluginSlot, "Rename " + padTitle(pad).toLowerCase(),
                                             [target, pad, name] { target->setPadName(pad, name); });
        }), true);
}

void DeviceEditorPanel::askToSaveDrumPreset(int pad)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(pad);
    if (!view.sound.has_value())
        return;
    // Filed by the kind of drum it is, so it lands in the browser beside
    // the samples of its kind.
    const auto folder = ContentLibrary::userDrums().getChildFile("Presets").getChildFile(drumTypeOfSound(*view.sound));
    askToSaveFile("drum preset", "A name for this drum preset:", view.sound->displayName(), folder,
                  DrumFiles::soundExtension,
                  [this, pad] (const juce::File& file)
                  {
                      const auto result = session.saveDrumPadPreset(track, pluginSlot, pad, file);
                      if (status)
                          status(result.wasOk() ? "Saved " + DrumFiles::nameOf(file) + " to your drum presets"
                                                : result.getErrorMessage());
                      if (result.wasOk() && presetsChanged)
                          presetsChanged();
                  });
}

void DeviceEditorPanel::showDrumKitMenu()
{
    constexpr int saveItem = 100000;
    juce::PopupMenu menu;
    menu.addSectionHeader("DRUM RACK KITS");
    std::vector<juce::File> files;
    // Rhino's own first, then the person's, as the browser lists them.
    auto lastWasUser = false;
    for (const auto& kit : ContentLibrary::drumKits())
    {
        if (kit.user && !lastWasUser && !files.empty())
            menu.addSeparator();
        lastWasUser = kit.user;
        files.push_back(kit.file);
        menu.addItem(static_cast<int>(files.size()), kit.name);
    }
    if (files.empty())
        menu.addItem(saveItem + 1, "No kits yet", false);
    menu.addSeparator();
    menu.addItem(saveItem, "Save kit...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&title),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this), files] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == saveItem)
            {
                safe->askToSaveFile("kit", "A name for this kit:", safe->deviceName,
                                    ContentLibrary::userDrums().getChildFile("Kits"), DrumFiles::kitExtension,
                                    [panel = safe.getComponent()] (const juce::File& file)
                                    {
                                        const auto saved = panel->session.saveDrumKit(panel->track, panel->pluginSlot, file);
                                        if (panel->status)
                                            panel->status(saved.wasOk() ? "Saved " + DrumFiles::nameOf(file) + " to your kits"
                                                                        : saved.getErrorMessage());
                                        if (saved.wasOk() && panel->presetsChanged)
                                            panel->presetsChanged();
                                    });
                return;
            }
            if (result > static_cast<int>(files.size()))
                return;
            const auto& file = files[static_cast<size_t>(result - 1)];
            // Loading syncs the rack, which may rebuild this panel, so what is
            // said afterwards goes through a copy of the callback.
            const auto report = safe->status;
            const auto outcome = safe->session.loadDrumKit(safe->track, safe->pluginSlot, file);
            if (report)
                report(outcome.wasOk() ? "Loaded " + DrumFiles::nameOf(file) : outcome.getErrorMessage());
        });
}

// ---- frames ------------------------------------------------------------------------

// Lights each pad as it is struck, and lets the light go out. A strike is read
// from a counter the audio thread bumps, so the face asks for a repaint of one
// pad only when something actually happened on it.
void DeviceEditorPanel::tickDrums()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    device->collectSamples();
    const auto layout = drumLayoutFor(getLocalBounds());
    for (int index = 0; index < padCount; ++index)
    {
        const auto i = static_cast<size_t>(index);
        const auto strikes = device->padStrikes(index);
        auto& flash = drumFlash[i];
        const auto before = flash;
        if (strikes != drumStrikesSeen[i])
        {
            drumStrikesSeen[i] = strikes;
            flash = 1.0f;
        }
        else if (flash > 0.0f)
        {
            flash = std::max(0.0f, flash - flashFade);
        }
        if (flash != before)
            repaint(layout.pad[i]);
    }
}
}
