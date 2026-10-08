#include "DeviceEditorPanelDrumsInternal.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <optional>

// The Drum Rack's face, left side: a map of all 128 notes and the sixteen pads
// of the bank it shows, four rows of four with the lowest note at the bottom
// left, as on a pad controller and in Live.
//
// The map is how the face moves among the pads: each note a cell, four to a
// row, the bank on the pads framed, the notes that hold a sound lit, and a key
// flashing orange as it arrives, so a controller playing the wrong octave says
// so at once. A click or a drag on the map, or the wheel over the pads, shows
// another bank; the note editor's drum rows follow it.
//
// The pads are the rack's surface for the hand. A click picks a pad and its
// three buttons mute, play and solo it; a right-click offers everything else
// that can happen to one; a sample or a drum preset dragged from the browser
// lands on the pad under the pointer (DeviceRack hands the drop over). Nothing
// here is a child component: the rack rebuilds its panels whenever the chain
// changes, and sixteen pads of buttons would be a lot to build each time.
//
// The rest of the face: where everything stands and the drawn controls it is
// made of, DeviceEditorPanelDrumParts.cpp; clicks, drags and tooltips,
// DeviceEditorPanelDrumGestures.cpp; the pad's menus and dialogs,
// DeviceEditorPanelDrumMenus.cpp; the selected pad's side,
// DeviceEditorPanelDrumSample.cpp, and its sample's knobs,
// DeviceEditorPanelDrumSampleControls.cpp.
namespace rhino
{
namespace drumface
{
namespace
{
// How much of a pad's light goes out each frame after it is struck: at 24
// frames a second, a strike glows for a fifth of a second. A key on the map
// glows a little longer, and its name in the name bar longer still.
constexpr float flashFade = 0.2f;
constexpr float keyFade = 0.12f;
constexpr float keyNameFade = 0.03f;

// Where the name bar names the last key to arrive: at its right end, or left
// of the window button where the face has one.
juce::Rectangle<int> keyNameArea(juce::Rectangle<int> panel, bool besideButton)
{
    return {panel.getRight() - 78 - (besideButton ? 24 : 0), 4, 70, DeviceEditorPanel::headerHeight - 8};
}

// Two corners pointing apart: open bigger, somewhere else.
void drawWindowGlyph(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto box = area.withSizeKeepingCentre(10.0f, 10.0f);
    const auto left = box.getX(), top = box.getY(), right = box.getRight(), bottom = box.getBottom();
    juce::Path glyph;
    glyph.startNewSubPath(right - 4.0f, top);
    glyph.lineTo(right, top);
    glyph.lineTo(right, top + 4.0f);
    glyph.startNewSubPath(right, top);
    glyph.lineTo(right - 4.5f, top + 4.5f);
    glyph.startNewSubPath(left + 4.0f, bottom);
    glyph.lineTo(left, bottom);
    glyph.lineTo(left, bottom - 4.0f);
    glyph.startNewSubPath(left, bottom);
    glyph.lineTo(left + 4.5f, bottom - 4.5f);
    g.setColour(palette::textDim);
    g.strokePath(glyph, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}
}

using namespace drumface;

// ---- for the rack --------------------------------------------------------------

int DeviceEditorPanel::drumPadAt(juce::Point<int> position) const
{
    if (face != Face::DrumRack)
        return -1;
    const auto* device = drumsIn(session, track, pluginSlot);
    const auto first = device != nullptr ? device->firstShownNote() : DrumRackEngine::defaultFirstNote;
    const auto layout = layoutFor(getLocalBounds());
    for (int index = 0; index < padsShown; ++index)
        if (layout.pad[static_cast<size_t>(index)].contains(position))
            return first + index;
    return -1;
}

void DeviceEditorPanel::showDrumDropTarget(int pad)
{
    pad = face == Face::DrumRack && juce::isPositiveAndBelow(pad, DrumRackDevice::padCount) ? pad : -1;
    if (pad == drumDropTarget)
        return;
    const auto* device = drumsIn(session, track, pluginSlot);
    const auto first = device != nullptr ? device->firstShownNote() : DrumRackEngine::defaultFirstNote;
    const auto layout = layoutFor(getLocalBounds());
    for (const auto lit : {drumDropTarget, pad})
    {
        if (juce::isPositiveAndBelow(lit - first, padsShown))
            repaint(layout.pad[static_cast<size_t>(lit - first)].expanded(2));
        if (lit >= 0)
            repaint(mapCell(layout, lit).expanded(2.0f).getSmallestIntegerContainer());
    }
    drumDropTarget = pad;
    repaint(layout.picture.expanded(1));
}

void DeviceEditorPanel::dropDrumSounds(int pad, const std::vector<juce::File>& sounds, bool presets)
{
    const auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    if (pad < 0)
        pad = device->selectedPad();
    // Each load is announced as it lands, so what is needed afterwards is
    // copied rather than read from the panel.
    const auto onTrack = track, slot = pluginSlot;
    const auto report = status;
    auto loaded = 0;
    juce::String failure;
    // Several files fill the pads one after another from the one dropped on;
    // the rack stops at its last pad.
    for (const auto& sound : sounds)
    {
        const auto target = pad + loaded;
        if (!juce::isPositiveAndBelow(target, DrumRackDevice::padCount))
            break;
        const auto result = presets ? session.loadDrumPadPreset(onTrack, slot, target, sound)
                                    : session.loadDrumPadSample(onTrack, slot, target, sound);
        if (result.failed())
        {
            failure = result.getErrorMessage();
            break;
        }
        ++loaded;
    }
    if (report == nullptr)
        return;
    if (failure.isNotEmpty())
        report(failure);
    else if (loaded == 1)
        report("Loaded " + sounds.front().getFileNameWithoutExtension() + " on " + DrumRackDevice::noteName(pad));
    else if (loaded > 1)
        report("Loaded " + juce::String(loaded) + " sounds on " + DrumRackDevice::noteName(pad) + " to "
               + DrumRackDevice::noteName(pad + loaded - 1));
}

void DeviceEditorPanel::showDrumBank(int firstNote)
{
    const auto result = session.showDrumBank(track, pluginSlot, firstNote);
    if (result.failed() && status)
        status(result.getErrorMessage());
}

// ---- the knobs -------------------------------------------------------------------

void DeviceEditorPanel::readDrumParameters()
{
    const auto count = session.deviceParameterCount(track, pluginSlot);
    if (static_cast<int>(parameters.size()) != count)
    {
        parameters.assign(static_cast<size_t>(count), {});
        drumParametersFrom = -1;
    }
    const auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto first = DrumRackDevice::parameterIndex(device->selectedPad(), 0);
    if (drumParametersFrom >= 0 && drumParametersFrom != first)
        for (int i = 0; i < DrumRackDevice::controlCount; ++i)
            if (juce::isPositiveAndBelow(drumParametersFrom + i, count))
                parameters[static_cast<size_t>(drumParametersFrom + i)] = {};
    const auto read = session.deviceParameters(track, pluginSlot, first, DrumRackDevice::controlCount);
    for (size_t i = 0; i < read.size(); ++i)
        if (juce::isPositiveAndBelow(first + static_cast<int>(i), count))
            parameters[static_cast<size_t>(first) + i] = read[i];
    drumParametersFrom = first;
}

void DeviceEditorPanel::ensureDrumControls()
{
    ensureDrumSampleControls();
    if (!drumSliders.isEmpty())
        return;
    for (int i = 0; i < controlCells; ++i)
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
    // A face retargeted at another rack takes that rack's counts as where it
    // stands, rather than flashing every pad struck before it came.
    if (drumStrikesDevice != deviceKey)
    {
        drumStrikesDevice = deviceKey;
        for (int note = 0; note < DrumRackDevice::padCount; ++note)
        {
            drumStrikesSeen[static_cast<size_t>(note)] = device->padStrikes(note);
            drumNotesSeen[static_cast<size_t>(note)] = device->notesReceived(note);
        }
        drumFlash.fill(0.0f);
        drumKeyFlash.fill(0.0f);
        drumLastKey = -1;
        drumLastKeyGlow = 0.0f;
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
    styleDrumSampleControls();
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
    const auto layout = layoutFor(getLocalBounds());
    for (int i = 0; i < drumSliders.size(); ++i)
    {
        drumSliders[i]->setBounds(knobIn(layout.cell[static_cast<size_t>(i)]));
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
        const auto first = device->firstShownNote();
        drawn << device->selectedPad() << '|' << first << '|' << drumDropTarget << '|' << (isSelected ? "selected" : "");
        drawn << '|';
        for (const auto filled : device->filledPads())
            drawn << (filled ? '1' : '0');
        for (int note = first; note < first + padsShown; ++note)
        {
            const auto view = device->pad(note);
            drawn << '|';
            if (view.sound.has_value())
                drawn << view.sound->displayName() << (view.sound->source == DrumRackEngine::Source::synth ? ":synth" : ":sample");
            drawn << (view.muted ? ":muted" : "") << (view.soloed ? ":soloed" : "") << (view.unreadable ? ":unreadable" : "");
        }
    }
    if (drawn == drumPadsDrawn)
    {
        repaint(layoutFor(getLocalBounds()).editor.expanded(gapAfterGrid / 2 + 1, 2));
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
    const auto layout = layoutFor(getLocalBounds());
    const auto accent = faceAccent();
    const auto chosen = device->selectedPad();
    const auto first = device->firstShownNote();

    // ---- the name bar: the window button and the last key -------------------------
    const auto windowButton = openInWindow != nullptr;
    if (windowButton && g.clipRegionIntersects(windowButtonIn(getLocalBounds())))
        drawWindowGlyph(g, windowButtonIn(getLocalBounds()).toFloat());
    if (drumLastKey >= 0 && g.clipRegionIntersects(keyNameArea(getLocalBounds(), windowButton)))
    {
        const auto area = keyNameArea(getLocalBounds(), windowButton);
        const auto glow = std::max(0.3f, drumLastKeyGlow);
        g.setColour(keyInk.withAlpha(glow));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({static_cast<float>(area.getX()) + 4.0f,
                                                                       static_cast<float>(area.getCentreY())}));
        g.setFont(uiFontBold(8.5f));
        drawLine(g, "KEY " + DrumRackDevice::noteName(drumLastKey), area.withTrimmedLeft(10),
                 juce::Justification::centredLeft);
    }

    // ---- the map ---------------------------------------------------------------
    if (g.clipRegionIntersects(layout.map.expanded(3)))
    {
        const auto filled = device->filledPads();
        for (int note = 0; note < DrumRackDevice::padCount; ++note)
        {
            const auto i = static_cast<size_t>(note);
            const auto cell = mapCell(layout, note);
            const auto shown = note >= first && note < first + padsShown;
            auto ink = filled[i] ? juce::Colour(0xff8f969c) : shown ? juce::Colour(0xff3a4046) : juce::Colour(0xff2a2d31);
            if (note == chosen)
                ink = accent;
            g.setColour(ink);
            g.fillRect(cell);
            if (drumKeyFlash[i] > 0.0f)
            {
                g.setColour(keyInk.withAlpha(drumKeyFlash[i]));
                g.fillRect(cell.expanded(0.5f));
            }
            if (note == drumDropTarget)
            {
                g.setColour(palette::selection);
                g.drawRect(cell.expanded(1.0f), 1.0f);
            }
        }
        // The bank on the pads, framed.
        const auto top = mapCell(layout, first + padsShown - 1);
        const auto bottom = mapCell(layout, first);
        const auto frame = juce::Rectangle<float>(static_cast<float>(layout.map.getX()) - 1.5f, top.getY() - 1.5f,
                                                  static_cast<float>(layout.map.getWidth()) + 2.0f,
                                                  bottom.getBottom() - top.getY() + 3.0f);
        g.setColour(palette::text.withAlpha(0.85f));
        g.drawRect(frame, 1.0f);
    }
    if (g.clipRegionIntersects(layout.autoSelect))
        drawToggle(g, layout.autoSelect, "AUTO", keyInk, device->autoSelect(), true, 6.5f);

    // ---- the pads ------------------------------------------------------------
    for (int index = 0; index < padsShown; ++index)
    {
        const auto i = static_cast<size_t>(index);
        const auto note = first + index;
        const auto cell = layout.pad[i];
        // A flash repaints one pad, and the others are not worth drawing again.
        if (!g.clipRegionIntersects(cell.expanded(2)))
            continue;
        const auto view = device->pad(note);
        const auto filled = view.sound.has_value();
        const auto box = cell.toFloat();
        g.setColour(filled ? filledPadInk : emptyPadInk);
        g.fillRoundedRectangle(box, 2.5f);
        if (const auto flash = drumFlash[static_cast<size_t>(note)]; flash > 0.0f)
        {
            g.setColour(accent.withAlpha(0.5f * flash));
            g.fillRoundedRectangle(box, 2.5f);
        }
        if (note == drumDropTarget)
        {
            g.setColour(palette::selection);
            g.drawRoundedRectangle(box.reduced(1.0f), 2.5f, 2.0f);
        }
        else
        {
            g.setColour(note == chosen ? accent : palette::border);
            g.drawRoundedRectangle(box.reduced(0.5f), 2.5f, note == chosen ? 1.5f : 1.0f);
        }

        // The window's pads are big enough for larger names, and for the
        // note under a filled pad's name.
        const auto nameSize = layout.stacked ? 10.5f : 8.5f;
        const juce::Rectangle<int> nameBox(cell.getX() + 4, cell.getY() + 3, cell.getWidth() - 8, layout.stacked ? 16 : 13);
        if (filled)
        {
            const auto synth = view.sound->source == DrumRackEngine::Source::synth;
            g.setColour(view.unreadable ? missingInk : synth ? synthInk : sampleInk);
            g.fillEllipse(static_cast<float>(cell.getRight()) - 8.0f, static_cast<float>(cell.getY()) + 6.0f, 4.0f, 4.0f);
            g.setColour(view.unreadable ? palette::disabled : palette::text);
            g.setFont(uiFontBold(nameSize));
            drawLine(g, view.sound->displayName(), nameBox.withTrimmedRight(6), juce::Justification::centredLeft, true);
            if (layout.stacked && layout.mute[i].getY() >= nameBox.getBottom() + 12)
            {
                g.setColour(palette::textDim);
                g.setFont(uiFont(8.5f));
                drawLine(g, DrumRackDevice::noteName(note), nameBox.translated(0, nameBox.getHeight()).withHeight(12),
                         juce::Justification::centredLeft);
            }
        }
        else
        {
            g.setColour(palette::disabled);
            g.setFont(uiFont(nameSize));
            drawLine(g, DrumRackDevice::noteName(note), nameBox, juce::Justification::centredLeft);
        }
        drawToggle(g, layout.mute[i], "M", muteInk, view.muted, filled);
        drawToggle(g, layout.play[i], {}, accent, false, filled);
        drawToggle(g, layout.solo[i], "S", soloInk, view.soloed, filled);
    }

    // ---- the selected pad ------------------------------------------------------
    paintDrumSample(g);

    // ---- a pad in hand -------------------------------------------------------
    if (drumPadDragging && g.clipRegionIntersects(drumGhostArea().expanded(2)))
        if (const auto held = device->pad(drumDragPad); held.sound.has_value())
        {
            const auto ghost = drumGhostArea().toFloat();
            g.setColour(filledPadInk.withAlpha(0.9f));
            g.fillRoundedRectangle(ghost, 2.5f);
            g.setColour(accent);
            g.drawRoundedRectangle(ghost.reduced(0.5f), 2.5f, 1.0f);
            g.setColour(palette::text);
            g.setFont(uiFontBold(8.5f));
            drawLine(g, held.sound->displayName(), drumGhostArea().reduced(5, 0), juce::Justification::centredLeft, true);
        }
}

// ---- frames ------------------------------------------------------------------------

// Lights each pad as it is struck and each key on the map as it arrives, and
// lets the light go out. Both are read from counters the audio thread bumps,
// so the face asks for a repaint only where something actually happened.
void DeviceEditorPanel::tickDrums()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    device->collectSamples();
    // The other face on this rack, the rack's or its window's, may have picked
    // another pad. This one catches up, so its knobs never stand for a pad
    // other than the one they would turn.
    if (DrumRackDevice::parameterIndex(device->selectedPad(), 0) != drumParametersFrom)
    {
        readDrumParameters();
        styleDrumControls();
        repaintDrums();
    }
    const auto layout = layoutFor(getLocalBounds());
    const auto first = device->firstShownNote();
    auto keyArrived = false;
    // The first note to arrive this frame on a pad that holds a sound, for
    // Auto Select. An empty pad has nothing to select, though its key still
    // flashes on the map.
    auto played = -1;
    std::optional<std::array<bool, DrumRackDevice::padCount>> filled;
    for (int note = 0; note < DrumRackDevice::padCount; ++note)
    {
        const auto i = static_cast<size_t>(note);
        const auto shown = note >= first && note < first + padsShown;
        const auto strikes = device->padStrikes(note);
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
        if (flash != before && shown)
            repaint(layout.pad[static_cast<size_t>(note - first)]);

        const auto received = device->notesReceived(note);
        auto& key = drumKeyFlash[i];
        const auto keyBefore = key;
        if (received != drumNotesSeen[i])
        {
            drumNotesSeen[i] = received;
            key = 1.0f;
            drumLastKey = note;
            keyArrived = true;
            if (played < 0)
            {
                if (!filled.has_value())
                    filled = device->filledPads();
                if ((*filled)[i])
                    played = note;
            }
        }
        else if (key > 0.0f)
        {
            key = std::max(0.0f, key - keyFade);
        }
        if (key != keyBefore)
            repaint(mapCell(layout, note).expanded(1.0f).getSmallestIntegerContainer());
    }
    // Auto Select: the pad just played becomes the selected one, as in Live,
    // but never under a hand that is turning a knob or dragging on the face,
    // whose control would change pads beneath it.
    if (played >= 0 && played != device->selectedPad() && device->autoSelect() && !isMouseButtonDown(true))
    {
        const auto before = device->selectedPad();
        device->setSelectedPad(played);
        readDrumParameters();
        styleDrumControls();
        for (const auto note : {before, played})
        {
            if (note >= first && note < first + padsShown)
                repaint(layout.pad[static_cast<size_t>(note - first)].expanded(2));
            repaint(mapCell(layout, note).expanded(2.0f).getSmallestIntegerContainer());
        }
        repaint(layout.editor.expanded(gapAfterGrid / 2 + 1, 2));
    }

    const auto glowBefore = drumLastKeyGlow;
    drumLastKeyGlow = keyArrived ? 1.0f : std::max(0.0f, drumLastKeyGlow - keyNameFade);
    if (drumLastKeyGlow != glowBefore || keyArrived)
        repaint(keyNameArea(getLocalBounds(), openInWindow != nullptr));

    // The selected pad's playhead, while a voice plays its sample.
    const auto playhead = device->padPlayhead(device->selectedPad());
    if (std::abs(playhead - drumPlayheadDrawn) > 0.001f)
    {
        drumPlayheadDrawn = playhead;
        repaint(layout.picture);
    }
}
}
