#include "DrumSynth.h"
#include <algorithm>
#include <cmath>

namespace rhino
{
namespace
{
constexpr float twoPi = 6.283185307179586f;
// The natural log of a thousand: an envelope multiplied by e to the minus this
// over T seconds has fallen 60 dB by the end of them.
constexpr float sixtyDecibels = 6.907755279f;
// Below this an envelope is inaudible and the strike is over: -80 dB.
constexpr float silence = 1.0e-4f;

constexpr std::array<DrumModelInfo, drumModelCount> models {{
    { "Kick",      "Kick",       "Kick",       0.60f, 0.40f },
    { "Snare",     "Snare",      "Snare",      0.28f, 0.60f },
    { "Tom",       "Tom",        "Tom",        0.50f, 0.30f },
    { "ClosedHat", "Closed Hat", "Hat",        0.07f, 0.60f },
    { "OpenHat",   "Open Hat",   "Hat",        0.45f, 0.60f },
    { "Clap",      "Clap",       "Clap",       0.32f, 0.50f },
    { "Rim",       "Rim",        "Percussion", 0.06f, 0.50f },
    { "Cowbell",   "Cowbell",    "Percussion", 0.35f, 0.50f },
}};

// The TR-808's six hat oscillators, in Hz. They share no common pitch, which
// is what makes their sum sound like metal rather than a chord.
constexpr std::array<float, 6> metal { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };

// What an envelope is multiplied by each sample to fall 60 dB in `seconds`.
float fallingBy60(double rate, float seconds)
{
    return static_cast<float>(std::exp(-sixtyDecibels / (std::max(0.001, static_cast<double>(seconds)) * rate)));
}

// The same for a time constant: down to 1/e in `seconds`.
float fallingBy(double rate, float seconds)
{
    return static_cast<float>(std::exp(-1.0 / (std::max(1.0e-5, static_cast<double>(seconds)) * rate)));
}

// A one-pole low-pass coefficient; the noise minus its low-pass is a high-pass.
float onePole(double rate, float hertz)
{
    return static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * hertz / rate));
}

float square(float phase)
{
    return phase < 0.5f ? 1.0f : -1.0f;
}

// Multiplies an envelope down, and lets it go to nothing once it is far below
// hearing. Left to fall on, a click's few milliseconds reach the denormal
// range long before the drum has finished, where every multiply on x86 is a
// hundred times slower: a kick cost four times what it should.
void fall(float& envelope, float factor) noexcept
{
    envelope *= factor;
    if (envelope < 1.0e-9f)
        envelope = 0.0f;
}
}

const DrumModelInfo& drumModelInfo(DrumModel model)
{
    return models[static_cast<size_t>(juce::jlimit(0, drumModelCount - 1, static_cast<int>(model)))];
}

std::optional<DrumModel> drumModelFromId(const juce::String& id)
{
    for (int i = 0; i < drumModelCount; ++i)
        if (id.equalsIgnoreCase(models[static_cast<size_t>(i)].id))
            return static_cast<DrumModel>(i);
    return std::nullopt;
}

void DrumSynthVoice::Filter::set(double sampleRate, float hertz, float q) noexcept
{
    const auto cutoff = std::clamp(static_cast<double>(hertz), 10.0, sampleRate * 0.45);
    g = static_cast<float>(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate));
    k = 1.0f / std::max(0.1f, q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    ic1 = ic2 = 0.0f;
    low = band = high = 0.0f;
}

void DrumSynthVoice::Filter::run(float input) noexcept
{
    const auto v3 = input - ic2;
    const auto v1 = a1 * ic1 + a2 * v3;
    const auto v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    low = v2;
    band = v1;
    high = input - k * v1 - v2;
}

float DrumSynthVoice::noise() noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(seed)) * (1.0f / 2147483648.0f);
}

float DrumSynthVoice::clampFrequency(float hertz) const noexcept
{
    return std::clamp(hertz, 10.0f, static_cast<float>(rate * 0.45));
}

void DrumSynthVoice::start(DrumModel which, double sampleRate, float tuneRatio, float decaySeconds, float tone,
                           std::uint32_t strikeSeed) noexcept
{
    model = which;
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    step = static_cast<float>(1.0 / rate);
    age = 0;
    seed = strikeSeed;
    tone = std::clamp(tone, 0.0f, 1.0f);
    tuneRatio = std::clamp(tuneRatio, 0.0625f, 16.0f);
    const auto decay = std::clamp(decaySeconds, 0.005f, 30.0f);
    // Nothing rings on past its decay by more than half again, which is 90 dB
    // down, with a tenth of a second to spare for a clap's bursts.
    limit = static_cast<int>(rate * (decay * 1.5 + 0.1));

    phase.fill(0.0f);
    frequency.fill(0.0f);
    body = 1.0f;
    bodyFactor = fallingBy60(rate, decay);
    second = 0.0f;
    secondFactor = 1.0f;
    sweep = 0.0f;
    sweepFactor = 1.0f;
    sweepDepth = 0.0f;
    click = 0.0f;
    clickFactor = 1.0f;
    clickLevel = 0.0f;
    drive = 1.0f;
    driveNormal = 1.0f;
    bodyLevel = 1.0f;
    noiseLevel = 0.0f;
    noiseState = 0.0f;
    noiseCoefficient = 0.0f;
    bursts = {};
    tailStart = 0;
    filter.set(rate, 1000.0f, 0.7f);

    switch (model)
    {
        case DrumModel::Kick:
            // A sine that starts several times higher and falls to its pitch in
            // a few tens of milliseconds, with a click of noise on the front.
            // Tone adds click and drives the whole thing into a soft clip.
            frequency[0] = clampFrequency(52.0f * tuneRatio);
            sweep = 1.0f;
            sweepFactor = fallingBy(rate, 0.024f);
            sweepDepth = 2.6f;
            click = 1.0f;
            clickFactor = fallingBy(rate, 0.0018f);
            clickLevel = 0.12f + 0.55f * tone;
            drive = 1.0f + 2.5f * tone;
            driveNormal = 1.0f / std::tanh(drive);
            noiseCoefficient = onePole(rate, 3000.0f);
            break;

        case DrumModel::Snare:
            // Two drumheads that die early, under the wires: noise, high-passed
            // so it does not muddy the heads, and low-passed by Tone.
            frequency[0] = clampFrequency(180.0f * tuneRatio);
            frequency[1] = clampFrequency(330.0f * tuneRatio);
            sweep = 1.0f;
            sweepFactor = fallingBy(rate, 0.012f);
            sweepDepth = 0.4f;
            bodyFactor = fallingBy60(rate, std::max(0.02f, decay * 0.45f));
            second = 1.0f;
            secondFactor = fallingBy60(rate, decay);
            bodyLevel = 0.75f - 0.3f * tone;
            noiseLevel = 0.35f + 0.5f * tone;
            noiseCoefficient = onePole(rate, 1200.0f);
            filter.set(rate, (2500.0f + 11000.0f * tone) * std::sqrt(tuneRatio), 0.6f);
            break;

        case DrumModel::Tom:
            // A kick's idea an octave up and slower: a shallower fall in pitch,
            // and a stick's worth of noise on the front, by Tone.
            frequency[0] = clampFrequency(105.0f * tuneRatio);
            sweep = 1.0f;
            sweepFactor = fallingBy(rate, 0.05f);
            sweepDepth = 0.55f;
            click = 1.0f;
            clickFactor = fallingBy(rate, 0.003f);
            clickLevel = 0.05f + 0.3f * tone;
            filter.set(rate, 2000.0f * std::sqrt(tuneRatio), 0.7f);
            break;

        case DrumModel::ClosedHat:
        case DrumModel::OpenHat:
            // Six squares and a little noise through a high-pass whose corner
            // Tone raises. Their phases start apart, as six free-running
            // oscillators would be when the hat is struck.
            for (size_t i = 0; i < metal.size(); ++i)
            {
                frequency[i] = clampFrequency(metal[i] * tuneRatio);
                phase[i] = noise() * 0.5f + 0.5f;
            }
            bodyLevel = 0.7f;
            noiseLevel = 0.3f;
            filter.set(rate, (6500.0f + 4500.0f * tone) * std::sqrt(tuneRatio), 0.9f);
            break;

        case DrumModel::Clap:
            // Several hands a few milliseconds apart, then the room: three
            // bursts of band-passed noise and a tail that takes the decay.
            bursts = { 0, static_cast<int>(0.0105 * rate), static_cast<int>(0.0215 * rate) };
            tailStart = static_cast<int>(0.032 * rate);
            clickFactor = fallingBy(rate, 0.0035f);
            secondFactor = fallingBy60(rate, decay);
            body = 0.0f;
            filter.set(rate, (850.0f + 1100.0f * tone) * tuneRatio, 1.6f);
            break;

        case DrumModel::Rim:
            // A high partial and a low one, the low one dying sooner, and a
            // sharp click of high-passed noise.
            frequency[0] = clampFrequency(1720.0f * tuneRatio);
            frequency[1] = clampFrequency(480.0f * tuneRatio);
            second = 1.0f;
            secondFactor = fallingBy60(rate, std::max(0.005f, decay * 0.6f));
            click = 1.0f;
            clickFactor = fallingBy(rate, 0.0012f);
            clickLevel = 0.2f + 0.5f * tone;
            filter.set(rate, 2500.0f, 0.7f);
            break;

        case DrumModel::Cowbell:
            // Two squares a fifth-and-a-bit apart through a band-pass, with a
            // fast clank over a slower ring.
            frequency[0] = clampFrequency(540.0f * tuneRatio);
            frequency[1] = clampFrequency(800.0f * tuneRatio);
            bodyFactor = fallingBy(rate, 0.012f);
            second = 1.0f;
            secondFactor = fallingBy60(rate, decay);
            filter.set(rate, (1100.0f + 1900.0f * tone) * tuneRatio, 1.3f);
            break;
    }
    running = true;
}

float DrumSynthVoice::next() noexcept
{
    if (!running)
        return 0.0f;
    const auto advance = [this] (size_t i, float bend)
    {
        phase[i] += frequency[i] * bend * step;
        phase[i] -= std::floor(phase[i]);
    };

    auto out = 0.0f;
    switch (model)
    {
        case DrumModel::Kick:
        {
            advance(0, 1.0f + sweepDepth * sweep);
            fall(sweep, sweepFactor);
            const auto white = noise();
            noiseState += noiseCoefficient * (white - noiseState);
            const auto head = std::sin(twoPi * phase[0]) * body;
            const auto snap = (white - noiseState) * click * clickLevel;
            out = std::tanh(drive * (head + snap)) * driveNormal;
            fall(body, bodyFactor);
            fall(click, clickFactor);
            if (body < silence && click < silence)
                running = false;
            break;
        }
        case DrumModel::Snare:
        {
            const auto bend = 1.0f + sweepDepth * sweep;
            fall(sweep, sweepFactor);
            advance(0, bend);
            advance(1, bend);
            const auto heads = (std::sin(twoPi * phase[0]) + 0.55f * std::sin(twoPi * phase[1])) * (1.0f / 1.55f);
            const auto white = noise();
            noiseState += noiseCoefficient * (white - noiseState);
            filter.run(white - noiseState);
            out = bodyLevel * heads * body + noiseLevel * filter.low * second;
            fall(body, bodyFactor);
            fall(second, secondFactor);
            if (body < silence && second < silence)
                running = false;
            break;
        }
        case DrumModel::Tom:
        {
            advance(0, 1.0f + sweepDepth * sweep);
            fall(sweep, sweepFactor);
            filter.run(noise());
            out = 0.9f * (std::sin(twoPi * phase[0]) * body + filter.low * click * clickLevel);
            fall(body, bodyFactor);
            fall(click, clickFactor);
            if (body < silence && click < silence)
                running = false;
            break;
        }
        case DrumModel::ClosedHat:
        case DrumModel::OpenHat:
        {
            auto squares = 0.0f;
            for (size_t i = 0; i < metal.size(); ++i)
            {
                advance(i, 1.0f);
                squares += square(phase[i]);
            }
            filter.run(bodyLevel * squares * (1.0f / 6.0f) + noiseLevel * noise());
            out = 1.8f * filter.high * body;
            fall(body, bodyFactor);
            if (body < silence)
                running = false;
            break;
        }
        case DrumModel::Clap:
        {
            if (age == bursts[0] || age == bursts[1] || age == bursts[2])
                click = 1.0f;
            if (age == tailStart)
                second = 0.8f;
            filter.run(noise());
            out = 1.8f * filter.band * std::max(click, second);
            fall(click, clickFactor);
            fall(second, secondFactor);
            if (age > tailStart && click < silence && second < silence)
                running = false;
            break;
        }
        case DrumModel::Rim:
        {
            advance(0, 1.0f);
            advance(1, 1.0f);
            filter.run(noise());
            out = 0.9f * (0.55f * std::sin(twoPi * phase[0]) * body
                          + 0.45f * std::sin(twoPi * phase[1]) * second
                          + clickLevel * filter.high * click);
            fall(body, bodyFactor);
            fall(second, secondFactor);
            fall(click, clickFactor);
            if (body < silence && second < silence && click < silence)
                running = false;
            break;
        }
        case DrumModel::Cowbell:
        {
            advance(0, 1.0f);
            advance(1, 1.0f);
            filter.run((square(phase[0]) + square(phase[1])) * 0.5f);
            out = 1.2f * filter.band * (0.6f * body + 0.4f * second);
            fall(body, bodyFactor);
            fall(second, secondFactor);
            if (body < silence && second < silence)
                running = false;
            break;
        }
    }
    if (++age >= limit)
        running = false;
    return out;
}
}
