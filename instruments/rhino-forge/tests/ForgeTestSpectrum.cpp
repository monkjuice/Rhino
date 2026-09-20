#include "ForgeTestSpectrum.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace rhino::forge::tests
{
// The magnitude spectrum of a patch rendered through Core, measured past the
// attack so the window holds steady state and not the envelope's edge.
std::vector<double> renderedSpectrum(const rhino::forge::Patch& patch, int note, double sampleRate)
{
    rhino::forge::Core core;
    core.initialise(sampleRate);
    core.noteOn(note, 1.0f, patch);
    for (int i = 0; i < 4096; ++i) { auto l = 0.0f, r = 0.0f; core.renderSample(patch, l, r); }

    std::vector<float> data(2 * spectrumSize, 0.0f);
    for (int i = 0; i < spectrumSize; ++i)
    {
        auto l = 0.0f, r = 0.0f;
        core.renderSample(patch, l, r);
        const auto w = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
                                            * static_cast<double>(i) / static_cast<double>(spectrumSize));
        data[static_cast<size_t>(i)] = static_cast<float>(0.5 * (l + r) * w);
    }

    juce::dsp::FFT fft(spectrumOrder);
    fft.performRealOnlyForwardTransform(data.data());
    std::vector<double> magnitude(static_cast<size_t>(spectrumSize / 2), 0.0);
    for (int bin = 0; bin < spectrumSize / 2; ++bin)
    {
        const auto re = static_cast<double>(data[static_cast<size_t>(2 * bin)]);
        const auto im = static_cast<double>(data[static_cast<size_t>(2 * bin + 1)]);
        magnitude[static_cast<size_t>(bin)] = std::sqrt(re * re + im * im);
    }
    return magnitude;
}

SpectrumPeak peakAt(const std::vector<double>& magnitude, int bin, double sampleRate)
{
    const auto at = [&magnitude] (int index)
    {
        if (index < 0 || index >= static_cast<int>(magnitude.size())) return 1.0e-20;
        return std::max(1.0e-20, magnitude[static_cast<size_t>(index)]);
    };
    const auto a = std::log(at(bin - 1)), b = std::log(at(bin)), c = std::log(at(bin + 1));
    const auto denominator = a - 2.0 * b + c;
    const auto shift = std::abs(denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
    return {(static_cast<double>(bin) + shift) * sampleRate / static_cast<double>(spectrumSize),
            std::exp(b - 0.25 * (a - c) * shift)};
}

// The tallest partial in a spectrum, which for every shape measured here is the
// fundamental.
SpectrumPeak loudestPeak(const std::vector<double>& magnitude, double sampleRate)
{
    auto best = 2;
    for (int bin = 3; bin < static_cast<int>(magnitude.size()) - 1; ++bin)
        if (magnitude[static_cast<size_t>(bin)] > magnitude[static_cast<size_t>(best)]) best = bin;
    return peakAt(magnitude, best, sampleRate);
}

// The amplitude of one harmonic of a known fundamental, found by looking for
// the local maximum where that harmonic should be rather than trusting a bin
// index: a fundamental measured to a fraction of a bin puts the tenth harmonic
// several bins from wherever the arithmetic lands.
double partialAmplitude(const std::vector<double>& magnitude, double fundamental, int harmonic,
                        double sampleRate)
{
    const auto centre = static_cast<int>(std::lround(fundamental * harmonic
                                                     * static_cast<double>(spectrumSize) / sampleRate));
    if (centre < 2 || centre >= static_cast<int>(magnitude.size()) - 2) return 0.0;
    auto best = centre;
    for (int bin = centre - 2; bin <= centre + 2; ++bin)
        if (magnitude[static_cast<size_t>(bin)] > magnitude[static_cast<size_t>(best)]) best = bin;
    return peakAt(magnitude, best, sampleRate).amplitude;
}

// The patch tuningSuite asks its question of: one oscillator on the saw frame
// and nothing else sounding.
rhino::forge::Patch sawOnly()
{
    rhino::forge::Patch patch;
    patch.a.enable = 1.0f;
    patch.a.position = 6.0f / 9.0f;   // the SAW frame, landed on exactly
    patch.a.unison = 1.0f;
    patch.a.level = 0.75f;
    patch.b.enable = 0.0f;
    patch.subEnable = 0.0f;
    patch.noiseEnable = 0.0f;
    patch.filterEnable = 0.0f;
    patch.envs[rhino::forge::ampEnv].attack = 0.001f;
    patch.envs[rhino::forge::ampEnv].sustain = 1.0f;
    return patch;
}

double renderedFundamental(int note, double sampleRate)
{
    return loudestPeak(renderedSpectrum(sawOnly(), note, sampleRate), sampleRate).frequency;
}
}
