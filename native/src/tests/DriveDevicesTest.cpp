#include "model/Session.h"
#include "ui/rack/DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "dsp/Oversampler.h"
#include "dsp/SaturatorEngine.h"
#include "dsp/ReduxEngine.h"
#include "dsp/ErosionEngine.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <vector>

// Saturator, Redux and Erosion, and the oversampler Saturator and Amp share,
// held to what each claims by measuring what comes out of it. Nothing here
// reads a coefficient: a level is a windowed single-frequency transform of
// rendered audio, so it cannot agree with the DSP by construction.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512;
constexpr double pi = 3.14159265358979323846;
constexpr int settle = 9600;      // a fifth of a second, past every filter's start
constexpr int measure = 16384;

void require(bool valid, const char* what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(std::string("Drive devices: ") + what + " (line " + std::to_string(location.line()) + ")");
}

// "A is louder than B by at least margin": says what it measured when it fails.
void requireAbove(double louderDb, double quieterDb, double marginDb, const char* what,
                  const std::source_location location = std::source_location::current())
{
    if (louderDb - quieterDb < marginDb)
        throw std::runtime_error((juce::String("Drive devices: ") + what + ": " + juce::String(louderDb, 2) + " dB against "
            + juce::String(quieterDb, 2) + " dB, wanted " + juce::String(marginDb, 1) + " dB apart (line "
            + juce::String(static_cast<int>(location.line())) + ")").toStdString());
}

std::vector<float> sine(double frequency, double amplitude, int count, double sampleRate = rate)
{
    std::vector<float> audio(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * i / sampleRate));
    return audio;
}

// The amplitude of the component at `frequency`: a Hann-windowed transform at
// that one frequency, scaled so a sine of amplitude A reads A.
double amplitudeAt(const std::vector<float>& audio, int start, int count, double frequency, double sampleRate = rate)
{
    const auto omega = 2.0 * pi * frequency / sampleRate;
    auto real = 0.0, imaginary = 0.0, windowSum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * i / count);
        const auto sample = audio[static_cast<size_t>(start + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
        windowSum += window;
    }
    return 2.0 * std::sqrt(real * real + imaginary * imaginary) / windowSum;
}

double decibels(double amplitude)
{
    return 20.0 * std::log10(std::max(amplitude, 1.0e-12));
}

double peakOf(const std::vector<float>& audio, int start)
{
    auto peak = 0.0;
    for (auto i = static_cast<size_t>(start); i < audio.size(); ++i)
        peak = std::max(peak, static_cast<double>(std::abs(audio[i])));
    return peak;
}

// Drives an engine the way its device does: two channels, in prepared-size
// blocks.
template <typename Engine>
void renderPair(Engine& engine, std::vector<float>& left, std::vector<float>& right)
{
    for (size_t start = 0; start < left.size(); start += block)
    {
        const auto count = static_cast<int>(std::min<size_t>(block, left.size() - start));
        float* channels[2] { left.data() + start, right.data() + start };
        engine.process(channels, 2, count);
    }
}

// ---- the oversampler --------------------------------------------------------

void checkOversampler()
{
    // A round trip with nothing done in between is the input, late by exactly
    // the latency it reports.
    for (const auto factor : { 2, 4 })
    {
        Oversampler oversampler;
        oversampler.prepare(factor, 1, block);
        require(oversampler.latencySamples() == (factor == 2 ? 31 : 37), "the latency is the whole number designed for");
        const auto input = sine(1000.0, 0.5, settle + measure);
        std::vector<float> output(input.size());
        for (size_t start = 0; start < input.size(); start += block)
        {
            const auto count = static_cast<int>(std::min<size_t>(block, input.size() - start));
            oversampler.up(0, input.data() + start, count);
            oversampler.down(0, output.data() + start, count);
        }
        const auto latency = oversampler.latencySamples();
        auto worst = 0.0;
        for (int i = settle; i < settle + measure; ++i)
            worst = std::max(worst, static_cast<double>(std::abs(output[static_cast<size_t>(i)]
                                                                 - input[static_cast<size_t>(i - latency)])));
        require(worst < 1.0e-3, "a round trip is a whole-sample delay");

        // And it passes the top of the audible band at the level it came in.
        const auto high = sine(18000.0, 0.5, settle + measure);
        std::vector<float> highOut(high.size());
        oversampler.reset();
        for (size_t start = 0; start < high.size(); start += block)
        {
            const auto count = static_cast<int>(std::min<size_t>(block, high.size() - start));
            oversampler.up(0, high.data() + start, count);
            oversampler.down(0, highOut.data() + start, count);
        }
        require(std::abs(decibels(amplitudeAt(highOut, settle, measure, 18000.0)) - decibels(0.5)) < 0.1,
                "18 kHz passes within a tenth of a dB");
    }

    // Upsampling must not leave the images it filters out: a 10 kHz tone at
    // four times shows nothing at 38 kHz (its image about 24 kHz) or 86 kHz
    // (about 48 kHz).
    {
        Oversampler oversampler;
        oversampler.prepare(4, 1, block);
        const auto input = sine(10000.0, 0.5, settle + measure);
        std::vector<float> inner;
        for (size_t start = 0; start < input.size(); start += block)
        {
            const auto count = static_cast<int>(std::min<size_t>(block, input.size() - start));
            const auto* up = oversampler.up(0, input.data() + start, count);
            inner.insert(inner.end(), up, up + count * 4);
        }
        const auto innerRate = rate * 4;
        const auto tone = decibels(amplitudeAt(inner, settle * 4, measure * 4, 10000.0, innerRate));
        requireAbove(tone, decibels(amplitudeAt(inner, settle * 4, measure * 4, 38000.0, innerRate)), 75.0,
                     "the first doubling's image is gone");
        requireAbove(tone, decibels(amplitudeAt(inner, settle * 4, measure * 4, 86000.0, innerRate)), 75.0,
                     "the second doubling's image is gone");
    }
}

// ---- Saturator ----------------------------------------------------------------

std::vector<float> saturate(const SaturatorEngine::Settings& settings, const std::vector<float>& input)
{
    SaturatorEngine engine;
    engine.setSettings(settings);
    engine.prepare(rate, block);
    auto left = input, right = input;
    renderPair(engine, left, right);
    return left;
}

void checkSaturator()
{
    // Every curve passes a quiet signal at the level it came in at, since
    // each has a slope of one through zero.
    for (int curve = 0; curve < saturator::curveCount; ++curve)
    {
        SaturatorEngine::Settings settings;
        settings.curve = static_cast<saturator::Curve>(curve);
        const auto out = saturate(settings, sine(1000.0, 0.01, settle + measure));
        require(std::abs(decibels(amplitudeAt(out, settle, measure, 1000.0)) - decibels(0.01)) < 0.1,
                "a quiet signal passes every curve unchanged");
    }

    // Driven, an odd curve makes odd harmonics and no even ones.
    {
        SaturatorEngine::Settings settings;
        settings.curve = saturator::Curve::analogClip;
        settings.driveDb = 18.0f;
        const auto out = saturate(settings, sine(1000.0, 0.5, settle + measure));
        const auto fundamental = decibels(amplitudeAt(out, settle, measure, 1000.0));
        requireAbove(decibels(amplitudeAt(out, settle, measure, 3000.0)), fundamental - 30.0, 0.0,
                     "a driven clip makes a third harmonic");
        requireAbove(fundamental, decibels(amplitudeAt(out, settle, measure, 2000.0)), 80.0,
                     "and no second");
    }

    // Oversampled: a driven 7 kHz tone's fifth harmonic, 35 kHz, would fold
    // to 13 kHz at the plain rate. Here it is filtered off, while the true
    // third harmonic at 21 kHz is still there.
    {
        SaturatorEngine::Settings settings;
        settings.curve = saturator::Curve::mediumCurve;
        settings.driveDb = 12.0f;
        const auto out = saturate(settings, sine(7000.0, 0.5, settle + measure));
        const auto fundamental = decibels(amplitudeAt(out, settle, measure, 7000.0));
        requireAbove(decibels(amplitudeAt(out, settle, measure, 21000.0)), fundamental - 40.0, 0.0,
                     "the third harmonic is made");
        requireAbove(fundamental, decibels(amplitudeAt(out, settle, measure, 13000.0)), 70.0,
                     "the fifth does not fold back down");
    }

    // The curve the face draws is the curve the audio goes through: a slow
    // sine at full scale peaks where the drawn curve ends.
    {
        SaturatorEngine::Settings settings;
        settings.curve = saturator::Curve::mediumCurve;
        settings.driveDb = 6.0f;
        std::vector<float> curve(65);
        SaturatorEngine::transferCurve(settings, curve.data(), static_cast<int>(curve.size()));
        const auto out = saturate(settings, sine(50.0, 1.0, settle + measure));
        require(std::abs(peakOf(out, settle) - curve.back()) < 0.01 * curve.back(),
                "the drawn curve's end is the audio's peak");
        require(std::abs(curve[32]) < 1.0e-6f && std::abs(curve.front() + curve.back()) < 1.0e-6f,
                "and the drawn curve is odd through zero");
    }

    // The colour filters are undone after the curve, so they change nothing
    // the curve leaves straight...
    {
        SaturatorEngine::Settings settings;
        settings.colour = true;
        settings.colourLow = 1.0f;
        settings.colourHigh = -0.7f;
        settings.colourFrequency = 2000.0f;
        for (const auto frequency : { 100.0, 2000.0 })
        {
            const auto out = saturate(settings, sine(frequency, 0.01, settle + measure));
            require(std::abs(decibels(amplitudeAt(out, settle, measure, frequency)) - decibels(0.01)) < 0.1,
                    "the colour filters cancel where the curve is straight");
        }
    }
    // ...and change what saturates where it is not: lows cut before the curve
    // saturate less.
    {
        SaturatorEngine::Settings plain;
        plain.curve = saturator::Curve::analogClip;
        plain.driveDb = 18.0f;
        auto coloured = plain;
        coloured.colour = true;
        coloured.colourLow = -1.0f;
        const auto input = sine(100.0, 0.5, settle + measure);
        const auto thirdOf = [&input] (const SaturatorEngine::Settings& settings)
        {
            const auto out = saturate(settings, input);
            return decibels(amplitudeAt(out, settle, measure, 300.0)) - decibels(amplitudeAt(out, settle, measure, 100.0));
        };
        requireAbove(thirdOf(plain), thirdOf(coloured), 6.0, "Amt Lo turned down saturates the lows less");
    }

    // Post Clip at Hard keeps the output under Output's level, whatever the
    // curve does before it.
    {
        SaturatorEngine::Settings settings;
        settings.curve = saturator::Curve::waveshaper;
        settings.waves.depth = 1.0;
        settings.driveDb = 36.0f;
        settings.postClip = saturator::PostClip::hard;
        settings.outputDb = -6.0f;
        const auto out = saturate(settings, sine(200.0, 0.5, settle + measure));
        // The decimating filter rings a little over a hard edge.
        require(peakOf(out, settle) < 0.5 * 1.12, "Post Clip holds the output under Output");
    }

    // Dry/Wet mixes the dry signal back in step with the wet: half and half
    // of a signal the curve leaves alone is that signal, a latency late, with
    // no comb in it.
    {
        SaturatorEngine engine;
        SaturatorEngine::Settings settings;
        settings.dryWet = 0.5f;
        engine.setSettings(settings);
        engine.prepare(rate, block);
        const auto input = sine(15000.0, 0.01, settle + measure);
        auto left = input, right = input;
        renderPair(engine, left, right);
        const auto latency = engine.latencySamples();
        auto worst = 0.0;
        for (int i = settle; i < settle + measure; ++i)
            worst = std::max(worst, static_cast<double>(std::abs(left[static_cast<size_t>(i)]
                                                                 - input[static_cast<size_t>(i - latency)])));
        require(worst < 1.0e-4, "Dry/Wet mixes the dry signal back in step");
    }
}

// ---- Redux --------------------------------------------------------------------

void checkRedux()
{
    // The steps: three bits are four steps either side of zero.
    require(std::abs(ReduxEngine::quantise(0.3, 3.0f, 0.0f, false) - 0.25) < 1.0e-12, "three bits round to quarters");
    require(ReduxEngine::quantise(0.05, 3.0f, 0.0f, false) == 0.0, "and a quiet value to nothing");
    // DC Shift puts zero between two steps, so a quiet value is half a step
    // either side of it rather than nothing, and the two sides match.
    require(std::abs(ReduxEngine::quantise(0.01, 3.0f, 0.0f, true) - 0.125) < 1.0e-12
                && std::abs(ReduxEngine::quantise(-0.01, 3.0f, 0.0f, true) + 0.125) < 1.0e-12,
            "DC Shift steps half a step either side of zero");
    // Shape keeps finer steps for quiet detail.
    require(std::abs(ReduxEngine::quantise(0.02, 4.0f, 1.0f, false) - 0.02)
                < std::abs(ReduxEngine::quantise(0.02, 4.0f, 0.0f, false) - 0.02),
            "Shape resolves a quiet value more finely");
    require(std::abs(ReduxEngine::quantise(-0.7, 16.0f, 1.0f, false) + 0.7) < 1.0e-3,
            "and companding is undone after the steps");

    const auto render = [] (ReduxEngine::Settings settings, double frequency)
    {
        ReduxEngine engine;
        engine.setSettings(settings);
        engine.prepare(rate);
        auto left = sine(frequency, 0.5, settle + measure), right = left;
        renderPair(engine, left, right);
        return std::pair { left, right };
    };
    ReduxEngine::Settings plain;
    plain.rateHz = 8000.0f;
    plain.bits = 16.0f;
    plain.preFilter = false;
    plain.postFilter = false;

    // Downsampling folds: 3 kHz sampled at 8 kHz has an image at 5 kHz.
    {
        const auto [out, unused] = render(plain, 3000.0);
        requireAbove(decibels(amplitudeAt(out, settle, measure, 5000.0)), decibels(amplitudeAt(out, settle, measure, 3000.0)) - 20.0,
                     0.0, "downsampling folds an image down");
    }
    // The Pre filter keeps what is above half the rate out of the sampler...
    {
        auto filtered = plain;
        filtered.preFilter = true;
        const auto [bare, unusedBare] = render(plain, 5000.0);
        const auto [kept, unusedKept] = render(filtered, 5000.0);
        requireAbove(decibels(amplitudeAt(bare, settle, measure, 3000.0)), decibels(amplitudeAt(kept, settle, measure, 3000.0)),
                     8.0, "the Pre filter keeps a tone over half the rate from folding");
    }
    // ...and the Post filter smooths the image off the steps.
    {
        auto smoothed = plain;
        smoothed.postFilter = true;
        const auto [bare, unusedBare] = render(plain, 3000.0);
        const auto [kept, unusedKept] = render(smoothed, 3000.0);
        requireAbove(decibels(amplitudeAt(bare, settle, measure, 5000.0)), decibels(amplitudeAt(kept, settle, measure, 5000.0)),
                     6.0, "the Post filter takes the image off");
    }
    // Jitter samples the two sides at different moments; without it they are
    // one signal.
    {
        const auto [left, right] = render(plain, 1000.0);
        require(left == right, "without Jitter both sides are sampled together");
        auto shaken = plain;
        shaken.jitter = 1.0f;
        const auto [jitterLeft, jitterRight] = render(shaken, 1000.0);
        require(jitterLeft != jitterRight, "Jitter samples each side on a clock of its own");
    }
}

// ---- Erosion ------------------------------------------------------------------

void checkErosion()
{
    const auto render = [] (ErosionEngine::Settings settings, std::vector<float> left, std::vector<float> right)
    {
        ErosionEngine engine;
        engine.setSettings(settings);
        engine.prepare(rate);
        renderPair(engine, left, right);
        return std::tuple { left, right, engine.latencySamples() };
    };
    const auto input = sine(1000.0, 0.5, settle + measure);

    // At no Amount the device is the delay it reports, exactly.
    {
        ErosionEngine::Settings settings;
        settings.amount = 0.0f;
        const auto [out, unused, latency] = render(settings, input, input);
        require(latency > 0, "Erosion reports its delay");
        for (int i = latency; i < settle + measure; ++i)
            require(out[static_cast<size_t>(i)] == input[static_cast<size_t>(i - latency)],
                    "with no Amount it is exactly its reported delay");
    }
    // The sine moves the delay at its own frequency, which is phase
    // modulation: sidebands either side of the tone, at 1 kHz plus and minus
    // the 3 kHz modulator.
    {
        ErosionEngine::Settings settings;
        settings.frequency = 3000.0f;
        settings.amount = 1.0f;
        settings.noiseBlend = 0.0f;
        const auto [out, unused, latency] = render(settings, input, input);
        const auto tone = decibels(amplitudeAt(out, settle, measure, 1000.0));
        requireAbove(decibels(amplitudeAt(out, settle, measure, 4000.0)), tone - 40.0, 0.0, "a sideband above");
        requireAbove(decibels(amplitudeAt(out, settle, measure, 2000.0)), tone - 40.0, 0.0, "and below");
    }
    // Noise erodes it: what is left after the tone is taken away is far
    // louder with the noise than without any modulation.
    {
        ErosionEngine::Settings settings;
        settings.amount = 1.0f;
        settings.noiseBlend = 1.0f;
        const auto [out, unused, latency] = render(settings, input, input);
        auto residue = 0.0;
        for (int i = settle; i < settle + measure; ++i)
        {
            const auto difference = out[static_cast<size_t>(i)] - input[static_cast<size_t>(i - latency)];
            residue += difference * difference;
        }
        requireAbove(decibels(std::sqrt(residue / measure)), decibels(0.5 / std::sqrt(2.0)) - 40.0, 0.0,
                     "noise modulation leaves a residue");
    }
    // Stereo Width: one modulator for both sides at none, one each at full.
    {
        ErosionEngine::Settings settings;
        settings.amount = 1.0f;
        settings.stereoWidth = 0.0f;
        const auto [left, right, latency] = render(settings, input, input);
        require(left == right, "at no Stereo Width both sides are modulated alike");
        settings.stereoWidth = 1.0f;
        const auto [wideLeft, wideRight, wideLatency] = render(settings, input, input);
        require(wideLeft != wideRight, "at full width each side is modulated on its own");
    }
}

// ---- the devices on a track ---------------------------------------------------

// Each is made through the catalog, added through the session, shows a
// generated face that fits, reports the latency its engine has, and pictures
// what its display claims.
void checkDriveDevicesOnATrack(Session& session)
{
    struct Expected
    {
        const char* id;
        int latency;
        bool pictured;
    };
    const Expected expected[] { { "Saturator", 37, true }, { "Redux", 0, true }, { "Erosion", 6, true }, { "Amp", 37, false } };
    require(session.addAudioTrack().wasOk(), "a track to put them on");
    const auto track = session.firstReturnTrack() - 1;
    for (const auto& device : expected)
    {
        require(DeviceCatalog::byId(device.id) != nullptr, "the catalog has the device");
        require(session.addDevice(device.id, track).wasOk(), "the session adds the device");
        std::optional<Session::DeviceSlot> slot;
        for (const auto& candidate : session.deviceSlots(track))
            if (candidate.deviceId == device.id)
                slot = candidate;
        require(slot.has_value() && slot->native, "the device is native");
        require(session.devicePlugin(track, slot->pluginIndex) != nullptr, "the session finds the device");
        // Latency is read off one of its own: the track's may be in the
        // graph, and initialising it here would race the audio thread.
        {
            auto own = session.edit->getPluginCache().createNewPlugin(DeviceCatalog::byId(device.id)->typeName, {});
            require(own != nullptr, "the engine makes one from its type name");
            own->initialise({ {}, rate, block });
            require(juce::roundToInt(own->getLatencySeconds() * rate) == device.latency, "the device reports its latency");
            own->deinitialise();
        }
        require(session.deviceDisplay(track, slot->pluginIndex).traces.empty() != device.pictured,
                "the device pictures what it says it does");

        auto panel = std::make_unique<DeviceEditorPanel>(session);
        panel->setTarget(track, *slot, true);
        panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);
        for (auto* child : panel->getChildren())
            require(!child->isVisible() || panel->getLocalBounds().contains(child->getBounds()),
                    "every control is on the face");
        Theme theme;
        panel->setLookAndFeel(&theme);
        const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
        panel->setLookAndFeel(nullptr);
        // RHINO_DEVICE_SNAPSHOTS pictures these faces with every other one, in
        // DeviceFacesTest.cpp.
        require(snapshot.getWidth() == panel->getWidth(), "the face paints");
        require(session.deleteDevice(track, slot->pluginIndex).wasOk(), "and the device comes off again");
    }

    // A chain is not full at Tracktion's default of sixteen plugins, seven of
    // which every track spends on its own: ten more devices go on, and each
    // one added is there.
    const auto before = session.deviceSlots(track).size();
    for (int round = 0; round < 3; ++round)
        for (const auto& device : expected)
            if (juce::String(device.id) != "Amp" || round == 0)
                require(session.addDevice(device.id, track).wasOk(), "a long chain takes another device");
    require(session.deviceSlots(track).size() == before + 10, "and every device added is in it");
}
}

void checkDriveDevices(Session& session)
{
    checkOversampler();
    checkSaturator();
    checkRedux();
    checkErosion();
    checkDriveDevicesOnATrack(session);
}
}
