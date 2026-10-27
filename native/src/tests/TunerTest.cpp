#include "model/Session.h"
#include "ui/rack/DeviceEditorPanel.h"
#include "ui/shared/Theme.h"
#include "dsp/TunerEngine.h"
#include "audio/TunerDevice.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <vector>

// The Tuner, held to what a tuner is for: naming the note a string plays and
// how many cents it is off, to a cent, from a bass's low B to well above a
// guitar's top string, and touching nothing that passes through it.
//
// The tones are synthesised at frequencies worked out here from cents, so a
// reading that agrees has agreed with arithmetic the tracker never saw.
namespace rhino
{
namespace
{
constexpr double rate = 48000.0;
constexpr int block = 480;
constexpr double pi = 3.14159265358979323846;

void require(bool valid, const juce::String& what, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error(("Tuner: " + what + " (line " + juce::String(static_cast<int>(location.line())) + ")")
                                     .toStdString());
}

// Feeds `seconds` of a tone through an engine in device-sized blocks. A saw
// stands in for a plucked string, whose strongest partial is not always its
// fundamental.
void feed(TunerEngine& engine, double frequency, double seconds, bool rich, double amplitude = 0.3)
{
    const auto total = static_cast<int>(seconds * rate);
    std::vector<float> audio(static_cast<size_t>(total));
    const auto harmonics = rich ? std::max(1, static_cast<int>(18000.0 / frequency)) : 1;
    for (int i = 0; i < total; ++i)
    {
        auto sample = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            sample += std::sin(2.0 * pi * k * frequency * i / rate) / k;
        audio[static_cast<size_t>(i)] = static_cast<float>(amplitude * sample / (rich ? 1.7 : 1.0));
    }
    for (int start = 0; start < total; start += block)
    {
        const float* channels[2] { audio.data() + start, audio.data() + start };
        engine.process(channels, 2, std::min(block, total - start));
    }
}

void checkAccuracy()
{
    TunerEngine engine;
    engine.prepare(rate, block);
    // From a five-string's low B and a four-string's low E up past the
    // guitar's top E, each in tune and off either way.
    const double notes[] { 23.0, 28.0, 40.0, 45.0, 52.0, 64.0, 69.0, 76.0, 88.0 };
    for (const auto midi : notes)
        for (const auto offCents : { 0.0, 7.0, -13.0 })
            for (const auto rich : { false, true })
            {
                const auto frequency = 440.0 * std::pow(2.0, (midi - 69.0 + offCents / 100.0) / 12.0);
                engine.reset();
                feed(engine, frequency, 0.6, rich);
                const auto reading = engine.reading();
                const auto note = TunerEngine::noteFor(reading.frequency, 440.0f);
                const auto what = juce::String(rich ? "a saw" : "a sine") + " at " + juce::String(frequency, 3) + " Hz";
                require(reading.voiced, what + " is heard");
                require(juce::roundToInt(note) == juce::roundToInt(midi), what + " is named "
                        + juce::String(note, 2) + ", not " + juce::String(midi));
                const auto cents = (note - static_cast<float>(midi)) * 100.0f;
                require(std::abs(cents - offCents) <= 1.0, what + " reads " + juce::String(cents, 2) + " cents, not "
                        + juce::String(offCents, 0));
            }

    // A reference other than 440 moves the note, not the reading: 432 Hz is
    // A4 against a 432 reference.
    require(std::abs(TunerEngine::noteFor(432.0f, 432.0f) - 69.0f) < 1.0e-5f, "432 Hz is A against a 432 reference");
    require(std::abs((TunerEngine::noteFor(440.0f, 432.0f) - 69.0f) * 100.0f - 31.77f) < 0.05f,
            "and 440 Hz is 31.8 cents sharp of it");

    // Silence lets the note go, after a moment's hold.
    engine.reset();
    feed(engine, 110.0, 0.5, true);
    require(engine.reading().voiced, "a note is held");
    feed(engine, 110.0, 0.1, true, 0.0);
    require(engine.reading().voiced, "and stays up through a moment's silence");
    feed(engine, 110.0, 0.5, true, 0.0);
    require(!engine.reading().voiced, "and goes once the silence lasts");

    // The history is the readings in order, the newest last, and nothing for
    // the silence at its end.
    std::vector<float> history(TunerEngine::historyLength);
    const auto count = engine.history(history.data(), TunerEngine::historyLength);
    require(count > 50, "the history fills");
    require(history[static_cast<size_t>(count - 1)] == 0.0f, "the silence is in it as no pitch");
    require(std::any_of(history.begin(), history.begin() + count,
                        [] (float frequency) { return std::abs(frequency - 110.0f) < 0.2f; }),
            "and the note before it");
}

void checkDevice(Session& session)
{
    // A meter, not an effect: what goes in comes out, bit for bit, mono or
    // stereo.
    auto plugin = session.edit->getPluginCache().createNewPlugin(TunerDevice::xmlTypeName, {});
    auto* tuner = dynamic_cast<TunerDevice*>(plugin.get());
    require(tuner != nullptr, "the engine makes a Tuner from its type name");
    require(tuner->getLatencySeconds() == 0.0 && tuner->getTailLength() == 0.0, "the Tuner adds no delay and no tail");
    require(tuner->getNumOutputChannelsGivenInputs(1) == 1, "a mono input stays mono");
    tuner->initialise({ {}, rate, block });
    juce::AudioBuffer<float> buffer(2, block * 40);
    juce::Random noise(3);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(c, i, static_cast<float>(0.3 * std::sin(2.0 * pi * 196.0 * i / rate))
                                       + (noise.nextFloat() - 0.5f) * 0.001f);
    juce::AudioBuffer<float> original(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += block)
    {
        te::PluginRenderContext context(&buffer, start, block, nullptr, 0.0, {}, false, false, true, false);
        tuner->applyToBuffer(context);
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            require(buffer.getSample(c, i) == original.getSample(c, i), "the Tuner leaves the audio alone");
    const auto reading = tuner->reading();
    require(reading.voiced && juce::roundToInt(TunerEngine::noteFor(reading.frequency, 440.0f)) == 55,
            "and names the G it heard");
    tuner->deinitialise();
}

// The face, driven by the clicks a person makes. It is drawn rather than
// composed, so a paint is the only other coverage it can have.
void checkFace(Session& session)
{
    require(session.addAudioTrack().wasOk(), "a track for the Tuner");
    const auto track = session.firstReturnTrack() - 1;
    require(session.addDevice("Tuner", track).wasOk(), "the session adds a Tuner");
    std::optional<Session::DeviceSlot> slot;
    for (const auto& device : session.deviceSlots(track))
        if (device.deviceId == "Tuner")
            slot = device;
    require(slot.has_value(), "the Tuner is in the chain");
    auto* tuner = dynamic_cast<TunerDevice*>(session.devicePlugin(track, slot->pluginIndex));
    require(tuner != nullptr, "the session finds it");

    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(track, *slot, true);
    require(panel->preferredWidth() == DeviceEditorPanel::tunerFaceWidth, "the Tuner has a face of its own");
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);
    auto sliders = 0;
    for (auto* child : panel->getChildren())
    {
        require(!child->isVisible() || panel->getLocalBounds().contains(child->getBounds()), "every control is on the face");
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            ++sliders;
    }
    require(sliders == 1, "Reference is its one knob");

    const auto click = [&panel] (int x, int y)
    {
        const juce::Point<float> at { static_cast<float>(x), static_cast<float>(y) };
        panel->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                          juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                          panel.get(), panel.get(), juce::Time::getCurrentTime(), at,
                                          juce::Time::getCurrentTime(), 1, false));
    };
    const auto snapshot = [&panel] (const juce::String& name)
    {
        Theme theme;
        panel->setLookAndFeel(&theme);
        const auto image = panel->createComponentSnapshot(panel->getLocalBounds());
        panel->setLookAndFeel(nullptr);
        require(image.getWidth() == panel->getWidth(), "the face paints");
        if (const auto folder = juce::SystemStats::getEnvironmentVariable("RHINO_DEVICE_SNAPSHOTS", {}); folder.isNotEmpty())
        {
            const auto file = juce::File(folder).getChildFile("Tuner " + name + ".png");
            file.deleteFile();
            if (auto stream = file.createOutputStream())
                juce::PNGImageFormat().writeImageToStream(image, *stream);
        }
        return image;
    };

    // Nothing is fed to it: the track's Tuner may be in the running graph,
    // and only the audio thread may play it. So the face is drawn hearing
    // nothing, which is what a Tuner just dropped looks like.
    snapshot("target");

    // The switches down the left are the device's own controls, each click an
    // undo step. Their rows: Classic at 28, Histogram 51, Target 83, Strobe
    // 106, and Hz and Cents along the bottom.
    const auto contentTop = 28;
    click(30, contentTop + 88);
    require(tuner->mode() == TunerDevice::Mode::strobe, "Strobe switches the arc to a band of lights");
    snapshot("strobe");
    session.undo();
    require(tuner->mode() == TunerDevice::Mode::target, "and the switch is one undo step");

    click(30, contentTop + 33);
    require(tuner->view() == TunerDevice::View::histogram, "Histogram shows pitch over time");
    snapshot("histogram");
    // Target and Strobe belong to Classic, so they do nothing in the histogram.
    click(30, contentTop + 88);
    require(tuner->mode() == TunerDevice::Mode::target, "and Classic's own switches stand idle there");
    click(30, contentTop + 10);
    require(tuner->view() == TunerDevice::View::classic, "Classic brings the arc back");

    click(70, DeviceEditorPanel::standardHeight - 12);
    require(tuner->showsCents(), "Cents reads the distance from the note");
    snapshot("cents");
    click(20, DeviceEditorPanel::standardHeight - 12);
    require(!tuner->showsCents(), "and Hz the frequency");

    require(session.deleteDevice(track, slot->pluginIndex).wasOk(), "the Tuner comes off again");
}
}

void checkTuner(Session& session)
{
    checkAccuracy();
    checkDevice(session);
    checkFace(session);
}
}
