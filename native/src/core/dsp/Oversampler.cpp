#include "Oversampler.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// The zeroth-order modified Bessel function, by its power series. The Kaiser
// window needs it at arguments under 10, where forty terms are far more than
// double precision can use.
double besselI0(double x)
{
    auto sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

// The steep first doubling and the short second, as pairs of side taps. A
// half-band filter of K pairs is 4K - 1 taps long and delays by 2K - 1 inner
// samples, which is what the latency arithmetic in prepare() rests on.
constexpr int firstStagePairs = 16;
constexpr int secondStagePairs = 6;
constexpr double stopbandDb = 80.0;
}

Oversampler::Stage Oversampler::design(int halfLength, double attenuationDb, int channelCount)
{
    // The ideal half-band low-pass, cut at a quarter of the inner rate, is
    // 0.5 sinc(n / 2): a half at the centre, zero at every other even offset,
    // and (-1)^j / (pi (2j + 1)) at offset 2j + 1. The Kaiser window trades
    // the transition's width for how far the stopband is held down.
    Stage stage;
    stage.halfLength = halfLength;
    stage.sideTaps.resize(static_cast<size_t>(halfLength));
    const auto centre = 2.0 * halfLength - 1.0;
    const auto beta = 0.1102 * (attenuationDb - 8.7);
    const auto norm = besselI0(beta);
    auto sum = 0.0;
    for (int j = 0; j < halfLength; ++j)
    {
        const auto offset = 2.0 * j + 1.0;
        const auto ideal = ((j % 2 == 0) ? 1.0 : -1.0) / (pi * offset);
        const auto ratio = offset / centre;
        const auto window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / norm;
        stage.sideTaps[static_cast<size_t>(j)] = static_cast<float>(ideal * window);
        sum += 2.0 * ideal * window;
    }
    // Windowing moves the gain at DC a little; the side taps are scaled back
    // so the whole filter passes DC at exactly one.
    for (auto& tap : stage.sideTaps)
        tap = static_cast<float>(tap * 0.5 / sum);

    stage.channels.resize(static_cast<size_t>(channelCount));
    for (auto& channel : stage.channels)
    {
        channel.upHistory.assign(static_cast<size_t>(halfLength) * 4, 0.0f);
        channel.evenHistory.assign(static_cast<size_t>(halfLength) * 4, 0.0f);
        channel.oddHistory.assign((static_cast<size_t>(halfLength) + 1) * 2, 0.0f);
    }
    return stage;
}

void Oversampler::prepare(int factor, int channels, int maximumBlockSize)
{
    ratio = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    channels = std::max(1, channels);
    maximumBlockSize = std::max(1, maximumBlockSize);
    stages.clear();
    if (ratio >= 2)
        stages.push_back(design(firstStagePairs, stopbandDb, channels));
    if (ratio >= 4)
        stages.push_back(design(secondStagePairs, stopbandDb, channels));

    // Each doubling's round trip is twice its delay, 4K - 2 samples at its
    // inner rate. At two times that is 2K - 1 outer samples, already whole.
    // The second doubling's 4K - 2 samples at four times are half a sample
    // short of a whole one, so two more samples of delay at four times make
    // it K exactly, and the whole trip a whole number of outer samples.
    latency = 0;
    padLength = 0;
    if (ratio >= 2)
        latency += 2 * firstStagePairs - 1;
    if (ratio >= 4)
    {
        padLength = 2;
        latency += secondStagePairs;
    }

    twice.assign(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(maximumBlockSize) * 2, 0.0f));
    fourTimes.assign(static_cast<size_t>(channels),
                     std::vector<float>(ratio >= 4 ? static_cast<size_t>(maximumBlockSize) * 4 : 0, 0.0f));
    pad.assign(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(padLength), 0.0f));
    padAt.assign(static_cast<size_t>(channels), 0);
    reset();
}

void Oversampler::reset()
{
    for (auto& stage : stages)
        for (auto& channel : stage.channels)
        {
            std::fill(channel.upHistory.begin(), channel.upHistory.end(), 0.0f);
            std::fill(channel.evenHistory.begin(), channel.evenHistory.end(), 0.0f);
            std::fill(channel.oddHistory.begin(), channel.oddHistory.end(), 0.0f);
            channel.upAt = channel.evenAt = channel.oddAt = 0;
        }
    for (auto& line : pad)
        std::fill(line.begin(), line.end(), 0.0f);
    std::fill(padAt.begin(), padAt.end(), 0);
}

// One input sample x[m] makes two outputs. With s_j the side taps:
//   y[2m]     = 2 sum_j s_j (x[m - (K - 1 - j)] + x[m - (K + j)])
//   y[2m + 1] = x[m - (K - 1)]
// the factor of two making up for the zeros the doubling stuffs in. The ring
// holds the last 2K inputs and is written twice, so x[m - i] is always
// ring[at + 2K - i] without a wrap inside the loop.
void Oversampler::upStage(Stage& stage, int channelIndex, const float* input, float* output, int count)
{
    auto& channel = stage.channels[static_cast<size_t>(channelIndex)];
    const auto k = stage.halfLength;
    const auto length = 2 * k;
    auto* ring = channel.upHistory.data();
    const auto* taps = stage.sideTaps.data();
    for (int i = 0; i < count; ++i)
    {
        channel.upAt = (channel.upAt + 1) % length;
        ring[channel.upAt] = ring[channel.upAt + length] = input[i];
        const auto* newest = ring + channel.upAt + length;   // newest[-i] is x[m - i]
        auto even = 0.0f;
        for (int j = 0; j < k; ++j)
            even += taps[j] * (newest[-(k - 1 - j)] + newest[-(k + j)]);
        output[2 * i] = 2.0f * even;
        output[2 * i + 1] = newest[-(k - 1)];
    }
}

// Two inputs, the even v[2m] then the odd v[2m + 1], make one output, the
// filter's value at the even input's moment, as the upsampler's x[m] stands at
// 2m:
//   y[m] = 0.5 O[m - K] + sum_j s_j (E[m - K + 1 + j] + E[m - K - j])
// where E holds the even inputs and O the odd ones. The centre tap only ever
// meets odd inputs and every side tap only even ones, which is half the work
// a plain FIR would do. Taking the value at the odd input's moment instead
// came out half an outer sample early, and the dry signal no longer lined up.
void Oversampler::downStage(Stage& stage, int channelIndex, const float* input, float* output, int count)
{
    auto& channel = stage.channels[static_cast<size_t>(channelIndex)];
    const auto k = stage.halfLength;
    const auto evenLength = 2 * k;
    const auto oddLength = k + 1;
    auto* even = channel.evenHistory.data();
    auto* odd = channel.oddHistory.data();
    const auto* taps = stage.sideTaps.data();
    for (int i = 0; i < count; ++i)
    {
        channel.evenAt = (channel.evenAt + 1) % evenLength;
        even[channel.evenAt] = even[channel.evenAt + evenLength] = input[2 * i];
        channel.oddAt = (channel.oddAt + 1) % oddLength;
        odd[channel.oddAt] = odd[channel.oddAt + oddLength] = input[2 * i + 1];
        const auto* newestEven = even + channel.evenAt + evenLength;   // newestEven[-i] is E[m - i]
        // O[m - K] is the oldest of the last K + 1 odd inputs.
        auto sum = 0.5f * odd[channel.oddAt + 1];
        for (int j = 0; j < k; ++j)
            sum += taps[j] * (newestEven[-(k - 1 - j)] + newestEven[-(k + j)]);
        output[i] = sum;
    }
}

float* Oversampler::up(int channel, const float* input, int count)
{
    auto* first = twice[static_cast<size_t>(channel)].data();
    if (ratio == 1)
    {
        std::copy(input, input + count, first);
        return first;
    }
    upStage(stages[0], channel, input, first, count);
    if (ratio == 2)
        return first;
    auto* second = fourTimes[static_cast<size_t>(channel)].data();
    upStage(stages[1], channel, first, second, count * 2);
    return second;
}

void Oversampler::down(int channel, float* output, int count)
{
    auto* first = twice[static_cast<size_t>(channel)].data();
    if (ratio == 1)
    {
        std::copy(first, first + count, output);
        return;
    }
    if (ratio == 4)
    {
        auto* second = fourTimes[static_cast<size_t>(channel)].data();
        auto& line = pad[static_cast<size_t>(channel)];
        auto& at = padAt[static_cast<size_t>(channel)];
        for (int i = 0; i < count * 4; ++i)
        {
            const auto delayed = line[static_cast<size_t>(at)];
            line[static_cast<size_t>(at)] = second[i];
            at = (at + 1) % padLength;
            second[i] = delayed;
        }
        downStage(stages[1], channel, second, first, count * 2);
    }
    downStage(stages[0], channel, first, output, count);
}
}
