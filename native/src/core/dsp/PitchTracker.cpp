#include "PitchTracker.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// Under this the window is treated as silence rather than as an unclear
// pitch.  Room tone and console hiss are periodic enough to produce a
// confident nonsense answer, and a gate is cheaper than arguing with one.
constexpr float silenceFloor = 1.0e-4f;   // about -80 dBFS RMS

// YIN's absolute threshold.  Below this the window is periodic enough to
// name a period; the first lag that clears it is preferred to the deepest,
// which is what stops the tracker reporting an octave down on a rich voice.
constexpr float yinThreshold = 0.15f;

// Above this the estimate is trusted enough to correct.  A vowel sits well
// over 0.9; a fricative or a breath sits far below, which is what keeps
// consonants out of the corrector.
constexpr float voicedClarity = 0.72f;

int nextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value) result *= 2;
    return result;
}
}

int PitchTracker::frameSizeFor(Range range, double sampleRate)
{
    const auto base = range == Range::High ? 1024 : range == Range::Mid ? 2048 : 4096;
    const auto scaled = static_cast<double>(base) * (sampleRate > 0.0 ? sampleRate : 48000.0) / 48000.0;
    return nextPowerOfTwo(std::max(256, static_cast<int>(std::lround(scaled))));
}

float PitchTracker::lowestFrequency(Range range)
{
    return range == Range::High ? 150.0f : range == Range::Mid ? 80.0f : 45.0f;
}

float PitchTracker::highestFrequency(Range range)
{
    return range == Range::High ? 1600.0f : range == Range::Mid ? 1000.0f : 500.0f;
}

void PitchTracker::prepare(double rate, Range r)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    range = r;
    frame = frameSizeFor(range, sampleRate);
    window = frame / 2;
    hop = std::max(32, frame / 8);

    // The lag bounds come from the band, then are clamped to what a window of
    // this length can actually resolve: the difference function only reaches
    // `window` samples of lag.
    minLag = std::clamp(static_cast<int>(sampleRate / highestFrequency(range)), 2, window - 2);
    maxLag = std::clamp(static_cast<int>(sampleRate / lowestFrequency(range)), minLag + 2, window - 1);

    history.assign(static_cast<size_t>(frame), 0.0f);
    frameBuffer.assign(static_cast<size_t>(frame), 0.0f);
    cumulativeEnergy.assign(static_cast<size_t>(frame) + 1, 0.0f);
    difference.assign(static_cast<size_t>(window), 0.0f);
    normalised.assign(static_cast<size_t>(window), 1.0f);
    realA.assign(static_cast<size_t>(frame), 0.0f);
    imagA.assign(static_cast<size_t>(frame), 0.0f);
    realB.assign(static_cast<size_t>(frame), 0.0f);
    imagB.assign(static_cast<size_t>(frame), 0.0f);

    // Twiddles and the bit-reversal permutation for an FFT of exactly `frame`
    // points.  The correlation needs no zero padding: the two operands are
    // supported on [0, window) and [0, frame), so a lag below `window` can
    // never wrap around the transform.
    cosTable.assign(static_cast<size_t>(frame) / 2, 0.0f);
    sinTable.assign(static_cast<size_t>(frame) / 2, 0.0f);
    for (int i = 0; i < frame / 2; ++i)
    {
        const auto angle = -2.0 * 3.14159265358979323846 * i / frame;
        cosTable[static_cast<size_t>(i)] = static_cast<float>(std::cos(angle));
        sinTable[static_cast<size_t>(i)] = static_cast<float>(std::sin(angle));
    }
    bitReversed.assign(static_cast<size_t>(frame), 0);
    int bits = 0;
    while ((1 << bits) < frame) ++bits;
    for (int i = 0; i < frame; ++i)
    {
        int reversed = 0;
        for (int bit = 0; bit < bits; ++bit)
            if ((i >> bit) & 1) reversed |= 1 << (bits - 1 - bit);
        bitReversed[static_cast<size_t>(i)] = reversed;
    }
    reset();
}

void PitchTracker::reset()
{
    std::fill(history.begin(), history.end(), 0.0f);
    writeIndex = 0;
    sinceAnalysis = 0;
    primed = false;
    estimate = {};
}

void PitchTracker::process(const float* samples, int count)
{
    if (history.empty() || samples == nullptr) return;
    for (int i = 0; i < count; ++i)
    {
        history[static_cast<size_t>(writeIndex)] = samples[i];
        writeIndex = (writeIndex + 1) % frame;
        if (writeIndex == 0) primed = true;
        if (++sinceAnalysis >= hop)
        {
            sinceAnalysis = 0;
            if (primed) analyse();
        }
    }
}

// Iterative radix-2 Cooley-Tukey, in place.  Written out rather than taken
// from juce_dsp because RhinoCore deliberately depends on nothing but the
// standard library, and the whole of it is forty lines.
void PitchTracker::transform(std::vector<float>& re, std::vector<float>& im, bool inverse) const
{
    const auto n = frame;
    for (int i = 0; i < n; ++i)
    {
        const auto j = bitReversed[static_cast<size_t>(i)];
        if (j > i)
        {
            std::swap(re[static_cast<size_t>(i)], re[static_cast<size_t>(j)]);
            std::swap(im[static_cast<size_t>(i)], im[static_cast<size_t>(j)]);
        }
    }
    for (int span = 2; span <= n; span *= 2)
    {
        const auto step = n / span;
        for (int start = 0; start < n; start += span)
        {
            for (int k = 0; k < span / 2; ++k)
            {
                const auto twiddle = static_cast<size_t>(k * step);
                const auto wr = cosTable[twiddle];
                const auto wi = inverse ? -sinTable[twiddle] : sinTable[twiddle];
                const auto a = static_cast<size_t>(start + k);
                const auto b = static_cast<size_t>(start + k + span / 2);
                const auto tr = re[b] * wr - im[b] * wi;
                const auto ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    if (inverse)
    {
        const auto scale = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
        {
            re[static_cast<size_t>(i)] *= scale;
            im[static_cast<size_t>(i)] *= scale;
        }
    }
}

void PitchTracker::analyse()
{
    // Unwrap the ring so the oldest sample is at index 0.
    for (int i = 0; i < frame; ++i)
        frameBuffer[static_cast<size_t>(i)] = history[static_cast<size_t>((writeIndex + i) % frame)];

    cumulativeEnergy[0] = 0.0f;
    for (int i = 0; i < frame; ++i)
    {
        const auto sample = frameBuffer[static_cast<size_t>(i)];
        cumulativeEnergy[static_cast<size_t>(i) + 1] = cumulativeEnergy[static_cast<size_t>(i)] + sample * sample;
    }
    const auto windowEnergy = cumulativeEnergy[static_cast<size_t>(window)];
    if (windowEnergy / static_cast<float>(window) < silenceFloor * silenceFloor)
    {
        estimate = {};
        return;
    }

    // corr(lag) = sum over the window of x[j] * x[j + lag], as one circular
    // correlation.  A is the window, B is the whole frame.
    for (int i = 0; i < frame; ++i)
    {
        realA[static_cast<size_t>(i)] = i < window ? frameBuffer[static_cast<size_t>(i)] : 0.0f;
        imagA[static_cast<size_t>(i)] = 0.0f;
        realB[static_cast<size_t>(i)] = frameBuffer[static_cast<size_t>(i)];
        imagB[static_cast<size_t>(i)] = 0.0f;
    }
    transform(realA, imagA, false);
    transform(realB, imagB, false);
    for (int i = 0; i < frame; ++i)
    {
        // conj(A) * B, which turns the convolution into a correlation.
        const auto ar = realA[static_cast<size_t>(i)], ai = imagA[static_cast<size_t>(i)];
        const auto br = realB[static_cast<size_t>(i)], bi = imagB[static_cast<size_t>(i)];
        realA[static_cast<size_t>(i)] = ar * br + ai * bi;
        imagA[static_cast<size_t>(i)] = ar * bi - ai * br;
    }
    transform(realA, imagA, true);

    // The squared difference, expanded so the correlation does the work:
    // d(lag) = energy(0) + energy(lag) - 2 * corr(lag).
    for (int lag = 0; lag < window; ++lag)
    {
        const auto lagEnergy = cumulativeEnergy[static_cast<size_t>(lag + window)] - cumulativeEnergy[static_cast<size_t>(lag)];
        difference[static_cast<size_t>(lag)] =
            std::max(0.0f, windowEnergy + lagEnergy - 2.0f * realA[static_cast<size_t>(lag)]);
    }

    // Cumulative mean normalisation.  Without it d() falls away with lag and
    // the deepest dip is always the longest one it looked at.
    normalised[0] = 1.0f;
    auto running = 0.0f;
    for (int lag = 1; lag < window; ++lag)
    {
        running += difference[static_cast<size_t>(lag)];
        normalised[static_cast<size_t>(lag)] = running > 0.0f
            ? difference[static_cast<size_t>(lag)] * static_cast<float>(lag) / running
            : 1.0f;
    }

    // The first dip under the threshold, not the deepest one: a periodic
    // signal dips again at every multiple of its period, and taking the
    // deepest is exactly how a tracker ends up an octave low.
    auto chosen = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (normalised[static_cast<size_t>(lag)] >= yinThreshold) continue;
        while (lag + 1 <= maxLag && normalised[static_cast<size_t>(lag + 1)] < normalised[static_cast<size_t>(lag)])
            ++lag;
        chosen = lag;
        break;
    }
    if (chosen < 0)
    {
        // Nothing cleared the threshold, so fall back to the shallowest dip
        // available and let clarity say how little it is worth.
        auto best = normalised[static_cast<size_t>(minLag)];
        chosen = minLag;
        for (int lag = minLag + 1; lag <= maxLag; ++lag)
            if (normalised[static_cast<size_t>(lag)] < best)
            {
                best = normalised[static_cast<size_t>(lag)];
                chosen = lag;
            }
    }

    // Parabolic interpolation through the three points around the dip. A
    // period is an integer number of samples otherwise, which at 1 kHz is a
    // 40 cent quantisation -- far too coarse to correct against.
    auto period = static_cast<float>(chosen);
    if (chosen > 0 && chosen + 1 < window)
    {
        const auto before = normalised[static_cast<size_t>(chosen - 1)];
        const auto here = normalised[static_cast<size_t>(chosen)];
        const auto after = normalised[static_cast<size_t>(chosen + 1)];
        const auto denominator = 2.0f * (2.0f * here - before - after);
        if (std::abs(denominator) > 1.0e-9f)
            period += (after - before) / denominator;
    }
    period = std::clamp(period, static_cast<float>(minLag), static_cast<float>(maxLag));

    const auto clarity = std::clamp(1.0f - normalised[static_cast<size_t>(chosen)], 0.0f, 1.0f);
    const auto frequency = static_cast<float>(sampleRate) / period;
    estimate.frequency = frequency;
    estimate.midiNote = 69.0f + 12.0f * std::log2(frequency / 440.0f);
    estimate.clarity = clarity;
    estimate.voiced = clarity >= voicedClarity;
}
}
