#include "EqEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
// How long a moved control takes to arrive. Long enough that a sweep has no
// stair in it, short enough that a nudge lands where the number says it did.
constexpr double smoothingSeconds = 0.03;

// A band switching on or off, or changing what kind of filter it is,
// crossfades over this, sample by sample. Block granularity is what makes a
// bypass click, so these do not use the block-rate smoother the parameters do.
constexpr float bypassFadeSeconds = 0.02f;

// One biquad, transposed direct form II.
inline double runStage(const Biquad& filter, std::array<double, 2>& z, double input)
{
    const auto output = filter.b0 * input + z[0];
    z[0] = filter.b1 * input - filter.a1 * output + z[1];
    z[1] = filter.b2 * input - filter.a2 * output;
    return output;
}

double smoothTowards(double current, double target, double coefficient)
{
    return current + (target - current) * coefficient;
}

bool nearlyEqual(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}
}

void EqEngine::prepare(double rateIn, int channels, int maxBlockSize)
{
    sampleRate = rateIn > 0.0 ? rateIn : 48000.0;
    state.assign(static_cast<size_t>(std::max(1, channels)), {});
    spectrum.prepare(sampleRate);
    primed = false;
    (void) maxBlockSize;
    reset();
}

void EqEngine::reset()
{
    for (auto& channel : state)
        for (auto& band : channel)
            for (auto& stage : band)
                stage = {};
    spectrum.reset();
    outputGain = outputGainTarget;
    for (auto& band : runtime)
    {
        band.level = band.levelTarget;
        band.morph = 1.0f;
    }
}

EqBandSettings EqEngine::scaledBand(const EqBandSettings& band, float scalePercent)
{
    auto scaled = band;
    if (eqTypeUsesGain(band.type))
        scaled.gainDb = band.gainDb * std::clamp(scalePercent, 0.0f, 400.0f) * 0.01f;
    return scaled;
}

double EqEngine::responseDbAt(const Settings& settings, double frequency, double sampleRate)
{
    auto total = static_cast<double>(settings.outputGainDb);
    for (const auto& band : settings.band)
        total += eqBandMagnitudeDbAt(scaledBand(band, settings.scalePercent), frequency, sampleRate);
    return total;
}

void EqEngine::setSettings(const Settings& next)
{
    settings = next;
    outputGainTarget = std::pow(10.0, std::clamp(next.outputGainDb, -30.0f, 30.0f) / 20.0);
    for (int i = 0; i < bandCount; ++i)
        runtime[static_cast<size_t>(i)].levelTarget = next.band[static_cast<size_t>(i)].enabled ? 1.0f : 0.0f;

    // The very first block has nothing to smooth from: jumping to the saved
    // settings is right, and sweeping up to them from a default is a swoop
    // every time a project opens.
    if (!primed)
    {
        for (int i = 0; i < bandCount; ++i)
        {
            const auto band = scaledBand(next.band[static_cast<size_t>(i)], next.scalePercent);
            auto& target = runtime[static_cast<size_t>(i)];
            target.logFrequency = std::log(eqClampedFrequency(band.frequency, sampleRate));
            target.gainDb = band.gainDb;
            target.q = std::clamp(band.q, eqMinimumQ, eqMaximumQ);
            target.type = band.type;
            target.level = target.levelTarget;
            target.morph = 1.0f;
            target.coefficientsValid = false;
            refreshCoefficients(target);
            target.leaving = target.stages;
        }
        outputGain = outputGainTarget;
        primed = true;
    }
}

void EqEngine::refreshCoefficients(BandRuntime& band)
{
    EqBandSettings resolved;
    resolved.enabled = true;
    resolved.type = band.type;
    resolved.frequency = static_cast<float>(std::exp(band.logFrequency));
    resolved.gainDb = static_cast<float>(band.gainDb);
    resolved.q = static_cast<float>(band.q);
    band.stages = eqStagesFor(resolved, sampleRate);
    band.coefficientsValid = true;
}

void EqEngine::flushDenormals()
{
    // Filter state decaying in silence reaches denormal range and stalls the
    // FPU there. Checked once a block rather than once a sample: the state is
    // already inaudible long before it gets that small.
    for (auto& channel : state)
        for (auto& band : channel)
            for (auto& stage : band)
                for (auto* pair : {&stage.current, &stage.leaving})
                    for (auto& value : *pair)
                        if (std::abs(value) < 1.0e-25)
                            value = 0.0;
}

void EqEngine::process(float* const* data, int channels, int count)
{
    if (data == nullptr || channels <= 0 || count <= 0 || state.empty())
        return;
    const auto usedChannels = std::min(channels, static_cast<int>(state.size()));

    if (settings.analyser == AnalyserMode::Pre)
        spectrum.write(data, usedChannels, count);

    const auto blockSeconds = count / sampleRate;
    const auto coefficient = std::clamp(1.0 - std::exp(-blockSeconds / smoothingSeconds), 0.0, 1.0);
    const auto fadeStep = 1.0f / std::max(1.0f, bypassFadeSeconds * static_cast<float>(sampleRate));

    for (int index = 0; index < bandCount; ++index)
    {
        auto& band = runtime[static_cast<size_t>(index)];
        const auto wanted = scaledBand(settings.band[static_cast<size_t>(index)], settings.scalePercent);
        const auto targetLog = std::log(eqClampedFrequency(wanted.frequency, sampleRate));
        const auto targetGain = static_cast<double>(wanted.gainDb);
        const auto targetQ = static_cast<double>(std::clamp(wanted.q, eqMinimumQ, eqMaximumQ));

        const auto nextLog = smoothTowards(band.logFrequency, targetLog, coefficient);
        const auto nextGain = smoothTowards(band.gainDb, targetGain, coefficient);
        const auto nextQ = smoothTowards(band.q, targetQ, coefficient);

        // A static band pays no trigonometry. Eight bands recomputing four
        // biquads each every block is not expensive, but it is not free
        // either, and an EQ nobody is touching is the common case.
        const auto typeChanged = band.coefficientsValid && band.type != wanted.type;
        const auto moved = !band.coefficientsValid
            || typeChanged
            || !nearlyEqual(nextLog, band.logFrequency, 1.0e-7)
            || !nearlyEqual(nextGain, band.gainDb, 1.0e-5)
            || !nearlyEqual(nextQ, band.q, 1.0e-6);

        band.logFrequency = nextLog;
        band.gainDb = nextGain;
        band.q = nextQ;
        band.type = wanted.type;
        // A type change is the one parameter that cannot be smoothed towards:
        // the new coefficients have nothing to do with the old ones. The
        // filter that is leaving keeps running on a copy of the state it had,
        // and the two are crossfaded, which starts the blend at exactly what
        // the band was already producing.
        if (typeChanged)
        {
            band.leaving = band.stages;
            band.morph = 0.0f;
            for (int channel = 0; channel < usedChannels; ++channel)
                for (auto& stage : state[static_cast<size_t>(channel)][static_cast<size_t>(index)])
                    stage.leaving = stage.current;
        }
        if (moved)
            refreshCoefficients(band);

        if (band.level <= 0.0f && band.levelTarget <= 0.0f)
            continue;

        auto levelAfter = band.level;
        auto morphAfter = band.morph;
        for (int channel = 0; channel < usedChannels; ++channel)
        {
            auto* samples = data[channel];
            if (samples == nullptr) continue;
            auto& stages = state[static_cast<size_t>(channel)][static_cast<size_t>(index)];
            auto level = band.level;
            auto morph = band.morph;
            for (int i = 0; i < count; ++i)
            {
                const auto dry = static_cast<double>(samples[i]);
                auto wet = dry;
                for (int s = 0; s < band.stages.count; ++s)
                    wet = runStage(band.stages.stage[static_cast<size_t>(s)],
                                   stages[static_cast<size_t>(s)].current, wet);
                if (morph < 1.0f)
                {
                    auto old = dry;
                    for (int s = 0; s < band.leaving.count; ++s)
                        old = runStage(band.leaving.stage[static_cast<size_t>(s)],
                                       stages[static_cast<size_t>(s)].leaving, old);
                    wet = old + morph * (wet - old);
                    morph = std::min(1.0f, morph + fadeStep);
                }
                samples[i] = static_cast<float>(dry + level * (wet - dry));
                level = band.levelTarget > band.level
                    ? std::min(band.levelTarget, level + fadeStep)
                    : std::max(band.levelTarget, level - fadeStep);
            }
            levelAfter = level;
            morphAfter = morph;
        }
        band.level = levelAfter;
        band.morph = morphAfter;
    }

    if (!nearlyEqual(outputGain, outputGainTarget, 1.0e-7) || outputGain != 1.0)
    {
        const auto gainStep = (outputGainTarget - outputGain) / count;
        for (int channel = 0; channel < usedChannels; ++channel)
        {
            auto* samples = data[channel];
            if (samples == nullptr) continue;
            auto gain = outputGain;
            for (int i = 0; i < count; ++i)
            {
                samples[i] = static_cast<float>(samples[i] * gain);
                gain += gainStep;
            }
        }
        outputGain = outputGainTarget;
    }

    flushDenormals();

    if (settings.analyser == AnalyserMode::Post)
        spectrum.write(data, usedChannels, count);
}

}
