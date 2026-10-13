#include "SpectrumAnalyser.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// A full-scale sine through a Hann window lands at magnitude A * N / 4 in its
// peak bin, so this is the factor that turns a bin back into dBFS. Getting it
// wrong does not change the shape of anything, which is exactly why it is
// worth writing down and testing.
float dbFromMagnitude(float magnitude, int size)
{
    const auto amplitude = magnitude * 4.0f / static_cast<float>(size);
    return 20.0f * std::log10(std::max(amplitude, 1.0e-6f));
}
}

void SpectrumTap::prepare(double sampleRateIn)
{
    sampleRate.store(sampleRateIn > 0.0 ? sampleRateIn : 48000.0, std::memory_order_relaxed);
    reset();
}

void SpectrumTap::reset()
{
    ring.fill(0.0f);
    writePosition.store(0, std::memory_order_relaxed);
    written.store(0, std::memory_order_release);
}

void SpectrumTap::write(const float* const* data, int channels, int count) noexcept
{
    if (data == nullptr || channels <= 0 || count <= 0)
        return;
    const auto scale = 1.0f / static_cast<float>(channels);
    auto position = writePosition.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
    {
        auto sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            if (data[channel] != nullptr)
                sum += data[channel][i];
        ring[static_cast<size_t>(position)] = sum * scale;
        position = (position + 1) & (ringSize - 1);
    }
    writePosition.store(position, std::memory_order_release);
    written.fetch_add(count, std::memory_order_relaxed);
}

void SpectrumTap::readTail(float* destination, int count) const noexcept
{
    if (destination == nullptr || count <= 0)
        return;
    const auto wanted = std::min(count, ringSize);
    auto position = (writePosition.load(std::memory_order_acquire) - wanted + ringSize) & (ringSize - 1);
    for (int i = 0; i < wanted; ++i)
    {
        destination[i] = ring[static_cast<size_t>(position)];
        position = (position + 1) & (ringSize - 1);
    }
    for (int i = wanted; i < count; ++i)
        destination[i] = 0.0f;
}

SpectrumReader::SpectrumReader()
{
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t>(i)] =
            0.5f - 0.5f * static_cast<float>(std::cos(2.0 * pi * i / (fftSize - 1)));

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const auto angle = -2.0 * pi * i / fftSize;
        cosTable[static_cast<size_t>(i)] = static_cast<float>(std::cos(angle));
        sinTable[static_cast<size_t>(i)] = static_cast<float>(std::sin(angle));
    }

    for (int i = 0; i < fftSize; ++i)
    {
        int reversed = 0;
        for (int bit = 0; bit < fftOrder; ++bit)
            if ((i >> bit) & 1) reversed |= 1 << (fftOrder - 1 - bit);
        bitReversed[static_cast<size_t>(i)] = reversed;
    }

    clear();
}

void SpectrumReader::clear()
{
    display.fill(floorDb);
}

float SpectrumReader::binFrequency(int index)
{
    const auto position = static_cast<double>(std::clamp(index, 0, binCount - 1)) / (binCount - 1);
    return static_cast<float>(lowestFrequency
        * std::pow(highestFrequency / lowestFrequency, position));
}

void SpectrumReader::buildBinMap(double sampleRateIn)
{
    mappedRate = sampleRateIn;
    const auto hertzPerBin = sampleRateIn / fftSize;
    for (int i = 0; i < binCount; ++i)
    {
        // Each display bin owns the half-decade-step either side of its
        // centre, so neighbouring bins tile the axis rather than sampling it.
        const auto centre = static_cast<double>(binFrequency(i));
        const auto lower = i > 0 ? std::sqrt(centre * binFrequency(i - 1)) : centre * 0.97;
        const auto upper = i < binCount - 1 ? std::sqrt(centre * binFrequency(i + 1)) : centre * 1.03;
        binStart[static_cast<size_t>(i)] = static_cast<float>(lower / hertzPerBin);
        binEnd[static_cast<size_t>(i)] = static_cast<float>(upper / hertzPerBin);
    }
}

// Iterative radix-2 Cooley-Tukey, in place, the same shape as the one in
// PitchTracker: RhinoCore depends on nothing but the standard library, and
// forty lines is cheaper than the dependency.
void SpectrumReader::transform()
{
    for (int i = 0; i < fftSize; ++i)
    {
        const auto j = bitReversed[static_cast<size_t>(i)];
        if (j > i)
        {
            std::swap(real[static_cast<size_t>(i)], real[static_cast<size_t>(j)]);
            std::swap(imaginary[static_cast<size_t>(i)], imaginary[static_cast<size_t>(j)]);
        }
    }
    for (int span = 2; span <= fftSize; span *= 2)
    {
        const auto step = fftSize / span;
        for (int start = 0; start < fftSize; start += span)
        {
            for (int k = 0; k < span / 2; ++k)
            {
                const auto twiddle = static_cast<size_t>(k * step);
                const auto wr = cosTable[twiddle];
                const auto wi = sinTable[twiddle];
                const auto a = static_cast<size_t>(start + k);
                const auto b = static_cast<size_t>(start + k + span / 2);
                const auto tr = real[b] * wr - imaginary[b] * wi;
                const auto ti = real[b] * wi + imaginary[b] * wr;
                real[b] = real[a] - tr;
                imaginary[b] = imaginary[a] - ti;
                real[a] += tr;
                imaginary[a] += ti;
            }
        }
    }
}

void SpectrumReader::analyse(const float* samples, int count, double sampleRateIn, float decayDb)
{
    if (samples == nullptr || sampleRateIn <= 0.0)
        return;
    if (mappedRate != sampleRateIn)
        buildBinMap(sampleRateIn);

    const auto available = std::min(count, fftSize);
    const auto offset = fftSize - available;
    std::fill(frame.begin(), frame.end(), 0.0f);
    for (int i = 0; i < available; ++i)
        frame[static_cast<size_t>(offset + i)] = samples[count - available + i];

    for (int i = 0; i < fftSize; ++i)
    {
        real[static_cast<size_t>(i)] = frame[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        imaginary[static_cast<size_t>(i)] = 0.0f;
    }
    transform();

    for (int i = 0; i <= fftSize / 2; ++i)
    {
        const auto re = real[static_cast<size_t>(i)], im = imaginary[static_cast<size_t>(i)];
        magnitude[static_cast<size_t>(i)] = std::sqrt(re * re + im * im);
    }

    const auto lastBin = fftSize / 2;
    for (int i = 0; i < binCount; ++i)
    {
        const auto lower = binStart[static_cast<size_t>(i)];
        const auto upper = binEnd[static_cast<size_t>(i)];
        auto peak = 0.0f;
        if (upper - lower < 1.0f)
        {
            // Narrower than one FFT bin: interpolate, or the low end of the
            // display comes out as a staircase of repeated values.
            const auto centre = std::clamp(0.5f * (lower + upper), 0.0f, static_cast<float>(lastBin));
            const auto low = static_cast<int>(centre);
            const auto high = std::min(low + 1, lastBin);
            const auto fraction = centre - static_cast<float>(low);
            peak = magnitude[static_cast<size_t>(low)] * (1.0f - fraction)
                 + magnitude[static_cast<size_t>(high)] * fraction;
        }
        else
        {
            const auto first = std::clamp(static_cast<int>(std::floor(lower)), 0, lastBin);
            const auto last = std::clamp(static_cast<int>(std::ceil(upper)), first, lastBin);
            for (int bin = first; bin <= last; ++bin)
                peak = std::max(peak, magnitude[static_cast<size_t>(bin)]);
        }

        const auto measured = std::max(dbFromMagnitude(peak, fftSize), floorDb);
        auto& value = display[static_cast<size_t>(i)];
        // Instant attack, slow release: a transient has to be visible on the
        // frame it happened, and everything else has to stay still enough to
        // read a shape off.
        value = measured >= value ? measured : std::max(measured, value - decayDb);
    }
}

bool SpectrumReader::update(const SpectrumTap& tap, float decayDb)
{
    if (!tap.hasRun())
        return false;
    tap.readTail(tail.data(), fftSize);
    analyse(tail.data(), fftSize, tap.rate(), decayDb);
    return true;
}

}
