#include "PsolaShifter.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr int hannPoints = 2048;

int nextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value) result *= 2;
    return result;
}

// Four-point third-order Hermite.  A grain is read at a fractional rate
// whenever the formants move, and linear interpolation there is audible as a
// dull, slightly gritty top end -- the error is a comb whose first null sits
// at the sample rate, so it lands squarely in the sibilance.
float hermite(float previous, float here, float next, float after, float t)
{
    const auto c = (next - previous) * 0.5f;
    const auto v = here - next;
    const auto w = c + v;
    const auto a = w + v + (after - here) * 0.5f;
    const auto b = w + a;
    return ((a * t - b) * t + c) * t + here;
}
}

// A grain is never cut longer than a period and a quarter: moving the
// formants down stretches it, and past that the stretch is smearing two
// glottal pulses together rather than lengthening one.
//
// The delay is what pays for that.  A grain has to be laid into the output
// one full length past the block it belongs to, or its leading half would
// land on samples that have already gone out; it reaches a period back from
// its own centre to pick up the pulse; and the mark it is cut on can sit up
// to half a period from where the synthesis clock says, because a grain is
// repeated or dropped whole rather than in fractions.  Together that is three
// and a half periods of input the shifter must already hold.
int PsolaShifter::minimumLatencyFor(int longestPeriod)
{
    return static_cast<int>(std::ceil(3.5 * std::max(8, longestPeriod)));
}

void PsolaShifter::prepare(double rate, int channels, int maxBlockSize, int longestPossiblePeriod)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    preparedChannels = std::max(1, channels);
    capacityPeriod = std::max(8, longestPossiblePeriod);

    const auto block = std::max(64, maxBlockSize);
    const auto worstLatency = minimumLatencyFor(capacityPeriod) + 4 * capacityPeriod;
    const auto inputSize = nextPowerOfTwo(worstLatency + 4 * capacityPeriod + block + 64);
    const auto outputSize = nextPowerOfTwo(block + 3 * capacityPeriod + 64);
    inputMask = inputSize - 1;
    outputMask = outputSize - 1;

    input.assign(static_cast<size_t>(preparedChannels), std::vector<float>(static_cast<size_t>(inputSize), 0.0f));
    output.assign(static_cast<size_t>(preparedChannels), std::vector<float>(static_cast<size_t>(outputSize), 0.0f));
    mono.assign(static_cast<size_t>(inputSize), 0.0f);
    windowSum.assign(static_cast<size_t>(outputSize), 0.0f);

    hannTable.assign(static_cast<size_t>(hannPoints) + 1, 0.0f);
    for (int i = 0; i <= hannPoints; ++i)
        hannTable[static_cast<size_t>(i)] =
            0.5f - 0.5f * static_cast<float>(std::cos(2.0 * 3.14159265358979323846 * i / hannPoints));

    configure(minimumLatencyFor(capacityPeriod), capacityPeriod);
}

void PsolaShifter::configure(int latencySamples, int longestPeriodForRange)
{
    longestPeriod = std::clamp(longestPeriodForRange, 8, capacityPeriod);
    maxGrainHalf = static_cast<int>(std::ceil(1.25 * longestPeriod));
    latency = std::max(latencySamples, minimumLatencyFor(longestPeriod));
    reset();
}

void PsolaShifter::reset()
{
    for (auto& ring : input) std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& ring : output) std::fill(ring.begin(), ring.end(), 0.0f);
    std::fill(mono.begin(), mono.end(), 0.0f);
    std::fill(windowSum.begin(), windowSum.end(), 0.0f);
    // The ring already holds `latency` samples of silence, which is what puts
    // the output exactly that far behind the input from the first block.  One
    // index therefore means the same sample on both sides, and the delay
    // shows up as the grains being able to read past where the output has
    // got to rather than as an offset between the two clocks.
    inputWritten = latency;
    outputProduced = 0;
    synthPosition = 0.0;
    analysisPosition = 0.0;
    epochOffset = 0.0;
    started = false;
    current = {};
}

float PsolaShifter::hann(double phase) const
{
    const auto scaled = std::clamp(phase, 0.0, 1.0) * hannPoints;
    const auto index = static_cast<int>(scaled);
    const auto fraction = static_cast<float>(scaled - index);
    const auto lower = hannTable[static_cast<size_t>(std::min(index, hannPoints))];
    const auto upper = hannTable[static_cast<size_t>(std::min(index + 1, hannPoints))];
    return lower + (upper - lower) * fraction;
}

float PsolaShifter::readInput(int channel, double position) const
{
    const auto base = static_cast<long long>(std::floor(position));
    const auto fraction = static_cast<float>(position - static_cast<double>(base));
    const auto& ring = input[static_cast<size_t>(channel)];
    const auto at = [&ring, this] (long long index)
    {
        return ring[static_cast<size_t>(index & inputMask)];
    };
    return hermite(at(base - 1), at(base), at(base + 1), at(base + 2), fraction);
}

void PsolaShifter::emitGrain(int channels)
{
    const auto period = static_cast<double>(std::clamp(current.periodSamples, 8.0f, static_cast<float>(longestPeriod)));
    const auto formant = static_cast<double>(std::clamp(current.formantRatio, 0.5f, 2.0f));
    const auto pitch = static_cast<double>(std::clamp(current.pitchRatio, 0.25f, 4.0f));

    // The grain covers two periods of source however the formants move: the
    // read step and the output length cancel, so what changes is how long
    // that source is stretched over, which is exactly a formant shift.
    const auto halfLength = std::clamp(period / formant, 8.0, static_cast<double>(maxGrainHalf));
    const auto span = halfLength * 2.0;

    // Nudge the mark onto the loudest sample nearby, which for a voice is the
    // glottal pulse.  Grains cut on the pulse cancel coherently when they
    // overlap; grains cut at an arbitrary phase beat against each other and
    // the result sounds like a chorus laid over the take.
    if (current.voiced)
    {
        const auto search = static_cast<long long>(period * 0.25);
        const auto centre = static_cast<long long>(std::llround(analysisPosition));
        auto bestOffset = 0LL;
        auto bestLevel = -1.0f;
        for (auto offset = -search; offset <= search; ++offset)
        {
            const auto level = std::abs(mono[static_cast<size_t>((centre + offset) & inputMask)]);
            if (level > bestLevel)
            {
                bestLevel = level;
                bestOffset = offset;
            }
        }
        // Slewed rather than jumped: a single mis-picked peak should bend the
        // marks, not step them.
        epochOffset += (static_cast<double>(bestOffset) - epochOffset) * 0.35;
        epochOffset = std::clamp(epochOffset, -period * 0.25, period * 0.25);
    }
    else
    {
        epochOffset *= 0.5;
    }

    // How much window lands on each output sample once the grains are laid
    // down.  At unity it is exactly one, which is why an untouched signal
    // comes back out of here sample for sample.  Below one the grains have
    // stopped touching -- pitching down, or moving the formants up, leaves
    // real silence between the pulses -- and what is lost there is energy, so
    // that is what gets put back rather than amplitude.
    const auto coverage = halfLength / (period / pitch);
    const auto grainGain = static_cast<float>(1.0 / std::sqrt(std::min(1.0, coverage)));

    // The offset moves the read and the write together.  Moving only the read
    // would slide the output against the input by whatever the offset happens
    // to be, and because it is slewed rather than fixed that slide is a slow
    // time warp -- an audible wobble on a signal the device was asked to
    // leave alone.
    const auto readCentre = analysisPosition + epochOffset;
    const auto writeCentre = synthPosition + epochOffset;
    const auto writeStart = writeCentre - halfLength;
    const auto count = static_cast<int>(std::ceil(span));
    for (int i = 0; i < count; ++i)
    {
        const auto outputPosition = static_cast<long long>(std::llround(writeStart)) + i;
        if (outputPosition < outputProduced) continue;
        const auto phase = static_cast<double>(i) / span;
        const auto shape = hann(phase);
        const auto source = readCentre + (static_cast<double>(outputPosition) - writeCentre) * formant;
        const auto slot = static_cast<size_t>(outputPosition & outputMask);
        for (int channel = 0; channel < channels; ++channel)
            output[static_cast<size_t>(channel)][slot] += readInput(channel, source) * shape * grainGain;
        windowSum[slot] += shape;
    }

    // Advance.  The synthesis marks carry the new pitch; the analysis marks
    // walk the input at its own rate, and the drift between them is taken out
    // a whole period at a time so a mark never lands off a pulse.
    synthPosition += period / pitch;
    analysisPosition += period;
    const auto drift = synthPosition - analysisPosition;
    if (drift > period * 0.5)
        analysisPosition += period;     // the input ran ahead: drop a grain
    else if (drift < -period * 0.5)
        analysisPosition -= period;     // the input ran behind: repeat one
}

void PsolaShifter::process(float* const* data, int channels, int count)
{
    if (input.empty() || data == nullptr || count <= 0) return;
    channels = std::clamp(channels, 1, preparedChannels);

    for (int i = 0; i < count; ++i)
    {
        const auto index = static_cast<size_t>((inputWritten + i) & inputMask);
        auto sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto sample = data[channel][i];
            input[static_cast<size_t>(channel)][index] = sample;
            sum += sample;
        }
        mono[index] = sum / static_cast<float>(channels);
    }
    inputWritten += count;

    if (!started)
    {
        started = true;
        synthPosition = static_cast<double>(outputProduced);
        analysisPosition = synthPosition;
    }
    current = pending;

    // Everything that can reach into this block has to be laid down before
    // the block is read out, which means scheduling a grain's length past
    // its end -- a grain centred beyond that cannot touch these samples.
    const auto horizon = static_cast<double>(outputProduced + count + maxGrainHalf + longestPeriod / 4);
    // The bound is a guard against a nonsense period, not a real limit: at a
    // sane period this loop runs a handful of times.
    for (int guard = 0; synthPosition < horizon && guard < 4096; ++guard)
        emitGrain(channels);

    for (int i = 0; i < count; ++i)
    {
        const auto slot = static_cast<size_t>((outputProduced + i) & outputMask);
        // Overlap above one is scaled back; overlap below one is left alone.
        // Dividing by a window sum that dips towards zero -- which is exactly
        // what pitching down does, because the grains stop touching -- would
        // turn the quiet part between two pulses into a burst of noise.
        const auto coverage = std::max(1.0f, windowSum[slot]);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& ring = output[static_cast<size_t>(channel)];
            data[channel][i] = ring[slot] / coverage;
            ring[slot] = 0.0f;
        }
        windowSum[slot] = 0.0f;
    }
    outputProduced += count;
}
}
