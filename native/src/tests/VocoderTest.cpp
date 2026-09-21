#include "../Session.h"
#include "../DeviceEditorPanel.h"
#include "VocoderEngine.h"
#include "audio/VocoderDevice.h"
#include <algorithm>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <optional>
#include <vector>

// What the vocoder claims to do is settled by measuring rendered audio.
//
// The claim is a single sentence: the *modulator* decides which parts of the
// spectrum are loud, and the *carrier* supplies what is actually heard there.
// So the test builds a carrier out of four tones five octaves apart, puts a
// single tone through as the modulator, and measures which of the four came
// out. Nothing here reads a filter coefficient or an envelope, so it cannot
// agree with the engine by construction -- which is the whole point.
//
// The formant check is the same measurement run twice. Shifting the carrier
// bank up an octave should move the energy from the tone under the modulator
// to the tone an octave above it, and if the two banks were ever laid out from
// the same numbers that is the comparison which would notice.
namespace rhino
{
namespace
{
constexpr double testRate = 48000.0;
constexpr int testBlock = 256;
constexpr double pi = 3.14159265358979323846;
constexpr int settleSamples = 24000;     // half a second, past every follower
constexpr int measureSamples = 16384;

void require(bool valid, const std::source_location location = std::source_location::current())
{
    if (!valid)
        throw std::runtime_error("Rhino Vocoder check failed at line " + std::to_string(location.line()));
}

// A level comparison says what it measured when it fails: "failed at line 210"
// on a number that came out of a transform costs a second run to learn
// anything at all.
void requireLouderBy(double louder, double quieter, double marginDb, const char* what,
                     const std::source_location location = std::source_location::current())
{
    const auto difference = 20.0 * std::log10(std::max(louder, 1.0e-12) / std::max(quieter, 1.0e-12));
    if (difference < marginDb)
        throw std::runtime_error((juce::String(what) + ": measured " + juce::String(difference, 2)
            + " dB of separation, wanted at least " + juce::String(marginDb, 2)
            + ", at line " + juce::String(static_cast<int>(location.line()))).toStdString());
}

// A windowed single-frequency transform. It shares nothing with the filter
// bank: it never sees a coefficient.
double magnitudeAt(const std::vector<float>& audio, int start, int count, double frequency)
{
    const auto omega = 2.0 * pi * frequency / testRate;
    auto real = 0.0, imaginary = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(2.0 * pi * i / count);
        const auto sample = audio[static_cast<size_t>(start + i)] * window;
        real += sample * std::cos(omega * i);
        imaginary -= sample * std::sin(omega * i);
    }
    return std::sqrt(real * real + imaginary * imaginary);
}

double rms(const std::vector<float>& audio, int start, int count)
{
    auto sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto sample = static_cast<double>(audio[static_cast<size_t>(start + i)]);
        sum += sample * sample;
    }
    return std::sqrt(sum / std::max(1, count));
}

// The four tones sit in four well-separated bands of the default layout: at
// twenty bands between 100 Hz and 12 kHz they land in bands 3, 9, 12 and 15,
// so leakage between them is five octaves of a 12 dB/octave skirt.
const double carrierTones[] {250.0, 1000.0, 2000.0, 4000.0};

std::vector<float> renderCarrier(int total)
{
    std::vector<float> audio(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
    {
        auto sample = 0.0;
        for (const auto frequency : carrierTones)
            sample += 0.25 * std::sin(2.0 * pi * frequency * i / testRate);
        audio[static_cast<size_t>(i)] = static_cast<float>(sample);
    }
    return audio;
}

// Modulator in, vocoded audio out - which is what the device does too, so the
// test drives the engine exactly as the audio thread does.
std::vector<float> renderVocoder(const VocoderEngine::Settings& settings,
                                 double modulatorFrequency, double modulatorAmplitude)
{
    const auto total = settleSamples + measureSamples;
    auto audio = std::vector<float>(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
        audio[static_cast<size_t>(i)] = static_cast<float>(
            modulatorAmplitude * std::sin(2.0 * pi * modulatorFrequency * i / testRate));
    const auto carrier = renderCarrier(total);

    VocoderEngine engine;
    engine.prepare(testRate, 1, testBlock);
    engine.setSettings(settings);
    for (int start = 0; start < total; start += testBlock)
    {
        const auto count = std::min(testBlock, total - start);
        float* channels[1] {audio.data() + start};
        const float* carrierChannels[1] {carrier.data() + start};
        engine.process(channels, carrierChannels, 1, count);
    }
    return audio;
}

VocoderEngine::Settings plainSettings()
{
    VocoderEngine::Settings settings;
    settings.bands = 20;
    settings.lowHz = 100.0f;
    settings.highHz = 12000.0f;
    settings.bandwidth = 1.0f;
    settings.formantSemitones = 0.0f;
    settings.attackMs = 2.0f;
    settings.releaseMs = 30.0f;
    settings.gateDb = -140.0f;   // off, so the gate is one thing at a time
    settings.unvoiced = 0.0f;    // no noise, so every measurement is of the bank
    settings.enhance = 0.0f;
    settings.depth = 1.0f;
    settings.outputGainDb = 0.0f;
    settings.dryWet = 1.0f;
    return settings;
}

// The face is drawn rather than composed, so a render is the only coverage it
// can have: the chooser, the lamps and the band display are rectangles, and
// nothing but a paint would notice one landing off the panel.
void checkVocoderPanel(Session& session)
{
    require(session.addAudioTrack().wasOk());
    const auto track = session.trackCount() - 1;
    require(session.addDevice("RhinoVocoder", track).wasOk());
    auto slot = std::optional<Session::DeviceSlot>();
    for (const auto& device : session.deviceSlots(track))
        if (device.type == VocoderDevice::xmlTypeName)
            slot = device;
    require(slot.has_value());

    auto panel = std::make_unique<DeviceEditorPanel>(session);
    panel->setTarget(track, *slot, true);
    require(panel->preferredWidth() > 700);
    panel->setSize(panel->preferredWidth(), DeviceEditorPanel::standardHeight);

    std::vector<juce::Rectangle<int>> knobs;
    for (auto* child : panel->getChildren())
    {
        require(panel->getLocalBounds().contains(child->getBounds()));
        if (child->isVisible() && dynamic_cast<juce::Slider*>(child) != nullptr)
            knobs.push_back(child->getBounds());
    }
    require(knobs.size() == 13);
    for (size_t i = 0; i < knobs.size(); ++i)
        for (auto j = i + 1; j < knobs.size(); ++j)
            require(!knobs[i].intersects(knobs[j]));

    // Painting reaches the device through Session::devicePlugin and the source
    // track through Session::deviceSidechainSource; if either path is wrong
    // this is where it shows. Drawn twice: with no carrier, which is what a
    // device just dropped looks like, and with one.
    const auto unwired = panel->createComponentSnapshot(panel->getLocalBounds());
    require(unwired.getWidth() == panel->getWidth());
    require(unwired.getHeight() == DeviceEditorPanel::standardHeight);
    require(session.setDeviceSidechainSource(track, slot->pluginIndex, 0).wasOk());
    panel->setTarget(track, *slot, true);
    const auto wired = panel->createComponentSnapshot(panel->getLocalBounds());
    require(wired.getWidth() == panel->getWidth());

    require(session.deleteDevice(track, slot->pluginIndex).wasOk());
}
}

void checkVocoderDsp(Session& session)
{
    // ---- the modulator decides which band is loud -------------------------
    {
        const auto audio = renderVocoder(plainSettings(), 1000.0, 0.5);
        const auto atModulator = magnitudeAt(audio, settleSamples, measureSamples, 1000.0);
        requireLouderBy(atModulator, magnitudeAt(audio, settleSamples, measureSamples, 250.0), 20.0,
                        "The carrier tone under the modulator is louder than the one two octaves below");
        requireLouderBy(atModulator, magnitudeAt(audio, settleSamples, measureSamples, 4000.0), 20.0,
                        "and than the one two octaves above");
        requireLouderBy(atModulator, magnitudeAt(audio, settleSamples, measureSamples, 2000.0), 15.0,
                        "and than the one an octave above");
    }

    // ---- the formant control moves the carrier's bank, not the modulator's -
    {
        auto shifted = plainSettings();
        shifted.formantSemitones = 12.0f;
        const auto audio = renderVocoder(shifted, 1000.0, 0.5);
        // The band that heard the modulator at 1 kHz now filters the carrier an
        // octave higher, so the 2 kHz tone is the one it lets through.
        requireLouderBy(magnitudeAt(audio, settleSamples, measureSamples, 2000.0),
                        magnitudeAt(audio, settleSamples, measureSamples, 1000.0), 15.0,
                        "An octave of formant shift moves the output up an octave");
    }

    // ---- depth at nothing takes the shaping away --------------------------
    {
        auto flat = plainSettings();
        flat.depth = 0.0f;
        const auto audio = renderVocoder(flat, 1000.0, 0.5);
        // Every band is driven by the average of the bank instead of its own
        // envelope, so the carrier comes through with its own spectrum: four
        // tones at the level they went in at, relative to each other.
        const auto atModulator = magnitudeAt(audio, settleSamples, measureSamples, 1000.0);
        const auto atOther = magnitudeAt(audio, settleSamples, measureSamples, 4000.0);
        require(std::abs(20.0 * std::log10(std::max(atModulator, 1.0e-12)
                                           / std::max(atOther, 1.0e-12))) < 12.0);
    }

    // ---- the gate shuts the bank on a modulator that is only room tone -----
    {
        auto gated = plainSettings();
        gated.gateDb = -20.0f;
        const auto quiet = renderVocoder(gated, 1000.0, 0.01f);
        const auto loud = renderVocoder(gated, 1000.0, 0.5f);
        requireLouderBy(rms(loud, settleSamples, measureSamples),
                        rms(quiet, settleSamples, measureSamples), 30.0,
                        "A modulator under the gate plays nothing");
    }

    // ---- no carrier: the voice passes through rather than going silent -----
    {
        const auto total = testBlock * 8;
        std::vector<float> audio(static_cast<size_t>(total)), original(static_cast<size_t>(total));
        for (int i = 0; i < total; ++i)
            original[static_cast<size_t>(i)] = audio[static_cast<size_t>(i)]
                = static_cast<float>(0.4 * std::sin(2.0 * pi * 440.0 * i / testRate));
        VocoderEngine engine;
        engine.prepare(testRate, 1, testBlock);
        engine.setSettings(plainSettings());
        for (int start = 0; start < total; start += testBlock)
        {
            float* channels[1] {audio.data() + start};
            engine.process(channels, nullptr, 1, testBlock);
        }
        for (int i = 0; i < total; ++i)
            require(std::abs(audio[static_cast<size_t>(i)] - original[static_cast<size_t>(i)]) < 1.0e-6f);
        require(!engine.readout().carrierPresent);
        require(engine.readout().modulatorPresent);
    }

    // ---- the device's side of the contract with the engine's graph ---------
    //
    // The carrier reaches the device as two extra channels on the same buffer.
    // Everything about that is a promise between the device and the graph
    // builder -- four input names, two output channels, a sidechain offered --
    // and none of it is visible in the DSP, so it is checked here.
    //
    // On a device that belongs to nobody: one in a track's chain may be being
    // called on the audio thread, and this drives applyToBuffer directly.
    {
        auto plugin = session.edit->getPluginCache().createNewPlugin(VocoderDevice::xmlTypeName, {});
        auto* vocoder = dynamic_cast<VocoderDevice*>(plugin.get());
        require(vocoder != nullptr);
        // The catalog is the only place the device is declared, so the engine
        // has to be able to make one from the type name alone.
        require(DeviceCatalog::byId("RhinoVocoder") != nullptr);
        require(DeviceCatalog::byId("RhinoVocoder")->typeName == VocoderDevice::xmlTypeName);

        juce::StringArray ins, outs;
        vocoder->getChannelNames(&ins, &outs);
        require(ins.size() == 4 && outs.size() == 2);
        require(vocoder->canSidechain());
        require(vocoder->getNumOutputChannelsGivenInputs(4) == 2);
        const auto busses = vocoder->getBusses();
        require(busses.inputs.size() == 2 && busses.outputs.size() == 1);
        require(busses.inputs.front().getNumChannels() == 2);
        require(busses.inputs.back().getNumChannels() == 2);

        vocoder->initialise({{}, testRate, 1024});
        juce::AudioBuffer<float> buffer(4, 1024);
        for (int i = 0; i < 1024; ++i)
        {
            const auto voice = static_cast<float>(0.4 * std::sin(2.0 * pi * 300.0 * i / testRate));
            auto carrier = 0.0;
            for (const auto frequency : carrierTones)
                carrier += 0.25 * std::sin(2.0 * pi * frequency * i / testRate);
            buffer.setSample(0, i, voice);
            buffer.setSample(1, i, voice);
            buffer.setSample(2, i, static_cast<float>(carrier));
            buffer.setSample(3, i, static_cast<float>(carrier));
        }
        const auto carrierBefore = buffer.getSample(2, 512);
        te::PluginRenderContext context(&buffer, 0, 1024, nullptr, 0.0, {}, false, false, true, false);
        vocoder->applyToBuffer(context);
        // The carrier channels are the graph's, not the device's: it reads them
        // and leaves them, and the graph trims them off the output.
        require(std::abs(buffer.getSample(2, 512) - carrierBefore) < 1.0e-6f);
        require(std::abs(buffer.getSample(3, 512) - carrierBefore) < 1.0e-6f);
        // Something came out of the main bus, and the same thing on both sides:
        // the band gains are shared, so a mono carrier stays centred.
        auto peak = 0.0f;
        for (int i = 0; i < 1024; ++i)
        {
            peak = std::max(peak, std::abs(buffer.getSample(0, i)));
            require(std::abs(buffer.getSample(0, i) - buffer.getSample(1, i)) < 1.0e-6f);
        }
        require(peak > 1.0e-4f);

        // Everything it holds has to survive being written out and read back.
        vocoder->getAutomatableParameterByID("formant")->setParameter(7.0f, juce::dontSendNotification);
        vocoder->getAutomatableParameterByID("bands")->setParameter(32.0f, juce::dontSendNotification);
        const auto roundTrip = vocoder->state.createCopy();
        auto reloaded = session.edit->getPluginCache().createNewPlugin(roundTrip);
        auto* restored = dynamic_cast<VocoderDevice*>(reloaded.get());
        require(restored != nullptr);
        require(restored->bandCount() == 32);
        require(std::abs(restored->getAutomatableParameterByID("formant")->getCurrentValue() - 7.0f) < 0.01f);
        // The bank is laid out from the band count and the range together, so
        // the first and last lamps of the face move with both.
        require(restored->bandCentreHz(0) > 100.0f && restored->bandCentreHz(0) < 200.0f);
        require(restored->bandCentreHz(31) > 8000.0f && restored->bandCentreHz(31) < 12000.0f);
    }

    checkVocoderPanel(session);
}
}
