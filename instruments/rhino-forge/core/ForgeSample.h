#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// A sample, analysed into frequency: what a spectral oscillator reads.
//
// This is the spectral counterpart of ForgeWavetable.h, and it is deliberately
// the same shape. A table is frames of *samples* and is read a cycle at a time;
// this is frames of *spectrum* and is read a hop at a time. Both are built on
// the message thread, both are immutable once built, and both are handed to a
// voice as a bare pointer by a store that knows when the old one can be freed.
//
// Nothing here is called from renderSample except the const readers at the
// bottom. Building one allocates and runs an FFT per frame; that happens on the
// message thread, before the sample is ever handed to a voice. See SPECTRAL.md.
namespace rhino::forge
{
// The analysis window, and how far it moves between frames.
//
// 2048 at 48 kHz is a 43 ms window: long enough to resolve the low end of a
// tonal sample into separate partials, which is the whole point of analysing
// it, and short enough that the smearing it does to a transient is still worth
// paying. It is also the size Forge already stores wavetables at, so a table
// loaded as a spectral source needs no resampling.
//
// A quarter-window hop is the standard choice for a phase vocoder, and it is
// not arbitrary: squared Hann windows overlapped at a quarter sum to a
// constant, so analysis and synthesis can both be windowed — which is what
// keeps the edges of a resynthesised frame from clicking — and the overlap
// still adds up to unity. The constant is 3/2, which is where spectralGain
// comes from.
inline constexpr int spectralFftSize = 2048;
inline constexpr int spectralFftOrder = 11;
inline constexpr int spectralHop = spectralFftSize / 4;
inline constexpr int spectralBins = spectralFftSize / 2 + 1;
inline constexpr float spectralGain = 2.0f / 3.0f;

// How much of a file is analysed. Twenty seconds at 48 kHz is 1875 frames,
// about 15 MB of spectrum — large, but it is one allocation per loaded sample
// on the message thread, and a spectral source longer than this is a texture
// nobody is scanning through by hand.
inline constexpr int spectralMaxLength = 48000 * 20;

// The phase advance a bin gets for free, from the hop alone, if it is sitting
// exactly on its own centre frequency. The vocoder measures every bin's advance
// against this and keeps the difference, which is what tells it the partial's
// true frequency rather than the bin's nominal one.
inline constexpr float spectralBinAdvance(int bin) noexcept
{
    return 2.0f * juce::MathConstants<float>::pi
         * static_cast<float>(bin) * static_cast<float>(spectralHop)
         / static_cast<float>(spectralFftSize);
}

// Wrap to -pi..pi. The phase difference between two analysis frames is only
// known modulo a turn, and the deviation from the expected advance is the part
// that carries the frequency, so it has to be brought back into one turn before
// it can be added to anything.
inline float spectralWrap(float phase) noexcept
{
    constexpr auto twoPi = 2.0f * juce::MathConstants<float>::pi;
    return phase - twoPi * std::floor(phase / twoPi + 0.5f);
}

class Sample final
{
public:
    // Mono, because a spectral oscillator resynthesises one spectrum and places
    // it with its own PAN. Averaging a stereo file is what the sample loader
    // does on the way in; nothing here sees two channels.
    Sample(const float* mono, int length, double rate, juce::String name)
        : title(std::move(name)), sourceRate(rate > 0.0 ? rate : 48000.0)
    {
        length = juce::jlimit(0, spectralMaxLength, length);
        if (mono == nullptr || length < spectralFftSize) return;

        audio.assign(mono, mono + length);
        frames = (length - spectralFftSize) / spectralHop + 1;
        if (frames < 1) { frames = 0; audio.clear(); return; }

        // Peak-normalised, so the loudest moment of any sample meets full
        // scale, which is where a wavetable's loudest frame already is. A
        // recording arrives at whatever level it was mastered or bounced at —
        // the cowbell loop this was found on peaks at -5 dB and sits at -28 dB
        // RMS — and an oscillator playing it at that level sounds broken beside
        // two that are not. Done here rather than in the loader so that every
        // way a Sample is made, a patch reopening included, gets the same
        // answer; and it is idempotent, so a sample saved normalised and
        // loaded again comes back exactly as it went.
        auto peak = 0.0f;
        for (const auto value : audio) peak = juce::jmax(peak, std::abs(value));
        if (peak > 1.0e-6f)
            for (auto& value : audio) value /= peak;

        magnitudeStore.assign(static_cast<size_t>(frames) * spectralBins, 0.0f);
        phaseStore.assign(static_cast<size_t>(frames) * spectralBins, 0.0f);
        flux.assign(static_cast<size_t>(frames), 0.0f);

        // Hann, periodic rather than symmetric: the periodic one is what sums
        // to a constant under overlap-add, and the difference at this size is
        // one sample but it is the sample that would leave a ripple.
        std::vector<float> window(static_cast<size_t>(spectralFftSize));
        for (int i = 0; i < spectralFftSize; ++i)
            window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(
                2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
                / static_cast<float>(spectralFftSize));

        juce::dsp::FFT fft(spectralFftOrder);
        std::vector<float> work(static_cast<size_t>(2 * spectralFftSize), 0.0f);
        std::vector<float> previous(static_cast<size_t>(spectralBins), 0.0f);

        for (int frame = 0; frame < frames; ++frame)
        {
            std::fill(work.begin(), work.end(), 0.0f);
            const auto* from = audio.data() + static_cast<size_t>(frame) * spectralHop;
            for (int i = 0; i < spectralFftSize; ++i)
                work[static_cast<size_t>(i)] = from[i] * window[static_cast<size_t>(i)];
            fft.performRealOnlyForwardTransform(work.data());

            auto* mags = magnitudeStore.data() + static_cast<size_t>(frame) * spectralBins;
            auto* phs = phaseStore.data() + static_cast<size_t>(frame) * spectralBins;
            auto rising = 0.0f;
            for (int bin = 0; bin < spectralBins; ++bin)
            {
                const auto re = work[static_cast<size_t>(2 * bin)];
                const auto im = work[static_cast<size_t>(2 * bin + 1)];
                const auto magnitude = std::sqrt(re * re + im * im);
                mags[bin] = magnitude;
                phs[bin] = std::atan2(im, re);
                // Spectral flux, half-wave rectified: only bins that got louder
                // count. That is what separates a transient from a decay, which
                // moves just as much energy in the other direction.
                rising += juce::jmax(0.0f, magnitude - previous[static_cast<size_t>(bin)]);
                previous[static_cast<size_t>(bin)] = magnitude;
            }
            flux[static_cast<size_t>(frame)] = rising;
        }

        // Flux is only meaningful against the rest of this sample, so it is
        // normalised here rather than compared with a threshold at render time
        // that would mean something different for every file.
        const auto loudest = *std::max_element(flux.begin(), flux.end());
        if (loudest > 0.0f)
            for (auto& value : flux) value /= loudest;
    }

    // --- Read from the audio thread, const throughout -------------------------

    bool isEmpty() const noexcept { return frames <= 0; }
    int frameCount() const noexcept { return frames; }
    double rate() const noexcept { return sourceRate; }
    const juce::String& name() const noexcept { return title; }

    const float* magnitudes(int frame) const noexcept
    {
        return magnitudeStore.data()
             + static_cast<size_t>(juce::jlimit(0, frames - 1, frame)) * spectralBins;
    }

    const float* phases(int frame) const noexcept
    {
        return phaseStore.data()
             + static_cast<size_t>(juce::jlimit(0, frames - 1, frame)) * spectralBins;
    }

    // How much of a transient this frame is, 0..1, against the sharpest one in
    // this sample. What the TRANSIENTS option tests. See SPECTRAL.md.
    float transient(int frame) const noexcept
    {
        if (flux.empty()) return 0.0f;
        return flux[static_cast<size_t>(juce::jlimit(0, frames - 1, frame))];
    }

    // The samples themselves, kept for the display to draw and for the loader
    // to hand back. Not read by the voice.
    const std::vector<float>& waveform() const noexcept { return audio; }

private:
    juce::String title;
    double sourceRate = 48000.0;
    int frames = 0;
    std::vector<float> audio;
    std::vector<float> magnitudeStore;
    std::vector<float> phaseStore;
    std::vector<float> flux;
};
}
