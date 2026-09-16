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

    // A copy of this table with one frame re-authored. Building a whole table
    // costs a transform per frame; this costs one frame's worth of transforms
    // and a copy of the rest, which is the difference between hearing a stroke
    // of the pen while the hand is still moving and hearing it afterwards.
    std::unique_ptr<Wavetable> withFrame(int frame, const float* samples) const
    {
        auto copy = std::make_unique<Wavetable>(*this);
        copy->fillFrame(juce::jlimit(0, frames - 1, frame), samples);
        return copy;
    }

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

// --- The authored side of a table --------------------------------------------
//
// A Wavetable is immutable once it has been built, because the audio thread
// reads it without a lock and the band-limited copies inside it are derived
// data. This is the thing a person actually changes: plain samples, one frame
// after another, touched only on the message thread. Every change ends with a
// new Wavetable built from it and handed over — see ForgeTableStore.h for how
// that hand-over is made safe.
//
// Frames here are always wavetableFrameSize long and always bipolar. Nothing in
// this type knows about drawing, files or presets; it knows what a frame is and
// what can be done to one.

// The ceiling on a hand-edited table. Every frame costs 8 KB of level 0 and
// about 17 KB with its band-limited copies, and a preset carries the lot, so a
// table you drew is allowed to be large but not unbounded.
inline constexpr int maxEditableFrames = 64;

class WavetableEdit
{
public:
    WavetableEdit() = default;

    int frameCount() const noexcept { return static_cast<int>(store.size()) / wavetableFrameSize; }
    const juce::String& title() const noexcept { return tableTitle; }

    // Whether this is still exactly the table it was handed, untouched. A table
    // nobody has drawn on needs no room in a preset, and its frames can still be
    // named after the shapes they are rather than numbered.
    bool isUntouched() const noexcept { return untouched; }

    const float* frame(int index) const noexcept
    {
        return store.data() + static_cast<size_t>(juce::jlimit(0, frameCount() - 1, index)) * wavetableFrameSize;
    }

    juce::String frameTitle(int index) const
    {
        if (index >= 0 && index < static_cast<int>(names.size())) return names[static_cast<size_t>(index)];
        return juce::String(index + 1);
    }

    // Replace the whole table: what a preset load, a file import and a reset to
    // the built-in ten all do. Anything shorter than one frame is refused rather
    // than padded, because half a frame is not a table.
    bool setFrames(const float* samples, int count, juce::String name,
                   std::vector<juce::String> namesOfFrames = {}, bool startsUntouched = false)
    {
        if (samples == nullptr || count < 1) return false;
        count = std::min(count, maxEditableFrames);
        store.assign(samples, samples + static_cast<size_t>(count) * wavetableFrameSize);
        tableTitle = std::move(name);
        names = std::move(namesOfFrames);
        untouched = startsUntouched;
        return true;
    }

    // Everything between two points of one frame, set to the straight line
    // between them. The pen calls this once per mouse move, so a fast drag
    // leaves no gaps behind it; the line tool calls it once over a whole
    // gesture. They are the same operation, which is why there is only one.
    void draw(int index, float phaseFrom, float valueFrom, float phaseTo, float valueTo)
    {
        if (frameCount() < 1) return;
        touch();
        auto* data = mutableFrame(index);
        auto from = pointIndex(phaseFrom), to = pointIndex(phaseTo);
        auto start = juce::jlimit(-1.0f, 1.0f, valueFrom), end = juce::jlimit(-1.0f, 1.0f, valueTo);
        if (from == to) { data[from] = end; return; }
        if (to < from) { std::swap(from, to); std::swap(start, end); }
        const auto span = static_cast<float>(to - from);
        for (auto i = from; i <= to; ++i)
            data[i] = start + (end - start) * (static_cast<float>(i - from) / span);
    }

    // Back to a sine: the frame with the least in it rather than an empty one.
    // A silent frame makes the oscillator drop out at that position, which
    // reads as a fault rather than as a starting point.
    void initFrame(int index)
    {
        touch();
        auto* data = mutableFrame(index);
        for (int i = 0; i < wavetableFrameSize; ++i)
            data[i] = std::sin(juce::MathConstants<float>::twoPi
                               * static_cast<float>(i) / static_cast<float>(wavetableFrameSize));
    }

    // Scaled so the frame's largest excursion reaches full scale. A silent frame
    // is left alone: there is nothing to scale up, and its peak is the divisor.
    void normaliseFrame(int index)
    {
        auto* data = mutableFrame(index);
        auto peak = 0.0f;
        for (int i = 0; i < wavetableFrameSize; ++i) peak = std::max(peak, std::abs(data[i]));
        if (peak < 1.0e-6f || std::abs(peak - 1.0f) < 1.0e-6f) return;
        touch();
        const auto scale = 1.0f / peak;
        for (int i = 0; i < wavetableFrameSize; ++i) data[i] *= scale;
    }

    // A new frame after the one given, either a copy of it or a plain sine.
    // Returns where the new frame landed so the caller can select it; a table
    // already at the ceiling returns the frame it was asked about instead.
    int insertFrame(int after, bool asCopy)
    {
        const auto count = frameCount();
        if (count >= maxEditableFrames) return juce::jlimit(0, count - 1, after);
        touch();
        const auto source = juce::jlimit(0, count - 1, after);
        const auto target = source + 1;
        store.insert(store.begin() + static_cast<ptrdiff_t>(target) * wavetableFrameSize,
                     wavetableFrameSize, 0.0f);
        if (asCopy) std::copy_n(store.data() + static_cast<size_t>(source) * wavetableFrameSize,
                                wavetableFrameSize, mutableFrame(target));
        else initFrame(target);
        return target;
    }

    // Returns the frame to select afterwards. The last frame cannot be removed:
    // a table with no frames leaves POSITION nothing to land on.
    int removeFrame(int index)
    {
        const auto count = frameCount();
        if (count <= 1) return 0;
        touch();
        const auto target = juce::jlimit(0, count - 1, index);
        const auto first = store.begin() + static_cast<ptrdiff_t>(target) * wavetableFrameSize;
        store.erase(first, first + wavetableFrameSize);
        return juce::jlimit(0, frameCount() - 1, target);
    }

    // The table as authored, at a position and a phase — the same morph the
    // voice hears, read off the frames rather than off the band-limited copies,
    // so what the panel draws is what the table holds.
    float sample(float position, float phase) const noexcept
    {
        const auto count = frameCount();
        if (count < 1) return 0.0f;
        int index = 0;
        auto blend = 0.0f;
        wavetableFrameAt(count, position, index, blend);
        const auto first = wavetableInterpolate(frame(index), wavetableFrameSize, phase);
        if (count == 1) return first;
        return juce::jmap(blend, first, wavetableInterpolate(frame(index + 1), wavetableFrameSize, phase));
    }

    // The band-limited table a voice can read. Allocates and runs a transform
    // per frame per level, so it belongs nowhere near the audio thread.
    std::unique_ptr<Wavetable> build() const
    {
        return std::make_unique<Wavetable>(store.data(), frameCount(), tableTitle, names);
    }

    // The frames end to end, for a preset to write out.
    const std::vector<float>& samples() const noexcept { return store; }

private:
    float* mutableFrame(int index) noexcept
    {
        return store.data() + static_cast<size_t>(juce::jlimit(0, frameCount() - 1, index)) * wavetableFrameSize;
    }

    static int pointIndex(float phase) noexcept
    {
        return juce::jlimit(0, wavetableFrameSize - 1,
                            static_cast<int>(juce::jlimit(0.0f, 1.0f, phase)
                                             * static_cast<float>(wavetableFrameSize)));
    }

    // The first edit takes the table's identity away from whatever it was
    // handed. The frame names go with it: "SAW" is a lie about a frame somebody
    // has drawn over, and numbering every frame is the honest answer.
    void touch()
    {
        if (!untouched) return;
        untouched = false;
        names.clear();
        tableTitle = "CUSTOM";
    }

    std::vector<float> store;
    std::vector<juce::String> names;
    juce::String tableTitle {"CUSTOM"};
    bool untouched = false;
};
}
