#include "model/Session.h"
#include "dsp/AmpEngine.h"
#include <algorithm>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <vector>

// Amp, measured. Its seven models are voicings rather than circuits anyone can
// check against, so what is held here is what the manual promises of every
// one of them: Gain is the distortion, the tone knobs do what they are named,
// Volume drives the power stage, Mono plays one amp and Dual two, and the
// defaults come out near the level that went in. The tone knobs are checked
// twice over: measured through the whole amp at a level too low to distort,
// and against the tone stack's own response, which reaches the same
// coefficients by evaluating the transfer function rather than running it.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr double pi = 3.14159265358979323846;
constexpr int settle = 14400;
constexpr int measure = 16384;

void require(bool valid, const juce::String& what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(("Amp: " + what + " (line " + juce::String(static_cast<int>(location.line())) + ")")
                                     .toStdString());
}

juce::String modelName(int model)
{
    return AmpEngine::modelNames[model];
}

std::vector<float> sine(double frequency, double amplitude, int count)
{
    std::vector<float> audio(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * i / rate));
    return audio;
}

// A band-limited saw, as a guitar's string is rich in harmonics.
std::vector<float> saw(double frequency, double amplitude, int count)
{
    std::vector<float> audio(static_cast<size_t>(count));
    const auto harmonics = static_cast<int>(20000.0 / frequency);
    for (int i = 0; i < count; ++i)
    {
        auto sample = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            sample += ((k % 2 == 1) ? 1.0 : -1.0) * std::sin(2.0 * pi * k * frequency * i / rate) / k;
        audio[static_cast<size_t>(i)] = static_cast<float>(amplitude * sample * 2.0 / pi);
    }
    return audio;
}

double amplitudeAt(const std::vector<float>& audio, double frequency)
{
    const auto omega = 2.0 * pi * frequency / rate;
    auto real = 0.0, imaginary = 0.0, windowSum = 0.0;
    for (int i = 0; i < measure; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * i / measure);
        const auto sample = audio[static_cast<size_t>(settle + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
        windowSum += window;
    }
    return 2.0 * std::sqrt(real * real + imaginary * imaginary) / windowSum;
}

double decibels(double amplitude)
{
    return 20.0 * std::log10(std::max(amplitude, 1.0e-15));
}

double rmsOf(const std::vector<float>& audio)
{
    auto sum = 0.0;
    for (int i = settle; i < settle + measure; ++i)
        sum += static_cast<double>(audio[static_cast<size_t>(i)]) * audio[static_cast<size_t>(i)];
    return std::sqrt(sum / measure);
}

struct Stereo
{
    std::vector<float> left, right;
};

Stereo play(const AmpEngine::Settings& settings, std::vector<float> left, std::vector<float> right)
{
    AmpEngine engine;
    engine.setSettings(settings);
    engine.prepare(rate, block);
    for (size_t start = 0; start < left.size(); start += block)
    {
        const auto count = static_cast<int>(std::min<size_t>(block, left.size() - start));
        float* channels[2] { left.data() + start, right.data() + start };
        engine.process(channels, 2, count);
    }
    return { std::move(left), std::move(right) };
}

std::vector<float> playMono(const AmpEngine::Settings& settings, const std::vector<float>& input)
{
    return play(settings, input, input).left;
}

// The harmonics' share of what came out, against the fundamental, in dB.
double distortionDb(const std::vector<float>& audio, double fundamental)
{
    auto harmonics = 0.0;
    for (int k = 2; k <= 8; ++k)
    {
        const auto amplitude = amplitudeAt(audio, fundamental * k);
        harmonics += amplitude * amplitude;
    }
    return decibels(std::sqrt(harmonics)) - decibels(amplitudeAt(audio, fundamental));
}
}

void checkAmp(Session&)
{
    const auto total = settle + measure;
    for (int model = 0; model < AmpEngine::modelCount; ++model)
    {
        AmpEngine::Settings settings;
        settings.model = static_cast<AmpEngine::Model>(model);
        const auto name = modelName(model);

        // Gain is the distortion: a sine comes out dirtier with it up.
        {
            auto clean = settings;
            clean.gain = 0.0f;
            clean.volume = 2.0f;
            auto dirty = clean;
            dirty.gain = 10.0f;
            const auto input = sine(220.0, 0.25, total);
            const auto lowGain = distortionDb(playMono(clean, input), 220.0);
            const auto highGain = distortionDb(playMono(dirty, input), 220.0);
            require(highGain > lowGain + 6.0, name + ": Gain distorts (" + juce::String(lowGain, 1) + " dB at 0, "
                                                  + juce::String(highGain, 1) + " dB at 10)");
        }

        // The tone knobs, measured through the whole amp at a level that does
        // not distort, against the tone stack's own response. Every other
        // filter stands still between the two renders, so the difference is
        // the stack's alone.
        {
            auto quiet = settings;
            quiet.gain = 0.0f;
            quiet.volume = 0.0f;
            const struct { float AmpEngine::Settings::* knob; double frequency; const char* what; } knobs[] {
                { &AmpEngine::Settings::treble, 5000.0, "Treble" },
                { &AmpEngine::Settings::bass, 80.0, "Bass" },
                { &AmpEngine::Settings::middle, 600.0, "Middle" },
            };
            for (const auto& knob : knobs)
            {
                auto down = quiet, up = quiet;
                down.*knob.knob = 0.0f;
                up.*knob.knob = 10.0f;
                const auto input = sine(knob.frequency, 0.001, total);
                const auto measured = decibels(amplitudeAt(playMono(up, input), knob.frequency))
                                    - decibels(amplitudeAt(playMono(down, input), knob.frequency));
                const auto predicted = AmpEngine::toneStackDb(up, rate, knob.frequency)
                                     - AmpEngine::toneStackDb(down, rate, knob.frequency);
                require(measured > 3.0, name + ": " + knob.what + " lifts its band (" + juce::String(measured, 2) + " dB)");
                require(std::abs(measured - predicted) < 0.5,
                        name + ": " + knob.what + " measures " + juce::String(measured, 2) + " dB, the stack says "
                            + juce::String(predicted, 2));
            }
        }

        // Volume drives the power stage: up, louder.
        {
            auto soft = settings;
            soft.volume = 2.0f;
            auto loud = settings;
            loud.volume = 8.0f;
            const auto input = saw(110.0, 0.25, total);
            require(rmsOf(playMono(loud, input)) > rmsOf(playMono(soft, input)) * 2.0, name + ": Volume is louder up");
        }

        // The defaults come out near the level that went in, so trying a model
        // on a track does not jump its level.
        {
            const auto input = saw(110.0, 0.25, total);
            const auto output = playMono(settings, input);
            const auto change = decibels(rmsOf(output)) - decibels(rmsOf(input));
            juce::Logger::writeToLog("Amp: " + name + " at its defaults changes the level by " + juce::String(change, 1) + " dB");
            require(change > -3.0 && change < 3.0, name + ": the defaults keep the level within 3 dB ("
                                                       + juce::String(change, 1) + " dB)");
            for (const auto sample : output)
                require(std::isfinite(sample) && std::abs(sample) < 4.0f, name + ": the output is finite and bounded");
        }

        // Flat out it is still finite and bounded.
        {
            auto flatOut = settings;
            flatOut.gain = flatOut.volume = flatOut.bass = flatOut.middle = flatOut.treble = flatOut.presence = 10.0f;
            for (const auto sample : playMono(flatOut, saw(55.0, 1.0, total)))
                require(std::isfinite(sample) && std::abs(sample) < 4.0f, name + ": flat out it stays bounded");
        }
    }

    // Mono plays one amp, the sum of both sides, on both; Dual an amp a side.
    {
        AmpEngine::Settings settings;
        settings.model = AmpEngine::Model::rock;
        const auto left = saw(110.0, 0.25, settle + measure);
        const auto right = saw(165.0, 0.25, settle + measure);
        const auto mono = play(settings, left, right);
        require(mono.left == mono.right, "Mono puts the one amp on both sides");
        settings.dual = true;
        const auto dual = play(settings, left, right);
        require(dual.left != dual.right, "Dual plays each side through an amp of its own");
        // A side on its own through Dual is that side through Mono.
        settings.dual = false;
        auto alone = settings;
        const auto single = play(alone, left, left);
        settings.dual = true;
        const auto both = play(settings, left, left);
        for (size_t i = 0; i < left.size(); ++i)
            require(std::abs(both.left[i] - single.left[i]) < 1.0e-6f, "Dual's amps are the Mono amp");
    }

    // At no Dry/Wet it is its input, a latency late.
    {
        AmpEngine::Settings settings;
        settings.model = AmpEngine::Model::lead;
        settings.dryWet = 0.0f;
        AmpEngine engine;
        engine.setSettings(settings);
        engine.prepare(rate, block);
        const auto input = saw(110.0, 0.25, settle + measure);
        auto left = input, right = input;
        for (size_t start = 0; start < left.size(); start += block)
        {
            float* channels[2] { left.data() + start, right.data() + start };
            engine.process(channels, 2, static_cast<int>(std::min<size_t>(block, left.size() - start)));
        }
        const auto latency = engine.latencySamples();
        for (int i = latency; i < settle + measure; ++i)
            require(left[static_cast<size_t>(i)] == input[static_cast<size_t>(i - latency)], "Dry/Wet at none is the dry signal in step");
    }
}
}
