#include "model/Session.h"
#include "ui/rack/DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "content/ContentLibrary.h"
#include "dsp/MultibandDynamicsEngine.h"
#include "audio/MultibandDynamicsDevice.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <vector>

// Multiband Dynamics, held to what it claims by measuring what comes out of
// it: the split sums back to the input, each threshold moves a steady tone to
// where its curve says, the curve the face draws is the level the audio
// reaches, the bands stay apart, and Time and Amount scale what they say.
// A level is a windowed single-frequency transform of rendered audio, so it
// cannot agree with the engine by construction.
//
// Run by --device-test rather than --self-test, which is already at its time
// limit, because it builds and paints the device's face.
namespace rhino
{
namespace
{
using Engine = MultibandDynamicsEngine;
constexpr double rate = 48000.0;
constexpr int block = 480;
constexpr double pi = 3.14159265358979323846;
constexpr int settle = 24000;     // half a second, past every attack and release here
constexpr int measure = 16384;

void require(bool valid, const juce::String& what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(("Multiband Dynamics: " + what + " (line "
                                  + juce::String(static_cast<int>(location.line())) + ")").toStdString());
}

// "Measured is within tolerance of wanted": says what it measured when not.
void requireNear(double measuredDb, double wantedDb, double toleranceDb, const juce::String& what,
                 const std::source_location location = std::source_location::current())
{
    require(std::abs(measuredDb - wantedDb) <= toleranceDb,
            what + ": " + juce::String(measuredDb, 3) + " dB, wanted " + juce::String(wantedDb, 3) + " dB", location);
}

std::vector<float> sine(double frequency, double amplitude, int count)
{
    std::vector<float> audio(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * i / rate));
    return audio;
}

// The amplitude of the component at `frequency`: a Hann-windowed transform at
// that one frequency, scaled so a sine of amplitude A reads A.
double amplitudeAt(const std::vector<float>& audio, int start, int count, double frequency)
{
    const auto omega = 2.0 * pi * frequency / rate;
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

// Both channels the same, in blocks the size a live device sees.
std::vector<float> render(const Engine::Settings& settings, std::vector<float> left)
{
    Engine engine;
    engine.setSettings(settings);
    engine.prepare(rate);
    auto right = left;
    for (size_t start = 0; start < left.size(); start += block)
    {
        const auto count = static_cast<int>(std::min<size_t>(block, left.size() - start));
        float* channels[2] { left.data() + start, right.data() + start };
        engine.process(channels, 2, count);
    }
    return left;
}

double levelOut(const Engine::Settings& settings, double frequency, double amplitude)
{
    return decibels(amplitudeAt(render(settings, sine(frequency, amplitude, settle + measure)), settle, measure, frequency));
}

// One band, the mid, which is then the whole signal; hard knee, peak.
Engine::Settings singleBand()
{
    Engine::Settings settings;
    settings.lowBand = settings.highBand = false;
    settings.softKnee = false;
    settings.rms = false;
    return settings;
}

void checkSplit()
{
    // At its defaults every ratio is 1:1, so the three bands sum back to the
    // input, below, between and above both crossovers.
    const Engine::Settings defaults;
    for (const auto frequency : { 40.0, 120.0, 600.0, 2500.0, 9000.0 })
        requireNear(levelOut(defaults, frequency, 0.5), decibels(0.5), 0.01,
                    "the bands sum back to the input at " + juce::String(frequency) + " Hz");

    // Soloed, the high band is all that is heard: a low tone goes, a high
    // one stays, its crossover's slope aside.
    auto solo = defaults;
    solo.bands[2].solo = true;
    require(levelOut(solo, 100.0, 0.5) < decibels(0.5) - 60.0, "a low tone is gone with the high band soloed");
    requireNear(levelOut(solo, 9000.0, 0.5), decibels(0.5), 0.1, "a high tone passes with the high band soloed");

    // Compressing the low band leaves a high tone alone and turns a low one
    // down.
    auto low = defaults;
    low.bands[0].aboveRatio = Engine::infiniteRatio;
    requireNear(levelOut(low, 6000.0, 0.5), decibels(0.5), 0.05, "a high tone is untouched by the low band");
    require(levelOut(low, 50.0, 0.5) < decibels(0.5) - 8.0, "a low tone is held down by the low band");
}

void checkCurves()
{
    // Above the upper threshold: 4:1 at -20 dB takes a -6 dB tone 14 dB past
    // it back to 3.5 dB past it.
    auto down = singleBand();
    down.bands[1].aboveThresholdDb = -20.0f;
    down.bands[1].aboveRatio = 4.0f;
    requireNear(levelOut(down, 1000.0, 0.5), -20.0 + (decibels(0.5) + 20.0) / 4.0, 0.1, "4:1 downward compression");
    // The line the face draws for that band is where the tone lands.
    requireNear(levelOut(down, 1000.0, 0.5), Engine::transferDb(down, 1, decibels(0.5)), 0.1,
                "the face's curve is the level the audio reaches");
    auto limit = down;
    limit.bands[1].aboveRatio = Engine::infiniteRatio;
    requireNear(levelOut(limit, 1000.0, 0.5), -20.0, 0.1, "an infinite ratio holds the band at its threshold");
    // Under 1:1 the same threshold makes a loud tone louder.
    auto expand = down;
    expand.bands[1].aboveRatio = 0.5f;
    expand.bands[1].aboveThresholdDb = -12.0f;
    requireNear(levelOut(expand, 1000.0, 0.5), -12.0 + (decibels(0.5) + 12.0) * 2.0, 0.1, "1:2 upward expansion");

    // Below the lower threshold: 4:1 lifts a -60 dB tone 20 dB under -40 dB
    // to 5 dB under it, and 1:2 pushes it down to 40 dB under.
    auto up = singleBand();
    up.bands[1].belowThresholdDb = -40.0f;
    up.bands[1].belowRatio = 4.0f;
    requireNear(levelOut(up, 1000.0, 0.001), -45.0, 0.2, "4:1 upward compression");
    up.bands[1].belowRatio = 0.5f;
    requireNear(levelOut(up, 1000.0, 0.001), -80.0, 0.2, "1:2 downward expansion");
    // And nothing is lifted further than the cap, however quiet.
    auto lifted = singleBand();
    lifted.bands[1].belowThresholdDb = 0.0f;
    lifted.bands[1].aboveThresholdDb = 0.0f;
    lifted.bands[1].belowRatio = Engine::infiniteRatio;
    requireNear(levelOut(lifted, 1000.0, 1.0e-5), -100.0 + Engine::maximumBoostDb, 0.2,
                "upward compression stops at its cap");

    // Amount at 0 makes every ratio 1:1.
    auto none = down;
    none.amount = 0.0f;
    requireNear(levelOut(none, 1000.0, 0.5), decibels(0.5), 0.01, "Amount 0 does nothing");
    // RMS reads a sine 3 dB under its peak, 3 dB less past the threshold, so
    // at 4:1 it is turned down three quarters of 3 dB less.
    auto rms = down;
    rms.rms = true;
    requireNear(levelOut(rms, 1000.0, 0.5) - levelOut(down, 1000.0, 0.5), 10.0 * std::log10(2.0) * 0.75, 0.15,
                "RMS reads a sine 3 dB under its peak");
    // Switched off, a band passes as it came, Input and Output included.
    auto off = down;
    off.bands[1].active = false;
    off.bands[1].outputDb = 6.0f;
    requireNear(levelOut(off, 1000.0, 0.5), decibels(0.5), 0.01, "a band switched off passes as it came");
}

void checkTime()
{
    // A step from -40 dB to -6 dB into 4:1 at -20 dB, attack 50 ms: 20 ms
    // after the step a third of the reduction is on. Time at 10% makes the
    // attack 5 ms, and by then it is all on.
    auto settings = singleBand();
    settings.bands[1].aboveThresholdDb = -20.0f;
    settings.bands[1].aboveRatio = 4.0f;
    settings.bands[1].attackMs = 50.0f;
    const auto stepAt = settle;
    auto input = sine(1000.0, 0.01, stepAt);
    const auto loud = sine(1000.0, 0.5, settle + measure);
    input.insert(input.end(), loud.begin(), loud.end());
    const auto reductionAt = [&] (float time, int after)
    {
        auto scaled = settings;
        scaled.time = time;
        const auto output = render(scaled, input);
        const auto at = decibels(amplitudeAt(output, stepAt + after, 96, 1000.0));
        const auto settled = decibels(amplitudeAt(output, stepAt + settle, measure, 1000.0));
        return (decibels(0.5) - at) / (decibels(0.5) - settled);
    };
    const auto slow = reductionAt(1.0f, 960);
    const auto fast = reductionAt(0.1f, 960);
    require(slow > 0.2 && slow < 0.5, "a third of the reduction is on 20 ms into a 50 ms attack, measured "
                                          + juce::String(slow, 2));
    require(fast > 0.9, "Time at 10% puts it all on by then, measured " + juce::String(fast, 2));
}

void checkDevice(Session& session)
{
    // The face draws a curve for each band split off, and the 1:1 line.
    auto plugin = session.edit->getPluginCache().createNewPlugin(MultibandDynamicsDevice::xmlTypeName, {});
    auto* device = dynamic_cast<MultibandDynamicsDevice*>(plugin.get());
    require(device != nullptr, "the engine makes the device from its type name");
    require(device->getLatencySeconds() == 0.0 && device->getTailLength() == 0.0, "it adds no delay and no tail");
    const auto traces = [device]
    {
        DeviceDisplay display;
        device->describe(display);
        juce::StringArray names;
        for (const auto& trace : display.traces)
            names.add(trace.name);
        return names;
    };
    require(traces() == juce::StringArray { "Low", "Mid", "High", "1:1" }, "a curve for each band and the 1:1 line");
    for (auto* parameter : device->getAutomatableParameters())
        if (parameter->paramID == "lowBand")
            parameter->setParameter(0.0f, juce::sendNotificationSync);
    require(traces() == juce::StringArray { "Mid", "High", "1:1" }, "no curve for a band not split off");

    // On a track, with a factory preset that compresses each band upwards
    // below and downwards above, each band's curve leaves the 1:1 line at
    // both ends: lifted at the quiet end, held down at the loud one.
    require(session.addAudioTrack().wasOk(), "a track for the device");
    const auto track = session.firstReturnTrack() - 1;
    require(session.addDevice("MultibandDynamics", track).wasOk(), "the session adds the device");
    std::optional<Session::DeviceSlot> slot;
    for (const auto& candidate : session.deviceSlots(track))
        if (candidate.deviceId == "MultibandDynamics")
            slot = candidate;
    require(slot.has_value() && slot->native, "the device is native and in the chain");
    require(session.loadDevicePreset(track, slot->pluginIndex,
                                     ContentLibrary::file("Presets/MultibandDynamics/Upward and Downward.rnd")).wasOk(),
            "the factory preset loads");
    const auto display = session.deviceDisplay(track, slot->pluginIndex);
    require(display.traces.size() == 4, "a curve for each band and the 1:1 line");
    const auto& unchanged = display.traces.back().points;
    for (size_t b = 0; b < 3; ++b)
    {
        const auto& points = display.traces[b].points;
        require(points.front() > unchanged.front() + 0.1f && points.back() < unchanged.back() - 0.1f,
                display.traces[b].name + "'s curve is lifted at the quiet end and held down at the loud one");
    }

    // Its face is generated: every control on it, and it paints.
    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(track, *slot, true);
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);
    for (auto* child : panel->getChildren())
        require(!child->isVisible() || panel->getLocalBounds().contains(child->getBounds()), "every control is on the face");
    Theme theme;
    panel->setLookAndFeel(&theme);
    const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
    panel->setLookAndFeel(nullptr);
    require(snapshot.getWidth() == panel->getWidth(), "the face paints");
    // RHINO_DEVICE_SNAPSHOTS pictures it at its defaults with every other
    // face, in DeviceFacesTest.cpp, and here with the preset's curves.
    if (const auto folder = juce::SystemStats::getEnvironmentVariable("RHINO_DEVICE_SNAPSHOTS", {}); folder.isNotEmpty())
    {
        const auto file = juce::File(folder).getChildFile("MultibandDynamics Upward and Downward.png");
        // An existing file is opened at its end, not replaced.
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(snapshot, stream);
    }
    require(session.deleteDevice(track, slot->pluginIndex).wasOk(), "and the device comes off again");
}
}

void checkMultibandDynamics(Session& session)
{
    checkSplit();
    checkCurves();
    checkTime();
    checkDevice(session);
}
}
