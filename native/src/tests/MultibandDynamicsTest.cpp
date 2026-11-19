#include "model/Session.h"
#include "ui/rack/DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "ui/rack/MultibandDisplayGeometry.h"
#include "content/ContentLibrary.h"
#include "dsp/MultibandDynamicsEngine.h"
#include "audio/MultibandDynamicsDevice.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <tuple>
#include <vector>

// Multiband Dynamics, held to what it claims by measuring what comes out of
// it: the split sums back to the input, each threshold moves a steady tone to
// where its ratio says, the meters read the levels the audio has, the bands
// stay apart, and Time and Amount scale what they say. A level is a windowed
// single-frequency transform of rendered audio, so it cannot agree with the
// engine by construction. Then the display's arithmetic, and the face driven
// by real presses and drags.
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

void checkMeters()
{
    // The meters read what the audio does. Coming in is the tone after the
    // band's Input; going out, with nothing done to the band, reads the same,
    // and at 4:1 reads the tone the band sends, measured.
    const auto readings = [] (const Engine::Settings& settings)
    {
        Engine engine;
        engine.setSettings(settings);
        engine.prepare(rate);
        auto left = sine(1000.0, 0.5, settle + measure);
        auto right = left;
        Engine::Levels loudest;
        for (size_t start = 0; start < left.size(); start += block)
        {
            const auto count = static_cast<int>(std::min<size_t>(block, left.size() - start));
            float* channels[2] { left.data() + start, right.data() + start };
            engine.process(channels, 2, count);
            if (start >= static_cast<size_t>(settle))
            {
                loudest.input = std::max(loudest.input, engine.levels()[1].input);
                loudest.output = std::max(loudest.output, engine.levels()[1].output);
            }
        }
        return std::pair(loudest, decibels(amplitudeAt(left, settle, measure, 1000.0)));
    };
    const auto plain = readings(singleBand()).first;
    requireNear(decibels(plain.input), decibels(0.5), 0.1, "the meter coming in reads the tone's peak");
    requireNear(decibels(plain.output), decibels(plain.input), 0.01, "with nothing done, going out reads the same");
    auto down = singleBand();
    down.bands[1].inputDb = 6.0f;
    down.bands[1].aboveThresholdDb = -20.0f;
    down.bands[1].aboveRatio = 4.0f;
    const auto [squeezed, heard] = readings(down);
    requireNear(decibels(squeezed.input), decibels(0.5) + 6.0, 0.1, "coming in is after the band's Input");
    requireNear(decibels(squeezed.output), heard, 0.2, "going out reads the level the band sends");
}

void checkDisplayGeometry()
{
    namespace md = multibanddisplay;
    // A row runs -80 to 0 dB, its ends holding whatever is past them.
    require(std::abs(md::xForDb(-40.0f, 10.0f, 160.0f) - 90.0f) < 1.0e-4f, "-40 dB stands in the middle of a row");
    require(md::xForDb(-100.0f, 10.0f, 160.0f) == 10.0f && md::xForDb(6.0f, 10.0f, 160.0f) == 170.0f,
            "a row's ends hold what is past them");
    require(std::abs(md::dbForX(md::xForDb(-23.5f, 10.0f, 160.0f), 10.0f, 160.0f) + 23.5f) < 1.0e-3f,
            "a level and its place on the row are one");

    // A block's volume: 1:1 in the middle, compression the way it turns the
    // block's signals, and the far ends the steepest expansion and an
    // infinite ratio.
    require(md::volumeForRatio(1.0f, true) == 0.0f && md::volumeForRatio(1.0f, false) == 0.0f, "1:1 is the middle");
    require(std::abs(md::ratioForVolume(-0.5f, true) - 2.0f) < 1.0e-4f,
            "half way down the Above block compresses at 2:1");
    require(std::abs(md::ratioForVolume(0.5f, false) - 2.0f) < 1.0e-4f,
            "half way up the Below block compresses upwards at 2:1");
    require(md::ratioForVolume(-1.0f, true) == Engine::infiniteRatio && md::ratioForVolume(1.0f, false) == Engine::infiniteRatio,
            "all the way to compression is an infinite ratio");
    require(md::ratioForVolume(1.0f, true) == Engine::minimumRatio && md::ratioForVolume(-1.0f, false) == Engine::minimumRatio,
            "all the way to expansion is the steepest");
    for (const auto ratio : { 0.25f, 0.4f, 0.5f, 0.8f, 1.0f, 1.5f, 2.0f, 4.0f, 10.0f, 50.0f, 100.0f })
        for (const auto above : { false, true })
            require(std::abs(md::ratioForVolume(md::volumeForRatio(ratio, above), above) - ratio) < ratio * 1.0e-3f,
                    "a ratio and its volume are one at " + juce::String(ratio));

    // What a press takes hold of.
    require(md::gripAt(30.0f, 40.0f, 120.0f) == md::Grip::belowBlock, "left of the lower edge is the Below block");
    require(md::gripAt(38.0f, 40.0f, 120.0f) == md::Grip::belowEdge, "near the lower edge is the edge");
    require(md::gripAt(80.0f, 40.0f, 120.0f) == md::Grip::none, "between the edges is neither block");
    require(md::gripAt(123.0f, 40.0f, 120.0f) == md::Grip::aboveEdge, "near the upper edge is the edge");
    require(md::gripAt(150.0f, 40.0f, 120.0f) == md::Grip::aboveBlock, "right of the upper edge is the Above block");
    require(md::gripAt(79.0f, 80.0f, 80.0f) == md::Grip::belowEdge && md::gripAt(81.0f, 80.0f, 80.0f) == md::Grip::aboveEdge,
            "edges on top of each other go to the side pressed");
}

void checkDevice(Session& session)
{
    auto plugin = session.edit->getPluginCache().createNewPlugin(MultibandDynamicsDevice::xmlTypeName, {});
    auto* device = dynamic_cast<MultibandDynamicsDevice*>(plugin.get());
    require(device != nullptr, "the engine makes the device from its type name");
    require(device->getLatencySeconds() == 0.0 && device->getTailLength() == 0.0, "it adds no delay and no tail");

    // The face reaches each control by its place in the declaration order.
    using Device = MultibandDynamicsDevice;
    const std::pair<int, const char*> globals[] {
        { Device::lowBandParameter, "lowBand" }, { Device::lowCrossoverParameter, "lowCrossover" },
        { Device::highBandParameter, "highBand" }, { Device::highCrossoverParameter, "highCrossover" },
        { Device::softKneeParameter, "softKnee" }, { Device::rmsParameter, "rms" }, { Device::outputParameter, "output" },
        { Device::timeParameter, "time" }, { Device::amountParameter, "amount" } };
    for (const auto& [index, id] : globals)
        require(device->parameterSpec(index).id == id, juce::String("the face finds ") + id + " where it is declared");
    const char* bands[] { "low", "mid", "high" };
    const char* controls[] { "Active", "Solo", "Input", "Output", "BelowThreshold", "BelowRatio",
                             "AboveThreshold", "AboveRatio", "Attack", "Release" };
    for (int b = 0; b < 3; ++b)
        for (int k = 0; k < Device::bandControlCount; ++k)
        {
            const auto id = juce::String(bands[b]) + controls[k];
            require(device->parameterSpec(Device::bandParameter(b, static_cast<Device::BandControl>(k))).id == id,
                    "the face finds " + id + " where it is declared");
        }
    require(device->parameterCount() == Device::bandParameter(2, Device::BandControl::release) + 1, "and nothing else");
    // Times read in Live's three figures, whatever the value's fraction: a
    // drag on a readout lands between whole milliseconds.
    auto& attack = device->parameter(Device::bandParameter(1, Device::BandControl::attack)).automatable();
    auto& release = device->parameter(Device::bandParameter(1, Device::BandControl::release)).automatable();
    for (const auto& [control, value, reading] : { std::tuple(&attack, 5.0f, "5.00 ms"), std::tuple(&attack, 50.0f, "50.0 ms"),
                                                   std::tuple(&attack, 226.413f, "226 ms"), std::tuple(&release, 1500.0f, "1.50 s") })
    {
        control->setParameter(value, juce::sendNotificationSync);
        require(control->getCurrentValueAsString() == reading,
                juce::String("a time reads ") + reading + ", not " + control->getCurrentValueAsString());
    }

    // Played a tone, it says how loud its bands were, once. What the tone's
    // start rang into every band is read and spent first.
    device->initialise({ {}, rate, block });
    juce::AudioBuffer<float> buffer(2, block);
    auto frame = 0;
    const auto play = [&] (int blocks)
    {
        for (int pass = 0; pass < blocks; ++pass)
        {
            for (int i = 0; i < block; ++i, ++frame)
                for (int channel = 0; channel < 2; ++channel)
                    buffer.setSample(channel, i, static_cast<float>(0.5 * std::sin(2.0 * pi * 1000.0 * frame / rate)));
            te::PluginRenderContext context(&buffer, 0, block, nullptr, 0.0, {}, false, false, true, false);
            device->applyToBuffer(context);
        }
    };
    play(40);
    for (int b = 0; b < 3; ++b)
        device->takeBandLevels(b);
    play(20);
    const auto mid = device->takeBandLevels(1);
    require(mid.input > 0.25f && mid.output > 0.25f, "a tone in the mid band lights its meters");
    require(device->takeBandLevels(1).input == 0.0f, "and a reading is spent once taken");
    require(device->takeBandLevels(0).input < mid.input * 0.01f, "the low band hears next to nothing of 1 kHz");
    device->deinitialise();

    // A factory preset sets what it says.
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
    const auto valueOf = [&] (int index) { return session.deviceParameter(track, slot->pluginIndex, index)->value; };
    require(valueOf(Device::bandParameter(1, Device::BandControl::aboveRatio)) == Engine::infiniteRatio
                && valueOf(Device::bandParameter(1, Device::BandControl::belowRatio)) == 4.0f
                && valueOf(Device::amountParameter) == 0.5f,
            "Upward and Downward compresses each band both ways at half Amount");
    require(session.deleteDevice(track, slot->pluginIndex).wasOk(), "and the device comes off again");
}
}

// The face, driven by the presses and drags a person makes: real mouse
// events at the places its layout puts things, each drag one undo step.
void runMultibandFaceTest(Session& session)
{
    using Device = MultibandDynamicsDevice;
    using Control = Device::BandControl;
    namespace md = multibanddisplay;
    require(session.addAudioTrack().wasOk(), "a track for the face");
    const auto track = session.firstReturnTrack() - 1;
    require(session.addDevice("MultibandDynamics", track).wasOk(), "the session adds the device");
    std::optional<Session::DeviceSlot> slot;
    for (const auto& candidate : session.deviceSlots(track))
        if (candidate.deviceId == "MultibandDynamics")
            slot = candidate;
    require(slot.has_value(), "the device is in the chain");
    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(track, *slot, true);
    require(panel->face == DeviceEditorPanel::Face::Multiband && panel->preferredWidth() == DeviceEditorPanel::multibandFaceWidth,
            "Multiband Dynamics has a face of its own");
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);
    const auto layout = DeviceEditorPanel::multibandLayout(panel->getLocalBounds());

    // Everything drawn is on the face and clear of everything else, the
    // knobs and their readings included.
    std::vector<juce::Rectangle<int>> placed { layout.softKnee, layout.rms };
    for (const auto& row : layout.bands)
        for (const auto& area : { row.splitSwitch, row.active, row.crossover, row.solo, row.belowThreshold, row.belowRatio,
                                  row.meter, row.aboveThreshold, row.aboveRatio, row.attack, row.release })
            if (!area.isEmpty())
                placed.push_back(area);
    for (auto* child : panel->getChildren())
    {
        const auto* button = dynamic_cast<juce::TextButton*>(child);
        if (child->isVisible() && child->getY() >= DeviceEditorPanel::headerHeight
            && (button == nullptr || button->getButtonText() != "A"))
            placed.push_back(child->getBounds());
    }
    for (size_t a = 0; a < placed.size(); ++a)
    {
        require(panel->getLocalBounds().contains(placed[a]), "everything drawn is on the face");
        for (size_t b = a + 1; b < placed.size(); ++b)
            require(!placed[a].intersects(placed[b]), "and clear of everything else");
    }

    const auto valueOf = [&] (int index) { return session.deviceParameter(track, slot->pluginIndex, index)->value; };
    const auto refresh = [&] { panel->setTarget(track, *slot, true); };
    const auto event = [&panel] (juce::Point<float> at, juce::Point<float> downAt, juce::ModifierKeys mods, int clicks)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0,
                                panel.get(), panel.get(), juce::Time::getCurrentTime(), downAt,
                                juce::Time::getCurrentTime(), clicks, at != downAt);
    };
    const auto drag = [&] (juce::Point<float> from, juce::Point<float> to, int held = 0)
    {
        const juce::ModifierKeys mods(juce::ModifierKeys::leftButtonModifier | held);
        panel->mouseDown(event(from, from, mods, 1));
        for (int step = 1; step <= 4; ++step)
            panel->mouseDrag(event(from + (to - from) * (static_cast<float>(step) / 4.0f), from, mods, 1));
        panel->mouseUp(event(to, from, juce::ModifierKeys(held), 1));
    };
    const auto click = [&] (juce::Rectangle<int> area)
    {
        const auto at = area.getCentre().toFloat();
        panel->mouseDown(event(at, at, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1));
        panel->mouseUp(event(at, at, juce::ModifierKeys(), 1));
    };
    const auto undo = [&] { session.undo(); refresh(); };
    const auto near = [] (float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; };

    // A switch is a click and an undo step.
    const auto& mid = layout.bands[1];
    click(mid.solo);
    require(valueOf(Device::bandParameter(1, Control::solo)) == 1.0f, "S solos the band");
    undo();
    require(valueOf(Device::bandParameter(1, Control::solo)) == 0.0f, "and the click is one undo step");

    // The Above block's edge is its threshold, dragged sideways.
    const auto meter = mid.meter.toFloat();
    const auto dbPerPixel = (md::ceilingDb - md::floorDb) / meter.getWidth();
    const auto aboveEdge = juce::Point<float>(md::xForDb(-20.0f, meter.getX(), meter.getWidth()), meter.getCentreY());
    const auto aboveThreshold = Device::bandParameter(1, Control::aboveThreshold);
    const auto belowThreshold = Device::bandParameter(1, Control::belowThreshold);
    drag(aboveEdge, aboveEdge.translated(-20.0f, 0.0f));
    require(near(valueOf(aboveThreshold), -20.0f - 20.0f * dbPerPixel, 0.01f), "dragging the edge left lowers the threshold");
    undo();
    require(valueOf(aboveThreshold) == -20.0f, "and the drag is one undo step");
    drag(aboveEdge, aboveEdge.translated(-40.0f, 0.0f), juce::ModifierKeys::shiftModifier);
    require(near(valueOf(aboveThreshold), -20.0f - 10.0f * dbPerPixel, 0.01f), "Shift moves it a quarter as far");
    undo();
    // Ctrl moves the same edge on every band, in one undo step.
    drag(aboveEdge, aboveEdge.translated(-20.0f, 0.0f), juce::ModifierKeys::ctrlModifier);
    for (int b = 0; b < 3; ++b)
        require(near(valueOf(Device::bandParameter(b, Control::aboveThreshold)), -20.0f - 20.0f * dbPerPixel, 0.01f),
                "Ctrl moves every band's threshold");
    undo();
    for (int b = 0; b < 3; ++b)
        require(valueOf(Device::bandParameter(b, Control::aboveThreshold)) == -20.0f, "and one undo takes all three back");
    // Alt moves both of the band's thresholds.
    drag(aboveEdge, aboveEdge.translated(-20.0f, 0.0f), juce::ModifierKeys::altModifier);
    require(near(valueOf(aboveThreshold), -20.0f - 20.0f * dbPerPixel, 0.01f)
                && near(valueOf(belowThreshold), -60.0f - 20.0f * dbPerPixel, 0.01f),
            "Alt moves both of the band's thresholds");
    undo();
    // The lower threshold stops at the upper.
    const auto belowEdge = juce::Point<float>(md::xForDb(-60.0f, meter.getX(), meter.getWidth()), meter.getCentreY());
    drag(belowEdge, belowEdge.translated(300.0f, 0.0f));
    require(valueOf(belowThreshold) == -20.0f, "the lower threshold stops at the upper");
    undo();

    // A block's volume is its ratio: down in the Above block compresses,
    // up in the Below block compresses upwards, and a double-click is 1:1.
    const auto aboveRatio = Device::bandParameter(1, Control::aboveRatio);
    const auto belowRatio = Device::bandParameter(1, Control::belowRatio);
    const auto inAbove = juce::Point<float>(md::xForDb(-10.0f, meter.getX(), meter.getWidth()), meter.getCentreY());
    const auto inBelow = juce::Point<float>(md::xForDb(-70.0f, meter.getX(), meter.getWidth()), meter.getCentreY());
    drag(inAbove, inAbove.translated(0.0f, 50.0f));
    require(near(valueOf(aboveRatio), 2.0f, 0.01f), "half the travel down the Above block is 2:1");
    drag(inBelow, inBelow.translated(0.0f, -50.0f));
    require(near(valueOf(belowRatio), 2.0f, 0.01f), "half the travel up the Below block is 2:1 upwards");
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    panel->mouseDown(event(inAbove, inAbove, mods, 2));
    panel->mouseDoubleClick(event(inAbove, inAbove, mods, 2));
    panel->mouseUp(event(inAbove, inAbove, juce::ModifierKeys(), 2));
    require(valueOf(aboveRatio) == 1.0f, "a double-click puts the block back to 1:1");

    // A readout is dragged up and down across its control's travel.
    const auto attack = Device::bandParameter(1, Control::attack);
    const auto declared = *session.deviceParameter(track, slot->pluginIndex, attack);
    const juce::NormalisableRange<float> travel(declared.minimum, declared.maximum, declared.interval,
                                                static_cast<float>(declared.skew));
    const auto wanted = travel.convertFrom0to1(travel.convertTo0to1(declared.value) + 0.3f);
    drag(mid.attack.getCentre().toFloat(), mid.attack.getCentre().toFloat().translated(0.0f, -45.0f));
    require(near(valueOf(attack), wanted, wanted * 1.0e-3f), "dragging a readout up 45 pixels takes it 30% of its travel");

    // The band switches split a band off and back.
    click(layout.bands[0].splitSwitch);
    require(valueOf(Device::lowBandParameter) == 0.0f, "LOW stops splitting the low band off");

    // RHINO_DEVICE_SNAPSHOTS pictures it at its defaults with every other
    // face, in DeviceFacesTest.cpp, and here as these drags left it, with its
    // meters lit as a song would light them.
    if (const auto folder = juce::SystemStats::getEnvironmentVariable("RHINO_DEVICE_SNAPSHOTS", {}); folder.isNotEmpty())
    {
        click(layout.bands[0].splitSwitch);
        refresh();
        for (size_t b = 0; b < 3; ++b)
        {
            panel->multibandIn[b].levelDb = -18.0f + 4.0f * static_cast<float>(b);
            panel->multibandOut[b].levelDb = -24.0f + 2.0f * static_cast<float>(b);
        }
        Theme theme;
        panel->setLookAndFeel(&theme);
        const auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds());
        panel->setLookAndFeel(nullptr);
        const auto file = juce::File(folder).getChildFile("MultibandDynamics face.png");
        // An existing file is opened at its end, not replaced.
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(snapshot, stream);
    }
    panel.reset();
    require(session.deleteDevice(track, slot->pluginIndex).wasOk(), "the device comes off again");
}

void checkMultibandDynamics(Session& session)
{
    checkSplit();
    checkCurves();
    checkTime();
    checkMeters();
    checkDisplayGeometry();
    checkDevice(session);
    runMultibandFaceTest(session);
}
}
