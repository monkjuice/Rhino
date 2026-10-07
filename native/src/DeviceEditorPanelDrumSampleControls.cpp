#include "DeviceEditorPanelDrumsInternal.h"
#include "Theme.h"
#include <algorithm>
#include <array>
#include <cmath>

// The Drum Rack face's sample settings: what each knob of a sample pad's row
// is, how it reads and writes the pad's playback, and the knobs themselves.
// A knob or a marker dragged writes the playback as it goes without an undo
// step and then once more, as one step, when it is let go.
namespace rhino
{
namespace drumface
{
using Playback = DrumRackEngine::Playback;
using PlayMode = DrumRackEngine::PlayMode;

namespace
{
float valueOf(SampleKnob knob, const Playback& playback)
{
    switch (knob)
    {
        case SampleKnob::start:       return playback.start;
        case SampleKnob::end:         return playback.end;
        case SampleKnob::fadeIn:      return playback.fadeIn;
        case SampleKnob::fadeOut:     return playback.fadeOut;
        case SampleKnob::attack:      return playback.attack;
        case SampleKnob::sustain:     return playback.sustain;
        case SampleKnob::release:     return playback.release;
        case SampleKnob::sensitivity: return playback.sensitivity;
        case SampleKnob::divisions:   return std::log2(static_cast<float>(std::max(2, playback.divisions)));
        case SampleKnob::count:       break;
    }
    return 0.0f;
}

void applyTo(SampleKnob knob, float value, Playback& playback)
{
    switch (knob)
    {
        // The part is never less than a sliver, whichever end moves.
        case SampleKnob::start:       playback.start = std::min(value, playback.end - 0.001f); break;
        case SampleKnob::end:         playback.end = std::max(value, playback.start + 0.001f); break;
        case SampleKnob::fadeIn:      playback.fadeIn = value; break;
        case SampleKnob::fadeOut:     playback.fadeOut = value; break;
        case SampleKnob::attack:      playback.attack = value; break;
        case SampleKnob::sustain:     playback.sustain = value; break;
        case SampleKnob::release:     playback.release = value; break;
        case SampleKnob::sensitivity: playback.sensitivity = value; break;
        case SampleKnob::divisions:   playback.divisions = 1 << juce::jlimit(1, 6, juce::roundToInt(value)); break;
        case SampleKnob::count:       break;
    }
}
}

SampleKnobSpec specOf(SampleKnob knob)
{
    const Playback plain;
    switch (knob)
    {
        case SampleKnob::start:       return { "START", "Move the sample's start", 0.0f, 1.0f, plain.start, 0.0f, 0.0 };
        case SampleKnob::end:         return { "END", "Move the sample's end", 0.0f, 1.0f, plain.end, 0.0f, 0.0 };
        case SampleKnob::fadeIn:      return { "FADE IN", "Set the fade in", 0.0f, DrumRackEngine::longestFade, plain.fadeIn, 0.15f, 0.0 };
        case SampleKnob::fadeOut:     return { "FADE OUT", "Set the fade out", 0.0f, DrumRackEngine::longestFade, plain.fadeOut, 0.15f, 0.0 };
        case SampleKnob::attack:      return { "ATTACK", "Set the attack", 0.0f, DrumRackEngine::longestFade, plain.attack, 0.15f, 0.0 };
        case SampleKnob::sustain:     return { "SUSTAIN", "Set the sustain", 0.0f, 1.0f, plain.sustain, 0.0f, 0.0 };
        case SampleKnob::release:     return { "RELEASE", "Set the release", 0.0f, DrumRackEngine::longestRelease, plain.release, 0.5f, 0.0 };
        case SampleKnob::sensitivity: return { "SENS", "Set how finely Slice cuts", 0.0f, 1.0f, plain.sensitivity, 0.0f, 0.0 };
        case SampleKnob::divisions:   return { "SLICES", "Set how many slices", 1.0f, 6.0f, 3.0f, 0.0f, 1.0 };
        case SampleKnob::count:       break;
    }
    return { "", "", 0.0f, 1.0f, 0.0f, 0.0f, 0.0 };
}

juce::String textOf(SampleKnob knob, const Playback& playback, double fileSeconds)
{
    const auto value = valueOf(knob, playback);
    switch (knob)
    {
        case SampleKnob::start:
        case SampleKnob::end:         return secondsText(value * fileSeconds);
        case SampleKnob::fadeIn:
        case SampleKnob::fadeOut:
        case SampleKnob::attack:
        case SampleKnob::release:     return secondsText(value);
        case SampleKnob::sustain:     return value <= 0.0f ? juce::String("-inf dB")
                                                           : juce::String(20.0f * std::log10(value), 1) + " dB";
        case SampleKnob::sensitivity: return juce::String(juce::roundToInt(value * 100.0f)) + "%";
        case SampleKnob::divisions:   return juce::String(playback.divisions);
        case SampleKnob::count:       break;
    }
    return {};
}

// The cells a pad's sample row shows: Slice by divisions counts them rather
// than asking how finely to listen.
std::vector<SampleCell> cellsOf(const Playback& playback)
{
    auto cells = sampleCellsFor(playback.mode);
    if (playback.mode == PlayMode::slice && playback.sliceBy == DrumRackEngine::SliceBy::divisions)
        for (auto& cell : cells)
            if (cell.kind == SampleCell::knob && cell.setting == SampleKnob::sensitivity)
                cell.setting = SampleKnob::divisions;
    return cells;
}

// A sample pad the editor can work on: one that plays a file it could read.
bool playsSample(const DrumRackDevice::Pad& pad)
{
    return pad.sound.has_value() && pad.sound->source == DrumRackEngine::Source::sample && !pad.unreadable;
}

}

using namespace drumface;

// ---- the sample's knobs ------------------------------------------------------------

void DeviceEditorPanel::ensureDrumSampleControls()
{
    if (!drumSampleSliders.isEmpty())
        return;
    for (int index = 0; index < sampleKnobCount; ++index)
    {
        const auto knob = static_cast<SampleKnob>(index);
        auto* slider = drumSampleSliders.add(new juce::Slider());
        slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider->onDragStart = [this] { beginDrumPlaybackDrag(); };
        slider->onValueChange = [this, knob, slider]
        {
            if (syncing)
                return;
            auto* device = drumsIn(session, track, pluginSlot);
            if (device == nullptr)
                return;
            const auto view = device->pad(device->selectedPad());
            if (!playsSample(view))
                return;
            auto playback = view.sound->playback;
            applyTo(knob, static_cast<float>(slider->getValue()), playback);
            if (drumDragFrom.has_value())
                previewDrumPlayback(playback);
            else
                writeDrumPlayback(specOf(knob).action, playback);
        };
        slider->onDragEnd = [this, knob] { endDrumPlaybackDrag(specOf(knob).action); };
        addChildComponent(slider);
    }
}

void DeviceEditorPanel::styleDrumSampleControls()
{
    // Which knobs show is worked out first and each is shown or hidden once:
    // a knob turned elsewhere on the face restyles these every frame, and
    // hiding them all to show some again would repaint them every frame too.
    std::array<bool, sampleKnobCount> wanted {};
    const auto showWanted = [this, &wanted]
    {
        for (int index = 0; index < drumSampleSliders.size(); ++index)
            drumSampleSliders[index]->setVisible(wanted[static_cast<size_t>(index)]);
    };
    auto* device = drumsIn(session, track, pluginSlot);
    const auto view = device != nullptr && face == Face::DrumRack ? device->pad(device->selectedPad()) : DrumRackDevice::Pad {};
    if (!playsSample(view))
    {
        showWanted();
        return;
    }
    const auto& playback = view.sound->playback;
    const auto layout = layoutFor(getLocalBounds());
    const auto seconds = device->samplePicture(device->selectedPad(), layout.picture.reduced(3).getWidth()).seconds;
    const auto cells = cellsOf(playback);
    syncing = true;
    for (size_t place = 0; place < cells.size(); ++place)
    {
        const auto& cell = cells[place];
        if (cell.kind != SampleCell::knob)
            continue;
        const auto spec = specOf(cell.setting);
        auto* slider = drumSampleSliders[static_cast<int>(cell.setting)];
        slider->setBounds(knobIn(layout.cell[static_cast<size_t>(controlCells) + place]));
        slider->setRange(spec.minimum, spec.maximum, spec.interval);
        if (spec.midpoint > 0.0f)
            slider->setSkewFactorFromMidPoint(spec.midpoint);
        else
            slider->setSkewFactor(1.0);
        slider->setDoubleClickReturnValue(true, spec.defaultValue);
        slider->setValue(valueOf(cell.setting, playback), juce::dontSendNotification);
        const juce::String caption(spec.caption);
        slider->setTooltip(caption.substring(0, 1) + caption.substring(1).toLowerCase() + ": "
                           + textOf(cell.setting, playback, seconds));
        // The sample's own knobs wear the sample's ink, apart from the pad's.
        slider->setColour(juce::Slider::trackColourId, sampleInk);
        slider->setColour(juce::Slider::rotarySliderFillColourId, sampleInk);
        slider->setColour(juce::Slider::backgroundColourId, palette::control);
        slider->setColour(juce::Slider::rotarySliderOutlineColourId, palette::border);
        slider->setColour(juce::Slider::thumbColourId, sampleInk.brighter(0.28f));
        wanted[static_cast<size_t>(cell.setting)] = true;
    }
    syncing = false;
    showWanted();
}

void DeviceEditorPanel::writeDrumPlayback(const juce::String& actionName, const Playback& playback)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto note = device->selectedPad();
    const auto result = session.editDeviceSettings(track, pluginSlot, actionName,
                                                   [device, note, playback] { device->setPadPlayback(note, playback); });
    if (result.failed() && status)
        status(result.getErrorMessage());
}

void DeviceEditorPanel::previewDrumPlayback(const Playback& playback)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    device->setPadPlayback(device->selectedPad(), playback, false);
    repaint(layoutFor(getLocalBounds()).editor);
}

void DeviceEditorPanel::beginDrumPlaybackDrag()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    if (const auto view = device->pad(device->selectedPad()); playsSample(view))
        drumDragFrom = view.sound->playback;
}

void DeviceEditorPanel::endDrumPlaybackDrag(const juce::String& actionName)
{
    if (!drumDragFrom.has_value())
        return;
    const auto from = *drumDragFrom;
    drumDragFrom.reset();
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto note = device->selectedPad();
    const auto view = device->pad(note);
    if (!playsSample(view))
        return;
    const auto reached = view.sound->playback;
    // Back to where the drag began without a trace, then there in one step,
    // so a single undo takes the whole drag back.
    device->setPadPlayback(note, from, false);
    if (reached == from)
        return;
    writeDrumPlayback(actionName, reached);
}

void DeviceEditorPanel::showDrumSliceByMenu()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto view = device->pad(device->selectedPad());
    if (!playsSample(view))
        return;
    const auto by = view.sound->playback.sliceBy;
    juce::PopupMenu menu;
    menu.addSectionHeader("SLICE AT");
    menu.addItem(1, "Transients", true, by == DrumRackEngine::SliceBy::transients);
    menu.addItem(2, "Equal divisions", true, by == DrumRackEngine::SliceBy::divisions);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [safe = juce::Component::SafePointer<DeviceEditorPanel>(this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto* target = drumsIn(safe->session, safe->track, safe->pluginSlot);
            if (target == nullptr)
                return;
            const auto now = target->pad(target->selectedPad());
            if (!playsSample(now))
                return;
            auto playback = now.sound->playback;
            playback.sliceBy = result == 2 ? DrumRackEngine::SliceBy::divisions : DrumRackEngine::SliceBy::transients;
            safe->writeDrumPlayback(result == 2 ? "Slice into equal parts" : "Slice at transients", playback);
        });
}

void DeviceEditorPanel::spreadDrumSlices()
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return;
    const auto note = device->selectedPad();
    const auto view = device->pad(note);
    const auto name = view.sound.has_value() ? view.sound->displayName() : juce::String();
    auto spread = 0;
    const auto report = status;
    const auto result = session.spreadDrumSlices(track, pluginSlot, note, &spread);
    if (report == nullptr)
        return;
    if (result.failed())
        report(result.getErrorMessage());
    else
        report("Sliced " + name + " to " + juce::String(spread) + " pads, " + DrumRackDevice::noteName(note) + " to "
               + DrumRackDevice::noteName(note + spread - 1));
}
}
