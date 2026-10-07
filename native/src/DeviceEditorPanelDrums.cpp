#include "DeviceEditorPanelDrumsInternal.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>

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
// made of, DeviceEditorPanelDrumParts.cpp; the pad's menus and dialogs,
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

// Where the name bar names the last key to arrive.
juce::Rectangle<int> keyNameArea(juce::Rectangle<int> panel)
{
    return {panel.getRight() - 78, 4, 70, DeviceEditorPanel::headerHeight - 8};
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
        if (juce::isPositiveAndBelow(lit - first, padsShown))
            repaint(layout.pad[static_cast<size_t>(lit - first)].expanded(2));
    drumDropTarget = pad;
    repaint(layout.picture.expanded(1));
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

    // ---- the last key, in the name bar -------------------------------------------
    if (drumLastKey >= 0 && g.clipRegionIntersects(keyNameArea(getLocalBounds())))
    {
        const auto area = keyNameArea(getLocalBounds());
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
        }
        // The bank on the pads, framed.
        const auto top = mapCell(layout, first + padsShown - 1);
        const auto bottom = mapCell(layout, first);
        const auto frame = juce::Rectangle<float>(static_cast<float>(layout.map.getX()) - 1.5f, top.getY() - 1.5f,
                                                  static_cast<float>(mapWidth) + 2.0f, bottom.getBottom() - top.getY() + 3.0f);
        g.setColour(palette::text.withAlpha(0.85f));
        g.drawRect(frame, 1.0f);
    }

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

        const juce::Rectangle<int> nameBox(cell.getX() + 4, cell.getY() + 3, cell.getWidth() - 8, 13);
        if (filled)
        {
            const auto synth = view.sound->source == DrumRackEngine::Source::synth;
            g.setColour(view.unreadable ? missingInk : synth ? synthInk : sampleInk);
            g.fillEllipse(static_cast<float>(cell.getRight()) - 8.0f, static_cast<float>(cell.getY()) + 6.0f, 4.0f, 4.0f);
            g.setColour(view.unreadable ? palette::disabled : palette::text);
            g.setFont(uiFontBold(8.5f));
            drawLine(g, view.sound->displayName(), nameBox.withTrimmedRight(6), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(palette::disabled);
            g.setFont(uiFont(8.5f));
            drawLine(g, DrumRackDevice::noteName(note), nameBox, juce::Justification::centredLeft);
        }
        drawToggle(g, layout.mute[i], "M", muteInk, view.muted, filled);
        drawToggle(g, layout.play[i], {}, accent, false, filled);
        drawToggle(g, layout.solo[i], "S", soloInk, view.soloed, filled);
    }

    // ---- the selected pad ------------------------------------------------------
    paintDrumSample(g);
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
    if (event.eventComponent != this)
        return handleDrumSampleMouseDown(event);
    if (event.y < headerHeight)
        return false;

    const auto position = event.getPosition();
    const auto layout = layoutFor(getLocalBounds());
    const auto chosen = device->selectedPad();
    for (int i = 0; i < controlCells; ++i)
        if (automationIn(layout.cell[static_cast<size_t>(i)]).contains(position))
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

    // The map: the bank shown follows the pointer, the row under it the
    // second of the four.
    if (const auto note = mapNoteAt(layout, position); note >= 0)
    {
        drumDrag = DrumDrag::map;
        showDrumBank((note / padColumns - 1) * padColumns);
        return true;
    }

    const auto first = device->firstShownNote();
    for (int index = 0; index < padsShown; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        const auto note = first + index;
        // Picking a pad is a view of the rack, not an edit to it.
        if (note != chosen)
        {
            device->setSelectedPad(note);
            readDrumParameters();
            styleDrumControls();
            repaint();
        }
        if (event.mods.isPopupMenu())
        {
            showDrumPadMenu(note);
            return true;
        }
        const auto view = device->pad(note);
        const auto label = view.sound.has_value() ? view.sound->displayName() : padTitle(note);
        if (layout.play[i].contains(position))
        {
            device->previewPad(note);
            return true;
        }
        if (view.sound.has_value() && layout.mute[i].contains(position))
        {
            const auto muted = !view.muted;
            session.editDeviceSettings(track, pluginSlot, muted ? "Mute pad" : "Unmute pad",
                                       [device, note, muted] { device->setPadMuted(note, muted); });
            if (status) status(label + (muted ? " muted" : " unmuted"));
            return true;
        }
        if (view.sound.has_value() && layout.solo[i].contains(position))
        {
            const auto soloed = !view.soloed;
            session.editDeviceSettings(track, pluginSlot, soloed ? "Solo pad" : "Unsolo pad",
                                       [device, note, soloed] { device->setPadSoloed(note, soloed); });
            if (status) status(label + (soloed ? " soloed" : " no longer soloed"));
            return true;
        }
        if (status) status(label + " on " + DrumRackDevice::noteName(note));
        return true;
    }
    return handleDrumSampleMouseDown(event);
}

void DeviceEditorPanel::handleDrumDrag(const juce::MouseEvent& event)
{
    if (drumDrag == DrumDrag::map)
    {
        const auto layout = layoutFor(getLocalBounds());
        const auto row = (layout.map.getBottom() - 1 - event.y) / mapCellHeight;
        showDrumBank((juce::jlimit(0, mapRows - 1, row) - 1) * padColumns);
        return;
    }
    if (drumDrag != DrumDrag::start && drumDrag != DrumDrag::end)
        return;
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(device->selectedPad());
    if (!view.sound.has_value())
        return;
    const auto wave = layoutFor(getLocalBounds()).picture.reduced(3);
    const auto place = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.x - wave.getX()) / static_cast<float>(wave.getWidth()));
    auto playback = view.sound->playback;
    if (drumDrag == DrumDrag::start)
        playback.start = std::min(place, playback.end - 0.001f);
    else
        playback.end = std::max(place, playback.start + 0.001f);
    previewDrumPlayback(playback);
}

void DeviceEditorPanel::handleDrumMouseUp(const juce::MouseEvent&)
{
    const auto dragged = drumDrag;
    drumDrag = DrumDrag::none;
    if (dragged == DrumDrag::start)
        endDrumPlaybackDrag("Move the sample's start");
    else if (dragged == DrumDrag::end)
        endDrumPlaybackDrag("Move the sample's end");
}

bool DeviceEditorPanel::handleDrumWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr || wheel.deltaY == 0.0f)
        return false;
    // The wheel over the map or the pads moves a row of four at a time, up
    // the notes as it turns away.
    const auto layout = layoutFor(getLocalBounds());
    const auto grid = layout.pad[0].getUnion(layout.pad[padsShown - 1]);
    if (!layout.map.expanded(4).contains(event.getPosition()) && !grid.contains(event.getPosition()))
        return false;
    showDrumBank(device->firstShownNote() + (wheel.deltaY > 0.0f ? padColumns : -padColumns));
    return true;
}

juce::String DeviceEditorPanel::drumTooltip(juce::Point<int> position) const
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return {};
    const auto layout = layoutFor(getLocalBounds());
    const auto first = device->firstShownNote();
    if (const auto note = mapNoteAt(layout, position); note >= 0)
        return "All 128 notes, four to a row. The framed ones, " + DrumRackDevice::noteName(first) + " to "
               + DrumRackDevice::noteName(first + padsShown - 1)
               + ", are on the pads; click or drag to show others. Lit notes hold a sound, and a key flashes orange "
                 "as it is played.";
    for (int index = 0; index < padsShown; ++index)
    {
        const auto i = static_cast<size_t>(index);
        if (!layout.pad[i].contains(position))
            continue;
        const auto note = first + index;
        const auto view = device->pad(note);
        if (layout.play[i].contains(position))
            return "Play this pad";
        if (layout.mute[i].contains(position))
            return "Mute this pad";
        if (layout.solo[i].contains(position))
            return "Solo this pad: while any pad is soloed, only soloed pads sound";
        if (!view.sound.has_value())
            return padTitle(note) + " is empty. Drop a sample or a drum preset on it, or right-click for a synth.";
        return view.sound->displayName() + " on " + DrumRackDevice::noteName(note)
               + ". Drop a sound here to replace it; right-click for its synth, choke group and more.";
    }
    return drumSampleTooltip(position);
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
    const auto layout = layoutFor(getLocalBounds());
    const auto first = device->firstShownNote();
    auto keyArrived = false;
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
        }
        else if (key > 0.0f)
        {
            key = std::max(0.0f, key - keyFade);
        }
        if (key != keyBefore)
            repaint(mapCell(layout, note).expanded(1.0f).getSmallestIntegerContainer());
    }
    const auto glowBefore = drumLastKeyGlow;
    drumLastKeyGlow = keyArrived ? 1.0f : std::max(0.0f, drumLastKeyGlow - keyNameFade);
    if (drumLastKeyGlow != glowBefore || keyArrived)
        repaint(keyNameArea(getLocalBounds()));

    // The selected pad's playhead, while a voice plays its sample.
    const auto playhead = device->padPlayhead(device->selectedPad());
    if (std::abs(playhead - drumPlayheadDrawn) > 0.001f)
    {
        drumPlayheadDrawn = playhead;
        repaint(layout.picture);
    }
}
}
