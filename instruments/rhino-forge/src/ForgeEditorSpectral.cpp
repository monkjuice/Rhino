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

// What the header says a spectral oscillator is reading. The same slot the
// envelope names its stage in and the LFO its rate — what the module is doing,
// rather than what it is — and the same place Serum prints it (p. 105).
juce::String Editor::spectralHeaderDetail(int oscillator) const
{
    const auto name = processor.sampleStore().sourceName(oscillator);
    return name.isNotEmpty() ? name : juce::String("NO SAMPLE");
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
