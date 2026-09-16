#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

// A real wavetable: frames of samples, held in memory, read by the oscillator.
//
// This replaces the analytic shapes the oscillator computed per sample. The ten
// built-in frames are still written by the same formulas, but they are rendered
// into a table once, at startup, and the voice reads the table from then on —
// which is what lets a table come from somewhere other than a formula.
//
// Nothing here is called from renderSample except the const readers at the
// bottom. Building a table allocates and runs FFTs; that happens on the message
// thread, before the table is ever handed to a voice.
namespace theta::forge
{
// Every table is stored at this frame size. 2048 is the size the wavetable
// files everyone else writes assume, so a table read from disk needs no
// resampling to sit beside a built-in one.
inline constexpr int wavetableFrameSize = 2048;

// Band-limited copies stop shrinking here. Below this a frame has too few
// points for the interpolator to read cleanly, and the levels this small are
// already carrying so few harmonics that the extra points cost nothing.
inline constexpr int wavetableMinLevelSize = 64;

// --- Band-limiting -----------------------------------------------------------
//
// A frame drawn as a square has a vertical edge in it, which is a harmonic
// series running to infinity. Played at 440 Hz against a 48 kHz sample rate,
// only the first 54 of those harmonics fit under Nyquist and every one above it
// folds back down the spectrum as an inharmonic whistle that follows the note
// in the wrong direction. That is the aliasing Forge has had all along.
//
// The fix is the standard one: keep several copies of every frame, each with a
// different number of harmonics left in it, and read whichever copy has the
// most harmonics that still fit. Level 0 is the frame exactly as it was
// authored — it is what the panel draws and what a low note reads. Each level
// after it halves the harmonics, and halves the points it is stored at to
// match, so the whole set of levels costs about twice the table itself rather
// than eleven times it.
struct WavetableLevel
{
    int size = 0;       // points per frame at this level, always a power of two
    int harmonics = 0;  // the highest harmonic left in it
    int offset = 0;     // where this level's first frame starts in the sample store
};

// One sample of a frame, between its points. Catmull-Rom rather than a straight
// line: a line between two points of a 64-point frame is a visibly different
// curve from the frame itself, and that difference is broadband noise. The
// frames are powers of two long, so the wrap at each end is a mask rather than
// a branch, and a spline reads one point either side of the pair it sits
// between.
inline float wavetableInterpolate(const float* frame, int size, float phase) noexcept
{
    const auto mask = size - 1;
    const auto scaled = phase * static_cast<float>(size);
    const auto index = static_cast<int>(scaled);
    const auto t = scaled - static_cast<float>(index);
    const auto p0 = frame[(index - 1) & mask];
    const auto p1 = frame[index & mask];
    const auto p2 = frame[(index + 1) & mask];
    const auto p3 = frame[(index + 2) & mask];
    const auto a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const auto b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const auto c = -0.5f * p0 + 0.5f * p2;
    return ((a * t + b) * t + c) * t + p1;
}

// Where a position falls in a table: the frame at or below it, and how far past
// that frame it has travelled. A position of one lands on the last frame with
// nothing beyond it to blend toward. Kept free of the table itself so the panel
// can ask the question about a table it is only drawing.
inline void wavetableFrameAt(int frameCount, float position, int& frame, float& blend) noexcept
{
    if (frameCount <= 1) { frame = 0; blend = 0.0f; return; }
    const auto scaled = juce::jlimit(0.0f, 1.0f, position) * static_cast<float>(frameCount - 1);
    frame = std::min(frameCount - 2, static_cast<int>(scaled));
    blend = scaled - static_cast<float>(frame);
}

class Wavetable
{
public:
    // `samples` is frameCount frames of wavetableFrameSize samples laid end to
    // end, which is both how a wavetable file stores them and how the built-in
    // frames are generated. Names are optional: a table read from a file names
    // its frames by number.
    Wavetable(const float* samples, int frameCount, juce::String tableName,
              std::vector<juce::String> namesOfFrames = {})
        : frames(std::max(1, frameCount)),
          tableTitle(std::move(tableName)),
          names(std::move(namesOfFrames))
    {
        buildLevels();
        store.assign(static_cast<size_t>(totalSamples()), 0.0f);
        for (int frame = 0; frame < frames; ++frame)
            fillFrame(frame, samples + static_cast<size_t>(frame) * wavetableFrameSize);
    }

    int frameCount() const noexcept { return frames; }
    int levelCount() const noexcept { return static_cast<int>(levels.size()); }
    const juce::String& title() const noexcept { return tableTitle; }

    juce::String frameTitle(int frame) const
    {
        if (frame >= 0 && frame < static_cast<int>(names.size())) return names[static_cast<size_t>(frame)];
        return juce::String(frame + 1);
    }

    // The frame exactly as it was authored, for the panel to draw and for an
    // editor to work on. Always level 0: what you see is the table, not
    // whichever band-limited copy the note you happen to be holding reads.
    const float* frameData(int frame) const noexcept
    {
        return store.data() + static_cast<size_t>(juce::jlimit(0, frames - 1, frame)) * wavetableFrameSize;
    }

    // Which level a note may read without aliasing: the first one whose top
    // harmonic still fits under Nyquist. Cheap enough to ask per sample —
    // integer compares over a list that is eleven long and usually settles in
    // two or three.
    int levelFor(float hz, double sampleRate) const noexcept
    {
        const auto allowed = hz > 1.0e-3f ? static_cast<float>(sampleRate * 0.5) / hz
                                          : static_cast<float>(wavetableFrameSize);
        for (int level = 0; level < static_cast<int>(levels.size()); ++level)
            if (static_cast<float>(levels[static_cast<size_t>(level)].harmonics) <= allowed) return level;
        return static_cast<int>(levels.size()) - 1;
    }

    // One sample of one frame. Phase is assumed already inside its cycle; the
    // mask in the interpolator makes anything else safe rather than correct.
    float frameSample(int level, int frame, float phase) const noexcept
    {
        const auto& chosen = levels[static_cast<size_t>(juce::jlimit(0, levelCount() - 1, level))];
        const auto clamped = juce::jlimit(0, frames - 1, frame);
        return wavetableInterpolate(store.data() + chosen.offset + static_cast<size_t>(clamped) * chosen.size,
                                    chosen.size, phase);
    }

    // One sample of the table at a position: the two frames either side of it,
    // crossfaded. Only those two are read, so what this costs does not grow
    // with the size of the table — the property that has to hold before a table
    // can sensibly hold hundreds of frames.
    float sample(int level, float position, float phase) const noexcept
    {
        int frame = 0;
        auto blend = 0.0f;
        wavetableFrameAt(frames, position, frame, blend);
        if (frames == 1) return frameSample(level, 0, phase);
        return juce::jmap(blend, frameSample(level, frame, phase), frameSample(level, frame + 1, phase));
    }

    // The table as authored, for the display.
    float sample(float position, float phase) const noexcept { return sample(0, position, phase); }

private:
    int totalSamples() const noexcept
    {
        const auto& last = levels.back();
        return last.offset + last.size * frames;
    }

    void buildLevels()
    {
        auto harmonics = wavetableFrameSize / 2;
        auto size = wavetableFrameSize;
        auto offset = 0;
        while (harmonics >= 1)
        {
            levels.push_back({size, harmonics, offset});
            offset += size * frames;
            harmonics /= 2;
            size = std::max(wavetableMinLevelSize, size / 2);
        }
    }

    // Level 0 is copied straight in. Every level after it is the frame with its
    // top harmonics removed — done by transforming once, clearing the bins
    // above the limit and transforming back, so the cut is exact and leaves no
    // filter ripple behind it. The result is then band-limited well enough to
    // decimate by simply taking every nth point, with nothing left up there to
    // fold over.
    void fillFrame(int frame, const float* source)
    {
        auto* level0 = store.data() + static_cast<size_t>(frame) * wavetableFrameSize;
        std::copy(source, source + wavetableFrameSize, level0);

        static constexpr int order = []
        {
            auto bits = 0, size = wavetableFrameSize;
            while (size > 1) { size /= 2; ++bits; }
            return bits;
        }();
        juce::dsp::FFT fft(order);
        std::vector<float> spectrum(2 * wavetableFrameSize, 0.0f);
        std::copy(source, source + wavetableFrameSize, spectrum.begin());
        fft.performRealOnlyForwardTransform(spectrum.data());

        std::vector<float> band(2 * wavetableFrameSize);
        for (int level = 1; level < static_cast<int>(levels.size()); ++level)
        {
            const auto& target = levels[static_cast<size_t>(level)];
            band = spectrum;
            // A real signal's spectrum is mirrored, so a harmonic is cleared at
            // both the bin that names it and the bin that mirrors it.
            for (int bin = target.harmonics + 1; bin <= wavetableFrameSize / 2; ++bin)
            {
                band[static_cast<size_t>(2 * bin)] = 0.0f;
                band[static_cast<size_t>(2 * bin + 1)] = 0.0f;
                const auto mirror = wavetableFrameSize - bin;
                band[static_cast<size_t>(2 * mirror)] = 0.0f;
                band[static_cast<size_t>(2 * mirror + 1)] = 0.0f;
            }
            fft.performRealOnlyInverseTransform(band.data());

            const auto stride = wavetableFrameSize / target.size;
            auto* destination = store.data() + target.offset + static_cast<size_t>(frame) * target.size;
            for (int i = 0; i < target.size; ++i)
                destination[i] = band[static_cast<size_t>(i * stride)];
        }
    }

    int frames = 1;
    juce::String tableTitle;
    std::vector<juce::String> names;
    std::vector<WavetableLevel> levels;
    std::vector<float> store;
};
}
