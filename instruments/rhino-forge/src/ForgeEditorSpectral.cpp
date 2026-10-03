// The spectral oscillator's face: choosing the sample, and drawing the
// spectrogram the oscillator is reading.
#include "ForgeEditorInternal.h"
#include "../ui/ForgeTooltips.h"

namespace rhino::forge
{
namespace
{
// How far below the loudest bin in the sample is still drawn. Seventy-two
// decibels is twelve octaves of level, which puts the noise floor of an
// ordinary recording just off the bottom of the picture rather than filling it
// with a grey wash.
constexpr float spectrogramFloorDb = -72.0f;

// Where a row of the picture sits in the spectrum. Logarithmic, because an
// octave is an octave: a linear map gives the top octave half the height and
// squeezes everything a sample is actually made of into the bottom few pixels.
int spectrogramBinFor(float t)
{
    const auto top = static_cast<float>(spectralBins - 1);
    return juce::jlimit(1, spectralBins - 1, juce::roundToInt(std::pow(top, t)));
}

juce::Image buildSpectrogram(const Sample& sample, int width, int height, juce::Colour accent)
{
    juce::Image image(juce::Image::ARGB, juce::jmax(1, width), juce::jmax(1, height), true);
    if (sample.isEmpty() || width <= 0 || height <= 0) return image;

    // The loudest bin anywhere in the sample, so the picture is normalised
    // against the sample rather than against whatever happens to be in frame.
    auto loudest = 1.0e-9f;
    for (int frame = 0; frame < sample.frameCount(); ++frame)
    {
        const auto* mags = sample.magnitudes(frame);
        for (int bin = 1; bin < spectralBins; ++bin) loudest = juce::jmax(loudest, mags[bin]);
    }

    juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
    const auto frames = sample.frameCount();
    for (int x = 0; x < width; ++x)
    {
        const auto frame = juce::jlimit(0, frames - 1,
                                        frames * x / juce::jmax(1, width));
        const auto* mags = sample.magnitudes(frame);
        for (int y = 0; y < height; ++y)
        {
            // The band this row covers, read as its loudest bin rather than its
            // average: a partial one bin wide is the thing worth seeing, and
            // averaging it against its neighbours is what makes a spectrogram
            // look like fog.
            const auto low = spectrogramBinFor(static_cast<float>(height - 1 - y)
                                               / static_cast<float>(juce::jmax(1, height - 1)));
            const auto high = spectrogramBinFor(static_cast<float>(height - y)
                                                / static_cast<float>(juce::jmax(1, height - 1)));
            auto magnitude = 0.0f;
            for (int bin = low; bin <= juce::jmax(low, high) && bin < spectralBins; ++bin)
                magnitude = juce::jmax(magnitude, mags[bin]);

            const auto db = juce::Decibels::gainToDecibels(magnitude / loudest, spectrogramFloorDb);
            const auto level = juce::jlimit(0.0f, 1.0f,
                                            (db - spectrogramFloorDb) / -spectrogramFloorDb);
            if (level <= 0.01f) continue;
            // Brightened as well as faded, so a loud partial reads as hot
            // rather than merely opaque — which is what the green in the
            // manual's figures is doing (p. 106).
            pixels.setPixelColour(x, y, accent.withMultipliedBrightness(0.6f + 0.7f * level)
                                              .withAlpha(level));
        }
    }
    return image;
}
}

bool Editor::oscillatorIsSpectral(int oscillator) const
{
    if (oscillator < 0 || oscillator >= oscillatorCount) return false;
    return oscModeOf(value(juce::String(oscillatorPrefix(oscillator)) + "Mode")) == OscMode::spectral;
}

// The picture, kept rather than redrawn. The panel repaints whole at 24 Hz, so
// anything worked out inside paint() is worked out twenty-four times a second
// whether or not it has changed — and a spectrogram is a pass over every bin of
// every frame. It is rebuilt when the sample changes or the module is resized,
// and blitted on every other frame.
const juce::Image& Editor::spectrogramFor(int oscillator, juce::Rectangle<int> area,
                                          juce::Colour accent)
{
    const auto index = static_cast<size_t>(juce::jlimit(0, oscillatorCount - 1, oscillator));
    const auto revision = processor.sampleStore().revision(oscillator);
    if (spectrogramRevision[index] != revision || spectrogramArea[index] != area)
    {
        spectrogramRevision[index] = revision;
        spectrogramArea[index] = area;
        // Read on the message thread, which is also the only thread that
        // publishes one, so what is read here cannot be freed underneath it.
        const auto* sample = processor.sampleStore().sample(oscillator);
        spectrogramCache[index] = sample != nullptr
            ? buildSpectrogram(*sample, area.getWidth(), area.getHeight(), accent)
            : juce::Image();
    }
    return spectrogramCache[index];
}

// What a spectral oscillator is reading.
//
// Not drawn anywhere. A sample's filename is the one piece of text on this
// panel with no bound on its length — "MAN-1-3126333-0252252 PHONK X TOKYO..."
// is a real one — and there is nowhere to put it: in the header it ran under
// the title, and over the spectrogram it would cover the picture it was
// describing. So it is a tooltip, and the picture answers for itself when the
// hand rests on it.
juce::String Editor::spectralHeaderDetail(int oscillator) const
{
    const auto name = processor.sampleStore().sourceName(oscillator);
    return name.isNotEmpty() ? name : juce::String("No sample — click to load one");
}

// Which oscillator's spectral display is under this point, if any. Used by the
// tooltip, by the click that opens the sample menu and by the markers, so none
// of them can disagree about where the picture is. The plot rather than the
// whole display: the loop strip along its foot is a control, and a click beside
// the field is not a click on the sample.
int Editor::spectralDisplayAt(juce::Point<int> at) const
{
    for (const auto& module : moduleUis)
    {
        if (module.descriptor->display != ui::Display::oscillator) continue;
        if (!moduleShown(*module.descriptor)) continue;
        const auto which = oscillatorIndexFromId(juce::String(module.descriptor->id));
        if (which < 0 || !oscillatorIsSpectral(which)) continue;
        if (spectralPlotFor(*module.descriptor).contains(at)) return which;
    }
    return -1;
}

juce::Rectangle<int> Editor::spectralPlotFor(const ui::Module& module) const
{
    return ui::displayPlotBounds(moduleAreaFor(module), module, static_cast<int>(OscMode::spectral))
        .reduced(2);
}

// --- The loop field -----------------------------------------------------------
//
// Driven the way MODE is (see ForgeEditorOscMode.cpp), for the same reason: the
// parameter stores Serum's whole list and the field offers what is built, so
// stepping it with the arrows never lands on TAILED.

bool Editor::isLoopModeControl(const juce::String& id)
{
    return oscillatorIndexFromId(id) >= 0 && id.endsWith("LoopMode");
}

void Editor::setLoopMode(const juce::String& id, int mode)
{
    auto* parameter = processor.state.getParameter(id);
    if (parameter == nullptr) return;
    parameter->setValueNotifyingHost(
        parameter->convertTo0to1(static_cast<float>(juce::jlimit(0, spectralLoopCount - 1, mode))));
}

void Editor::refreshLoopFields()
{
    for (auto& module : moduleUis)
        for (auto& held : module.controls)
        {
            auto& control = *held;
            if (control.selector == nullptr || !isLoopModeControl(control.id)) continue;
            const auto position = spectralLoopPosition(juce::roundToInt(value(control.id)));
            const auto listed = control.selector->count() == spectralLoopBuiltCount;
            if (listed && control.selector->chosen == position) continue;
            if (!listed)
            {
                control.selector->choices.clear();
                for (int i = 0; i < spectralLoopBuiltCount; ++i)
                    control.selector->choices.push_back(spectralLoopName(spectralLoopAt(i)));
                control.selector->setTooltip(ui::tooltipFor(control.id));
            }
            control.selector->chosen = position;
            control.selector->repaint();
        }
}

void Editor::showLoopMenu(Control& control)
{
    if (control.selector == nullptr) return;
    const auto current = static_cast<int>(spectralLoopOf(value(control.id)));

    juce::PopupMenu menu;
    for (int i = 0; i < spectralLoopBuiltCount; ++i)
    {
        const auto mode = spectralLoopAt(i);
        // The menu spells the modes the way the manual's list does; the field,
        // which has a strip's width to say it in, keeps the short capitals.
        static constexpr const char* spelled[] {"One-shot", "Fwd Loop", "Rev Loop", "Fwd/Rev Loop",
                                                 "Tailed", "Manual"};
        juce::PopupMenu::Item item(spelled[mode]);
        item.itemID = mode + 1;
        item.isTicked = mode == current;
        menu.addItem(item);
    }

    const auto id = control.id;
    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}.withTargetComponent(control.selector.get()),
                       [safe, id] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        safe->setLoopMode(id, choice - 1);
        safe->refreshLoopFields();
        safe->repaint();
    });
}

// --- The markers --------------------------------------------------------------
//
// The loop is the one thing marked on the spectrogram: its two ends stand down
// the whole height and a bar joins them along the top, an end dragged one at a
// time or the bar dragged whole (p. 110). There used to be a second pair, START
// and END, for the run the loop sat inside; switching ONE-SHOT to FWD LOOP
// moved from one pair to the other while the picture looked the same, so the
// loop's pair is the only one, and it is drawn only while the mode reaches it
// — see spectralLoopReachesLoop. Drawn and hit-tested from the one set of
// numbers below, so what the hand can pick up is exactly what is drawn.

namespace
{
// How tall the loop bar is, and how far either side of a line the hand may be
// and still pick it up. The grab is wider than the line because a line is one
// pixel and a hand is not.
constexpr int loopBarHeight = 7;
constexpr int markerGrab = 5;

// How close two markers may be dragged, as a share of the sample. Half a
// percent of a ten-second sample is fifty milliseconds, which is a few hops —
// short enough not to be in the way, long enough that a marker never vanishes
// behind the one it was dragged onto.
constexpr float markerMinimumGap = 0.005f;

float xFor(juce::Rectangle<int> plot, float proportion)
{
    return static_cast<float>(plot.getX()) + juce::jlimit(0.0f, 1.0f, proportion)
                                               * static_cast<float>(plot.getWidth());
}

float proportionAt(juce::Rectangle<int> plot, int x)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(x - plot.getX())
                                        / static_cast<float>(juce::jmax(1, plot.getWidth())));
}
}

Editor::SpectralMarker Editor::spectralMarkerAt(int oscillator, juce::Point<int> at) const
{
    if (oscillator < 0 || processor.sampleStore().sample(oscillator) == nullptr) return SpectralMarker::none;
    const auto prefix = juce::String(oscillatorPrefix(oscillator));
    if (!spectralLoopReachesLoop(spectralLoopOf(value(prefix + "LoopMode")))) return SpectralMarker::none;
    for (const auto& module : moduleUis)
    {
        if (oscillatorIndexFromId(juce::String(module.descriptor->id)) != oscillator
            || module.descriptor->display != ui::Display::oscillator)
            continue;
        const auto plot = spectralPlotFor(*module.descriptor);
        if (!plot.expanded(markerGrab, 0).contains(at)) return SpectralMarker::none;
        const auto near = [&] (const char* suffix)
        {
            return std::abs(static_cast<float>(at.x) - xFor(plot, value(prefix + suffix)))
                <= static_cast<float>(markerGrab);
        };
        // Either end, anywhere down the height it is drawn at; then the bar
        // between them along the top. Anywhere else is the sample itself,
        // which a click opens the menu for.
        if (near("LoopStart")) return SpectralMarker::loopStart;
        if (near("LoopEnd")) return SpectralMarker::loopEnd;
        const auto x = static_cast<float>(at.x);
        if (at.y <= plot.getY() + loopBarHeight + 2
            && x > xFor(plot, value(prefix + "LoopStart")) && x < xFor(plot, value(prefix + "LoopEnd")))
            return SpectralMarker::loop;
        return SpectralMarker::none;
    }
    return SpectralMarker::none;
}

void Editor::beginSpectralMarkerDrag(int oscillator, SpectralMarker marker, juce::Point<int> at)
{
    markerDragOscillator = oscillator;
    markerDrag = marker;
    const auto prefix = juce::String(oscillatorPrefix(oscillator));
    markerDragLoopStart = value(prefix + "LoopStart");
    markerDragLoopEnd = value(prefix + "LoopEnd");
    for (const auto& module : moduleUis)
        if (oscillatorIndexFromId(juce::String(module.descriptor->id)) == oscillator
            && module.descriptor->display == ui::Display::oscillator)
            markerDragFrom = proportionAt(spectralPlotFor(*module.descriptor), at.x);
    // One gesture per parameter the drag can move, so a host records the drag
    // as one edit rather than as every value it passed through.
    for (const auto* suffix : {"LoopStart", "LoopEnd"})
        if (auto* parameter = processor.state.getParameter(prefix + suffix))
            parameter->beginChangeGesture();
}

void Editor::dragSpectralMarker(juce::Point<int> at)
{
    if (markerDrag == SpectralMarker::none || markerDragOscillator < 0) return;
    const auto prefix = juce::String(oscillatorPrefix(markerDragOscillator));
    juce::Rectangle<int> plot;
    for (const auto& module : moduleUis)
        if (oscillatorIndexFromId(juce::String(module.descriptor->id)) == markerDragOscillator
            && module.descriptor->display == ui::Display::oscillator)
            plot = spectralPlotFor(*module.descriptor);
    if (plot.isEmpty()) return;

    const auto set = [this, &prefix] (const char* suffix, float proportion)
    {
        if (auto* parameter = processor.state.getParameter(prefix + suffix))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(0.0f, 1.0f, proportion)));
    };
    const auto loopStart = value(prefix + "LoopStart"), loopEnd = value(prefix + "LoopEnd");
    const auto here = proportionAt(plot, at.x);

    // The ends keep their order and the loop stays inside the sample.
    switch (markerDrag)
    {
        case SpectralMarker::loopStart:
            set("LoopStart", juce::jlimit(0.0f, loopEnd - markerMinimumGap, here));
            break;
        case SpectralMarker::loopEnd:
            set("LoopEnd", juce::jlimit(loopStart + markerMinimumGap, 1.0f, here));
            break;
        case SpectralMarker::loop:
        {
            // Whole, by however far the hand has moved, and stopped at the
            // sample's ends rather than squeezed against them.
            const auto length = markerDragLoopEnd - markerDragLoopStart;
            const auto moved = juce::jlimit(0.0f, juce::jmax(0.0f, 1.0f - length),
                                            markerDragLoopStart + here - markerDragFrom);
            set("LoopStart", moved);
            set("LoopEnd", moved + length);
            break;
        }
        case SpectralMarker::none:
            break;
    }
    repaint();
}

void Editor::endSpectralMarkerDrag()
{
    if (markerDragOscillator >= 0)
    {
        const auto prefix = juce::String(oscillatorPrefix(markerDragOscillator));
        for (const auto* suffix : {"LoopStart", "LoopEnd"})
            if (auto* parameter = processor.state.getParameter(prefix + suffix))
                parameter->endChangeGesture();
    }
    markerDrag = SpectralMarker::none;
    markerDragOscillator = -1;
}

void Editor::paintSpectralMarkers(juce::Graphics& g, int oscillator, juce::Rectangle<int> plot, float alpha)
{
    const auto prefix = juce::String(oscillatorPrefix(oscillator));
    // Nothing at all in a mode that never reaches the loop, rather than the
    // markers drawn faint: a faint marker still looks like something to drag.
    if (!spectralLoopReachesLoop(spectralLoopOf(value(prefix + "LoopMode")))) return;
    const auto top = static_cast<float>(plot.getY());
    const auto height = static_cast<float>(plot.getHeight());

    const auto loopLeft = xFor(plot, value(prefix + "LoopStart"));
    const auto loopRight = xFor(plot, value(prefix + "LoopEnd"));
    const auto bar = juce::Rectangle<float>(loopLeft, top, juce::jmax(1.0f, loopRight - loopLeft),
                                            static_cast<float>(loopBarHeight));
    g.setColour(ui::electricBlue.withAlpha(0.55f * alpha));
    g.fillRect(bar);
    g.setColour(ui::electricBlue.withAlpha(0.95f * alpha));
    g.fillRect(juce::Rectangle<float>(loopLeft, top, 1.5f, height));
    g.fillRect(juce::Rectangle<float>(loopRight - 1.5f, top, 1.5f, height));
}

// The editor answers for everything it painted itself; its child controls carry
// their own tooltips and are asked before this is. Over a loop marker that is
// what the marker does, since it is the loop's only control; anywhere else on
// the picture it is the sample's name.
juce::String Editor::getTooltip()
{
    const auto at = getMouseXYRelative();
    const auto oscillator = spectralDisplayAt(at);
    if (oscillator < 0) return {};
    const auto prefix = juce::String(oscillatorPrefix(oscillator));
    switch (spectralMarkerAt(oscillator, at))
    {
        case SpectralMarker::loopStart: return ui::tooltipFor(prefix + "LoopStart");
        case SpectralMarker::loopEnd:   return ui::tooltipFor(prefix + "LoopEnd");
        case SpectralMarker::loop:      return "Drag to move oscillator " + oscillatorLetter(oscillator)
                                               + "'s whole loop";
        case SpectralMarker::none:      break;
    }
    return spectralHeaderDetail(oscillator);
}

void Editor::showSampleMenu(int oscillator)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Load Sample...");
    menu.addItem(2, "Clear Sample", processor.sampleStore().sample(oscillator) != nullptr);

    const auto safe = juce::Component::SafePointer<Editor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options {}, [safe, oscillator] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == 1) safe->loadSampleInto(oscillator);
        else
        {
            safe->processor.clearSample(oscillator);
            safe->repaint();
        }
    });
}

void Editor::loadSampleInto(int oscillator)
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load a sample for OSC " + oscillatorLetter(oscillator), juce::File {},
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", true);
    const auto safe = juce::Component::SafePointer<Editor>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [safe, oscillator] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        const auto file = chooser.getResult();
        if (file == juce::File {}) return;
        // Decoding and analysis are both here, on the message thread. A long
        // file takes a moment; the alternative is a background thread handing a
        // sample across, which is the same hand-over SampleStore already does
        // and is worth doing when the files get long enough to notice.
        const auto result = safe->processor.importSample(oscillator, file);
        if (result.failed())
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "Rhino Forge",
                "Could not load that sample: " + result.getErrorMessage());
        safe->repaint();
    });
}
}
