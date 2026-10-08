#include "DeviceEditorPanelDrumsInternal.h"
#include "ContentLibrary.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>

// The Drum Rack's face, right side: the selected pad. Its name and note, what
// it plays and its choke group across the top; beside the picture, the three
// ways a sample plays, Classic, 1-Shot and Slice, stacked as Live stacks them;
// and under both a row of knobs, the pad's six controls and then its sample's
// own settings for the mode it is in.
//
// The picture is the sample itself, the whole file, with the part a strike
// plays lit between its start and end markers, the shape the pad's fades,
// Decay and envelope give it drawn over the top, the cuts Slice would make,
// and a playhead while a voice plays it. A synth pad shows one strike instead,
// played by a voice of its own exactly as the pad sounds.
//
// A sample's settings are not automatable controls -- 128 pads of them would be
// thousands -- but content of the pad, written through the undo manager. A
// knob or a marker dragged writes them as it goes without an undo step and
// then once more, as one step, when it is let go.
namespace rhino
{
using namespace drumface;

namespace
{
using Playback = DrumRackEngine::Playback;
using PlayMode = DrumRackEngine::PlayMode;

// How near a marker a press must be to take hold of it.
constexpr int markerReach = 4;

const char* modeName(PlayMode mode)
{
    return mode == PlayMode::classic ? "classic" : mode == PlayMode::slice ? "slice" : "one-shot";
}

const char* const modeLabels[] { "CLASSIC", "1-SHOT", "SLICE" };

// How loud a strike is at a place in the file, drawn over the picture: the
// same fades, Decay and envelope the voice applies, as a closed form.
float shapeAt(const Playback& playback, const DrumRackEngine::PadSettings& settings, double fileSeconds, float place)
{
    const auto ratio = std::pow(2.0, settings.tune / 12.0);
    const auto into = (place - playback.start) * fileSeconds / ratio;
    const auto left = (playback.end - place) * fileSeconds / ratio;
    const auto decaying = settings.decay < DrumRackEngine::fullDecay;
    const auto fall = decaying ? std::exp(-6.907755 * into / std::max(0.001, double(settings.decay))) : 1.0;
    if (playback.mode == PlayMode::classic)
    {
        const auto rise = playback.attack > 0.0f ? std::min(1.0, into / playback.attack) : 1.0;
        const auto held = decaying ? playback.sustain + (1.0 - playback.sustain) * fall : 1.0;
        return static_cast<float>(rise * held);
    }
    const auto rise = playback.fadeIn > 0.0f ? std::min(1.0, into / playback.fadeIn) : 1.0;
    const auto out = playback.fadeOut > 0.0f ? std::clamp(left / playback.fadeOut, 0.0, 1.0) : 1.0;
    return static_cast<float>(rise * fall * out);
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

const char* const knobCaptions[controlCells] { "TUNE", "DECAY", "TONE", "VEL", "LEVEL", "PAN" };
}

// ---- painting --------------------------------------------------------------------

void DeviceEditorPanel::paintDrumSample(juce::Graphics& g)
{
    auto* device = drumsIn(session, track, pluginSlot);
    const auto layout = layoutFor(getLocalBounds());
    if (device == nullptr || !g.clipRegionIntersects(layout.editor.getUnion(layout.separator)))
        return;
    const auto accent = faceAccent();
    const auto chosen = device->selectedPad();
    g.setColour(palette::border);
    g.fillRect(layout.separator);

    const auto view = device->pad(chosen);
    const auto filled = view.sound.has_value();
    const auto sampled = playsSample(view);

    // ---- the heading -------------------------------------------------------------
    g.setFont(uiFontBold(10.0f));
    const auto heading = filled ? view.sound->displayName() : "Pad " + DrumRackDevice::noteName(chosen);
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

    // ---- the modes -----------------------------------------------------------------
    const auto playback = sampled ? view.sound->playback : Playback {};
    for (int button = 0; button < 3; ++button)
        drawToggle(g, layout.mode[static_cast<size_t>(button)], modeLabels[button], sampleInk,
                   sampled && playback.mode == modeOfButton(button), sampled);

    // ---- the picture ---------------------------------------------------------------
    const auto well = layout.picture.toFloat();
    g.setColour(wellInk);
    g.fillRoundedRectangle(well, 3.0f);
    g.setColour(chosen == drumDropTarget ? palette::selection : palette::border);
    g.drawRoundedRectangle(well.reduced(0.5f), 3.0f, 1.0f);
    const auto inner = layout.picture.reduced(3);
    auto partSeconds = 0.0;
    if (!filled || view.unreadable)
    {
        g.setColour(view.unreadable ? missingInk : palette::textDim);
        g.setFont(uiFont(9.0f));
        const auto missing = filled ? ContentLibrary::resolveStoredPath(view.sound->sample).getFileName() : juce::String();
        drawLine(g, view.unreadable ? missing + " is missing or cannot be read"
                                    : "Drop a sample or a drum preset here, or click to choose a sample",
                 inner, juce::Justification::centred, true);
    }
    else if (!sampled)
    {
        // A synth: one strike as the pad plays it now, from the engine's own
        // voice, so the picture moves with Tune, Decay and Tone as the sound does.
        const auto& picture = device->padPicture(chosen, inner.getWidth());
        const auto middle = static_cast<float>(inner.getCentreY());
        const auto reach = static_cast<float>(inner.getHeight()) * 0.5f;
        // Scaled to its own peak, as a sampler draws its file: the shape is
        // what the picture is for, and Level reads below.
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
        partSeconds = picture.seconds;
    }
    else
    {
        // The whole file, with the part a strike plays lit.
        const auto& wave = device->samplePicture(chosen, inner.getWidth());
        const auto middle = static_cast<float>(inner.getCentreY());
        const auto reach = static_cast<float>(inner.getHeight()) * 0.5f - 1.0f;
        const auto scale = wave.peak > 1.0e-6f ? reach / wave.peak : 0.0f;
        const auto xOf = [&inner] (float place)
        {
            return static_cast<float>(inner.getX()) + place * static_cast<float>(inner.getWidth());
        };
        const auto startX = xOf(playback.start), endX = xOf(playback.end);
        // Outside the part first, then inside it, so each ink is set once.
        const auto columns = static_cast<int>(wave.highs.size());
        for (const auto inPart : { false, true })
        {
            g.setColour(inPart ? sampleInk.withAlpha(0.8f) : palette::textDim.withAlpha(0.3f));
            for (int column = 0; column < columns; ++column)
            {
                const auto x = inner.getX() + column;
                const auto centre = static_cast<float>(x) + 0.5f;
                if ((centre >= startX && centre <= endX) != inPart)
                    continue;
                const auto top = middle - wave.highs[static_cast<size_t>(column)] * scale;
                const auto bottom = middle - wave.lows[static_cast<size_t>(column)] * scale;
                g.drawVerticalLine(x, top, std::max(top + 1.0f, bottom));
            }
        }

        // The cuts Slice makes, numbered where there is room.
        if (playback.mode == PlayMode::slice)
        {
            const auto& starts = device->padSlices(chosen);
            g.setFont(uiFont(7.0f));
            for (size_t index = 0; index < starts.size(); ++index)
            {
                const auto x = xOf(starts[index]);
                g.setColour(palette::text.withAlpha(0.45f));
                if (index > 0)
                    g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(inner.getY()),
                                       static_cast<float>(inner.getBottom()));
                const auto next = index + 1 < starts.size() ? xOf(starts[index + 1]) : endX;
                if (next - x >= 12.0f)
                {
                    g.setColour(palette::text.withAlpha(0.8f));
                    drawLine(g, juce::String(static_cast<int>(index) + 1),
                             {juce::roundToInt(x) + 2, inner.getY(), 14, 9}, juce::Justification::centredLeft);
                }
            }
        }

        // The shape the pad gives the part, over it.
        const auto settings = view.sound->settings;
        juce::Path shape;
        // A path holding only its starting point still counts as empty, so
        // whether it has begun is kept apart.
        auto begun = false;
        for (auto x = startX; x <= endX; x += 2.0f)
        {
            const auto place = (x - static_cast<float>(inner.getX())) / static_cast<float>(inner.getWidth());
            const auto level = shapeAt(playback, settings, wave.seconds, std::min(place, playback.end));
            const auto y = static_cast<float>(inner.getBottom()) - level * static_cast<float>(inner.getHeight() - 2);
            if (begun)
                shape.lineTo(x, y);
            else
                shape.startNewSubPath(x, y);
            begun = true;
        }
        g.setColour(palette::text.withAlpha(0.8f));
        g.strokePath(shape, juce::PathStrokeType(1.25f));

        // The start and end markers, each with a flag pointing into the part.
        g.setColour(palette::text);
        for (const auto [x, inward] : { std::pair { startX, 1.0f }, std::pair { endX, -1.0f } })
        {
            g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(inner.getY()), static_cast<float>(inner.getBottom()));
            juce::Path flag;
            const auto top = static_cast<float>(inner.getY());
            flag.addTriangle(x, top, x + 6.0f * inward, top, x, top + 6.0f);
            g.fillPath(flag);
        }
        if (playback.mode == PlayMode::classic)
            drawToggle(g, layout.loop, "LOOP", sampleInk, playback.loop, true);

        // Where the latest voice on the pad is, while one plays it.
        if (const auto playhead = device->padPlayhead(chosen); playhead >= 0.0f)
        {
            g.setColour(palette::text.withAlpha(0.9f));
            g.drawVerticalLine(juce::roundToInt(xOf(playhead)), static_cast<float>(inner.getY()),
                               static_cast<float>(inner.getBottom()));
        }
        partSeconds = (playback.end - playback.start) * wave.seconds;
        if (playback.mode == PlayMode::slice)
        {
            g.setColour(palette::textDim);
            g.setFont(uiFont(8.0f));
            const auto count = static_cast<int>(device->padSlices(chosen).size());
            drawLine(g, juce::String(count) + (count == 1 ? " slice" : " slices"),
                     inner.withTrimmedTop(inner.getHeight() - 11), juce::Justification::centredLeft);
        }
    }
    if (partSeconds > 0.0)
    {
        g.setColour(palette::textDim);
        g.setFont(uiFont(8.0f));
        drawLine(g, secondsText(partSeconds), inner.withTrimmedTop(inner.getHeight() - 11),
                 juce::Justification::centredRight);
    }

    // ---- the pad's controls: captions, readings and lanes ----------------------------
    for (int index = 0; index < controlCells; ++index)
    {
        const auto cell = layout.cell[static_cast<size_t>(index)];
        const auto control = DrumRackDevice::parameterIndex(chosen, index);
        if (!juce::isPositiveAndBelow(control, static_cast<int>(parameters.size())))
            continue;
        const auto& parameter = parameters[static_cast<size_t>(control)];
        g.setColour(filled ? palette::textDim : palette::disabled);
        g.setFont(uiFont(8.0f));
        drawLine(g, knobCaptions[index], captionIn(cell), juce::Justification::centred);
        g.setColour(filled ? palette::text : palette::disabled);
        g.setFont(uiFont(8.5f));
        drawLine(g, filled ? parameter.valueText : juce::String("--"), valueIn(cell), juce::Justification::centred);
        // The badge shows only on a control with a lane, and it is the one
        // way back to the lane once the knob has been taken over by hand.
        if (!parameter.automated)
            continue;
        const auto badge = automationIn(cell);
        g.setColour(parameter.automationOverridden ? overriddenInk : lanedInk);
        g.fillRoundedRectangle(badge.toFloat(), 2.0f);
        g.setColour(palette::text);
        g.setFont(uiFontBold(8.0f));
        drawLine(g, "A", badge, juce::Justification::centred);
    }

    // ---- the sample's own ------------------------------------------------------------
    g.setColour(palette::border);
    g.fillRect(layout.divider.getCentreX(), layout.divider.getY() + 4, 1, layout.divider.getHeight() - 8);
    if (!sampled)
        return;
    const auto fileSeconds = device->samplePicture(chosen, inner.getWidth()).seconds;
    const auto cells = cellsOf(playback);
    for (size_t place = 0; place < cells.size(); ++place)
    {
        const auto cell = layout.cell[static_cast<size_t>(controlCells) + place];
        const auto& kind = cells[place];
        g.setColour(palette::textDim);
        g.setFont(uiFont(8.0f));
        if (kind.kind == SampleCell::knob)
        {
            drawLine(g, specOf(kind.setting).caption, captionIn(cell), juce::Justification::centred);
            g.setColour(palette::text);
            g.setFont(uiFont(8.5f));
            drawLine(g, textOf(kind.setting, playback, fileSeconds), valueIn(cell), juce::Justification::centred);
        }
        else if (kind.kind == SampleCell::sliceBy)
        {
            drawLine(g, "SLICE AT", captionIn(cell), juce::Justification::centred);
            drawChooser(g, knobIn(cell).withSizeKeepingCentre(cell.getWidth() - 4, 16),
                        playback.sliceBy == DrumRackEngine::SliceBy::divisions ? "Parts" : "Hits", true);
        }
        else
        {
            drawLine(g, "SPREAD", captionIn(cell), juce::Justification::centred);
            // A button, not a switch: it does something once, so it is drawn
            // raised in the sample's ink rather than lit.
            const auto button = knobIn(cell).withSizeKeepingCentre(cell.getWidth() - 6, 18);
            g.setColour(palette::control);
            g.fillRoundedRectangle(button.toFloat(), 2.0f);
            g.setColour(sampleInk.withAlpha(0.8f));
            g.drawRoundedRectangle(button.toFloat().reduced(0.5f), 2.0f, 1.0f);
            g.setColour(palette::text);
            g.setFont(uiFontBold(7.0f));
            drawLine(g, "TO PADS", button, juce::Justification::centred);
            g.setColour(palette::textDim);
            g.setFont(uiFont(8.0f));
            drawLine(g, "from " + DrumRackDevice::noteName(chosen), valueIn(cell), juce::Justification::centred);
        }
    }
}

// ---- the hand --------------------------------------------------------------------

bool DeviceEditorPanel::handleDrumSampleMouseDown(const juce::MouseEvent& event)
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr || event.eventComponent != this)
        return false;
    const auto position = event.getPosition();
    const auto layout = layoutFor(getLocalBounds());
    const auto chosen = device->selectedPad();
    const auto view = device->pad(chosen);
    const auto filled = view.sound.has_value();
    const auto sampled = playsSample(view);

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
    for (int button = 0; button < 3; ++button)
        if (layout.mode[static_cast<size_t>(button)].contains(position))
        {
            if (sampled && view.sound->playback.mode != modeOfButton(button))
            {
                auto playback = view.sound->playback;
                playback.mode = modeOfButton(button);
                writeDrumPlayback("Play " + view.sound->displayName() + " as " + modeName(playback.mode), playback);
                if (status) status(view.sound->displayName() + " plays " + modeName(playback.mode));
            }
            return true;
        }
    if (sampled && view.sound->playback.mode == PlayMode::classic && layout.loop.contains(position))
    {
        auto playback = view.sound->playback;
        playback.loop = !playback.loop;
        writeDrumPlayback(playback.loop ? "Loop the sample" : "Stop looping the sample", playback);
        return true;
    }
    if (layout.picture.contains(position))
    {
        if (!filled || view.unreadable)
        {
            chooseDrumSample(chosen);
            return true;
        }
        if (!sampled)
        {
            device->previewPad(chosen);
            return true;
        }
        const auto inner = layout.picture.reduced(3);
        const auto xOf = [&inner] (float place)
        {
            return static_cast<float>(inner.getX()) + place * static_cast<float>(inner.getWidth());
        };
        const auto& playback = view.sound->playback;
        const auto x = static_cast<float>(position.x);
        if (std::abs(x - xOf(playback.start)) <= markerReach || std::abs(x - xOf(playback.end)) <= markerReach)
        {
            // The nearer marker, so the two can be told apart when close.
            drumDrag = std::abs(x - xOf(playback.start)) <= std::abs(x - xOf(playback.end)) ? DrumDrag::start
                                                                                            : DrumDrag::end;
            beginDrumPlaybackDrag();
            return true;
        }
        // A click elsewhere plays it: in Slice, the slice under the pointer.
        if (playback.mode == PlayMode::slice)
        {
            const auto place = (x - static_cast<float>(inner.getX())) / static_cast<float>(inner.getWidth());
            const auto& starts = device->padSlices(chosen);
            for (size_t index = 0; index < starts.size(); ++index)
            {
                const auto end = index + 1 < starts.size() ? starts[index + 1] : playback.end;
                if (place >= starts[index] && place < end)
                {
                    device->previewPart(chosen, starts[index], end);
                    return true;
                }
            }
        }
        device->previewPad(chosen);
        return true;
    }
    if (!sampled)
        return false;
    const auto cells = cellsOf(view.sound->playback);
    for (size_t place = 0; place < cells.size(); ++place)
    {
        if (!layout.cell[static_cast<size_t>(controlCells) + place].contains(position))
            continue;
        if (cells[place].kind == SampleCell::sliceBy)
            showDrumSliceByMenu();
        else if (cells[place].kind == SampleCell::spread)
            spreadDrumSlices();
        return true;
    }
    return false;
}

juce::String DeviceEditorPanel::drumSampleTooltip(juce::Point<int> position) const
{
    auto* device = drumsIn(session, track, pluginSlot);
    if (device == nullptr)
        return {};
    const auto layout = layoutFor(getLocalBounds());
    const auto view = device->pad(device->selectedPad());
    const auto sampled = playsSample(view);
    if (layout.soundChooser.contains(position))
        return "What this pad plays: a sample, or one of Rhino's synthesised drums";
    if (layout.chokeChooser.contains(position))
        return "Pads in one choke group cut each other off, the way a closed hat stops an open one";
    const char* const modeTips[] {
        "Classic: the sample follows the key, rising by Attack, falling by Decay to Sustain while it is held, "
        "and by Release once it is let go. Loop repeats the part while it sounds",
        "1-Shot: the sample plays from start to end whatever the key does, as a drum does",
        "Slice: cut the sample at its hits or into equal parts, click a slice to hear it, and spread the slices "
        "across pads of their own",
    };
    for (int button = 0; button < 3; ++button)
        if (layout.mode[static_cast<size_t>(button)].contains(position))
            return sampled ? modeTips[button] : "A sample pad plays in one of three ways";
    if (sampled && view.sound->playback.mode == PlayMode::classic && layout.loop.contains(position))
        return "Loop the part while the sound lasts";
    if (layout.picture.contains(position))
    {
        if (!view.sound.has_value())
            return "Drop a sample or a drum preset here, or click to choose a sample";
        if (!sampled)
            return "One strike of this pad, as it plays now. Click to hear it";
        return view.sound->playback.mode == PlayMode::slice
            ? "The sample, cut where Slice cuts it. Drag the start and end markers; click a slice to hear it"
            : "The sample, the part a strike plays lit. Drag the start and end markers; click to hear it";
    }
    if (sampled)
    {
        const auto cells = cellsOf(view.sound->playback);
        for (size_t place = 0; place < cells.size(); ++place)
            if (layout.cell[static_cast<size_t>(controlCells) + place].contains(position))
            {
                if (cells[place].kind == SampleCell::sliceBy)
                    return "Cut at the sample's hits (transients) or into equal parts";
                if (cells[place].kind == SampleCell::spread)
                    return "Put each slice on a pad of its own, from this pad up, as a one-shot of its part. "
                           "Replaces what those pads hold; one undo takes it back";
            }
    }
    return {};
}
}
