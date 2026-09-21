#include "VocoderEngine.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr float tinyLevel = 1.0e-7f;

float decibelsToGain(float db)
{
    return db <= -100.0f ? 0.0f : std::pow(10.0f, db * 0.05f);
}

// One-pole coefficient for a follower that reaches 1/e of a step in `ms`.
float followerCoefficient(float ms, double sampleRate)
{
    const auto samples = std::max(1.0, static_cast<double>(ms) * 0.001 * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

float follow(float current, float target, float attack, float release)
{
    const auto coefficient = target > current ? attack : release;
    return target + (current - target) * coefficient;
}
}

void VocoderEngine::Biquad::setBandpass(double frequency, double q, double rate)
{
    // Constant skirt gain, peak gain = Q -- normalised below so the peak is
    // unity, which is what lets the band envelopes be read as amplitudes.
    const auto nyquist = rate * 0.5;
    frequency = std::clamp(frequency, 10.0, nyquist * 0.98);
    q = std::clamp(q, 0.25, 40.0);
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / rate;
    const auto sinOmega = std::sin(omega);
    const auto cosOmega = std::cos(omega);
    const auto alpha = sinOmega / (2.0 * q);
    const auto a0 = 1.0 + alpha;
    b0 = alpha / a0;
    b1 = 0.0;
    b2 = -alpha / a0;
    a1 = -2.0 * cosOmega / a0;
    a2 = (1.0 - alpha) / a0;
}

float VocoderEngine::bandCentreHz(int index, int bands, float lowHz, float highHz)
{
    bands = std::clamp(bands, minBands, maxBands);
    index = std::clamp(index, 0, bands - 1);
    lowHz = std::clamp(lowHz, lowestRangeHz, highestRangeHz);
    highHz = std::clamp(highHz, lowHz * 1.5f, highestRangeHz);
    // Log spaced, and each band sits in the middle of its slice rather than on
    // the edge, so the first and last bands are as wide as the ones between.
    const auto ratio = std::log(highHz / lowHz);
    const auto position = (static_cast<float>(index) + 0.5f) / static_cast<float>(bands);
    return lowHz * std::exp(static_cast<float>(ratio) * position);
}

void VocoderEngine::prepare(double rate, int channels, int maxBlockSize)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    const auto size = static_cast<size_t>(std::max(64, maxBlockSize));
    monoModulator.assign(size, 0.0f);
    gateScratch.assign(size, 0.0f);
    noiseScratch.assign(size, 0.0f);
    dryLeft.assign(size, 0.0f);
    dryRight.assign(size, 0.0f);
    for (auto& scratch : carrierScratch)
        scratch.assign(size, 0.0f);
    (void) channels;   // the bank is laid out per band, not per channel
    builtRate = 0.0;   // forces the bank to be rebuilt for the new rate
    reset();
}

void VocoderEngine::reset()
{
    for (auto& b : band)
    {
        for (auto& state : b.modulatorState)
            state.clear();
        for (auto& channel : b.carrierState)
            for (auto& state : channel)
                state.clear();
        b.envelope = 0.0f;
        b.carrierEnvelope = 0.0f;
    }
    carrierLevel = modulatorLevel = sibilantLevel = fullLevel = 0.0f;
    sibilanceLowpass = 0.0;
    for (auto& level : readoutLevel)
        level.store(0.0f, std::memory_order_relaxed);
    readoutCarrier.store(false, std::memory_order_relaxed);
    readoutModulator.store(false, std::memory_order_relaxed);
}

void VocoderEngine::setSettings(const Settings& next)
{
    settings = next;
    settings.bands = std::clamp(settings.bands, minBands, maxBands);
    settings.lowHz = std::clamp(settings.lowHz, lowestRangeHz, highestRangeHz * 0.5f);
    settings.highHz = std::clamp(settings.highHz, settings.lowHz * 1.5f, highestRangeHz);
    settings.bandwidth = std::clamp(settings.bandwidth, 0.25f, 4.0f);
    settings.formantSemitones = std::clamp(settings.formantSemitones, -24.0f, 24.0f);
    settings.depth = std::clamp(settings.depth, 0.0f, 1.0f);
    settings.enhance = std::clamp(settings.enhance, 0.0f, 1.0f);
    settings.unvoiced = std::clamp(settings.unvoiced, 0.0f, 1.0f);
    settings.dryWet = std::clamp(settings.dryWet, 0.0f, 1.0f);
}

void VocoderEngine::rebuildBank()
{
    const auto bands = settings.bands;
    // Width in octaves of one slice, which is what the bands would have to be
    // to meet edge to edge. The bandwidth control scales it from there.
    const auto span = std::log2(settings.highHz / settings.lowHz);
    const auto octaves = std::max(0.02f, static_cast<float>(span) / static_cast<float>(bands) * settings.bandwidth);
    // The standard bandwidth-to-Q identity: a band `octaves` wide between its
    // -3 dB points has this Q.
    constexpr double halfLn2 = 0.34657359027997264;
    const auto q = 1.0 / (2.0 * std::sinh(halfLn2 * octaves));
    const auto formantRatio = std::pow(2.0, settings.formantSemitones / 12.0);

    for (int i = 0; i < bands; ++i)
    {
        const auto centre = bandCentreHz(i, bands, settings.lowHz, settings.highHz);
        band[static_cast<size_t>(i)].modulator.setBandpass(centre, q, sampleRate);
        band[static_cast<size_t>(i)].carrier.setBandpass(centre * formantRatio, q, sampleRate);
    }
    // A band that has just moved carries the previous band's memory, which is
    // a burst rather than a slide. Cheaper to clear than to crossfade, and
    // these are chooser-shaped controls rather than ones a hand rides.
    if (builtBands != bands || builtRate != sampleRate)
        for (auto& b : band)
        {
            for (auto& state : b.modulatorState) state.clear();
            for (auto& channel : b.carrierState)
                for (auto& state : channel) state.clear();
            b.envelope = 0.0f;
            b.carrierEnvelope = 0.0f;
        }

    builtBands = bands;
    builtLow = settings.lowHz;
    builtHigh = settings.highHz;
    builtBandwidth = settings.bandwidth;
    builtFormant = settings.formantSemitones;
    builtRate = sampleRate;
}

void VocoderEngine::process(float* const* data, const float* const* carrier, int channels, int count)
{
    if (data == nullptr || channels <= 0 || count <= 0)
        return;
    if (static_cast<size_t>(count) > monoModulator.size())
        prepare(sampleRate, channels, count);

    const auto bands = settings.bands;
    if (builtBands != bands || builtLow != settings.lowHz || builtHigh != settings.highHz
        || builtBandwidth != settings.bandwidth || builtFormant != settings.formantSemitones
        || builtRate != sampleRate)
        rebuildBank();

    const auto used = std::min(channels, 2);
    const auto scale = 1.0f / static_cast<float>(used);

    // ---- the modulator, in mono, plus the dry copy the blend needs ---------
    float modulatorPeak = 0.0f;
    for (int n = 0; n < count; ++n)
    {
        auto sum = 0.0f;
        for (int channel = 0; channel < used; ++channel)
            sum += data[channel][n];
        const auto mono = sum * scale;
        monoModulator[static_cast<size_t>(n)] = mono;
        modulatorPeak = std::max(modulatorPeak, std::abs(mono));
    }
    for (int n = 0; n < count; ++n)
        dryLeft[static_cast<size_t>(n)] = data[0][n];
    if (used > 1)
        for (int n = 0; n < count; ++n)
            dryRight[static_cast<size_t>(n)] = data[1][n];

    const auto attack = followerCoefficient(settings.attackMs, sampleRate);
    const auto release = followerCoefficient(settings.releaseMs, sampleRate);
    // The broadband followers are deliberately slower than the band ones: they
    // decide whether there is a voice at all, and that answer should not
    // flicker between the syllables of a word.
    const auto slowAttack = followerCoefficient(5.0f, sampleRate);
    const auto slowRelease = followerCoefficient(120.0f, sampleRate);
    const auto gateLinear = decibelsToGain(settings.gateDb);

    // ---- the gate, and how sibilant the voice is right now ----------------
    // The sibilance measure is the energy above roughly 3.5 kHz against the
    // whole, which separates an s from a vowel cleanly and costs one pole.
    const auto hpCoefficient = std::exp(-2.0 * 3.14159265358979323846 * 3500.0 / sampleRate);
    for (int n = 0; n < count; ++n)
    {
        const auto mono = monoModulator[static_cast<size_t>(n)];
        sibilanceLowpass = mono * (1.0 - hpCoefficient) + sibilanceLowpass * hpCoefficient;
        const auto high = std::abs(mono - static_cast<float>(sibilanceLowpass));
        sibilantLevel = follow(sibilantLevel, high, slowAttack, slowRelease);
        fullLevel = follow(fullLevel, std::abs(mono), slowAttack, slowRelease);
        modulatorLevel = fullLevel;
        gateScratch[static_cast<size_t>(n)] = modulatorLevel > gateLinear ? 1.0f : 0.0f;
    }
    // Smooth the gate's own edges, so a syllable that crosses the threshold is
    // let in rather than clicked in.
    {
        auto smoothed = gateScratch[0];
        const auto edge = followerCoefficient(3.0f, sampleRate);
        for (int n = 0; n < count; ++n)
        {
            const auto target = gateScratch[static_cast<size_t>(n)];
            smoothed = target + (smoothed - target) * edge;
            gateScratch[static_cast<size_t>(n)] = smoothed;
        }
    }

    const auto haveModulator = modulatorPeak > tinyLevel;
    readoutModulator.store(haveModulator, std::memory_order_relaxed);

    // ---- no carrier wired: the voice passes through, and the face says so --
    if (carrier == nullptr)
    {
        readoutCarrier.store(false, std::memory_order_relaxed);
        readoutBands.store(bands, std::memory_order_relaxed);
        for (int i = 0; i < maxBands; ++i)
            readoutLevel[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        return;
    }

    // ---- the carrier, held at a fixed level and given its noise -----------
    float carrierPeak = 0.0f;
    for (int n = 0; n < count; ++n)
    {
        auto sum = 0.0f;
        for (int channel = 0; channel < used; ++channel)
            sum += carrier[channel][n];
        carrierLevel = follow(carrierLevel, std::abs(sum * scale), slowAttack, slowRelease);
        carrierPeak = std::max(carrierPeak, std::abs(sum * scale));
        // Limited either way: a silent carrier must not be amplified into its
        // own noise floor, and a hot one must not be crushed to nothing.
        const auto normalise = std::clamp(carrierTargetRms / std::max(carrierLevel, tinyLevel), 0.02f, 40.0f);
        for (int channel = 0; channel < used; ++channel)
            carrierScratch[static_cast<size_t>(channel)][static_cast<size_t>(n)] = carrier[channel][n] * normalise;

        // An s has no pitch, so no carrier can play it. Noise, let in only
        // while the voice is actually sibilant, is what every vocoder since
        // the EMS 2000 has done about that.
        const auto sibilance = std::clamp(sibilantLevel / std::max(fullLevel, tinyLevel) * 2.5f, 0.0f, 1.0f);
        noiseSeed ^= noiseSeed << 13;
        noiseSeed ^= noiseSeed >> 17;
        noiseSeed ^= noiseSeed << 5;
        const auto noise = static_cast<float>(static_cast<int>(noiseSeed)) * 4.6566129e-10f;
        noiseScratch[static_cast<size_t>(n)] = noise * settings.unvoiced * sibilance * carrierTargetRms * 2.0f;
    }
    readoutCarrier.store(carrierPeak > tinyLevel, std::memory_order_relaxed);

    // ---- the bank ---------------------------------------------------------
    // Depth measures each band against the average of the bank, and that
    // average is taken from where the envelopes stood at the end of the last
    // block. A block of lag on the *reference* is inaudible; the modulation
    // itself is never delayed.
    auto meanEnvelope = 0.0f;
    for (int i = 0; i < bands; ++i)
        meanEnvelope += band[static_cast<size_t>(i)].envelope;
    meanEnvelope /= static_cast<float>(bands);

    auto meanCarrierEnvelope = 0.0f;
    for (int i = 0; i < bands; ++i)
        meanCarrierEnvelope += band[static_cast<size_t>(i)].carrierEnvelope;
    meanCarrierEnvelope = std::max(meanCarrierEnvelope / static_cast<float>(bands), tinyLevel);

    for (int channel = 0; channel < used; ++channel)
        std::fill(data[channel], data[channel] + count, 0.0f);

    const auto carrierFollow = followerCoefficient(30.0f, sampleRate);
    const auto depth = settings.depth;
    const auto enhance = settings.enhance;
    // With enhance at zero every band is divided by the same number, so the
    // carrier keeps its own spectrum and only its level is corrected; at one
    // each band is divided by its own energy and the carrier is flattened,
    // which is what makes a dull carrier speak.
    const auto uniformNormalise = std::min(1.0f / meanCarrierEnvelope, 60.0f);
    // The floor under a band's own energy, so a band the carrier has nothing
    // in is not amplified into whatever noise it does have.
    const auto carrierFloor = std::max(meanCarrierEnvelope * 0.02f, tinyLevel);

    for (int i = 0; i < bands; ++i)
    {
        auto& b = band[static_cast<size_t>(i)];
        auto envelope = b.envelope;
        auto carrierEnvelope = b.carrierEnvelope;
        auto& modulatorState = b.modulatorState;
        const auto& modulatorCoefficients = b.modulator;
        const auto& carrierCoefficients = b.carrier;

        for (int n = 0; n < count; ++n)
        {
            auto sample = static_cast<double>(monoModulator[static_cast<size_t>(n)]);
            for (auto& state : modulatorState)
                sample = state.process(modulatorCoefficients, sample);
            envelope = follow(envelope, std::abs(static_cast<float>(sample)), attack, release);

            const auto gain = (depth * envelope + (1.0f - depth) * meanEnvelope)
                              * gateScratch[static_cast<size_t>(n)];
            const auto noise = noiseScratch[static_cast<size_t>(n)];

            float filtered[2] {};
            for (int channel = 0; channel < used; ++channel)
            {
                auto carrierSample = static_cast<double>(
                    carrierScratch[static_cast<size_t>(channel)][static_cast<size_t>(n)] + noise);
                for (auto& state : b.carrierState[static_cast<size_t>(channel)])
                    carrierSample = state.process(carrierCoefficients, carrierSample);
                filtered[channel] = static_cast<float>(carrierSample);
            }
            // The normalisation is a property of the band, not of a channel:
            // measuring it on one side and applying it to both is what keeps
            // a stereo carrier from being narrowed by its own level control.
            carrierEnvelope = follow(carrierEnvelope, std::abs(filtered[0]), carrierFollow, carrierFollow);
            const auto ownNormalise = std::min(1.0f / std::max(carrierEnvelope, carrierFloor), 60.0f);
            const auto normalise = (uniformNormalise + (ownNormalise - uniformNormalise) * enhance) * gain;
            for (int channel = 0; channel < used; ++channel)
                data[channel][n] += filtered[channel] * normalise;
        }
        b.envelope = envelope;
        b.carrierEnvelope = carrierEnvelope;
        readoutLevel[static_cast<size_t>(i)].store(envelope, std::memory_order_relaxed);
    }
    for (int i = bands; i < maxBands; ++i)
        readoutLevel[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
    readoutBands.store(bands, std::memory_order_relaxed);

    // ---- level, blend, and the channels past the second --------------------
    const auto outputGain = decibelsToGain(settings.outputGainDb);
    const auto wet = settings.dryWet;
    for (int channel = 0; channel < used; ++channel)
    {
        const auto* dry = channel == 0 ? dryLeft.data() : dryRight.data();
        auto* out = data[channel];
        for (int n = 0; n < count; ++n)
            out[n] = out[n] * outputGain * wet + dry[n] * (1.0f - wet);
    }
    for (int channel = used; channel < channels; ++channel)
        std::copy(data[0], data[0] + count, data[channel]);

    flushDenormals();
}

// A bandpass with a high Q left with no input decays for a long time, and a
// denormal in its state costs a hundred times what a normal one does. Once a
// block is enough: the states are doubles, so the drop to denormal takes
// thousands of samples.
void VocoderEngine::flushDenormals()
{
    constexpr double floorLevel = 1.0e-20;
    const auto flush = [](double& value) { if (std::abs(value) < floorLevel) value = 0.0; };
    for (auto& b : band)
    {
        for (auto& state : b.modulatorState) { flush(state.z1); flush(state.z2); }
        for (auto& channel : b.carrierState)
            for (auto& state : channel) { flush(state.z1); flush(state.z2); }
    }
}

VocoderEngine::Readout VocoderEngine::readout() const
{
    Readout snapshot;
    snapshot.bands = readoutBands.load(std::memory_order_relaxed);
    snapshot.carrierPresent = readoutCarrier.load(std::memory_order_relaxed);
    snapshot.modulatorPresent = readoutModulator.load(std::memory_order_relaxed);
    for (int i = 0; i < maxBands; ++i)
        snapshot.level[static_cast<size_t>(i)] = readoutLevel[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    return snapshot;
}

}
